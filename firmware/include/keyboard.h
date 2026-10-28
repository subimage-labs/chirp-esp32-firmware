// keyboard.h — the T-Deck keyboard (an ESP32-C3 on I2C, address 0x55). It
// holds one byte, the last key pressed, until read (keymap.h has what it
// sends). Nothing signals a waiting key, so the loop polls it every
// KBD_POLL_MS (I2C from the 5 ms key sampler would hold the timer task for
// the whole transaction) and routes each byte through keymap.h. Keys only
// type: they have no release codes, so none can stand in for a held button
// (the trackball does: click = the encoder key, roll right = side taps).
// Loop task only.
#pragma once

#include <stdint.h>

enum class KbdEv : uint8_t {
    None,
    Char,        // a printable character (c), space included
    Backspace,
    Enter,
    Raw,         // an unclassified code (raw), logged
};

struct KbdEvent {
    KbdEv kind = KbdEv::None;
    char c = 0;
    uint8_t raw = 0;
};

bool keyboardBegin();               // Wire on the board's I2C pins; false: no keyboard
bool keyboardOnline();
void keyboardSetBacklight(uint8_t v);  // 0..255

// Loop task: at most one event per KBD_POLL_MS (false when none or not due).
bool keyboardPoll(KbdEvent& ev);

#ifdef BENCH_CMDS
void keyboardEcho(bool on);         // print every raw code ([Kbd] raw XX)
bool keyboardEchoOn();
#endif
