#include "menu.h"

#include <WiFi.h>

#include <atomic>
#include <vector>

#include "avatar.h"
#include "board.h"
#include "character.h"
#include "fontpick.h"
#include "hermes.h"
#include "ota.h"
#include "portal.h"
#include "power.h"
#include "selftest_policy.h"
#include "settings.h"
#include "logo_draw.h"
#include "setup_view.h"
#include "speech.h"
#include "turn.h"
#include "venice_policy.h"
#include "ui.h"

static const int MENU_SESSIONS = 18;
static const size_t RESUME_TURNS = 20;
static const uint32_t MENU_FAST_MS = 90;          // ms per click below this: 2 rows

enum class Tab : uint8_t { None, Chats, Theme, Chirper, Text, Setup };

// ---- loop-owned state
static Tab tab = Tab::None;
static int menuSel = 0;                           // Chats: 0 = new chat (pinned), 1.. = sessions
static int menuFirst = 1;                         // Chats: first session row in the viewport
static std::vector<SessionItem> menuItems;        // [0] = new chat, then sessions
static bool listLoading = false;                  // a fetch for this visit is in flight
static int themeSel = 0;                          // Theme: theme code (family * 2 + light)
static int themeTop = 0;                          // Theme: first row in the viewport
static int setupSel = 0;                          // Setup: agents, then "change settings"
static int setupTop = 0;                          // Setup: first row in the viewport
static int textSel = 0;                           // Text: font family (display order)
static int textTop = 0;                           // Text: first row in the viewport

// ---- chat-list handoff (chatlist task → loop)
// The task fills fetchOut/fetchCode, then sets fetchState to DONE (release).
// The loop reads them only after seeing DONE (acquire) and sets IDLE when it
// has taken them; only an IDLE loop starts a new task. So the task and the
// loop never touch the slot at the same time.
enum : uint8_t { FETCH_IDLE, FETCH_RUNNING, FETCH_DONE };
static std::atomic<uint8_t> fetchState{FETCH_IDLE};
static std::vector<SessionItem> fetchOut;
static int fetchCode = 0;

static void chatListTask(void*) {
    std::vector<SessionItem> items;
    const int code = hermesListSessions(items, MENU_SESSIONS);
    fetchOut = std::move(items);
    fetchCode = code;
    fetchState.store(FETCH_DONE, std::memory_order_release);
    vTaskDelete(NULL);
}

// A fetch already in flight (menu left and reopened while the gateway was
// slow) serves this visit too.
static void startChatList() {
    listLoading = true;
    if (fetchState.load(std::memory_order_acquire) != FETCH_IDLE) return;
    fetchState.store(FETCH_RUNNING, std::memory_order_relaxed);
    if (xTaskCreatePinnedToCore(chatListTask, "chatlist", 8192, nullptr, 1, nullptr, 0) !=
        pdPASS) {
        fetchState.store(FETCH_IDLE, std::memory_order_relaxed);
        listLoading = false;
        Serial.println("[Menu] chat list task failed to start");
    }
}

bool menuOpen() { return tab != Tab::None; }

const char* menuTab() {
    switch (tab) {
    case Tab::Chats: return "chats";
    case Tab::Theme: return "theme";
    case Tab::Chirper: return "chirper";
    case Tab::Text: return "text";
    case Tab::Setup: return "setup";
    default: return "none";
    }
}

// Setup rows (setup_view.h): the agents, the screen choice, "change
// settings", a rule, then the info rows. The wheel selects every row but the rule.
static setupview::Layout setupLayout() {
    const DeviceSettings& s = settingsStored();
    return setupview::Layout((int)s.agents.size(), s.backend == "venice");   // the voice row: venice only
}

