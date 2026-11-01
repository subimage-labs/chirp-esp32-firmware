// ui.h — display, theme, fonts, status strip, scrollable transcript, backlight.
// Loop task only. Owns the TFT, the current theme and font (persisted in NVS), the
// transcript history and its pre-wrapped rows, the in-flight (pending) turn
// block, and the scroll position.
#pragma once

#include <TFT_eSPI.h>

#include <vector>

#include "anchor.h"
#include "app.h"
#include "battery.h"
#include "theme.h"
#include "ui_layout.h"

void uiBegin();                 // panel init, rotation, saved theme, backlight at full
void uiPanelOff();              // DISPOFF + SLPIN, backlight pin driven low and held
void uiPanelOn();               // re-init after uiPanelOff (panel may have lost power)
void uiBacklight(uint8_t duty); // 0..255
// The panel stops (or resumes) driving its pixels; its frame memory stays, so
// resuming shows the current screen at once. An idle screen blanks this way:
// a static image the panel keeps driving ghosts.
void uiPanelBlank(bool off);

TFT_eSPI& uiTft();              // for screens that draw themselves (menu, portal)
void uiPanelHold();             // loop, after drawing: re-sends the panel's inversion (ui.cpp)
int uiContentTop();
int uiContentBottom();
// The chat view's companion on this panel (ui_layout.h): its area and the
// avatar's scale, or Companion::None (no room, not this board, or the
// character view is up). character.cpp draws and animates it.
ui_layout::Companion uiCompanion(ui_layout::Rect& area, int& scale);
// The typed compose line (chat view, above the status strip): the text to
// show, or null/"" for the empty prompt. The cursor blinks on uiTick.
void uiComposeSet(const char* text);
int uiStripTop();               // character view: its bottom (the strip's top while it is up)
void uiClear();

// Theme: a family (theme.h) in dark or light. Every screen draws through
// uiInk(); switching saves the choice and the caller redraws (appRedraw).
uint16_t uiInk(Ink ink);
uint8_t uiThemeFamily();
bool uiThemeLight();
const char* uiThemeName();              // the family's name
void uiSetTheme(uint8_t family, bool light);

// Status strip (bottom row): text from the left edge, repainted in place (a
// repeat of the same text is a no-op). Text wider than the strip scrolls right
// to left like a news ticker, driven by uiTick. uiShimmer draws the label in
// the muted color with a bright band sweeping through it (working states);
// call it every frame, it repaints only the characters.
// The character view has no strip: it comes up over the view's bottom only
// for an error (Ink::Err) or a uiNotice (volume, firmware update), and the
// next message takes it away again (the character repaints under it).
void uiMsg(const char* msg, Ink ink);
void uiNotice(const char* msg, Ink ink);
void uiNoticeFor(const char* msg, Ink ink, uint32_t ms);   // a notice that clears itself after ms
void uiShimmer(const char* label);
void uiTick();                          // loop: advances a scrolling status text
#ifdef BENCH_CMDS
void uiPanelInvert(bool on);            // send INVON / INVOFF (bench.h)
void uiPanelStatus();                   // read the panel's status registers (bench.h)
void uiPanelReg(uint8_t cmd, const uint8_t* data, int n);   // write a panel register live (bench.h)
void uiPanelTest(bool checker, bool shifted);   // full-screen test pattern (bench.h)
void uiPanelFill(uint16_t c);   // whole screen one RGB565 color, no INVON re-send for 600 ms (bench.h)
#endif

// Compact strip (the menus): status text in the small size, no battery.
void uiStatusCompact(bool on);

// The battery gauge at the strip's right edge (hidden in the compact strip):
// an outline whose bar shows the level, a bolt left of it while charging, in
// the Ok ink on USB and the Err ink when low. The slot is reserved whether or
// not a reading exists, so status text never moves under it; uiBattery
// repaints just the block.
void uiBattery(const BatteryView& v);

