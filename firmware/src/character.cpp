#include "character.h"

#include <Preferences.h>

#include <vector>
#ifdef BENCH_CMDS
#include <mbedtls/base64.h>
#endif

#include "avatar.h"
#include "bubbles.h"
#include "capture.h"
#include "config.h"
#include "expiry.h"
#include "menu.h"
#include "power.h"
#include "settings.h"
#include "speech.h"
#include "textfold.h"
#include "turn.h"
#include "ui.h"
#include "voice.h"

// ---------------------------------------------------------------- layout
// The view takes the whole screen (no status strip). Landscape (wide panel):
// the sprite on the left, centred vertically, the bubble column on the right
// (bubbles.h). Portrait (tall panel): the sprite centred at the top, the
// column under it, nearly full width. Everything derives from the panel's
// rotated size, so a board just picks a rotation.
static const int SCALE = 4;                       // 32x32 logical -> 128x128 px
static const int SB_LANE = 4;                     // scrollbar (3 px) at the column's right edge
static const int BORDER = 2;                      // bubble outline, also the corner notch
static const int TAIL_ROWS = 4, TAIL_STEP = 2;    // the tail's stair: 4 steps of 2 px

static bool layoutTall() { return uiTft().height() > uiTft().width(); }
static int sprX() { return layoutTall() ? (uiTft().width() - avatar::W * SCALE) / 2 : 12; }
static int sprY() { return layoutTall() ? 8 : (uiTft().height() - avatar::H * SCALE) / 2; }
static int colX() { return layoutTall() ? 4 : 12 + avatar::W * SCALE + 6; }
static int colW() { return uiTft().width() - colX() - 2; }
static int colY() { return layoutTall() ? 8 + avatar::H * SCALE + 6 : 0; }
static int colView() { return uiTft().height() - colY(); }

static bubbles::Geometry geom() {
    // colW, tail, inset (owner), agentInset, padX, padY, lineH, gap, margin
    return {colW() - SB_LANE, 8, 10, 5, 6, 5, uiLineHeight(), 6, 6};
}

// Meters, drawn in the live bubble: the listening bars in the owner's bubble
// at the bottom while recording, the speaking wave under the answer, and
// three dots taking turns to light while voice mode waits for the owner.
static const int BAR_COUNT = 12, BAR_W = 6, BAR_GAP = 4, BAR_MAX = 12;    // half height
static const int BAR_BAND = 2 * BAR_MAX + 5;
static const int WAVE_COUNT = 24, WAVE_W = 3, WAVE_GAP = 2, WAVE_MAX = 9;
static const int WAVE_BAND = 2 * WAVE_MAX + 5;
static const int DOT_COUNT = 3, DOT = 6, DOT_GAP = 6;
static const uint32_t DOT_STEP_MS = 350;          // one dot lit, moving on every step
enum Band : uint8_t { BandNone, BandBars, BandWave, BandDots };
static int bandH(uint8_t b) {
    return b == BandBars ? BAR_BAND : b == BandWave ? WAVE_BAND : b == BandDots ? uiLineHeight() : 0;
}
static int bandW(uint8_t b) {
    return b == BandBars ? BAR_COUNT * (BAR_W + BAR_GAP) - BAR_GAP
         : b == BandWave ? WAVE_COUNT * (WAVE_W + WAVE_GAP) - WAVE_GAP
         : b == BandDots ? DOT_COUNT * (DOT + DOT_GAP) - DOT_GAP : 0;
}

static const char* VIEW_NS = "chirp";
static const char* VIEW_KEY = "view";             // uint8: 0 transcript, 1 character

// ---------------------------------------------------------------- text
// Word wrap to w px in the Body font: emit(line, width) per line. Newlines
// break; a token wider than w splits between code points.
template <class Emit>
static void wrapText(const String& text, int w, Emit emit) {
    uiFont(UiFont::Body);
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
            const String word = text.substring(wordStart, sp);
            const int ww = uiTextWidth(word);
            if (cur > 0 && cur + spaceW + ww > w) {
                emit(line, cur);
                line = "";
                cur = 0;
            }
            if (ww > w) {
                const char* p = word.c_str();
                const char* wend = p + word.length();
                while (p < wend) {
                    const char* q = p;
                    utf8Next(p, wend);                        // one whole code point
                    String ch;
                    for (const char* c = q; c < p; c++) ch += *c;
                    const int cw = uiTextWidth(ch);
                    if (cur + cw > w) {
                        emit(line, cur);
                        line = "";
                        cur = 0;
                    }
                    line += ch;
                    cur += cw;
                }
            } else if (word.length()) {
                if (cur > 0) { line += ' '; cur += spaceW; }
                line += word;
                cur += ww;
            }
            wordStart = sp + 1;
        }
        if (line.length()) emit(line, cur);
        i = br + 1;
    }
}

// Lines and widest line of text wrapped to the bubbles' text width.
struct Measure {
    uint16_t lines, width;
};
static Measure measure(const String& text, bubbles::Side side) {
    Measure m = {0, 0};
    wrapText(text, bubbles::maxTextW(geom(), side), [&m](const String&, int w) {
        m.lines++;
        if (w > m.width) m.width = (uint16_t)w;
    });
    return m;
}

// ---------------------------------------------------------------- state
static bool keptValid = false;                    // the agent has a kept look (else it shows its Original)
static avatar::Traits keptTraits;                 // the kept look; meaningful while keptValid
static int slotShown = 0;                         // the Chirper tab's browse position (avatar.h slots)
static bool previewing = false;
static uint32_t previewLast = 0, previewSince = 0;

static bool on = false;                           // the character view is selected
static bool ready = false;                        // avatar built for this agent
static bool needFull = true;                      // the content area must be painted whole

static avatar::Character figure;                  // ~7 KB: layers + scratch
static avatar::Traits figureTraits;
static uint8_t shown[avatar::PIXELS];             // what the panel shows
static uint8_t next[avatar::PIXELS];
static bool shownValid = false;
static uint16_t pal[avatar::SlotCount];           // panel byte order (pushPixels sends raw)
static avatar::State curState = avatar::Idle;
static uint32_t stateSince = 0, lastTick = 0, tickCount = 0;
static Expiry errWindow;                          // the failed-turn card, 2.5 s (expiry.h)
static String errQ, errLabel;
static uint8_t listenLevel = 0;                   // smoothed mic level
#ifdef BENCH_CMDS
static int forced = -1;
#endif

