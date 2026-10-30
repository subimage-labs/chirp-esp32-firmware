#include "ui.h"

#include <Preferences.h>
#include <driver/gpio.h>

#include "character.h"
#include "config.h"
#include "fontpick.h"
#include "line_store.h"
#include "logo_draw.h"
#include "meter.h"
#include "psram_alloc.h"

static TFT_eSPI tft;
#if CHIRP_BL_PULSE
// The T-Deck's backlight chip (LILYGO's setBrightness): a rising edge turns it
// on at the brightest of 16 levels, each short low pulse steps one level down
// (wrapping from 1 to 16), and 3 ms low turns it off. PWM on its pin is a pulse
// train: the level it lands on depends on history (a wake once stayed dark).
static uint8_t blLevel = 0;                       // loop: 0 = off, 1..16
static void blSet(uint8_t level) {
    if (level == 0) {
        digitalWrite(TFT_BL, LOW);
        delay(3);
        blLevel = 0;
        return;
    }
    if (blLevel == 0) {
        digitalWrite(TFT_BL, HIGH);
        blLevel = 16;
        delayMicroseconds(30);
    }
    for (int n = (16 + blLevel - level) % 16; n > 0; n--) {
        digitalWrite(TFT_BL, LOW);
        digitalWrite(TFT_BL, HIGH);
    }
    blLevel = level;
}
#else
static const uint8_t BL_LEDC_CH = 7;
#endif

// The theme choice lives beside the device settings but in its own keys: it
// changes live, without the settings' save-and-restart. Stored by name, so
// reordering or adding themes never changes what a device shows.
static const char* THEME_NVS_NAMESPACE = "chirp";
static const char* THEME_NVS_NAME = "themeName";
static const char* THEME_NVS_LIGHT = "themeLight";
static uint8_t themeFamily = 0;
static bool themeLight = false;
static const Palette* palette = &THEMES[0].dark;

static void applyTheme(uint8_t family, bool light) {
    themeFamily = family;
    themeLight = light;
    palette = light ? &THEMES[family].light : &THEMES[family].dark;
}

// The label the shimmer last painted; any other status write clears it.
static String shimmerLabel;

// Status strip state: whether the strip (rule + battery) is on screen, how far
// right the current label's pixels may reach, and the last uiMsg text, ink and
// notice flag (a repeat is a no-op). Status writes repaint in place; only an
// invalid strip (after uiClear, a font change or a compact switch) is cleared
// whole.
static bool stripValid = false;
// The chat view (companion, rows) is on screen whole: transcriptRender then
// repaints each row in place instead of clearing the area first, so a
// streaming answer never blanks the screen between updates. Invalid after
// anything painted over it: uiClear, a menu (uiStatusCompact), a font change,
// the character view, the empty-chat screen.
static bool chatValid = false;
static int labelEnd = 0;
static String msgShown;
static Ink msgInk = Ink::Text;
static bool msgNotice = false;                    // uiNotice: shown in the character view too
static bool stripCompact = false;                 // menus: small text, no battery
#if CHIRP_COMPOSE
static bool composeOn();          // the compose band's paint code sits with the strip
static void composePaint();
static uint32_t composeBlinkMs = 0;
static bool composeCursorOn = true;
#endif

// The character view has no strip, and neither has the chat view's empty
// screen (nothing said yet: the logo and the hints take the whole height).
// There it comes up over the bottom only while the message is an error or a
// notice, and goes with the next message. The chat view with a chat in it and
// the menus always show it.
static int totalRows();
static bool stripWanted() {
    if (stripCompact || msgInk == Ink::Err || msgNotice) return true;
    return !characterOn() && totalRows() > 0;
}

// A status text wider than the strip scrolls right to left, pausing at the
// start of each pass; a second copy follows TICKER_GAP behind so it loops.
static const int TICKER_GAP = 40;
static const int TICKER_PX_PER_S = 36;
static const uint32_t TICKER_PAUSE_MS = 1200;
static const uint32_t TICKER_FRAME_MS = 33;
static bool tickerOn = false;
static int tickerW = 0, tickerOffset = -1;
static uint32_t tickerStart = 0, tickerLast = 0;

static const int CONTENT_Y = 2;    // first row of Q/A content


// Transcript storage lives in PSRAM (psram_alloc.h): below 4 KB the heap
// serves every request from internal RAM, so 40 turns as many small Strings
// cost ~45 KB of it and left the largest internal block near 78 KB. Completed
// turns are PsStrings; their wrapped lines are two PSRAM buffers (line_store.h)
// with kind 0=Q 1=A 2=rule, so scrolling renders any window without re-layout.
struct StoredTurn { PsString q, a; };
typedef BasicLineStore<PsramAlloc> LineStore;
static PsVector<StoredTurn> history;
static uint32_t historyRev = 0;                   // bumped whenever completed turns change
static LineStore logLines;
static int scrollLine = 0;                        // window top (when not following)
static bool follow = true;                        // pinned to newest; wheel breaks it

// In-flight turn: kept OUT of history until it completes, so streaming
// deltas can never re-wrap or relocate completed turns.
static PsString pendingQ, pendingA;
static bool pendingActive = false;
static LineStore pendingLines;

// Where each turn's question and answer start in the wrapped rows and how
// many rows each takes (absolute row indices; the in-flight turn's follow
// logLines). The views' shared scroll anchor (anchor.h) maps through these.
static std::vector<PartRows> turnRows;            // completed turns
static PartRows pendingRows = {0, 0, 0, 0};       // the in-flight turn

static const size_t HISTORY_MAX = 40;

// ---------------------------------------------------------------- text
// The only place fonts are chosen and text is drawn. The fonts are
// anti-aliased (TFT_eSPI smooth fonts; the pixel family is 1-bit): edge
// pixels are blended against the background color, and the library plots
// them one SPI transaction at a time. So a string is composed in a RAM
// sprite one line high (blended against the caller's background) and pushed
// to the panel as one block of rows.
// Strings are UTF-8; spaces are drawn as U+00A0 to get the font's own space
// width. Loop task only.
//
// The choice (family by name, size S/M/L) is saved in NVS beside the theme,
// and every text height derives from the body cut's line box (`metrics`).
static TFT_eSprite lineSpr(&tft);
static bool sprReady = false;
static int sprW = 0;
static const FontCut* loadedCut = nullptr;       // what lineSpr has loaded

static const char* FONT_NVS_NAMESPACE = "chirp";
static const char* FONT_NVS_NAME = "fontName";
static const char* FONT_NVS_SIZE = "fontSize";
static uint8_t curFamily = 0;
static uint8_t curSize = FONT_SIZE_DEFAULT;
static UiMetrics metrics = uiMetricsFor(18);
static inline int contentBottom() { return tft.height() - metrics.status; }

static const FontCut& bodyCut() { return FONT_FAMILIES[curFamily].size[curSize]; }

static void applyFont(uint8_t family, uint8_t size) {
    curFamily = family;
    curSize = size;
    metrics = uiMetricsFor(bodyCut().box);
}

