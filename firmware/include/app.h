// app.h — app-wide state and the module map.
//
// Execution contexts:
//   loop  — Arduino loop task (core 1): input, UI, capture, playback, menu.
//   turn  — turn worker task (core 0): STT, Hermes stream (turn.cpp).
//   fetch — TTS chunk fetch task (core 0): synthesis downloads (speech.cpp).
//   chatlist — one-shot session-list task (core 0): the Chats list
//          (menu.cpp), so a slow gateway never stalls the side key.
//   keys  — esp_timer task, every 5 ms: samples the two keys (input.cpp).
//   kbd   — keyboard poll task (core 1, above the loop; keyboard boards):
//           reads the key matrix every 10 ms (keyboard.cpp); typed keys go
//           to the loop through a queue, the mic key's level through an
//           atomic the keys sampler reads. Shares the I2C bus with the mic
//           codec's setup under i2c_bus.h's lock.
// Every module's state is owned by exactly one context; each header states
// which. Cross-context data moves only through the documented handoffs:
// TurnSnap (turn → loop progressive text), the stage cell and the
// beginTurn/end-of-turn notify (turn.cpp), the speech chunk queue and the
// PSRAM chunk slots (speech.cpp: a slot is the loop's until the decoder
// closes it), the chat-list result slot (menu.cpp), and the key ring
// (keyring.h: keys → loop level changes). Settings are
// loaded before any task starts and never change
// during a boot, so every context reads them freely.
//
// Setup mode (portal) replaces all of the above for a whole boot: nothing
// but the portal loop runs, and it ends in a restart.
//
// Modules: settings (NVS device settings, derived endpoints) · portal (setup
// mode: soft AP + captive form) · console (serial lines: provisioning, bench)
// · selftest (boot reachability check) · ota (boot firmware update from the
// shim) · ui (display, theme, text and fonts, transcript view, status
// shimmer, backlight; the transcript's storage — history turns, wrapped
// lines, the pending block — lives in PSRAM: psram_alloc.h, line_store.h;
// loop task only) · character (the optional character view: a seeded
// pixel-art avatar of the active agent, pure in avatar.h, with its 15 fps
// animation tick and an answer card; loop task, replaces the transcript view
// when chosen in Setup; reads the newest exchange from ui and the turn stage,
// playback level and mic level, all loop-task state; also the Chirper tab's
// browsing and the kept look (the character's trait record), saved per agent; one-shot
// reactions, the speaking level meter, and the agent's persona look from
// settings) · logo_draw (the chirp! logo, art and palette pure in logo.h: the
// boot splash, the empty chat screen and the Setup tab;
// loop task only, no state) · input (keys, wheel, gestures)
// · capture (mic, recording buffer, STT upload) · voice (hands-free +
// ambient floor) · speech (playback, TTS chunk pipeline; the decoder plays
// MP3 straight from memory, mem_source.h) · hermes (chat API,
// session) · turn (turn worker, THINKING/SPEAKING) · menu (Chats, Theme, Text and
// Setup tabs) · power (backlight timeline, sleep, battery level via the
// charger ADC, slow-animation flag) · bench (serial test commands,
// BENCH_CMDS builds only) · main (setup, loop, input dispatch,
// push-to-talk).
#pragma once

#include <Arduino.h>

#include "app_state.h"

// The app state machine. Read and written on the loop task only.
extern UiState uiState;

// What both views show of the owner's side (main.cpp; loop task). A capture
// (push-to-talk or a voice-mode turn) shows as listening only once it is
// RECORD_SHOW_MS old, so a tap, the view chord's dial press or a short noise
// never pops the bars up; voice mode waits for the owner when it is on, no
// turn runs and no capture shows.
bool appListeningShown();
bool appVoiceWaiting();

// One completed exchange in the transcript.
struct Turn {
    String q;
    String a;
};

// Redraws the whole screen for the current mode (transcript or menu);
// used after the panel was powered down. main.cpp.
void appRedraw();

// Heap health: fragmentation shows as `largest` falling well below `free`;
// `min` is the low-water mark since boot. Any context.
void logHeap(const char* tag);