// Push cost, for the bench status line.
static uint32_t pushN = 0, pushSumUs = 0, pushMaxUs = 0;

// Bubble column. Each bubble names its text (a transcript turn's question or
// answer, the in-flight turn, the failed turn, or the idle hint) instead of
// holding it: the text stays in the transcript (PSRAM) and only the bubbles
// in view are wrapped again to paint.
enum Part : uint8_t { PartQ, PartA, PartErrQ, PartErrLabel, PartHint, PartMeter, PartWaiting, PartCaption,
                      PartThinking };
static const int16_t TURN_PENDING = -1, TURN_NONE = -2;
struct Bubble {
    int16_t turn;                                 // transcript index, TURN_PENDING or TURN_NONE
    uint8_t part;                                 // Part
    uint8_t ink;                                  // Ink of the text
    uint8_t band;                                 // Band under the text
    bubbles::Side side;
    uint16_t lines;
    bubbles::Box box;                             // in column coordinates
};
static std::vector<Bubble> column;
static int colTotal = 0, colTop = 0;              // column height, offset at the view's top
static bool pinned = true;                        // the wheel has not moved it
static TFT_eSprite* colSpr = nullptr;             // the column composed off-panel (PSRAM), pushed whole
static ChatAnchor switchAnchor;                   // the place a view switch brought (anchor.h)
static bool anchorDue = false;                    // applied at the next layout

// Wrapped size of every completed turn, redone when the turns or the font change.
struct TurnMeasure {
    Measure q, a;
};
static std::vector<TurnMeasure> turnSizes;
static uint32_t sizesRev = 0;
static int sizesFont = -1;

// The live band as painted: sprite rows [bandY, bandY + bandH) at bandX.
static uint8_t bandShown = BandNone;
static int bandX = 0, bandY = 0, bandRows = 0;
static uint16_t bandFill = 0;
static Ink bandInk = Ink::Listen;                 // the lit dot's ink (dots bands)
static uint8_t barHalf[BAR_COUNT];                // meter heights (half height)
static uint8_t waveBar[WAVE_COUNT];               // speaking meter heights (half)
static uint8_t wavePeak = 0;                      // loudest playback level since the last bar
static bool waitShown = false;                    // the column holds the voice-mode waiting bubble
static bool thinkShown = false;                   // the column holds the agent's thinking bubble
static int dotLit = 0;                            // its lit dot
static int spokenLine = -1;                       // the line of the answer being spoken, held
                                                  // between chunks; -1 outside speaking

enum CardMode : uint8_t { CardText, CardBars, CardWave };
static CardMode cardMode = CardText;

// One-shot expression on top of the state (avatar.h Reaction).
static avatar::Reaction reactNow = avatar::ReactNone;
static uint32_t reactSince = 0;

bool characterOn() { return on && ready; }

// Chirper tab layout: the sprite at 3x (96 px, it must fit under the menu
// header), the labels to its right.
static const int AV_SCALE = 3, AV_X = 18, AV_TEXT_X = 132;

const char* characterStateName() {
    static const char* const N[] = {"idle", "listening", "thinking", "speaking", "error", "sleep"};
    return N[curState];
}

void characterStats(uint32_t& pushes, uint32_t& avgUs, uint32_t& maxUs) {
    pushes = pushN;
    avgUs = pushN ? pushSumUs / pushN : 0;
    maxUs = pushMaxUs;
}

// ---------------------------------------------------------------- setup
// The active agent's persona look from the host (hex of trait indexes; empty =
// none), stored next to its key.
static const char* agentLook() {
    const DeviceSettings& st = settingsStored();
    return st.active < st.agents.size() ? st.agents[st.active].look.c_str() : "";
}

static const char* agentName() { return settingsProfile().c_str(); }

// The agent's Original: its persona look over the seeded character.
static avatar::Traits originalTraits() {
    return avatar::traitsForAgent(agentName(), 0, agentLook());
}
static const avatar::Traits* keptPtr() { return keptValid ? &keptTraits : nullptr; }
// The agent's character: the kept look, else its Original.
static avatar::Traits currentTraits() { return keptValid ? keptTraits : originalTraits(); }
static int lowestSlot() { return keptValid ? avatar::KEPT_SLOT : 0; }

// The stored kept look (VIEW_NS, key from the profile name); none unless the
// blob decodes.
static void keptLoad() {
    keptValid = false;
    char key[16];
    avatar::keptKey(agentName(), key);
    Preferences prefs;
    if (!prefs.begin(VIEW_NS, true)) return;       // read-only open fails before first save
    uint8_t b[avatar::KEPT_BYTES];
    if (prefs.getBytesLength(key) == sizeof b && prefs.getBytes(key, b, sizeof b) == sizeof b)
        keptValid = avatar::keptDecode(b, sizeof b, keptTraits);
    prefs.end();
}

void characterBegin() {
    uint8_t view = 0;
    Preferences prefs;
    if (prefs.begin(VIEW_NS, true)) {              // read-only open fails before first save
        view = prefs.getUChar(VIEW_KEY, 0);
        prefs.end();
    }
    keptLoad();
    figureTraits = currentTraits();
    figure.build(figureTraits);
    ready = true;
    on = view == 1;
    needFull = true;
    shownValid = false;
    char tag[9];
    avatar::lookTag(figureTraits, tag);
    Serial.printf("[Character] view %s, agent %s, look %s\n", on ? "character" : "transcript",
                  agentLabel(settingsProfile()).c_str(), keptValid ? tag : "orig");
}

// The character view's place for a view switch (anchor.h): the newest while
// pinned, else the question or answer bubble at the top of the view.
static ChatAnchor characterAnchor() {
    if (pinned || column.empty()) return anchorNewest();
    const bubbles::Geometry g = geom();
    for (const Bubble& b : column) {
        if ((b.part != PartQ && b.part != PartA) || b.box.y + b.box.h <= colTop) continue;
        return {false, b.turn, (uint8_t)(b.part == PartA ? 1 : 0),
                permilleOf(bubbles::lineAtTop(g, b.box, colTop), b.lines)};
    }
    return anchorNewest();
}