void menuPosition(int& sel, int& count) {
    if (tab == Tab::Theme) {
        sel = themeSel;
        count = THEME_COUNT;
    } else if (tab == Tab::Chirper) {
        characterPreviewPosition(sel, count);
    } else if (tab == Tab::Text) {
        sel = textSel;
        count = (int)uiFontFamilyCount();
    } else if (tab == Tab::Setup) {
        sel = setupSel;
        count = setupLayout().total();
    } else {
        sel = menuSel;
        count = (int)menuItems.size();
    }
}

// The tabs in header order; "next tab" and the header both walk this table.
struct TabInfo {
    Tab tab;
    const char* name;
};
static const TabInfo TABS[] = {{Tab::Chats, "Chats"},
                               {Tab::Theme, "Theme"},
                               {Tab::Chirper, "Chirper"},
                               {Tab::Text, "Text"},
                               {Tab::Setup, "Setup"}};
static const size_t TAB_COUNT = sizeof(TABS) / sizeof(TABS[0]);

// "Chats | Theme | CHIRPER | Text | Setup": the current tab lit and in capitals,
// faint bars between, ruled off below (faint too). Drawn in the Header font
// (the chosen size, at most M) and as wide a gap around each bar as fits.
static void drawHeader(TFT_eSPI& tft, int top) {
    uiFont(UiFont::Header);
    String names[TAB_COUNT];
    int nameW[TAB_COUNT];
    for (size_t i = 0; i < TAB_COUNT; i++) {
        names[i] = TABS[i].name;
        if (tab == TABS[i].tab) names[i].toUpperCase();
        nameW[i] = uiTextWidth(names[i]);
    }
    const int barW = uiTextWidth("|");
    const int pad = max(2, headerPad(nameW, (int)TAB_COUNT, barW, tft.width() - 12, 8, 2));
    int x = 6;
    for (size_t i = 0; i < TAB_COUNT; i++) {
        if (i) {
            x += pad;
            x += uiText("|", x, top + 2, Ink::Faint, Ink::Bg);
            x += pad;
        }
        x += uiText(names[i], x, top + 2, tab == TABS[i].tab ? Ink::Accent : Ink::Muted,
                    Ink::Bg);
    }
    tft.drawFastHLine(0, top + uiHeaderHeight() - 3, tft.width(), uiInk(Ink::Faint));
    uiFont(UiFont::Body);
}

static void drawRow(TFT_eSPI& tft, const String& label, bool sel, int y) {
    if (sel) tft.fillRect(0, y - 2, tft.width(), uiRowHeight(), uiInk(Ink::Surface));
    uiText(uiFit(label, tft.width() - 20), 8, y, sel ? Ink::Strong : Ink::Text,
           sel ? Ink::Surface : Ink::Bg);
}

// A menu action: the speech bubbles' notched 2 px outline around the label,
// a chevron at the right when it leads somewhere. The slot starts at y - 2
// (where a plain row's highlight starts) and is uiButtonHeight() tall; the
// button leaves a gap under it. Selected: surface fill, accent outline.
static void drawButton(TFT_eSPI& tft, const String& label, bool chevron, bool sel, int y) {
    const int b = 2, x = 4, w = tft.width() - 10, top = y - 2, h = uiButtonHeight() - 4;
    const uint16_t edge = uiInk(sel ? Ink::Accent : Ink::Muted);
    const Ink fill = sel ? Ink::Surface : Ink::Bg;
    tft.fillRect(x + b, top, w - 2 * b, h, edge);
    tft.fillRect(x, top + b, w, h - 2 * b, edge);
    tft.fillRect(x + b, top + b, w - 2 * b, h - 2 * b, uiInk(fill));
    const int ty = top + (h - uiLineHeight()) / 2;
    const String chev = uiFold("\xE2\x80\xBA");                   // "›" where the font has it
    const int chW = chevron ? uiTextWidth(chev) : 0;
    uiText(uiFit(label, w - 16 - (chevron ? chW + 8 : 0)), x + 8, ty, sel ? Ink::Strong : Ink::Text,
           fill);
    if (chevron) uiText(chev, x + w - 8 - chW, ty, sel ? Ink::Accent : Ink::Muted, fill);
}