static const FontCut* cutFor(UiFont f) {
    switch (f) {
    case UiFont::Header:
        return &FONT_FAMILIES[curFamily].size[curSize < FONT_SIZE_M ? curSize : FONT_SIZE_M];
    case UiFont::Small: return &FONT_FAMILIES[curFamily].size[FONT_SIZE_S];
    case UiFont::Title: return &FONT_FAMILIES[curFamily].title;
    case UiFont::Fixed: return &FONT_FAMILIES[fontFind(FONT_FIXED_FAMILY)].size[FONT_SIZE_M];
    default: return &bodyCut();
    }
}

// Loads a cut into the sprite; a no-op when it is the loaded one, so
// per-frame callers (shimmer) never reload.
static void loadCut(const FontCut* c) {
    if (!sprReady || c == loadedCut) return;
    lineSpr.loadFont(c->vlw);                    // replaces the loaded one
    loadedCut = c;
}

void uiFont(UiFont f) { loadCut(cutFor(f)); }
void uiFontSample(size_t family) {
    if (family < FONT_FAMILY_COUNT) loadCut(&FONT_FAMILIES[family].size[curSize]);
}

static void fontBegin() {
    if (sprReady) return;
    sprW = tft.width();
    lineSpr.setColorDepth(16);
    sprReady = lineSpr.createSprite(sprW, FONT_MAX_BOX) != nullptr;
    if (!sprReady) {
        Serial.println("[UI] text sprite alloc failed");
        return;
    }
    lineSpr.setTextWrap(false, false);
    uiFont(UiFont::Body);
}

int uiFontHeight() { return loadedCut ? loadedCut->box : 0; }
int uiLineHeight() { return metrics.line; }
int uiRowHeight() { return metrics.row; }
int uiStatusHeight() { return metrics.status; }
int uiHeaderHeight() { return metrics.header; }
int uiButtonHeight() { return metrics.button; }
int uiPinnedHeight() { return metrics.pinned; }
size_t uiFontFamilyCount() { return FONT_FAMILY_COUNT; }
const char* uiFontFamilyName(size_t i) { return i < FONT_FAMILY_COUNT ? FONT_FAMILIES[i].name : ""; }
size_t uiFontFamily() { return curFamily; }
uint8_t uiFontSize() { return curSize; }

// Table index of a code point in the current font; false when it lacks it.
static bool glyphIndex(uint32_t cp, uint16_t& i) {
    if (cp == ' ') cp = 0xA0;
    return cp <= 0xFFFF && lineSpr.getUnicodeIndex((uint16_t)cp, &i);
}

int uiTextWidth(const char* s) {
    if (!sprReady) return 0;
    int w = 0;
    const char* p = s;
    const char* end = s + strlen(s);
    uint16_t gi;
    while (p < end)
        if (glyphIndex(utf8Next(p, end), gi)) w += lineSpr.gxAdvance[gi];
    return w;
}

int uiTextWidth(const String& s) { return uiTextWidth(s.c_str()); }

// Pushes columns [L, R) of the line sprite to row y in slices, re-sending the
// panel's inversion after each (uiPanelHold): a text stream can drop it, and a
// slice is then the longest it stays dropped (a thin column, not a band).
// T-Deck camera, Rose Pine dark menu repaints, flash frames per run: one push
// per line 5, 32 px slices 1-3, 8 px slices 3-7 (each slice is a new transfer).
static const int PUSH_SLICE = 32;   // px
static void pushLine(int L, int R, int y, int h) {
    for (int x = L; x < R; x += PUSH_SLICE) {
        const int w = min(PUSH_SLICE, R - x);
        tft.startWrite();
        lineSpr.pushSprite(x, y, x, 0, w, h);
        tft.endWrite();
        uiPanelHold();
    }
}

// Draws s with its top-left at (x, y), glyph i in colorOf(i), and returns the
// width. Pushes exactly the columns the text touches (its advance, plus any
// ink that overhangs it), so neighbours drawn edge to edge stay intact.
template <class ColorOf>
static int paintText(const char* s, int x, int y, uint16_t bg, ColorOf colorOf) {
    const int w = uiTextWidth(s);
    if (!sprReady || !loadedCut) return w;
    const int h = loadedCut->box;
    const int f0 = max(0, x - 4), f1 = min(sprW, x + w + 4);
    if (f1 <= f0) return w;
    lineSpr.fillRect(f0, 0, f1 - f0, h, bg);
    lineSpr.setCursor(x, 0);
    const char* p = s;
    const char* end = s + strlen(s);
    int i = 0, cx = x, lo = x, hi = x + w;
    uint16_t gi;
    while (p < end) {
        const uint32_t cp = utf8Next(p, end);
        if (!glyphIndex(cp, gi)) continue;
        lineSpr.setTextColor(colorOf(i++), bg);
        lineSpr.drawGlyph(cp == ' ' ? 0xA0 : (uint16_t)cp);
        lo = min(lo, cx + lineSpr.gdX[gi]);
        hi = max(hi, cx + lineSpr.gdX[gi] + lineSpr.gWidth[gi]);
        cx += lineSpr.gxAdvance[gi];
    }
    lo = max(lo, f0);
    hi = min(hi, f1);
    if (hi > lo) pushLine(lo, hi, y, h);
    return w;
}

// A scrolling text: draws s (width w) from textX and, so it loops, a second
// copy w + gap px later, then pushes only the window [L, R). Glyphs wholly
// outside the window are skipped; one starting left of the sprite (x < 0) is
// dropped a few px early rather than drawn off its edge.
static void paintWindow(const char* s, int w, int textX, int gap, int y, uint16_t fg,
                        uint16_t bg, int L, int R) {
    if (!sprReady || !loadedCut || R <= L) return;
    const int h = loadedCut->box;
    lineSpr.fillRect(L, 0, R - L, h, bg);
    lineSpr.setTextColor(fg, bg);
    const char* end = s + strlen(s);
    for (int copy = 0; copy < 2; copy++) {
        int cx = textX + copy * (w + gap);
        const char* p = s;
        uint16_t gi;
        while (p < end && cx < R) {
            const uint32_t cp = utf8Next(p, end);
            if (!glyphIndex(cp, gi)) continue;
            const int adv = lineSpr.gxAdvance[gi];
            if (cx >= 0 && cx + adv > L) {
                lineSpr.setCursor(cx, 0);
                lineSpr.drawGlyph(cp == ' ' ? 0xA0 : (uint16_t)cp);
            }
            cx += adv;
        }
    }
    pushLine(L, R, y, h);
}

int uiText(const char* s, int x, int y, Ink fg, Ink bg) {
    const uint16_t color = uiInk(fg);
    return paintText(s, x, y, uiInk(bg), [color](int) { return color; });
}

int uiText(const String& s, int x, int y, Ink fg, Ink bg) {
    return uiText(s.c_str(), x, y, fg, bg);
}