// A view switch keeps the place: the view being left gives its anchor, the
// one coming up scrolls to it (the column applies it at its next layout).
void characterSetOn(bool value) {
    if (value == on) return;
    const ChatAnchor a = on ? characterAnchor() : transcriptAnchor();
    on = value;
    needFull = true;
    shownValid = false;
    if (!on) {
        transcriptScrollTo(a);
    } else if (a.newest) {
        pinned = true;
    } else {
        switchAnchor = a;
        anchorDue = true;
    }
    Preferences prefs;
    if (prefs.begin(VIEW_NS, false)) {
        prefs.putUChar(VIEW_KEY, on ? 1 : 0);
        prefs.end();
    }
    Serial.printf("[Character] view %s\n", on ? "character" : "transcript");
}

// ---------------------------------------------------------------- sprite
// The character's own colors (the theme supplies only the background, which
// also decides the silhouette edge), byte-swapped for the panel.
static void updatePalette() {
    uint16_t p[avatar::SlotCount];
    avatar::paletteFor(figureTraits, uiInk(Ink::Bg), p);
    for (int i = 0; i < avatar::SlotCount; i++) pal[i] = (uint16_t)((p[i] >> 8) | (p[i] << 8));
}

// One logical row expands to SCALE identical panel rows; a single window and
// one pushPixels per logical row keep the SPI busy with pixel data only. Rows
// at or below uiStripTop (the strip up over the character view) are skipped.
static void pushSprite(const avatar::Rect& r, const uint8_t* frame, int ox, int oy, int scale) {
    const uint32_t t0 = micros();
    TFT_eSPI& tft = uiTft();
    const int w = r.x1 - r.x0, pw = w * scale;
    const int top = oy + r.y0 * scale;
    const int rows = min((r.y1 - r.y0) * scale, uiStripTop() - top);
    if (rows <= 0) return;
    alignas(4) static uint16_t rowbuf[SCALE * avatar::W * SCALE + 2];   // +2: pushPixels reads whole words
    tft.startWrite();
    tft.setAddrWindow(ox + r.x0 * scale, top, pw, rows);
    int left = rows;
    for (int y = r.y0; y < r.y1 && left > 0; y++) {
        const uint8_t* src = frame + y * avatar::W + r.x0;
        uint16_t* d = rowbuf;
        for (int x = 0; x < w; x++)
            for (int k = 0; k < scale; k++) *d++ = pal[src[x]];
        for (int k = 1; k < scale; k++) memcpy(rowbuf + k * pw, rowbuf, pw * sizeof(uint16_t));
        const int n = min(scale, left);
        tft.pushPixels(rowbuf, n * pw);
        left -= n;
    }
    tft.endWrite();
    const uint32_t us = micros() - t0;
    pushN++;
    pushSumUs += us;
    if (us > pushMaxUs) pushMaxUs = us;
}

// ---------------------------------------------------------------- app state
static avatar::State wantedState(uint32_t now) {
#ifdef BENCH_CMDS
    if (forced >= 0) return (avatar::State)forced;
#endif
    if (expiryActive(errWindow, now)) return avatar::Error;
    switch (uiState) {
    case UI_RECORDING:
        if (appListeningShown()) return avatar::Listening;
        break;
    case UI_THINKING:                              // answer audio can start while Hermes streams
        return !turnTranscribing() && speechSpeaking() ? avatar::Speaking : avatar::Thinking;
    case UI_SPEAKING:
        return avatar::Speaking;
    default:
        break;
    }
    if (appListeningShown()) return avatar::Listening;
    return powerBacklight() == Backlight::Dim ? avatar::Sleep : avatar::Idle;
}

void characterReact(avatar::Reaction r) {
    if (r == avatar::ReactNone || r >= avatar::ReactionCount) return;
    reactNow = r;
    reactSince = millis();
}

static uint8_t levelFor(avatar::State s, uint32_t now) {
    const uint8_t played = speechLevel();          // always consumed: peak-hold per tick
#ifdef BENCH_CMDS
    if (forced >= 0 && (s == avatar::Speaking || s == avatar::Listening))
    {
        const uint8_t lvl = (uint8_t)(30 + ((now / 70) % 8) * 28);
        if (s == avatar::Speaking && lvl > wavePeak) wavePeak = lvl;
        return lvl;
    }
#endif
    (void)now;
    if (s == avatar::Speaking) {
        const uint8_t lvl = speechPlaying() ? played : 0;
        if (lvl > wavePeak) wavePeak = lvl;
        return lvl;
    }
    if (s == avatar::Listening) {
        const uint8_t raw = captureLevel();
        listenLevel = raw > listenLevel ? raw : (uint8_t)(listenLevel * 3 / 4);
        return listenLevel;
    }
    return 0;
}

// The draw context right now: state age, live level, STT flag, any reaction.
static avatar::Ctx ctxNow(uint32_t now) {
    avatar::Ctx ctx = {now, now - stateSince, levelFor(curState, now),
                       curState == avatar::Thinking && turnTranscribing()};
    if (reactNow != avatar::ReactNone) {
        const uint32_t rt = now - reactSince;
        if (rt < avatar::reactionMs(reactNow)) {
            ctx.react = reactNow;
            ctx.reactT = rt;
        } else {
            reactNow = avatar::ReactNone;
        }
    }
    return ctx;
}

// Render the current frame and push what changed (all of it when forced)
// into the buffer last shown at (ox, oy). Returns the level the frame used.
static uint8_t drawFrameInto(uint8_t* shownBuf, bool& valid, int ox, int oy, int scale,
                             uint32_t now, bool all) {
    const avatar::Ctx ctx = ctxNow(now);
    figure.render(curState, ctx, next);
    avatar::Rect r;
    if (all || !valid) {
        r.x0 = 0;
        r.y0 = 0;
        r.x1 = avatar::W;
        r.y1 = avatar::H;
        pushSprite(r, next, ox, oy, scale);
        valid = true;
    } else if (avatar::dirtyRect(shownBuf, next, r)) {
        pushSprite(r, next, ox, oy, scale);
    }
    memcpy(shownBuf, next, avatar::PIXELS);
    return ctx.level;
}

static void drawFrame(uint32_t now, bool all) {
    drawFrameInto(shown, shownValid, sprX(), sprY(), SCALE, now, all);
}

// ---------------------------------------------------------------- banner
#if CHIRP_BANNER
// The chat view's companion banner (ui.cpp reserves uiBannerHeight()): the
// avatar at 4x, its state and a level meter, always visible while chatting.
// It shares the view's state machine and reactions; the transcript renders
// under it and its dirty-rect pushes animate between renders.
// The banner's scale follows the band ui.cpp reserved (uiBannerHeight).
static int bScale() { return max(1, (uiBannerHeight() - 8) / avatar::H); }
static const int B_X = 6, B_Y = 4;
static uint8_t bShown[avatar::PIXELS];
static bool bValid = false;
static uint32_t bLastTick = 0;
static int bMeterW = -1;                        // last painted meter fill