static void dottedRule(TFT_eSPI& tft, int y) {
    for (int x = 6; x < tft.width() - 6; x += 4) tft.drawPixel(x, y, uiInk(Ink::Faint));
}

// A list's scrollbar, like the chat's: a 3 px thumb at the right edge over the
// list's area [top, bottom) when it holds more rows than show (`first` of
// `total` is the first shown, `shown` at a time). Drawn after the rows.
static void listScrollbar(TFT_eSPI& tft, int top, int bottom, int first, int shown, int total) {
    if (total <= shown || bottom <= top) return;
    const int track = bottom - top;
    const int thumb = min(track, max(8, track * shown / total));
    const int y = top + (track - thumb) * constrain(first, 0, total - shown) / (total - shown);
    tft.fillRect(tft.width() - 4, y, 3, thumb, uiInk(Ink::Muted));
}

static void chatsHint() {
    uiMsg(CHIRP_TURN "=select  " CHIRP_PRESS "=open" CHIRP_NEXT, Ink::Muted);
}

// One row per theme. The selection is the theme in use: turning the wheel
// applies (and saves) each one as it lands on it; the encoder flips it
// between dark and light.
static void renderThemes(TFT_eSPI& tft, int top, int bottom) {
    const int listTop = top + uiHeaderHeight() + 2;
    const int rows = max(1, (bottom - listTop) / uiRowHeight());
    themeSel = constrain(themeSel, 0, THEME_COUNT - 1);
    if (themeSel < themeTop) themeTop = themeSel;
    if (themeSel >= themeTop + rows) themeTop = themeSel - rows + 1;
    themeTop = constrain(themeTop, 0, max(0, THEME_COUNT - rows));
    int shown = 0;
    for (int f = themeTop, y = listTop; f < THEME_COUNT && y + uiLineHeight() <= bottom;
         f++, y += uiRowHeight(), shown++)
        drawRow(tft, THEMES[f].name, f == themeSel, y);
    listScrollbar(tft, listTop - 2, listTop - 2 + shown * uiRowHeight(), themeTop, shown, THEME_COUNT);
    uiMsg(uiThemeLight() ? CHIRP_TURN "=theme  " CHIRP_PRESS "=dark" CHIRP_NEXT
                         : CHIRP_TURN "=theme  " CHIRP_PRESS "=light" CHIRP_NEXT,
          Ink::Muted);
}

// One row per font family, each drawn in its own face at the chosen size. The
// selection is the family in use: turning the wheel applies (and saves) each
// as it lands on it; the encoder cycles the size S -> M -> L.
static void textHint() {
    char hint[48];
    snprintf(hint, sizeof hint, CHIRP_TURN "=font  " CHIRP_PRESS "=size %c" CHIRP_NEXT,
             fontSizeLetter(fontSizeNext(uiFontSize())));
    uiMsg(hint, Ink::Muted);
}

static void renderText(TFT_eSPI& tft, int top, int bottom) {
    const int count = (int)uiFontFamilyCount();
    const int listTop = top + uiHeaderHeight() + 2;
    const int rows = max(1, (bottom - listTop) / uiRowHeight());
    textSel = constrain(textSel, 0, count - 1);
    if (textSel < textTop) textTop = textSel;
    if (textSel >= textTop + rows) textTop = textSel - rows + 1;
    textTop = constrain(textTop, 0, max(0, count - rows));
    int shown = 0;
    for (int f = textTop, y = listTop; f < count && y + uiLineHeight() <= bottom;
         f++, y += uiRowHeight(), shown++) {
        uiFontSample((size_t)f);
        String label = uiFontFamilyName((size_t)f);
        if (f == (int)uiFontFamily()) label += String("  ") + fontSizeLetter(uiFontSize());
        drawRow(tft, label, f == textSel, y);
    }
    listScrollbar(tft, listTop - 2, listTop - 2 + shown * uiRowHeight(), textTop, shown, count);
    uiFont(UiFont::Body);
    textHint();
}

