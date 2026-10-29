// menu.h — the side-key screens. Chats: new chat pinned above the recent
// sessions, which load in the background (chatlist task, app.h) so a slow
// gateway never holds up the side key. Theme: one row per theme; turning
// wheel applies each live, the encoder flips dark/light (saved). Chirper: the
// active agent's character, large and animating; the wheel browses the kept look,
// the Original and numbered variants (clockwise = next, a quick spin jumps ten),
// the encoder keeps the shown one (saved per agent), leaving without keeping
// restores the agent's character. Text: one
// row per font family drawn in its own face; the wheel applies each live, the
// encoder cycles the size S/M/L (saved). Setup: the stored
// agents (wheel + encoder switches, which saves and restarts), the Screen
// row (encoder toggles Transcript / Character, live and saved), "Change
// settings on phone" (restarts into setup mode), then device info. A side
// tap steps chat → Chats → Theme → Chirper → Text → Setup → chat.
// Loop task only, apart from the chat-list result slot (menu.cpp documents
// the handoff). Entering is gated by main.cpp (capture/playback/voice are
// stopped first).
#pragma once

#include <Arduino.h>

bool menuOpen();
void menuEnter();                       // Chats tab; starts the list fetch
void menuTick();                        // loop: takes a finished chat list
void menuNext();                        // side tap: Chats → Theme → Chirper → Text → Setup → chat
void menuPrev();                        // the tab before; from Chats, back to chat
void menuClose();                       // straight back to chat (keyboard: Backspace)
void menuMove(int32_t clicks, uint32_t perClickMs);
void menuActivate();                    // encoder: new chat / resume / theme / font size / agent / setup
void menuRender();
void menuPosition(int& sel, int& count);   // bench status (selection in the open tab)
const char* menuTab();                  // bench status: "none" / "chats" / "theme" / "chirper" / "setup"