static int bannerMeterY() { return uiBannerHeight() - 16; }

static void bannerMetaPaint() {
    TFT_eSPI& tft = uiTft();
    const int x = B_X + avatar::W * bScale() + 8, w = tft.width() - x - 6;
    const int y = B_Y + 2;
    tft.fillRect(x, B_Y, w, bannerMeterY() - B_Y - 2, uiInk(Ink::Bg));   // a shorter word leaves nothing
    uiFont(UiFont::Body);
    const std::string& prof = settingsProfile();
    uiText(uiFit(prof.empty() ? "chirp" : prof.c_str(), w), x, y, Ink::Accent, Ink::Bg);
    uiFont(UiFont::Small);
    uiText(characterStateName(), x, y + uiLineHeight() + 2, Ink::Muted, Ink::Bg);
    bMeterW = -1;                               // the meter repaints with its band
}

static void bannerMeterPaint(uint8_t level, Ink ink) {
    TFT_eSPI& tft = uiTft();
    const int x = B_X + avatar::W * bScale() + 8, w = tft.width() - x - 6;
    const int y = bannerMeterY(), h = 8;
    const int fill = (w - 4) * level / 255;
    if (fill == bMeterW) return;
    bMeterW = fill;
    tft.fillRect(x, y, w, h, uiInk(Ink::Bg));
    tft.drawRect(x, y, w, h, uiInk(Ink::Muted));
    if (fill > 0) tft.fillRect(x + 2, y + 2, fill, h - 4, uiInk(ink));
}

void characterBannerPaint() {
    if (!ready) return;                         // no avatar built yet
    updatePalette();
    TFT_eSPI& tft = uiTft();
    const int h = uiBannerHeight();
    const uint32_t now = millis();
    const avatar::State s = wantedState(now);
    if (s != curState) {
        curState = s;
        stateSince = now;
    }
    tft.fillRect(0, 0, tft.width(), h, uiInk(Ink::Bg));
    drawFrameInto(bShown, bValid, B_X, B_Y, bScale(), now, true);
    bannerMetaPaint();
    tft.drawFastHLine(0, h - 1, tft.width(), uiInk(Ink::Faint));
    bLastTick = now;
}

static void characterBannerTick() {
    if (!ready || menuOpen()) return;
    const uint32_t now = millis();
    const bool trickle = powerAnimSlow() && (curState == avatar::Idle || curState == avatar::Sleep);
    if (now - bLastTick < (trickle ? (uint32_t)ANIM_SLOW_FRAME_MS : avatar::FRAME_MS)) return;
    if (powerBacklight() == Backlight::Off) return;   // panel dark: nothing to animate for
    bLastTick = now;
    const avatar::State s = wantedState(now);
    if (s != curState) {
        curState = s;
        stateSince = now;
        bannerMetaPaint();                      // the state word follows the turn
    }
    const uint8_t lvl = drawFrameInto(bShown, bValid, B_X, B_Y, bScale(), now, false);
    bannerMeterPaint(lvl, curState == avatar::Speaking ? Ink::Ok : Ink::Listen);
}
#endif

// ---------------------------------------------------------------- column layout
static bool errActive() { return expiryActive(errWindow, millis()); }

// A turn has its words (STT done) and no answer text yet, nothing speaking:
// the agent's thinking bubble shows. Tracked across ticks (characterTick)
// because STT -> LLM changes neither the avatar state nor the transcript.
static bool agentThinking() {
    TurnText pend;
    return transcriptPending(pend) && pend.an == 0 && cardMode != CardWave && !turnTranscribing();
}

static const char* HINT = "Hold the encoder to talk\nDouble-tap for voice mode";
static const char* EXIT_HINT = "Double-tap to exit";   // the caption under the waiting bubble

// The text a bubble shows, folded for the font.
static String bubbleText(const Bubble& b) {
    TurnText t;
    switch (b.part) {
    case PartErrQ: return uiFold(errQ);
    case PartErrLabel: return uiFold(errLabel);
    case PartHint: return String(HINT);
    case PartMeter: return String();
    default: break;
    }
    const bool have = b.turn == TURN_PENDING ? transcriptPending(t)
                                             : transcriptTurn((size_t)b.turn, t);
    if (!have) return String();
    uiFont(UiFont::Body);
    return b.part == PartQ ? uiFoldN(t.q, t.qn) : uiFoldN(t.a, t.an);
}

static void measureTurns() {
    const int font = (int)uiFontFamily() * 8 + uiFontSize();
    if (sizesRev == transcriptRevision() && sizesFont == font &&
        turnSizes.size() == transcriptTurnCount())
        return;
    sizesRev = transcriptRevision();
    sizesFont = font;
    turnSizes.clear();
    turnSizes.reserve(transcriptTurnCount());
    TurnText t;
    uiFont(UiFont::Body);
    for (size_t i = 0; transcriptTurn(i, t); i++)
        turnSizes.push_back({measure(uiFoldN(t.q, t.qn), bubbles::User),
                             measure(uiFoldN(t.a, t.an), bubbles::Agent)});
}