// The same text composed into a 16-bit sprite (dst coordinates, clipped to
// it), for views that build a frame off-panel and push it whole. The 4 px
// either side of the text are filled with bg too.
int uiTextInto(TFT_eSprite& dst, const char* s, int x, int y, uint16_t fg, uint16_t bg) {
    const int w = uiTextWidth(s);
    if (!sprReady || !loadedCut) return w;
    const int h = loadedCut->box;
    const int dw = dst.width(), dh = dst.height();
    const int f0 = max(0, x - 4), f1 = min(min(sprW, dw), x + w + 4);
    if (f1 <= f0 || y >= dh || y + h <= 0) return w;
    lineSpr.fillRect(f0, 0, f1 - f0, h, bg);
    lineSpr.setTextColor(fg, bg);
    lineSpr.setCursor(x, 0);
    const char* p = s;
    const char* end = s + strlen(s);
    uint16_t gi;
    while (p < end) {
        const uint32_t cp = utf8Next(p, end);
        if (glyphIndex(cp, gi)) lineSpr.drawGlyph(cp == ' ' ? 0xA0 : (uint16_t)cp);
    }
    const uint16_t* src = (const uint16_t*)lineSpr.getPointer();
    uint16_t* out = (uint16_t*)dst.getPointer();
    for (int r = 0; r < h; r++) {
        const int dy = y + r;
        if (dy >= 0 && dy < dh)
            memcpy(out + dy * dw + f0, src + r * sprW + f0, (f1 - f0) * sizeof(uint16_t));
    }
    return w;
}

static String foldRaw(const char* s, size_t n) {
    const FontCut* c = loadedCut;
    const std::string out = foldText(s, n, [c](uint32_t cp) {
        return c && vlwHas(*c, cp);
    });
    return String(out.c_str());
}

String uiFoldN(const char* s, size_t n) { return foldRaw(s, n); }

String uiFold(const String& in) { return foldRaw(in.c_str(), in.length()); }

String uiFit(const String& in, int avail) {
    const std::string s = fitText(uiFold(in).c_str(), avail, [](const char* t) {
        return uiTextWidth(t);
    });
    return String(s.c_str());
}

// Switches font live: saved, the character card re-laid, the transcript
// re-wrapped for the new metrics when next shown. The caller redraws (appRedraw).
static bool layoutStale = false;
void uiSetFont(size_t family, uint8_t size) {
    if (family >= FONT_FAMILY_COUNT || size >= FONT_SIZE_COUNT) return;
    applyFont((uint8_t)family, size);
    Preferences prefs;
    if (prefs.begin(FONT_NVS_NAMESPACE, false)) {
        prefs.putString(FONT_NVS_NAME, FONT_FAMILIES[family].name);
        prefs.putString(FONT_NVS_SIZE, String(fontSizeLetter(size)));
        prefs.end();
    }
    uiFont(UiFont::Body);
    shimmerLabel = "";                           // the strip changed height: repaint it whole
    stripValid = false;
    chatValid = false;
    layoutStale = true;
    characterTextChanged();
    Serial.printf("[UI] font %s %c line=%d\n", FONT_FAMILIES[family].name,
                  fontSizeLetter(size), metrics.line);
}

// ---------------------------------------------------------------- panel
void uiBegin() {
    Preferences prefs;
    String name = THEME_DEFAULT;
    bool light = false;
    if (prefs.begin(THEME_NVS_NAMESPACE, true)) {   // read-only open fails before first save
        name = prefs.getString(THEME_NVS_NAME, THEME_DEFAULT);
        light = prefs.getBool(THEME_NVS_LIGHT, false);
        prefs.end();
    }
    int family = themeFind(name.c_str());
    if (family < 0) {
        family = themeFind(THEME_DEFAULT);
        light = false;
    }
    applyTheme((uint8_t)family, light);
    String fontName = FONT_DEFAULT_FAMILY, fontSize = String(fontSizeLetter(FONT_SIZE_DEFAULT));
    if (prefs.begin(FONT_NVS_NAMESPACE, true)) {
        fontName = prefs.getString(FONT_NVS_NAME, fontName);
        fontSize = prefs.getString(FONT_NVS_SIZE, fontSize);
        prefs.end();
    }
    const FontChoice fc = fontChoiceFrom(fontName.c_str(), fontSize.c_str());
    applyFont(fc.family, fc.size);
    // The panel shares its SPI lines with the CC1101 and the SD slot; with
    // their selects floating they listen to (and may act on) the panel's
    // traffic. Hold them deselected before the bus runs.
    for (const int cs : {RADIO_CS_PIN, SD_CS_PIN}) {
        digitalWrite(cs, HIGH);                     // latch high first: no low glitch on enable
        pinMode(cs, OUTPUT);
    }
    tft.init();
    tft.setRotation(CHIRP_ROTATION);             // board.h: landscape 320x170 / portrait 240x320
    fontBegin();
#if CHIRP_BL_PULSE
    pinMode(TFT_BL, OUTPUT);
    blSet(16);
#else
    ledcSetup(BL_LEDC_CH, 2000, 8);               // backlight PWM (dimming)
    ledcAttachPin(TFT_BL, BL_LEDC_CH);
    ledcWrite(BL_LEDC_CH, 255);
#endif
}

void uiPanelOff() {
    tft.writecommand(TFT_DISPOFF);
    tft.writecommand(TFT_SLPIN);
#if CHIRP_BL_PULSE
    blSet(0);
#else
    ledcDetachPin(TFT_BL);
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, LOW);                    // the line floats high otherwise
#endif
    gpio_hold_en((gpio_num_t)TFT_BL);
}

void uiPanelOn() {
    gpio_hold_dis((gpio_num_t)TFT_BL);
    tft.init();
    tft.setRotation(CHIRP_ROTATION);
#if CHIRP_BL_PULSE
    blSet(16);
#else
    ledcAttachPin(TFT_BL, BL_LEDC_CH);
    ledcWrite(BL_LEDC_CH, 255);
#endif
}

void uiBacklight(uint8_t duty) {
#if CHIRP_BL_PULSE
    blSet(duty == 0 ? 0 : (uint8_t)(1 + (duty * 15 + 127) / 255));   // 255 -> 16
#else
    ledcWrite(BL_LEDC_CH, duty);
#endif
}
void uiPanelBlank(bool off) { tft.writecommand(off ? TFT_DISPOFF : TFT_DISPON); }

TFT_eSPI& uiTft() { return tft; }
int uiContentTop() { return CONTENT_Y; }          // the menus' top; the chat has its own (topY)
int uiContentBottom() { return contentBottom(); }
int uiStripTop() { return stripValid ? contentBottom() : tft.height(); }
void uiClear() {
    tft.fillScreen(palette->ink[(int)Ink::Bg]);
    stripValid = false;
    chatValid = false;
}
// ---------------------------------------------------------------- theme
uint16_t uiInk(Ink ink) {
    if (ink == Ink::Faint)
        return blend565(palette->ink[(int)Ink::Bg], palette->ink[(int)Ink::Muted], 64);
    return palette->ink[(int)ink];
}
uint8_t uiThemeFamily() { return themeFamily; }
bool uiThemeLight() { return themeLight; }
const char* uiThemeName() { return THEMES[themeFamily].name; }