// Text. Four font families (Atkinson, Inter, Paper Mono, Terminus;
// firmware/src/fonts/) in three sizes S/M/L, chosen live from the Text tab and
// saved by name; ui.cpp is the only place fonts are chosen and text is drawn.
// uiFont selects a cut (loads once; per-frame calls are free). uiText draws
// UTF-8 with its top-left at (x, y), blended against the background ink, and
// returns its width. Draw and measure text only through these, and fold
// anything from outside with uiFold first: it keeps what the font has,
// substitutes ASCII look-alikes where it can and drops the rest, so no
// character ever renders as a box.
enum class UiFont : uint8_t {
    Body,    // the chosen family and size: transcript, menus, status
    Header,  // Body, but at most size M: dense chrome (the tab header)
    Small,   // the chosen family at size S: the compact status strip (menus)
    Title,   // the chosen family's large cut (ASCII): the READY screen, the agent's name
    Fixed,   // Atkinson M whatever is chosen: the setup portal, drawn for 18 px lines
};
void uiFont(UiFont f);
void uiFontSample(size_t family);                // family i at the chosen size (the Text tab's rows)
int uiFontHeight();                              // line box of the loaded font
int uiTextWidth(const char* s);                  // loaded font
int uiTextWidth(const String& s);
int uiText(const char* s, int x, int y, Ink fg, Ink bg);
int uiText(const String& s, int x, int y, Ink fg, Ink bg);
// uiText into a 16-bit sprite (its coordinates, clipped to it) instead of the
// panel: views that compose a frame off-panel and push it whole.
int uiTextInto(TFT_eSprite& dst, const char* s, int x, int y, uint16_t fg, uint16_t bg);
String uiFold(const String& in);
String uiFoldN(const char* s, size_t n);         // uiFold of n bytes (not NUL-terminated)
String uiFit(const String& in, int avail);      // folded, cut to avail px with "…"

// Every text height derives from the Body cut's line box, so S/M/L reflow
// everything. Row pitches for menus, the status strip, the tab header.
int uiLineHeight();                              // transcript line pitch (= Body line box)
int uiRowHeight();                               // menu row pitch
int uiStatusHeight();                            // bottom status strip
int uiHeaderHeight();                            // tab names + rule
int uiButtonHeight();                            // a menu action drawn as a button
int uiPinnedHeight();                            // pinned "+ New chat" button + gap

// The font choice. Changing it saves, re-wraps the transcript and the
// character view's bubbles; the caller redraws (appRedraw).
size_t uiFontFamilyCount();
const char* uiFontFamilyName(size_t i);
size_t uiFontFamily();                           // current family index (display order)
uint8_t uiFontSize();                            // FONT_SIZE_S/M/L (fontpick.h)
void uiSetFont(size_t family, uint8_t size);

// Transcript. Completed turns are pre-wrapped once; the pending block holds
// the in-flight turn so streaming text never re-wraps finished turns.
bool transcriptEmpty();
void transcriptClear();                          // + READY screen
void transcriptLoad(std::vector<Turn>&& turns);  // resumed chat, pinned to bottom
void transcriptCommit(const String& q, const String& a);  // ends pending
void transcriptPendingBegin();                   // reserves the question row
void transcriptPendingSet(const char* q, const char* a);
void transcriptPendingCancel();
void transcriptScroll(int rows);                 // + = older; stops following
void transcriptFollow();                         // pin to newest + render
// A view switch keeps the place (anchor.h): the chat view's place, and putting
// one at the top of its window on the next render.
ChatAnchor transcriptAnchor();
void transcriptScrollTo(const ChatAnchor& a);
void transcriptRender();                         // whole content area (character view when selected)
// The chat view's live row: the owner's level bars while a capture shows and
// three dots while voice mode waits (their side), the agent's green wave while
// it speaks (its side). main.cpp calls it every loop with what to show (app.h
// appListeningShown / appVoiceWaiting, speech), that side's level, and
// whether the chat is on screen (not under the menu).
enum class LiveRow : uint8_t { None, Bars, Dots, Wave };
void transcriptLive(LiveRow row, uint8_t level, bool visible);
// The chat as stored, for the character view's bubble column: completed turns
// oldest first, then the in-flight one. The pointers stay valid until the
// transcript next changes (loop context); transcriptRevision changes whenever
// the completed turns do.
struct TurnText {
    const char* q;
    size_t qn;
    const char* a;
    size_t an;
};
size_t transcriptTurnCount();
bool transcriptTurn(size_t i, TurnText& out);
bool transcriptPending(TurnText& out);           // false when no turn is in flight
uint32_t transcriptRevision();
void transcriptPosition(int& top, int& rows, bool& following);  // bench status