// Every bubble, top to bottom: the completed turns (question, then answer),
// the in-flight turn, a failed turn, the listening meter; the hint when there
// is nothing else. The speaking wave hangs under the newest answer. A pinned
// column rests at the bottom, or follows the speech (bubbles::speakTop).
static void layoutColumn() {
    measureTurns();
    const bubbles::Geometry g = geom();
    column.clear();
    int y = g.margin;
    auto add = [&](int16_t turn, uint8_t part, bubbles::Side side, Ink ink, uint8_t band, Measure m) {
        if (m.lines == 0 && band == BandNone) return;
        Bubble b = {turn, part, (uint8_t)ink, band, side, m.lines, {}};
        b.box = bubbles::place(g, side, m.lines, m.width, bandH(band), bandW(band), y);
        y = bubbles::below(g, b.box);
        column.push_back(b);
    };
    TurnText pend;
    const bool inFlight = transcriptPending(pend);
    const bool wave = cardMode == CardWave;
    const size_t n = turnSizes.size();
    for (size_t i = 0; i < n; i++) {
        add((int16_t)i, PartQ, bubbles::User, Ink::Text, BandNone, turnSizes[i].q);
        add((int16_t)i, PartA, bubbles::Agent, Ink::Strong,
            wave && !inFlight && i + 1 == n ? BandWave : BandNone, turnSizes[i].a);
    }
    if (inFlight) {
        uiFont(UiFont::Body);
        add(TURN_PENDING, PartQ, bubbles::User, Ink::Text, BandNone,
            measure(uiFoldN(pend.q, pend.qn), bubbles::User));
        add(TURN_PENDING, PartA, bubbles::Agent, Ink::Strong, wave ? BandWave : BandNone,
            measure(uiFoldN(pend.a, pend.an), bubbles::Agent));
    }
    thinkShown = agentThinking();
    if (thinkShown)                               // heard, no answer yet: it is thinking
        add(TURN_PENDING, PartThinking, bubbles::Agent, Ink::Busy, BandDots, {0, 0});
    if (errActive()) {
        add(TURN_NONE, PartErrQ, bubbles::User, Ink::Muted, BandNone, measure(uiFold(errQ), bubbles::User));
        add(TURN_NONE, PartErrLabel, bubbles::Agent, Ink::Err, BandNone,
            measure(uiFold(errLabel), bubbles::Agent));
    }
    if (cardMode == CardBars) {                   // the bars, the countdown line under them
        uiFont(UiFont::Body);                     // wide enough for the longest countdown
        add(TURN_NONE, PartMeter, bubbles::User, Ink::Listen, BandBars,
            {1, (uint16_t)uiTextWidth("Listening 00s")});
    }
    waitShown = appVoiceWaiting();
    if (waitShown) {                              // voice mode: your turn, it is listening
        add(TURN_NONE, PartWaiting, bubbles::User, Ink::Listen, BandDots, {0, 0});
        uiFont(UiFont::Small);                    // how to leave: a caption under it, right-aligned
        const int hw = uiTextWidth(EXIT_HINT), hh = uiFontHeight();
        const Bubble c = {TURN_NONE, PartCaption, (uint8_t)Ink::Muted, BandNone, bubbles::User, 0,
                          {g.colW - g.tail - hw, y - g.gap + 2, hw, hh}};
        column.push_back(c);
        y = c.box.y + hh + g.gap;
    }
    if (column.empty())
        add(TURN_NONE, PartHint, bubbles::Agent, Ink::Muted, BandNone, measure(HINT, bubbles::Agent));
    colTotal = column.empty() ? 0 : bubbles::total(g, &column.back().box);
    const int view = colView();
    if (anchorDue) {                              // a view switch: the place the chat view showed
        anchorDue = false;
        pinned = true;                            // unless its bubble is found
        for (const Bubble& b : column) {
            if (b.turn != switchAnchor.turn || (b.part != PartQ && b.part != PartA) ||
                (b.part == PartA) != (switchAnchor.part == 1))
                continue;
            colTop = bubbles::lineTop(g, b.box, lineAt(switchAnchor.permille, b.lines));
            pinned = false;
        }
    }
    if (pinned) {
        // The live answer (streaming in, or being spoken) is followed from its
        // start: line 0 until speech gets further, so the column never runs to
        // the streaming end and then jumps back up when the speech begins.
        colTop = bubbles::maxTop(colTotal, view);
        for (const Bubble& b : column)
            if (b.band == BandWave || (b.turn == TURN_PENDING && b.part == PartA))
                colTop = bubbles::speakTop(g, b.box, spokenLine > 0 ? spokenLine : 0, colTotal, view);
    }
    colTop = bubbles::clampTop(colTop, colTotal, view);
}

// ---------------------------------------------------------------- column paint
static bool columnReady() {
    if (!colSpr) {
        colSpr = new TFT_eSprite(&uiTft());
        colSpr->setColorDepth(16);
        if (!colSpr->createSprite(colW(), colView()))
            Serial.println("[Character] column sprite alloc failed");
    }
    return colSpr->created();
}

// Sprite rows [y0, y1) to the panel, above the strip when it is up.
static void pushColumn(int y0, int y1) {
    y1 = min(y1, uiStripTop());
    if (y1 <= y0) return;
    TFT_eSPI& tft = uiTft();
    tft.startWrite();
    colSpr->pushSprite(colX(), y0 + colY(), 0, y0, colW(), y1 - y0);
    tft.endWrite();
}

static void paintBand(TFT_eSprite& s) {
    if (bandShown == BandNone) return;
    s.fillRect(bandX, bandY, bandW(bandShown), bandRows, bandFill);
    const int mid = bandY + bandRows / 2;
    if (bandShown == BandBars) {
        const uint16_t c = uiInk(Ink::Listen);
        for (int i = 0; i < BAR_COUNT; i++)
            s.fillRect(bandX + i * (BAR_W + BAR_GAP), mid - barHalf[i], BAR_W, 2 * barHalf[i] + 1, c);
    } else if (bandShown == BandDots) {
        const uint16_t lit = uiInk(bandInk), dim = blend565(bandFill, lit, 90);
        for (int i = 0; i < DOT_COUNT; i++)
            s.fillRect(bandX + i * (DOT + DOT_GAP), mid - DOT / 2, DOT, DOT, i == dotLit ? lit : dim);
    } else {
        const uint16_t c = uiInk(Ink::Ok);
        for (int i = 0; i < WAVE_COUNT; i++)
            s.fillRect(bandX + i * (WAVE_W + WAVE_GAP), mid - waveBar[i], WAVE_W, 2 * waveBar[i] + 1, c);
    }
}

// The listening bubble's countdown line ("Listening 3s", "Closing 2s"): the
// latest Listen-ink status message (ui.cpp), centred under the bars.
static char meterLabel[24] = "";
static int labelX = 0, labelY = 0, labelW = 0;

static void paintMeterLabel(TFT_eSprite& s) {
    const int lineH = uiLineHeight();
    s.fillRect(labelX, labelY, labelW, lineH, bandFill);
    uiFont(UiFont::Body);
    const String text = uiFit(String(meterLabel), labelW);   // never past the bubble's edge
    const int w = uiTextWidth(text);
    uiTextInto(s, text.c_str(), labelX + max(0, (labelW - w) / 2), labelY, uiInk(Ink::Listen),
               bandFill);
}