static void moveText(int32_t clicks) {
    const int sel = constrain(textSel - (int)clicks, 0, (int)uiFontFamilyCount() - 1);
    if (sel == textSel) return;
    textSel = sel;
    uiSetFont((size_t)sel, uiFontSize());           // live, saved
    appRedraw();
}

static void activateText() {
    uiSetFont(uiFontFamily(), fontSizeNext(uiFontSize()));   // S -> M -> L, live, saved
    appRedraw();
}

// Chirper: the active agent's character, large and animating; the wheel
// browses other characters (the kept look, the Original, variants), the
// encoder keeps the shown one.
// Leaving without keeping restores the kept character (character.h).
static void chirperHint() {
    uiMsg(CHIRP_TURN "=browse  " CHIRP_PRESS "=keep" CHIRP_NEXT, Ink::Muted);
}

static void renderChirper(TFT_eSPI& tft, int top, int bottom) {
    (void)tft;
    characterPreviewPaint(top + uiHeaderHeight() + 2, bottom);
    chirperHint();
}

// Counter-clockwise = next character (like scrolling down a list); a quick
// spin jumps ten.
static void moveChirper(int32_t clicks, uint32_t perClickMs) {
    characterPreviewMove(-(int)clicks * (perClickMs < MENU_FAST_MS ? 10 : 1));
    chirperHint();
}

static void activateChirper() {
    if (characterPreviewKeep()) uiMsg("Kept", Ink::Ok);
    else chirperHint();
}

static void renderChats(TFT_eSPI& tft, int top, int bottom) {
    drawButton(tft, menuItems[0].title, false, menuSel == 0, top + uiHeaderHeight() + 2);   // pinned

    const int listTop = top + uiHeaderHeight() + uiPinnedHeight();
    const int rows = max(1, (bottom - listTop - 2) / uiRowHeight());
    const int sessions = (int)menuItems.size() - 1;
    if (sessions == 0) {
        uiText(listLoading ? "Loading chats..." : "No recent conversations", 8, listTop + 2,
               Ink::Muted, Ink::Bg);
    }
    if (menuSel >= 1 && menuSel < menuFirst) menuFirst = menuSel;
    if (menuSel >= menuFirst + rows) menuFirst = menuSel - rows + 1;
    menuFirst = constrain(menuFirst, 1, max(1, sessions - rows + 1));
    int y = listTop + 2, shown = 0;
    for (int i = menuFirst; i <= sessions && y + uiLineHeight() <= bottom;
         i++, y += uiRowHeight(), shown++)
        drawRow(tft, menuItems[i].title, i == menuSel, y);
    listScrollbar(tft, listTop, listTop + shown * uiRowHeight(), menuFirst - 1, shown, sessions);
    chatsHint();
}

static String infoValue(int i) {
    const DeviceSettings& s = settingsStored();
    const Endpoints& e = settingsEndpoints();
    const bool online = WiFi.status() == WL_CONNECTED;
    switch (i) {
    case 0: return otaVersion();
    case 1: return online ? WiFi.SSID() + "  (" + String(WiFi.RSSI()) + " dBm)"
                          : String((unsigned)s.networks.size()) + " saved  (not connected)";
    case 2: return online ? WiFi.localIP().toString() : String("-");
    case 3: return s.backend == "venice" ? e.shimHost + ":" + String(e.shimPort) : String(s.hermes.c_str());
    case 4: return e.shimHost + ":" + String(e.shimPort);
    default: {
        char text[40];
        return powerBatteryText(text, sizeof text) ? String(text) : String("-");
    }
    }
}

static const char* const INFO_LABELS[] = {"Firmware", "WiFi",   "Address",
                                          "Hermes",   "Speech", "Battery"};