void uiSetTheme(uint8_t family, bool light) {
    if (family >= THEME_COUNT) return;
    applyTheme(family, light);
    Preferences prefs;
    if (prefs.begin(THEME_NVS_NAMESPACE, false)) {
        prefs.putString(THEME_NVS_NAME, THEMES[family].name);
        prefs.putBool(THEME_NVS_LIGHT, light);
        prefs.end();
    }
    Serial.printf("[UI] theme %s %s\n", uiThemeName(), light ? "light" : "dark");
}

// ---------------------------------------------------------------- status
// The status label sits at the strip's left edge (under the character); the
// battery block (battery.h layout) sits at the right edge, BATTERY_GAP px
// clear of the longest label.
static const int BATTERY_GAP = 8;
static BatteryView batteryNow;

static BatteryLayout batteryLayoutNow() { return batteryLayout(bodyCut().box); }

// The status label's room: from the left margin to BATTERY_GAP left of the
// battery block (to the right margin in the compact strip, which has none).
static int statusLeft() { return 6; }
static int statusRight() {
    return tft.width() - 6 - (stripCompact ? 0 : batteryLayoutNow().width + BATTERY_GAP);
}
static UiFont statusFont() { return stripCompact ? UiFont::Small : UiFont::Body; }
// Top of the status text in the loaded cut, centred on the Body line the
// strip is sized for.
static int statusTextY() { return contentBottom() + 3 + (bodyCut().box - uiFontHeight()) / 2; }

static void batteryPaint() {
    if (stripCompact) return;
    const BatteryLayout l = batteryLayoutNow();
    const int y0 = contentBottom();
    const int x0 = tft.width() - 6 - l.width;
    tft.fillRect(x0 - 2, y0 + 1, tft.width() - x0 + 2, metrics.status - 1, uiInk(Ink::Bg));
    if (!batteryNow.known) return;
    const Ink ink = batteryNow.low ? Ink::Err : batteryNow.usb ? Ink::Ok : Ink::Muted;
    const uint16_t c = uiInk(ink);
    const int top = y0 + 3;                                   // the status text's top
    const int gy = top + (bodyCut().box - l.glyphH) / 2;
    const int bx = x0;                                        // bolt slot
    const int gx = bx + l.boltW + l.gap;
    tft.drawRect(gx, gy, l.glyphW, l.glyphH, c);
    tft.fillRect(gx + l.glyphW, gy + (l.glyphH - l.nubH) / 2, l.nubW, l.nubH, c);
    const int innerW = l.glyphW - 4;
    int bar = innerW * batteryNow.pct / 100;
    if (batteryNow.pct > 0 && bar < 1) bar = 1;
    // The level at half strength, so the outline reads first.
    tft.fillRect(gx + 2, gy + 2, bar, l.glyphH - 4, blend565(uiInk(Ink::Bg), c, 128));
    if (batteryNow.charging) {                                // a bolt, two px thick
        const int bw = l.boltW - 2, mid = l.glyphH / 2;
        for (int k = 0; k < 2; k++) {
            tft.drawLine(bx + bw - 1 + k, gy, bx + k, gy + mid, c);
            tft.drawLine(bx + k, gy + mid, bx + bw - 1 + k, gy + mid, c);
            tft.drawLine(bx + bw - 1 + k, gy + mid, bx + k, gy + l.glyphH - 1, c);
        }
    }
}

void uiBattery(const BatteryView& v) {
    if (v == batteryNow) return;
    batteryNow = v;
    if (sprReady && stripValid) batteryPaint();  // else the next strip paint draws it
}

// Takes the strip off the character view or the empty chat: the view
// repaints what it covered.
static void stripHide() {
    tickerOn = false;
    if (!stripValid) return;
    stripValid = false;
    transcriptRender();
}

static void statusStrip() {
    const int y0 = contentBottom();
    tft.fillRect(0, y0, tft.width(), metrics.status, uiInk(Ink::Bg));
    tft.drawFastHLine(0, y0, tft.width(), uiInk(Ink::Muted));
    batteryPaint();
    stripValid = true;
    labelEnd = 0;
}

void uiStatusCompact(bool on) {
    if (on == stripCompact) return;
    stripCompact = on;
    stripValid = false;                          // repaint whole on the next write
    chatValid = false;
    msgShown = "";
    tickerOn = false;
}

// Puts a label of width w at x without blanking the strip: clears only what
// the previous label covered beyond the new one, then `paint` draws the new
// label over its own background (paintText pushes the text's columns).
template <class Paint>
static void statusLabel(int x, int w, Paint paint) {
    if (!stripValid) statusStrip();
    const int y0 = contentBottom();
    if (labelEnd > x + w)
        tft.fillRect(x + w, y0 + 1, labelEnd - (x + w), metrics.status - 1, uiInk(Ink::Bg));
    paint();
    labelEnd = x + w + 4;                        // paintText may push 4 px of overhang
}

static void tickerPaint(int offset) {
    tickerOffset = offset;
    paintWindow(msgShown.c_str(), tickerW, statusLeft() - offset, TICKER_GAP, statusTextY(),
                uiInk(msgInk), uiInk(Ink::Bg), statusLeft(), statusRight());
}

static void showMsg(const char* msg, Ink ink, bool notice) {
    if (ink == Ink::Listen) characterMeterLabel(msg);   // the character view's countdown line
    uiFont(statusFont());
    String text = uiFold(String(msg));
    const int left = statusLeft(), right = statusRight();
    if (uiTextWidth(text) > right - left) text.replace("  ", " ");   // hints use double gaps: first give those up
    if (stripValid && !shimmerLabel.length() && text == msgShown && ink == msgInk &&
        notice == msgNotice)
        return;
    shimmerLabel = "";
    msgShown = text;
    msgInk = ink;
    msgNotice = notice;
    if (!stripWanted()) {
        stripHide();
        return;
    }
    const int w = uiTextWidth(text);
    if (w > right - left) {                      // too long: scroll it
        if (!stripValid) statusStrip();
        tickerOn = true;
        tickerW = w;
        tickerStart = millis();
        tickerPaint(0);
        labelEnd = right;
        return;
    }
    tickerOn = false;
    statusLabel(left, w, [&] { uiText(text, left, statusTextY(), ink, Ink::Bg); });
}

void uiMsg(const char* msg, Ink ink) { showMsg(msg, ink, false); }
void uiNotice(const char* msg, Ink ink) { showMsg(msg, ink, true); }

#ifdef BENCH_CMDS
void uiPanelInvert(bool on) {
    tft.invertDisplay(on);
    Serial.printf("[Bench] panel invert %s\n", on ? "on" : "off");
}

void uiPanelReg(uint8_t cmd, const uint8_t* data, int n) {
    tft.writecommand(cmd);
    for (int i = 0; i < n; i++) tft.writedata(data[i]);
    Serial.printf("[Bench] panel reg %02X +%d bytes\n", cmd, n);
}

void uiPanelTest(bool checker, bool shifted) {
    chatValid = false;                         // painted over: the next render is whole
    stripValid = false;
    if (!checker) {
        tft.fillScreen(0x8410);                // mid-gray
    } else {
        const int cell = 40, off = shifted ? cell / 2 : 0;
        for (int y = -off; y < tft.height(); y += cell)
            for (int x = -off; x < tft.width(); x += cell)
                tft.fillRect(x, y, cell, cell,
                             ((((x + off) / cell) + ((y + off) / cell)) & 1) ? TFT_WHITE : TFT_BLACK);
    }
    Serial.printf("[Bench] panel test %s%s\n", checker ? "checker" : "gray", shifted ? " 1" : "");
}