// A comic bubble at sprite row y: a notched pixel outline, the text
// left-aligned inside, then the band. The owner's bubbles take an accent tint
// and a tail stepping out to the right near the bottom.
static void paintBubble(TFT_eSprite& s, const Bubble& b, int y) {
    if (b.part == PartCaption) {                          // bare text on the background
        uiFont(UiFont::Small);
        uiTextInto(s, EXIT_HINT, b.box.x, y, uiInk((Ink)b.ink), uiInk(Ink::Bg));
        return;
    }
    const bubbles::Geometry g = geom();
    const bool user = b.side == bubbles::User;
    const uint16_t surface = uiInk(Ink::Surface);
    const uint16_t fill = user ? blend565(surface, uiInk(Ink::Accent), 48) : surface;
    const uint16_t edge = user ? blend565(uiInk(Ink::Muted), uiInk(Ink::Accent), 110)
                               : uiInk(Ink::Muted);
    const int x = b.box.x, w = b.box.w, h = b.box.h;
    s.fillRect(x + BORDER, y, w - 2 * BORDER, h, edge);
    s.fillRect(x, y + BORDER, w, h - 2 * BORDER, edge);
    s.fillRect(x + BORDER, y + BORDER, w - 2 * BORDER, h - 2 * BORDER, fill);
    if (user) {                                           // the tail, toward the right edge
        bubbles::Box at = b.box;
        at.y = y;
        const int ty = bubbles::tailTop(g, at, TAIL_ROWS, TAIL_STEP);
        for (int i = 0; i < TAIL_ROWS; i++) {
            const int reach = bubbles::tailReach(g, i, TAIL_ROWS);
            const int ry = ty + i * TAIL_STEP;
            s.fillRect(x + w - BORDER, ry, reach + BORDER, TAIL_STEP, edge);
            if (i < TAIL_ROWS - 1) s.fillRect(x + w - BORDER, ry, reach, TAIL_STEP, fill);   // last: its bottom edge
        }
    }
    const int tx = x + g.padX, view = s.height();
    int ly = y + g.padY;
    if (b.lines && b.part != PartMeter) {
        const uint16_t fg = uiInk((Ink)b.ink);
        const int lineH = g.lineH;
        wrapText(bubbleText(b), bubbles::maxTextW(g, b.side), [&](const String& line, int) {
            if (ly + lineH > 0 && ly < view) uiTextInto(s, line.c_str(), tx, ly, fg, fill);
            ly += lineH;
        });
    }
    if (b.band != BandNone) {
        bandShown = b.band;
        bandX = tx + (w - 2 * g.padX - bandW(b.band)) / 2;
        bandY = ly;
        bandRows = bandH(b.band);
        bandFill = fill;
        bandInk = (Ink)b.ink;
        paintBand(s);
    }
    if (b.part == PartMeter) {                            // the countdown under the bars
        labelX = tx;
        labelY = ly + bandH(b.band);
        labelW = w - 2 * g.padX;
        paintMeterLabel(s);
    }
}

static void paintScrollbar(TFT_eSprite& s) {
    const int view = s.height();
    if (colTotal <= view) return;
    const int x = colW() - SB_LANE + 1, track = view - 8;
    const int thumb = max(10, track * view / colTotal);
    const int y = 4 + (track - thumb) * colTop / max(1, bubbles::maxTop(colTotal, view));
    s.fillRect(x, y, SB_LANE - 1, thumb, uiInk(Ink::Faint));
}

// The whole column into the sprite, then onto the panel in one push.
static void paintColumn() {
    if (!columnReady()) return;
    TFT_eSprite& s = *colSpr;
    s.fillSprite(uiInk(Ink::Bg));
    bandShown = BandNone;
    const int view = s.height();
    for (const Bubble& b : column)
        if (bubbles::visible(b.box, colTop, view)) paintBubble(s, b, b.box.y - colTop);
    paintScrollbar(s);
    pushColumn(0, view);
}

// The live meter only: its rows of the sprite, repainted and pushed.
static void paintMeter() {
    if (bandShown == BandNone || !columnReady()) return;
    paintBand(*colSpr);
    pushColumn(max(0, bandY), min((int)colSpr->height(), bandY + bandRows));
}

static void setCardMode(avatar::State s) {
    cardMode = s == avatar::Listening ? CardBars : s == avatar::Speaking ? CardWave : CardText;
}

// ---------------------------------------------------------------- public
void characterPaint() {
    if (!characterOn()) return;
    TFT_eSPI& tft = uiTft();
    tft.fillRect(0, 0, colX(), uiStripTop(), uiInk(Ink::Bg));
    updatePalette();
    const uint32_t now = millis();
    const avatar::State s = wantedState(now);
    if (s != curState) {
        curState = s;
        stateSince = now;
    }
    setCardMode(curState);
    memset(barHalf, 2, sizeof barHalf);
    memset(waveBar, 1, sizeof waveBar);
    layoutColumn();
    drawFrame(now, true);
    paintColumn();
    needFull = false;
    lastTick = now;
}

void characterTextChanged() {
    if (!characterOn()) return;
    if (menuOpen() || needFull) {                 // something else owns the content area
        needFull = true;
        if (!menuOpen()) characterPaint();
        return;
    }
    layoutColumn();
    paintColumn();
}

void characterFollow() {
    pinned = true;
    characterTextChanged();
}

void characterScroll(int rows) {
    if (!characterOn() || menuOpen() || needFull) return;
    int n = (rows < 0 ? -rows : rows) / 2;         // transcript units (4 / 8 / 16 per click): 2 / 4 / 8 lines
    if (n < 1) n = 1;
    if (rows < 0) n = -n;
    const int top = bubbles::clampTop(colTop - n * uiLineHeight(), colTotal, colView());
    pinned = false;
    if (top == colTop) return;
    colTop = top;
    paintColumn();
}

void characterMeterLabel(const char* text) {
    if (!strncmp(text, meterLabel, sizeof meterLabel - 1)) return;
    strncpy(meterLabel, text, sizeof meterLabel - 1);
    meterLabel[sizeof meterLabel - 1] = '\0';
    if (!characterOn() || menuOpen() || needFull || bandShown != BandBars || !columnReady()) return;
    paintMeterLabel(*colSpr);
    pushColumn(max(0, labelY), min((int)colSpr->height(), labelY + uiLineHeight()));
}

void characterError(const char* heard, const char* label) {
    errQ = heard ? heard : "";
    errLabel = label ? label : "";
    expiryArm(errWindow, millis(), 2500);
    if (!characterOn() || menuOpen() || needFull) return;
    const uint32_t now = millis();
    curState = avatar::Error;
    stateSince = now;
    setCardMode(curState);
    pinned = true;
    layoutColumn();
    paintColumn();
}