static_assert(sizeof(INFO_LABELS) / sizeof(INFO_LABELS[0]) == setupview::INFO_ROWS,
              "one label per info row");

// The backend row names what answers: Hermes, or Venice in venice mode.
static const char* infoLabel(int i) {
    return i == 3 && settingsStored().backend == "venice" ? "Venice" : INFO_LABELS[i];
}

// Where the info values start: right of the widest label (it grows with the font).
static int infoColumn() {
    int w = 0;
    for (int i = 0; i < setupview::INFO_ROWS; i++) w = max(w, uiTextWidth(infoLabel(i)));
    return 6 + w + 12;
}

static void setupHint() {
    const DeviceSettings& s = settingsStored();
    const setupview::Layout l = setupLayout();
    switch (l.kind(setupSel)) {
    case setupview::Kind::Change:
        uiMsg(CHIRP_PRESS "=restart into phone setup", Ink::Muted);
        break;
    case setupview::Kind::Restart:
        uiMsg(CHIRP_PRESS "=restart (keeps the chat)", Ink::Muted);
        break;
    case setupview::Kind::Voice:
        uiMsg(CHIRP_PRESS "=next voice", Ink::Muted);
        break;
    case setupview::Kind::Screen:
        uiMsg(characterOn() ? CHIRP_PRESS "=show the chat" : CHIRP_PRESS "=show the character", Ink::Muted);
        break;
    case setupview::Kind::Agent:
        if (s.agents.size() == 1) uiMsg("The only agent on this device", Ink::Muted);
        else if (setupSel == (int)s.active) uiMsg("Agent in use", Ink::Muted);
        else uiMsg(CHIRP_PRESS "=switch agent (restarts)", Ink::Muted);
        break;
    default:
        uiMsg(CHIRP_TURN "=scroll", Ink::Muted);
        break;
    }
}

// One scrolling list: the actions as buttons (the agents, the view, phone
// setup, restart), the chirp! logo, a dotted rule, then what the device runs
// and talks to as plain rows. The wheel selects every row but the logo and
// the rule (setup_view.h), so the info scrolls into view at any font size;
// activating an info row does nothing.
static void renderSetup(TFT_eSPI& tft, int top, int bottom) {
    const DeviceSettings& s = settingsStored();
    const setupview::Layout l = setupLayout();
    const setupview::Pitch p = {uiRowHeight(), uiButtonHeight(), logoHeight(2) + 8};
    const int total = l.total();
    const int listTop = top + uiHeaderHeight() + 2;
    setupSel = setupview::move(l, setupSel, 0);
    setupTop = setupview::viewTop(l, p, setupSel, setupTop, bottom - listTop);

    int shown = 0, y = listTop;
    for (int r = setupTop; r < total && y + p.of(l, r) <= bottom; y += p.of(l, r), r++, shown++) {
        const bool sel = r == setupSel;
        switch (l.kind(r)) {
        case setupview::Kind::Agent: {
            const String name = agentLabel(s.agents[r].profile).c_str();
            const bool active = r == (int)s.active;
            if (s.agents.size() == 1) drawButton(tft, name + " (only agent)", false, sel, y);
            else if (active) drawButton(tft, name + " (in use)", false, sel, y);
            else drawButton(tft, "Switch to " + name, true, sel, y);
            break;
        }
        case setupview::Kind::Voice:
            drawButton(tft, String("Voice: ") + venice_policy::VOICES[speechVoice()].label, true, sel, y);
            break;
        case setupview::Kind::Screen:
            drawButton(tft, characterOn() ? "View: Character" : "View: Chat", true, sel, y);
            break;
        case setupview::Kind::Change:
            drawButton(tft, "Set up on phone", true, sel, y);
            break;
        case setupview::Kind::Restart:
            drawButton(tft, "Restart", true, sel, y);
            break;
        case setupview::Kind::Logo:                    // the chirp! logo under the actions
            logoDraw((tft.width() - logoWidth(2)) / 2, y - 2 + 4, 2);
            break;
        case setupview::Kind::Rule:
            dottedRule(tft, y + uiRowHeight() / 2 - 2);
            break;
        case setupview::Kind::Info: {
            const int i = l.infoIndex(r);
            const int vx = infoColumn();
            const Ink bg = sel ? Ink::Surface : Ink::Bg;
            if (sel) tft.fillRect(0, y - 2, tft.width(), uiRowHeight(), uiInk(Ink::Surface));
            uiText(infoLabel(i), 6, y, Ink::Muted, bg);
            uiText(uiFit(infoValue(i), tft.width() - vx - 6), vx, y, Ink::Strong, bg);
            break;
        }
        }
    }
    listScrollbar(tft, listTop - 2, y - 2, setupTop, shown, total);
    setupHint();
}