static uint32_t holdOffUntilMs = 0;            // loop: uiPanelFill pauses uiPanelHold
void uiPanelFill(uint16_t c) {
    chatValid = false;
    stripValid = false;
    tft.fillScreen(c);
    holdOffUntilMs = millis() + 600;
    Serial.printf("[Bench] panel fill %04x\n", c);
}

// Reads `bits` bits after command `cmd`, bit-banged on the panel's one
// bidirectional data line (SDA = TFT_MOSI): SCK and MOSI leave the SPI
// peripheral for the read and are attached back after it.
static uint64_t panelRead(uint8_t cmd, int bits) {
    spi_t* bus = TFT_eSPI::getSPIinstance().bus();
    spiDetachSCK(bus, TFT_SCLK);
    spiDetachMOSI(bus, TFT_MOSI);
    pinMode(TFT_SCLK, OUTPUT);
    pinMode(TFT_MOSI, OUTPUT);
    digitalWrite(TFT_SCLK, LOW);
    digitalWrite(TFT_CS, LOW);
    digitalWrite(TFT_DC, LOW);
    for (int i = 7; i >= 0; i--) {                // command, MSB first, latched on the rising edge
        digitalWrite(TFT_MOSI, (cmd >> i) & 1);
        delayMicroseconds(1);
        digitalWrite(TFT_SCLK, HIGH);
        delayMicroseconds(1);
        digitalWrite(TFT_SCLK, LOW);
    }
    digitalWrite(TFT_DC, HIGH);
    pinMode(TFT_MOSI, INPUT);
    uint64_t v = 0;
    for (int i = 0; i < bits; i++) {              // the panel drives SDA after each falling edge
        delayMicroseconds(1);
        digitalWrite(TFT_SCLK, HIGH);
        delayMicroseconds(1);
        v = (v << 1) | (uint64_t)digitalRead(TFT_MOSI);
        digitalWrite(TFT_SCLK, LOW);
    }
    digitalWrite(TFT_CS, HIGH);
    spiAttachSCK(bus, TFT_SCLK);
    spiAttachMOSI(bus, TFT_MOSI);
    return v;
}

// Raw bit counts: RDDST has a dummy clock before its 32 bits; the one-byte
// reads are taken with 9 clocks (a possible dummy bit, then 8).
void uiPanelStatus() {
    Serial.printf("[Bench] panel st=%09llx dpm=%03x madctl=%03x colmod=%03x dim=%03x\n",
                  (unsigned long long)panelRead(0x09, 33), (unsigned)panelRead(0x0A, 9),
                  (unsigned)panelRead(0x0B, 9), (unsigned)panelRead(0x0C, 9),
                  (unsigned)panelRead(0x0D, 9));
}
#endif

// The panel drops its inversion on some pixel streams (confirmed by reading
// RDDIM back: rows of "Chats" in Rose Pine dark's Muted ink turn it off; on the
// T-Deck a camera sees every Rose Pine dark menu repaint flash white), and with
// inversion off every color shows inverted. Flat fills never do it (camera,
// every palette color and Muted blend). INVON on an inverted panel changes
// nothing, so it is re-sent after every text line push (uiText, the status
// strip), which keeps a dropped inversion to one line's push, and after each
// loop pass's drawing for everything else.
void uiPanelHold() {
#ifdef BENCH_CMDS
    if ((int32_t)(millis() - holdOffUntilMs) < 0) return;   // a panel fill test is showing
#endif
    tft.writecommand(TFT_INVON);
}

void uiTick() {
    // A screen clear (view or theme switch, wake) took the strip with it: put
    // the last message back. The shimmer repaints itself on its next frame.
    if (!stripValid && sprReady && !shimmerLabel.length() && stripWanted()) {
        const String text = msgShown;
        msgShown = "";
        showMsg(text.c_str(), msgInk, msgNotice);
    }
    // The compose cursor blinks like a terminal's (uiComposeSet repaints the
    // band when the text changes; this only flips the block).
#if CHIRP_COMPOSE
    if (composeOn() && sprReady && millis() - composeBlinkMs >= 530) {
        composeBlinkMs = millis();
        composeCursorOn = !composeCursorOn;
        composePaint();
    }
#endif
    if (!tickerOn || !stripValid) return;
    const uint32_t now = millis();
    if (now - tickerLast < TICKER_FRAME_MS) return;
    tickerLast = now;
    const uint32_t t = now - tickerStart;
    int offset = 0;
    if (t > TICKER_PAUSE_MS) {
        offset = (int)((t - TICKER_PAUSE_MS) * TICKER_PX_PER_S / 1000);
        if (offset >= tickerW + TICKER_GAP) {     // the copy is now where the text began
            tickerStart = now;
            offset = 0;
        }
    }
    if (offset == tickerOffset) return;          // paused: nothing moved
    uiFont(statusFont());
    tickerPaint(offset);
}

// One sprite push per frame (the text's own columns, background included),
// so a frame repaints in place without flicker; the strip is never blanked.
void uiShimmer(const char* label) {
    uiFont(statusFont());
    const int n = (int)strlen(label);
    const int x = statusLeft();
    shimmerLabel = label;
    msgShown = "";
    msgInk = Ink::Busy;
    msgNotice = false;
    tickerOn = false;
    if (!stripWanted()) {
        stripHide();
        return;
    }
    const uint32_t now = millis();
    const uint16_t base = uiInk(Ink::Muted), lit = uiInk(Ink::Busy);
    statusLabel(x, uiTextWidth(label), [&] {
        paintText(label, x, statusTextY(), uiInk(Ink::Bg), [=](int i) {
            return blend565(base, lit, shimmerLevel(i, n, now));
        });
    });
}

// ---------------------------------------------------------------- live row
// The chat view's newest row while someone has the floor: the owner's level
// bars while a capture shows and three dots while voice mode waits for them
// (their side, right), the agent's green wave while it speaks (its side,
// left), under a rule like the ones between turns. main.cpp drives it every
// loop (transcriptLive); the rows above shrink by its height while it shows.
static LiveRow liveRow = LiveRow::None;
static const int LIVE_MAX_BARS = 24, LIVE_DOT = 6;
static uint8_t liveBar[LIVE_MAX_BARS], liveBarShown[LIVE_MAX_BARS];
static uint8_t liveLevel = 0, livePeak = 0;    // mic level (decaying), playback peak per step
static uint32_t liveStepMs = 0;
static int liveDot = 0, liveDotShown = -1;

struct LiveBars {
    int count, w, gap;
    bool right;     // the owner's side
    Ink ink;
};
static LiveBars liveBars() {
    return liveRow == LiveRow::Wave ? LiveBars{24, 3, 2, false, Ink::Ok}
                                    : LiveBars{12, 5, 3, true, Ink::Listen};
}