void characterTick() {
    expiryActive(errWindow, millis());            // disarm once over, view shown or not
#if CHIRP_BANNER
    if (!characterOn()) {
        characterBannerTick();                   // the chat view's banner keeps animating
        return;
    }
#else
    if (!characterOn()) return;
#endif
    if (menuOpen()) {                             // the menu paints over both halves
        needFull = true;
        return;
    }
    const uint32_t now = millis();
    if (needFull) {
        characterPaint();
        return;
    }
    // On battery, a character that has idled for a while ticks at a trickle
    // (the first input or turn restores the full rate within a frame).
    const bool trickle = powerAnimSlow() && (curState == avatar::Idle || curState == avatar::Sleep);
    if (now - lastTick < (trickle ? (uint32_t)ANIM_SLOW_FRAME_MS : avatar::FRAME_MS)) return;
    if (powerBacklight() == Backlight::Off) return;   // panel dark: nothing to animate for
    lastTick = now;
    tickCount++;

    const avatar::State s = wantedState(now);
    if (s != curState) {
        const avatar::State was = curState;
        curState = s;
        stateSince = now;
        // Reactions the state change itself implies: a turn begins (attentive),
        // the answer starts (pleased).
        if (s == avatar::Listening && (was == avatar::Idle || was == avatar::Sleep))
            characterReact(avatar::Surprised);
        else if (s == avatar::Speaking && was == avatar::Thinking)
            characterReact(avatar::Pleased);
        const CardMode before = cardMode;
        setCardMode(s);
        if (cardMode == CardBars) memset(barHalf, 2, sizeof barHalf);
        if (cardMode == CardWave) {
            memset(waveBar, 1, sizeof waveBar);
            wavePeak = 0;
        }
        if (cardMode != CardWave) spokenLine = -1;
        if (cardMode != before || was == avatar::Error) {
            if (cardMode == CardBars) pinned = true;   // a new turn: back to the newest
            layoutColumn();
            paintColumn();
        }
    }
    if (cardMode == CardWave) {                   // follow the speech one line at a time
        const int spoken = speechSpokenChars();
        int line = -1, lines = 0;
        for (const Bubble& b : column) {
            if (b.band != BandWave || spoken < 0) continue;
            TurnText t;
            const bool have = b.turn == TURN_PENDING ? transcriptPending(t)
                                                     : transcriptTurn((size_t)b.turn, t);
            lines = b.lines;
            line = bubbles::lineOf(spoken, have ? (int)t.an : 0, lines);
            break;
        }
        if (line >= 0 && line != spokenLine) {    // relayout after the loop: it rebuilds column
            spokenLine = line;
            if (pinned) {
                layoutColumn();
                paintColumn();
#ifdef BENCH_CMDS
                Serial.printf("[Character] follow line %d/%d top %d\n", line, lines, colTop);
#endif
            }
        }
    }
    if (agentThinking() != thinkShown) {          // STT finished, or the answer started
        layoutColumn();
        paintColumn();
#ifdef BENCH_CMDS
        Serial.printf("[Character] thinking dots %s\n", thinkShown ? "on" : "off");
#endif
    }
    if (appVoiceWaiting() != waitShown) {         // voice mode on/off, or its turn came back
        pinned = true;
        layoutColumn();
        paintColumn();
    }
    if (bandShown == BandDots && (int)((now / DOT_STEP_MS) % DOT_COUNT) != dotLit) {
        dotLit = (int)((now / DOT_STEP_MS) % DOT_COUNT);
        paintMeter();
    }
    drawFrame(now, false);
    if (cardMode == CardWave && (tickCount & 1) == 0) {   // meter at 7.5 Hz
        memmove(waveBar, waveBar + 1, WAVE_COUNT - 1);
        waveBar[WAVE_COUNT - 1] = (uint8_t)waveHalf(wavePeak, WAVE_MAX);
        wavePeak = 0;
        if (bandShown == BandWave) paintMeter();
    }
    if (cardMode == CardBars && (tickCount & 1) == 0) {   // meter at 7.5 Hz
        memmove(barHalf, barHalf + 1, BAR_COUNT - 1);
        barHalf[BAR_COUNT - 1] = (uint8_t)waveHalf(listenLevel, BAR_MAX);
        if (bandShown == BandBars) paintMeter();
    }
}

// ---------------------------------------------------------------- the kept look
// Makes t the character on screen (kept or previewed): builds it, recolors.
static void showTraits(const avatar::Traits& t) {
    figureTraits = t;
    figure.build(figureTraits);
    updatePalette();
    shownValid = false;
}

static avatar::Traits slotTraits() {
    return avatar::traitsForSlot(agentName(), slotShown, agentLook(), keptPtr());
}

static void keptSave() {
    uint8_t b[avatar::KEPT_BYTES];
    avatar::keptEncode(keptTraits, b);
    char key[16];
    avatar::keptKey(agentName(), key);
    Preferences prefs;
    if (prefs.begin(VIEW_NS, false)) {
        prefs.putBytes(key, b, sizeof b);
        prefs.end();
    }
    char tag[9];
    avatar::lookTag(keptTraits, tag);
    Serial.printf("[Character] look kept %s\n", tag);
}

static void keptForget() {
    char key[16];
    avatar::keptKey(agentName(), key);
    Preferences prefs;
    if (prefs.begin(VIEW_NS, false)) {
        prefs.remove(key);
        prefs.end();
    }
    Serial.println("[Character] look cleared");
}

// Keeps t as the agent's character (avatar.h keepAction); false when it
// already is.
static bool keepTraits(const avatar::Traits& t) {
    switch (avatar::keepAction(t, originalTraits(), keptPtr())) {
    case avatar::KeepStore:
        keptTraits = t;
        keptValid = true;
        keptSave();
        return true;
    case avatar::KeepClear:
        keptValid = false;
        keptForget();
        return true;
    case avatar::KeepNothing:
        break;
    }
    return false;
}

// After a direct keep (bench): the agent's character is on screen again,
// the browse position back at its first slot.
static void showCurrent() {
    slotShown = lowestSlot();
    showTraits(currentTraits());
    needFull = true;
}

void characterPreviewPosition(int& sel, int& count) {
    sel = previewing ? slotShown - lowestSlot() : 0;
    count = avatar::VARIANT_MAX + 1 + (keptValid ? 1 : 0);
}