void menuRender() {
    TFT_eSPI& tft = uiTft();
    const int top = uiContentTop(), bottom = uiContentBottom();
    uiFont(UiFont::Body);
    tft.fillRect(0, 0, tft.width(), bottom, uiInk(Ink::Bg));   // from row 0: the character view paints there
    drawHeader(tft, top);
    if (tab == Tab::Chats) renderChats(tft, top, bottom);
    else if (tab == Tab::Theme) renderThemes(tft, top, bottom);
    else if (tab == Tab::Chirper) renderChirper(tft, top, bottom);
    else if (tab == Tab::Text) renderText(tft, top, bottom);
    else renderSetup(tft, top, bottom);
}

void menuEnter() {
    tab = Tab::Chats;
    uiStatusCompact(true);                        // small hints, no battery
    menuSel = 0;
    menuFirst = 1;
    menuItems.clear();
    menuItems.push_back({"", "+ New chat"});
    startChatList();
    menuRender();
}

// Takes a finished chat list. Shown when the Chats tab is up; otherwise kept
// for it (or dropped when the menu was left).
void menuTick() {
    if (tab == Tab::Chirper) characterPreviewTick();
    if (fetchState.load(std::memory_order_acquire) != FETCH_DONE) return;
    std::vector<SessionItem> items = std::move(fetchOut);
    fetchOut.clear();
    const int code = fetchCode;
    fetchState.store(FETCH_IDLE, std::memory_order_release);
    if (tab == Tab::None || !listLoading) return;
    listLoading = false;
    menuItems.resize(1);
    for (SessionItem& it : items) menuItems.push_back(std::move(it));
    if (tab != Tab::Chats) return;
    menuRender();
    if (const char* problem = hermesProblem(code)) uiMsg(problem, Ink::Err);   // new chat still works
}

static void closeMenu() {
    tab = Tab::None;
    uiStatusCompact(false);
    listLoading = false;                          // a late list is dropped
    transcriptRender();
    uiMsg(transcriptEmpty() ? "" : "Ready", Ink::Ok);
}

// Entering a tab puts its selection on what is in use.
static void enterTab(Tab t) {
    if (tab == Tab::Chirper && t != Tab::Chirper) characterPreviewEnd();   // back to the kept character
    tab = t;
    if (t == Tab::Theme) {
        themeSel = uiThemeFamily();
        themeTop = 0;
    } else if (t == Tab::Chirper) {
        characterPreviewBegin();
    } else if (t == Tab::Text) {
        textSel = (int)uiFontFamily();
        textTop = 0;
    } else if (t == Tab::Setup) {
        setupSel = (int)settingsStored().active;
        setupTop = 0;
    }
    menuRender();
}

void menuNext() {
    for (size_t i = 0; i + 1 < TAB_COUNT; i++) {
        if (tab == TABS[i].tab) {
            enterTab(TABS[i + 1].tab);
            return;
        }
    }
    closeMenu();
}

void menuClose() {
    if (tab == Tab::None) return;
    if (tab == Tab::Chirper) characterPreviewEnd();   // back to the kept character
    closeMenu();
}