// Drawn right under the last text row (liveTop, set by transcriptRender).
// The agent's wave keeps a blank line under its answer, then the wave (two
// lines); the owner's rows are one line under a rule, their bars or dots
// centred between that rule and the status strip.
static int liveTop = 0;
static int liveHeight() {
    return liveRow == LiveRow::None ? 0 : liveRow == LiveRow::Wave ? 2 * metrics.line : metrics.line;
}
// The typed compose line (board.h): a band of its own above the status
// strip, only in the chat view (the menus keep the compact strip). ui.cpp
// owns the pixels; main.cpp owns the buffer and calls uiComposeSet.
#if CHIRP_COMPOSE
static bool composeOn() { return !stripCompact && !characterOn(); }
static int composeH() { return metrics.status; }
static String composeText;
static int composeCursorX = 0;                     // where the cursor block sits

static void composePaint() {
    const int y0 = contentBottom() - composeH();
    TFT_eSPI& t = tft;
    t.fillRect(0, y0, t.width(), composeH(), uiInk(Ink::Bg));
    t.drawFastHLine(0, y0, t.width(), uiInk(Ink::Faint));
    uiFont(UiFont::Body);
    const char* prompt = "> ";
    const int px = 6, pw = uiTextWidth(prompt);
    String line = String(prompt) + uiFit(composeText, t.width() - px - pw - 8);
    uiText(line, px, y0 + (composeH() - bodyCut().box) / 2, Ink::Text, Ink::Bg);
    composeCursorX = px + uiTextWidth(line);
    if (composeCursorOn)
        t.fillRect(composeCursorX + 1, y0 + 2, 3, composeH() - 4, uiInk(Ink::Accent));
}
void uiComposeSet(const char* text) {
    if (!CHIRP_COMPOSE) return;
    const String s = text ? text : "";
    if (s == composeText) return;
    composeText = s;
    if (composeOn() && sprReady) composePaint();
}
#else
static bool composeOn() { return false; }
static int composeH() { return 0; }
void uiComposeSet(const char*) {}
#endif
static int areaBottom() {
    return (stripWanted() ? contentBottom() : tft.height()) - (composeOn() ? composeH() : 0);
}

// ---------------------------------------------------------------- chat regions
// Where the companion goes on this panel and the rectangle the transcript
// gets (ui_layout.h, host-tested on every common panel). The x extent is
// fixed per panel, so the transcript wraps once to it whichever view is up;
// the companion is drawn only while the chat view is, and a top band pushes
// the transcript down.
static ui_layout::Chat chatLay() {
    return ui_layout::chatLayout(tft.width(), tft.height(), CONTENT_Y, contentBottom() - composeH(),
                                 CHIRP_BANNER);
}
// The intro screen (nothing said yet, no turn in flight) is the logo and the
// hints across the whole width; the companion joins once something happens.
static bool introShown() { return history.empty() && !pendingActive; }
static bool companionShown() { return CHIRP_BANNER && !characterOn() && !introShown(); }
// The first transcript row: under a top band when it shows, with the taller
// panels' margin (ui_layout.h ROOMY_TOP) either way.
static int topY() {
    return ui_layout::chatLayout(tft.width(), tft.height(), CONTENT_Y, contentBottom() - composeH(),
                                 companionShown())
        .chat.y;
}
static int chatX0() { return chatLay().chat.x; }
static int chatX1() { return chatLay().chat.right(); }
// What the empty screen and the live row span: the whole width on the intro
// screen, the chat otherwise. Rows keep the chat's (they wrap once to it).
static int viewX0() { return introShown() ? 0 : chatX0(); }
static int viewX1() { return introShown() ? tft.width() : chatX1(); }

ui_layout::Companion uiCompanion(ui_layout::Rect& area, int& scale) {
    const ui_layout::Chat c = chatLay();
    if (!companionShown() || c.companion == ui_layout::Companion::None) return ui_layout::Companion::None;
    area = c.comp;
    scale = c.scale;
    return c.companion;
}
static int textBottom() { return areaBottom() - liveHeight(); }
static int liveAreaTop() { return liveRow == LiveRow::Wave ? liveTop + metrics.line : liveTop + 2; }
static int liveAreaBottom() {
    return liveRow == LiveRow::Wave ? liveTop + 2 * metrics.line : areaBottom();
}
static int liveHalfMax() {
    const int half = (liveAreaBottom() - liveAreaTop()) / 2 - 2;
    return constrain(half, 2, metrics.line / 2 + 2);
}

static void paintLive(bool full) {
    if (liveRow == LiveRow::None || characterOn() || !sprReady) return;
    const int right = viewX1() - 12;
    const int mid = (liveAreaTop() + liveAreaBottom()) / 2, hmax = liveHalfMax();
    const uint16_t bg = uiInk(Ink::Bg);
    if (full) {
        tft.fillRect(viewX0(), liveTop, viewX1() - viewX0(), liveAreaBottom() - liveTop, bg);
        if (liveRow != LiveRow::Wave)                 // the wave belongs to the answer above it
            tft.drawFastHLine(viewX0(), liveTop + 1, viewX1() - viewX0(), uiInk(Ink::Muted));
        memset(liveBarShown, 0xFF, sizeof liveBarShown);
        liveDotShown = -1;
    }
    if (liveRow == LiveRow::Dots) {
        if (liveDot == liveDotShown) return;
        const uint16_t lit = uiInk(Ink::Listen), dim = blend565(bg, lit, 90);
        const int x0 = right - (3 * LIVE_DOT * 2 - LIVE_DOT);
        for (int i = 0; i < 3; i++)
            tft.fillRect(x0 + i * LIVE_DOT * 2, mid - LIVE_DOT / 2, LIVE_DOT, LIVE_DOT,
                         i == liveDot ? lit : dim);
        liveDotShown = liveDot;
        return;
    }
    const LiveBars b = liveBars();
    const int span = b.count * (b.w + b.gap) - b.gap;
    const int x0 = b.right ? right - span : viewX0() + 6;
    const uint16_t c = uiInk(b.ink);
    for (int i = 0; i < b.count; i++) {
        if (liveBarShown[i] == liveBar[i]) continue;
        const int x = x0 + i * (b.w + b.gap);
        tft.fillRect(x, mid - hmax, b.w, 2 * hmax + 1, bg);
        const int half = min((int)liveBar[i], hmax);
        tft.fillRect(x, mid - half, b.w, 2 * half + 1, c);
        liveBarShown[i] = liveBar[i];
    }
}

void transcriptLive(LiveRow row, uint8_t level, bool visible) {
    liveLevel = level > liveLevel ? level : (uint8_t)(liveLevel * 3 / 4);
    if (level > livePeak) livePeak = level;
    if (row != liveRow) {
        liveRow = row;
        memset(liveBar, 1, sizeof liveBar);
        livePeak = 0;
        liveLevel = level;                         // the other side's level does not carry over
        if (visible && !characterOn()) transcriptRender();
        return;
    }
    if (row == LiveRow::None || !visible || characterOn()) return;
    const uint32_t now = millis();
    if (row == LiveRow::Dots) {
        liveDot = (int)((now / 350) % 3);
        paintLive(false);
    } else if (now - liveStepMs >= 133) {          // 7.5 Hz, like the character view
        liveStepMs = now;
        const int n = liveBars().count;
        memmove(liveBar, liveBar + 1, n - 1);
        liveBar[n - 1] = (uint8_t)waveHalf(row == LiveRow::Wave ? livePeak : liveLevel, liveHalfMax());
        livePeak = 0;
        paintLive(false);
    }
}