static void previewText(int top, int bottom) {
    TFT_eSPI& tft = uiTft();
    const int x = AV_TEXT_X, w = tft.width() - x - 4;
    tft.fillRect(x - 6, top, w + 6, bottom - top, uiInk(Ink::Bg));
    // Name in the Title face, then the position and the palette name in Body:
    // the block is centred, its heights come from the font.
    uiFont(UiFont::Title);
    const int titleBox = uiFontHeight();
    const int line = uiLineHeight();
    const int y0 = (top + bottom - (titleBox + 4 + line + 4 + line)) / 2;
    uiText(uiFit(String(agentLabel(settingsProfile()).c_str()), w), x, y0, Ink::Strong, Ink::Bg);
    uiFont(UiFont::Body);
    String id = slotShown == avatar::KEPT_SLOT ? String("Kept look")
                : slotShown == 0               ? String("Original")
                                               : "#" + String(slotShown);
    const int y1 = y0 + titleBox + 4;
    int ix = x + uiText(id, x, y1, Ink::Accent, Ink::Bg);
    if (slotShown != avatar::KEPT_SLOT && figureTraits == currentTraits())
        uiText("  kept", ix, y1, Ink::Muted, Ink::Bg);
    uiText(avatar::CHAR_PALETTES[figureTraits.palette].name, x, y1 + line + 4, Ink::Muted,
           Ink::Bg);
}

static int previewTop = 0, previewBottom = 0;

static void previewFrame(uint32_t now, bool all) {
    const avatar::Ctx ctx = {now, now - previewSince, 0, false};
    figure.render(avatar::Idle, ctx, next);
    avatar::Rect r;
    const int oy = previewTop + (previewBottom - previewTop - avatar::H * AV_SCALE) / 2;
    if (all || !shownValid) {
        r.x0 = 0;
        r.y0 = 0;
        r.x1 = avatar::W;
        r.y1 = avatar::H;
        pushSprite(r, next, AV_X, oy, AV_SCALE);
        shownValid = true;
    } else if (avatar::dirtyRect(shown, next, r)) {
        pushSprite(r, next, AV_X, oy, AV_SCALE);
    }
    memcpy(shown, next, sizeof shown);
}

void characterPreviewBegin() {
    previewing = true;
    slotShown = lowestSlot();
    previewSince = millis();
    previewLast = 0;
}

void characterPreviewPaint(int top, int bottom) {
    if (!previewing) return;
    previewTop = top;
    previewBottom = bottom;
    showTraits(slotTraits());                     // also after a theme change: recolor
    previewText(top, bottom);
    previewFrame(millis(), true);
    previewLast = millis();
}

void characterPreviewMove(int steps) {
    if (!previewing || !steps) return;
    const int n = constrain(slotShown + steps, lowestSlot(), (int)avatar::VARIANT_MAX);
    if (n == slotShown) return;
    slotShown = n;
    showTraits(slotTraits());
    previewSince = millis();
    previewText(previewTop, previewBottom);
    previewFrame(millis(), true);
    previewLast = millis();
}

bool characterPreviewKeep() {
    if (!previewing || !keepTraits(slotTraits())) return false;
    previewText(previewTop, previewBottom);       // "kept"
    return true;
}

void characterPreviewTick() {
    if (!previewing) return;
    const uint32_t now = millis();
    if (now - previewLast < avatar::FRAME_MS) return;
    if (powerBacklight() == Backlight::Off) return;
    previewLast = now;
    previewFrame(now, false);
}

void characterPreviewEnd() {
    if (!previewing) return;
    previewing = false;
    const avatar::Traits current = currentTraits();
    if (!(figureTraits == current)) showTraits(current);   // browsing was a preview
    slotShown = lowestSlot();
    needFull = true;
}

#ifdef BENCH_CMDS
void characterKeepVariant(uint16_t n) {
    if (n > avatar::VARIANT_MAX) n = avatar::VARIANT_MAX;
    keepTraits(avatar::traitsForAgent(agentName(), n, agentLook()));
    showCurrent();
}

bool characterKeepRecord(const uint8_t* rec, size_t n) {
    avatar::Traits t;
    if (!avatar::keptDecode(rec, n, t)) return false;
    keepTraits(t);
    showCurrent();
    return true;
}

bool characterKeptRecord(uint8_t* out) {
    if (!keptValid) return false;
    avatar::keptEncode(keptTraits, out);
    return true;
}

// "orig" for the agent's Original, else the look tag.
static void tagOf(const avatar::Traits& t, char* out) {
    if (t == originalTraits()) strcpy(out, "orig");
    else avatar::lookTag(t, out);
}

void characterLookTags(char kept[9], char shown[9]) {
    const avatar::Traits current = currentTraits();
    tagOf(current, kept);
    tagOf(previewing ? slotTraits() : current, shown);
}

void characterForce(int state) {
    forced = state;
    shownValid = false;
}

void characterColumnShot() {
    if (!columnReady()) {
        Serial.println("[Shot] none");
        return;
    }
    const int w = colSpr->width(), h = colSpr->height();
    const uint8_t* px = (const uint8_t*)colSpr->getPointer();
    Serial.printf("[Shot] %dx%d\n", w, h);
    static unsigned char b64[240 * 2 * 4 / 3 + 8];   // any board's widest column row
    for (int y = 0; y < h; y++) {
        size_t n = 0;
        mbedtls_base64_encode(b64, sizeof b64, &n, px + y * w * 2, (size_t)w * 2);
        Serial.printf("[Shot] %d %.*s\n", y, (int)n, (const char*)b64);
    }
    Serial.println("[Shot] end");
}

// The character sprite as last pushed: the slot frame and the palette the
// panel got (RGB565, panel byte order undone), as hex lines.
void characterSpriteShot() {
    Serial.printf("[Sprite] bg=%04x pal", uiInk(Ink::Bg));
    for (int i = 0; i < avatar::SlotCount; i++)
        Serial.printf(" %04x", (uint16_t)((pal[i] >> 8) | (pal[i] << 8)));
    Serial.println();
    for (int y = 0; y < avatar::H; y++) {
        Serial.printf("[Sprite] %02d ", y);
        for (int x = 0; x < avatar::W; x++) Serial.printf("%02x", shown[y * avatar::W + x]);
        Serial.println();
    }
    Serial.println("[Sprite] end");
}
#endif