// Slow clicks step one title (every row reachable); a spin moves two.
void menuMove(int32_t clicks, uint32_t perClickMs) {
    if (tab == Tab::Theme) {
        const int sel = constrain(themeSel - (int)clicks, 0, THEME_COUNT - 1);
        if (sel == themeSel) return;
        themeSel = sel;
        uiSetTheme((uint8_t)sel, uiThemeLight());   // live, saved
        appRedraw();
        return;
    }
    if (tab == Tab::Chirper) {
        moveChirper(clicks, perClickMs);
        return;
    }
    if (tab == Tab::Text) {
        moveText(clicks);
        return;
    }
    if (tab == Tab::Setup) {
        setupSel = setupview::move(setupLayout(), setupSel, -(int)clicks);
        menuRender();
        return;
    }
    const int step = perClickMs < MENU_FAST_MS ? 2 : 1;
    menuSel = constrain(menuSel - (int)clicks * step, 0, (int)menuItems.size() - 1);
    menuRender();                                   // natural feel: clockwise = up
}

static void activateSetup() {
    const DeviceSettings& s = settingsStored();
    const setupview::Layout l = setupLayout();
    switch (l.kind(setupSel)) {
    case setupview::Kind::Voice:                    // saved; Chirp says hello in the new voice
        speechSetVoice(venice_policy::voiceNext(speechVoice()));
        turnPreviewVoice(venice_policy::VOICE_PREVIEW);   // idle only (turn.h)
        menuRender();
        return;
    case setupview::Kind::Screen:                   // live, saved; the chat shows it on exit
        characterSetOn(!characterOn());
        menuRender();
        return;
    case setupview::Kind::Change:
        uiMsg("Restarting into setup...", Ink::Accent);
        portalRestartInto();
        return;
    case setupview::Kind::Restart:                  // settings, chat and look are saved already
        uiMsg("Restarting...", Ink::Accent);
        Serial.println("[Menu] restart");
        Serial.flush();
        delay(400);
        ESP.restart();
        return;
    case setupview::Kind::Logo:
    case setupview::Kind::Rule:
    case setupview::Kind::Info:
        return;
    case setupview::Kind::Agent:
        break;
    }
    if (setupSel == (int)s.active) return;
    const std::string name = agentLabel(s.agents[setupSel].profile);
    if (!settingsUseAgent((size_t)setupSel)) {
        uiMsg("Couldn't save the agent choice", Ink::Err);
        return;
    }
    uiMsg((String("Switching to ") + name.c_str() + "...").c_str(), Ink::Accent);
    Serial.printf("[Menu] switching to agent %s, restarting\n", name.c_str());
    Serial.flush();
    delay(400);
    ESP.restart();
}

void menuActivate() {
    if (tab == Tab::Theme) {                        // dark ↔ light, live, saved
        uiSetTheme(uiThemeFamily(), !uiThemeLight());
        appRedraw();
        return;
    }
    if (tab == Tab::Chirper) {
        activateChirper();
        return;
    }
    if (tab == Tab::Text) {
        activateText();
        return;
    }
    if (tab == Tab::Setup) {
        activateSetup();
        return;
    }
    if (menuSel == 0) {                             // new conversation
        tab = Tab::None;
        uiStatusCompact(false);
        listLoading = false;
        hermesSessionForget();
        transcriptClear();
        uiMsg("New chat", Ink::Accent);
        return;
    }
    uiMsg("Opening chat...", Ink::Accent);
    std::vector<Turn> turns;
    const String id = menuItems[menuSel].id;
    if (hermesLoadMessages(id, turns, RESUME_TURNS)) {
        tab = Tab::None;
        uiStatusCompact(false);
        hermesSessionSet(id);
        transcriptLoad(std::move(turns));
        uiMsg("Resumed chat", Ink::Ok);
    } else {
        uiMsg("Couldn't open chat", Ink::Err);
        menuRender();
    }
}
