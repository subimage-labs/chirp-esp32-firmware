// keyboard.h — the T-Keyboard (an ESP32-C3 coprocessor on I2C, address
// 0x55). It queues one ASCII/modified byte per keypress; the INT pin goes
// low while a queue exists. The loop polls it (I2C from the 5 ms key
// sampler would hold the timer task for the whole transaction) and routes
// each byte through keymap.h.
//
// Two keys hold the roles the board has no pins for, and their levels are
// sampled by input.cpp's key sampler like physical keys:
//   space, held  — the encoder key (push-to-talk; double-tap = voice mode)
//   ?123 (sym)   — the side key (tap = menu, hold = volume)
#pragma once

#include <stdint.h>

enum class KbdEv : uint8_t {
    None,
    Char,        // a printable character (c)
    Backspace,
    Enter,
    Raw,         // an unclassified code (raw): bench `keys` prints these
};

struct KbdEvent {
    KbdEv kind = KbdEv::None;
    char c = 0;
    uint8_t raw = 0;
};

bool keyboardBegin();               // Wire on the board's I2C pins; false: no keyboard
bool keyboardOnline();
void keyboardSetBacklight(uint8_t v);  // 0..255

// Loop task: drains the queue, one event per call (false when none).
bool keyboardPoll(KbdEvent& ev);

// Key levels the gesture decoder samples (input.cpp); set by keyboardPoll.
bool keyboardEncDown();             // space held
bool keyboardSideDown();            // sym held

#ifdef BENCH_CMDS
void keyboardEcho(bool on);         // print every raw code ([Kbd] raw XX)
bool keyboardEchoOn();
#endif