// ---------------------------------------------------------------- layout
static int visibleRows() { return (textBottom() - topY()) / metrics.line; }

static void pushLine(LineStore& out, const String& line, uint8_t kind) {
    out.push(line.c_str(), line.length(), kind);
}

static void wrapToLines(const PsString& raw, uint8_t kind, LineStore& out) {
    uiFont(UiFont::Body);
    const String text = foldRaw(raw.data(), raw.size());
    if (!text.length()) return;
    const int w = chatX1() - chatX0() - 16;       // right pad keeps clear of scrollbar
    const int spaceW = uiTextWidth(" ");
    int i = 0;
    while (i < (int)text.length()) {
        int br = text.indexOf('\n', i);
        if (br < 0) br = text.length();
        int cur = 0;
        String line;
        int wordStart = i;
        while (wordStart < br) {
            int sp = text.indexOf(' ', wordStart);
            if (sp < 0 || sp > br) sp = br;
            String word = text.substring(wordStart, sp);
            int ww = uiTextWidth(word);
            if (cur > 0 && cur + spaceW + ww > w) {
                pushLine(out, line, kind);
                line = "";
                cur = 0;
            }
            if (ww > w) {                                 // hard-split long token
                const char* p = word.c_str();
                const char* wend = p + word.length();
                while (p < wend) {
                    const char* q = p;
                    utf8Next(p, wend);                    // one whole code point
                    String ch;
                    for (const char* c = q; c < p; c++) ch += *c;
                    const int cw = uiTextWidth(ch);
                    if (cur + cw > w) {
                        pushLine(out, line, kind);
                        line = "";
                        cur = 0;
                    }
                    line += ch;
                    cur += cw;
                }
            } else {
                if (cur > 0) { line += ' '; cur += spaceW; }
                line += word;
                cur += ww;
                const char last = word[word.length() - 1];
                if (last == '.' || last == '!' || last == '?' ||
                    last == ';' || last == ':')
                    cur += spaceW / 2;                    // sentence pause
            }
            wordStart = sp + 1;
        }
        if (line.length() || wordStart == i) pushLine(out, line, kind);
        i = br + 1;
    }
}

static void layoutLog() {
    logLines.clear();
    turnRows.clear();
    turnRows.reserve(HISTORY_MAX + 1);
    for (const StoredTurn& t : history) {
        PartRows r;
        r.q = (int)logLines.size();
        wrapToLines(t.q, 0, logLines);
        r.qn = (int)logLines.size() - r.q;
        logLines.push("", 0, 2);
        r.a = (int)logLines.size();
        wrapToLines(t.a, 1, logLines);
        r.an = (int)logLines.size() - r.a;
        logLines.push("", 0, 2);
        turnRows.push_back(r);
    }
    while (!logLines.empty() && logLines.lastKind() == 2) logLines.popBack();
}

static void layoutPending();
// Wrapping is redone lazily after a font change (the menu is up while the
// font is picked; the transcript is only needed once it is shown again).
static void ensureLayout() {
    if (!layoutStale) return;
    layoutStale = false;
    layoutLog();
    layoutPending();
}

static int totalRows() {
    ensureLayout();
    return (int)logLines.size() + (pendingActive ? (int)pendingLines.size() : 0);
}

struct LogRow { const char* text; uint8_t kind; };   // text lives in the store until it changes
static bool getLogRow(int idx, LogRow& out) {
    if (idx < (int)logLines.size()) { out = {logLines.text(idx), logLines.kind(idx)}; return true; }
    idx -= (int)logLines.size();
    if (idx < (int)pendingLines.size()) { out = {pendingLines.text(idx), pendingLines.kind(idx)}; return true; }
    return false;
}

static void layoutPending() {
    pendingLines.clear();
    if (!pendingActive) return;
    const int base = (int)logLines.size();
    if (!logLines.empty()) pendingLines.push("", 0, 2);   // divider above
    pendingRows.q = base + (int)pendingLines.size();
    wrapToLines(pendingQ, 0, pendingLines);
    pendingRows.qn = base + (int)pendingLines.size() - pendingRows.q;
    pendingLines.push("", 0, 2);
    pendingRows.a = base + (int)pendingLines.size();
    wrapToLines(pendingA, 1, pendingLines);
    pendingRows.an = base + (int)pendingLines.size() - pendingRows.a;
    while (!pendingLines.empty() && pendingLines.lastKind() == 2) pendingLines.popBack();
}

// Minimal scrollbar: 3px thumb on the right edge, only when scrollable. It
// counts the same rows as the window (the in-flight turn too) and stays in
// its track above the live row; the whole lane is cleared first.
static void drawScrollbar() {
    const int total = totalRows();
    const int vis = visibleRows();
    const int x = chatX1() - 4, trackH = textBottom() - topY();
    tft.fillRect(x, topY(), 3, trackH, uiInk(Ink::Bg));
    if (total <= vis) return;
    const int thumbH = max(8, trackH * vis / total);
    const int span = total - vis;
    const int thumbY = topY() + (trackH - thumbH) * constrain(scrollLine, 0, span) / span;
    tft.fillRect(x, thumbY, 3, thumbH, uiInk(Ink::Muted));
}

