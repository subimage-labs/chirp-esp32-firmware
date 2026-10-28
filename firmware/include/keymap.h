// keymap.h — the T-Deck keyboard's I2C bytes classified into app actions.
// Pure (no Arduino), host-tested; keyboard.cpp feeds it every byte it reads.
//
// LILYGO's keyboard firmware (ESP32-C3, examples/Keyboard_ESP32C3) answers
// each read with one byte: the last key pressed, or 0x00. It sends ASCII for
// printable keys (space included), 0x08 for backspace, 0x0D for enter and
// 0x0C for alt+C. Sym and shift are applied on the C3 (symbols, upper case)
// and send nothing on their own; there are no release codes and no repeat,
// so no key can be held. Shift subtracts 32 from any key, so shift+space
// reads as 0x00 and shift+$ as a control code (Unknown).
#pragma once

#include <stdint.h>

enum class KeyAct : uint8_t {
    None,        // 0x00: no key pressed since the last read
    Print,       // a printable ASCII character, space included
    Backspace,
    Enter,
    Unknown,
};

inline KeyAct keyAct(uint8_t code) {
    if (code == 0x00 || code == 0xFF) return KeyAct::None;   // 0xFF: an idle bus reads high
    if (code == 0x08) return KeyAct::Backspace;
    if (code == '\r' || code == '\n') return KeyAct::Enter;
    if (code >= 0x20 && code < 0x7F) return KeyAct::Print;
    return KeyAct::Unknown;
}

// The compose line keeps one line of typed text; this is its whole editor
// (the keyboard has no cursor keys). Pure, host-tested.
struct ComposeBuffer {
    static const size_t CAP = 160;
    char buf[CAP + 1] = {0};
    size_t len = 0;

    bool put(char c) {
        if (c < 0x20 || c >= 0x7F || len >= CAP) return false;
        buf[len++] = c;
        buf[len] = 0;
        return true;
    }
    bool backspace() {
        if (!len) return false;
        buf[--len] = 0;
        return true;
    }
    void clear() {
        len = 0;
        buf[0] = 0;
    }
    const char* text() const { return buf; }
    bool empty() const { return len == 0; }
};
