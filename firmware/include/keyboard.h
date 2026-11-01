// keyboard.h — the T-Deck keyboard (an ESP32-C3 on I2C, address 0x55), run
// in raw mode: it reports which keys are down, and keymap.h decodes that into
// typing and the mic key. Nothing signals a change, so a task of its own
// (app.h: kbd) reads it every 10 ms, whatever the loop is doing: the mic key
// is the encoder key's level for the key sampler (input.cpp), and a busy
// loop must not stretch a tap into a hold. keyboardBegin, keyboardReinit,
// keyboardSetBacklight and keyboardPoll: loop task; keyboardMicDown: any task.
// The decoder and the typed-key queue's sender belong to the kbd task, but
// only under the I2C bus lock (i2c_bus.h), which keyboardReinit also holds
// to reset them: the task never reads between a C3 reboot and raw mode.
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
                                    // (or its task would not start: keyboardPoll stays empty)
bool keyboardOnline();
void keyboardSetBacklight(uint8_t v);  // 0..255; kept, and re-sent by keyboardReinit

// Loop task, after the peripheral rail came back (the C3 lost power and
// boots in character mode): waits for the C3, sets raw mode and the backlight
// again, and clears the decoder, the mic key's level and the queued keys.
// False: no keyboard, or the C3 never answered. Compiled only with a keyboard
// (CHIRP_HAS_KEYBOARD), like the rest of the module; callers guard on it.
bool keyboardReinit();

// Loop task: the next typed key (false when none).
bool keyboardPoll(KbdEvent& ev);

bool keyboardMicDown();             // the mic key held (as of the last poll)

#ifdef BENCH_CMDS
void keyboardEcho(bool on);         // bench `keys on`: print matrix changes and ball pulses
bool keyboardEchoOn();
#endif