// Single renderer of the visible window, pinned to the bottom while
// following. After a transition (chatValid false) it clears the area and
// paints the companion; otherwise each row repaints in place: its leftover
// strips first, then the text block over them, so no pixel blanks before
// its final value and a streaming answer does not flash.
void transcriptRender() {
    if (characterOn()) {                           // the character view replaces the transcript
        chatValid = false;
        characterPaint();
        return;
    }
    const int vis = visibleRows();
    const bool full = !chatValid;
    const uint16_t bg = uiInk(Ink::Bg);
#if CHIRP_BANNER
    bool companionPainted = true;
    if (full) companionPainted = characterBannerPaint();   // its animation tick keeps it current
#else
    const bool companionPainted = true;
#endif
    if (totalRows() == 0) {                        // empty chat: the logo, then the hints
        if (!stripWanted()) {                      // no strip here: painted over, not kept
            stripValid = false;
            tickerOn = false;
        }
        chatValid = false;                         // the first row repaints whole over the logo
        const int bottom = areaBottom();
        tft.fillRect(viewX0(), topY(), viewX1() - viewX0(), bottom - topY(), bg);
        // The logo as large as fits with the hints and a live row under them;
        // the group is centred in the space above the live row, so it moves up
        // only once something happens (you talk, voice mode waits), and keeps
        // its size.
        const int hintsH = 2 * metrics.line, gap = 10;
        const int cx0 = viewX0(), cw = viewX1() - cx0;
        const int scale = logoScaleFor(cw - 12,
                                       bottom - topY() - hintsH - gap - metrics.line - 4);
        const int logoH = logoHeight(scale);
        const int top = topY() + max(0, (textBottom() - topY() - logoH - gap - hintsH) / 2);
        logoDraw(cx0 + (cw - logoWidth(scale)) / 2, top, scale);
        uiFont(UiFont::Body);
        const String h1 = uiFit(CHIRP_HINT1, cw - 12);   // a wide font never runs past the area
        const String h2 = uiFit(CHIRP_HINT2, cw - 12);
        const int hy = top + logoH + gap;
        uiText(h1, cx0 + (cw - uiTextWidth(h1)) / 2, hy, Ink::Muted, Ink::Bg);
        uiText(h2, cx0 + (cw - uiTextWidth(h2)) / 2, hy + metrics.line, Ink::Muted, Ink::Bg);
        liveTop = textBottom();
        paintLive(true);
#if CHIRP_COMPOSE
        if (composeOn() && sprReady) composePaint();
#endif
        return;
    }
    int start = follow ? max(0, totalRows() - vis)
                       : constrain(scrollLine, 0, max(0, totalRows() - vis));
    scrollLine = start;
    const int x0 = chatX0(), x1 = chatX1(), rowEnd = x1 - 4;   // the scrollbar lane paints itself
    if (full) tft.fillRect(x0, topY(), x1 - x0, areaBottom() - topY(), bg);
    uiFont(UiFont::Body);
    int drawn = 0;
    for (int r = 0; r < vis; r++) {
        LogRow ln;
        if (!getLogRow(start + r, ln)) break;
        const int y = topY() + r * metrics.line;
        if (ln.kind == 2) {
            if (!full) tft.fillRect(x0, y, rowEnd - x0, metrics.line, bg);
            tft.drawFastHLine(x0, y + metrics.line / 2, x1 - x0, uiInk(Ink::Muted));
        } else {
            const int tw = uiTextWidth(ln.text);           // the agent left, the owner right
            const int x = ln.kind == 0 ? max(x0 + 6, x1 - 12 - tw) : x0 + 6;
            if (!full) {
                tft.fillRect(x0, y, x - x0, metrics.line, bg);
                if (rowEnd > x + tw) tft.fillRect(x + tw, y, rowEnd - (x + tw), metrics.line, bg);
            }
            uiText(ln.text, x, y, ln.kind == 0 ? Ink::Text : Ink::Strong, Ink::Bg);
        }
        drawn++;
    }
    liveTop = topY() + drawn * metrics.line;        // the live row: the next line
    if (!full) {                                    // below the rows: what the live row leaves
        const int rest = liveRow == LiveRow::None ? liveTop : liveAreaBottom();
        if (areaBottom() > rest) tft.fillRect(x0, rest, rowEnd - x0, areaBottom() - rest, bg);
    }
    paintLive(true);
    drawScrollbar();                                // after the live row, which spans the width
#if CHIRP_COMPOSE
    if (composeOn() && sprReady) composePaint();
#endif
    chatValid = companionPainted;                   // no avatar yet: paint it whole next time
}

// ---------------------------------------------------------------- transcript
// Transcript changes repaint through whichever view is up: the transcript
// redraws whole, the character view only its card (and follows the newest
// text when pinned).
static void refresh() {
    if (characterOn()) characterTextChanged();
    else transcriptRender();
}

static void refreshPinned() {
    if (characterOn()) characterFollow();
    else transcriptRender();
}

bool transcriptEmpty() { return history.empty(); }
size_t transcriptTurnCount() { return history.size(); }
uint32_t transcriptRevision() { return historyRev; }

bool transcriptTurn(size_t i, TurnText& out) {
    if (i >= history.size()) return false;
    out = {history[i].q.data(), history[i].q.size(), history[i].a.data(), history[i].a.size()};
    return true;
}

bool transcriptPending(TurnText& out) {
    if (!pendingActive) return false;
    out = {pendingQ.data(), pendingQ.size(), pendingA.data(), pendingA.size()};
    return true;
}

void transcriptClear() {
    history.clear();
    historyRev++;
    logLines.clear();
    pendingQ.clear();
    pendingA.clear();
    pendingActive = false;
    pendingLines.clear();
    follow = true;
    scrollLine = 0;
    refreshPinned();                               // empty -> READY screen
}

static StoredTurn stored(const String& q, const String& a) {
    return {PsString(q.c_str(), q.length()), PsString(a.c_str(), a.length())};
}

void transcriptLoad(std::vector<Turn>&& turns) {
    history.clear();
    history.reserve(turns.size());
    for (const Turn& t : turns) history.push_back(stored(t.q, t.a));
    turns.clear();
    historyRev++;
    pendingActive = false;
    pendingLines.clear();
    layoutLog();
    transcriptFollow();
}

void transcriptCommit(const String& q, const String& a) {
    history.push_back(stored(q, a));
    if (history.size() > HISTORY_MAX) history.erase(history.begin());
    historyRev++;
    pendingActive = false;
    pendingLines.clear();
    layoutLog();
    transcriptFollow();
}

void transcriptPendingBegin() {
    pendingQ.assign("…");             // reserve the Q row so the chat scrolls up
    pendingA.clear();
    pendingActive = true;
    follow = true;
    layoutPending();
    refreshPinned();
}

void transcriptPendingSet(const char* q, const char* a) {
    pendingQ.assign(q[0] ? q : "…");
    pendingA.assign(a);
    layoutPending();
    refresh();
}

void transcriptPendingCancel() {
    pendingActive = false;
    pendingLines.clear();
    refresh();
}

void transcriptScroll(int rows) {
    if (characterOn()) {
        characterScroll(rows);
        return;
    }
    follow = false;
    const int maxTop = max(0, totalRows() - visibleRows());
    const int ns = constrain(scrollLine - rows, 0, maxTop);
    if (ns != scrollLine) {
        scrollLine = ns;
        transcriptRender();
    }
}

void transcriptFollow() {
    follow = true;
    refreshPinned();
}

// The chat view's place for a view switch: the newest while following, else
// the turn part at the top of the window.
ChatAnchor transcriptAnchor() {
    ensureLayout();
    if (follow) return anchorNewest();
    ChatAnchor a;
    for (size_t i = 0; i < turnRows.size(); i++)
        if (anchorInTurn(turnRows[i], (int)i, scrollLine, a)) return a;
    if (pendingActive && anchorInTurn(pendingRows, -1, scrollLine, a)) return a;
    return anchorNewest();
}

// Puts that place at the top of the window on the next render (the newest
// when the turn is gone).
void transcriptScrollTo(const ChatAnchor& a) {
    ensureLayout();
    const PartRows* r = a.turn >= 0 && a.turn < (int)turnRows.size() ? &turnRows[a.turn]
                      : a.turn == -1 && pendingActive               ? &pendingRows
                                                                    : nullptr;
    if (a.newest || !r) {
        follow = true;
        return;
    }
    follow = false;
    scrollLine = anchorRow(*r, a);
}

void transcriptPosition(int& top, int& rows, bool& following) {
    top = scrollLine;
    rows = totalRows();
    following = follow;
}
