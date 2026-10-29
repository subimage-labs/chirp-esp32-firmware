// keyboard.h — the T-Deck keyboard (an ESP32-C3 on I2C, address 0x55), run
// in raw mode: it reports which keys are down, and keymap.h decodes that into
// typing and the mic key. Nothing signals a change, so a task of its own
// (app.h: kbd) reads it every 10 ms, whatever the loop is doing: the mic key
// is the encoder key's level for the key sampler (input.cpp), and a busy
// loop must not stretch a tap into a hold. keyboardBegin and keyboardPoll:
// loop task; keyboardMicDown: any task.
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
void keyboardEcho(bool on);         // bench `keys on`: print matrix changes and ball pulses
bool keyboardEchoOn();
#endif
