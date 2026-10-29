// keyboard.h — the T-Deck keyboard (an ESP32-C3 on I2C, address 0x55), run
// in raw mode: it reports which keys are down, and keymap.h decodes that into
// typing and the mic key. Nothing signals a change, so the loop polls it
// every 10 ms (I2C from the 5 ms key sampler would hold the timer task for
// the whole transaction). The mic key is the encoder key's level for the
// key sampler (input.cpp): hold to talk, double-tap for voice mode.
// Loop task, apart from keyboardMicDown (any task).
#pragma once

#include <stdint.h>

enum class KbdEv : uint8_t {
    None,
    Char,        // a printable character (c), space included
    Backspace,
    Enter,
};

struct KbdEvent {
    KbdEv kind = KbdEv::None;
    char c = 0;
};

bool keyboardBegin();               // Wire on the board's I2C pins, raw mode; false: no keyboard
bool keyboardOnline();
void keyboardSetBacklight(uint8_t v);  // 0..255

// Loop task: the next typed key (false when none).
bool keyboardPoll(KbdEvent& ev);

bool keyboardMicDown();             // the mic key held (as of the last poll)

#ifdef BENCH_CMDS
void keyboardEcho(bool on);         // print every matrix change ([Kbd] raw ...)
bool keyboardEchoOn();
#endif
