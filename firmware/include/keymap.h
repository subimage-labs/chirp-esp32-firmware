// keymap.h — the T-Keyboard's I2C scancodes classified into app actions.
// Pure (no Arduino), host-tested; keyboard.cpp feeds it every byte the ESP32-C3
// coprocessor queues and routes the result.
//
// The stock T-Keyboard firmware sends ASCII for printable keys, 0x08 for
// backspace, 0x0D for enter. Modifier keys (sym, shift, alt) arrive as
// codes >= 0x80 whose exact values the vendor firmware does not document;
// the table below carries the ones observed on the device (bench `keys`
// prints every raw code it sees, so an unlisted modifier shows up as
// KeyAct::Unknown and can be added here).
#pragma once

#include <stdint.h>

enum class KeyAct : uint8_t {
    None,        // 0x00: no key queued (the C3's idle byte)
    Print,       // a printable ASCII character
    Backspace,
    Enter,
    Space,       // held = push-to-talk (the encoder key's role)
    SymDown,     // the ?123 key's press (the side key's role)
    SymUp,
    Shift,       // modifiers the app currently ignores
    Alt,
    Unknown,
};

// Observed vendor modifier codes (bench `keys` on the device).
static const uint8_t KBD_CODE_SYM = 0xBB;    // ?123 key
static const uint8_t KBD_CODE_SHIFT = 0xB1;  // ⇧ key
static const uint8_t KBD_CODE_ALT = 0xB2;    // ALT (alt+B toggles key backlight)

inline KeyAct keyAct(uint8_t code) {
    if (code == 0x00 || code == 0xFF) return KeyAct::None;
    if (code == KBD_CODE_SYM) return KeyAct::SymDown;
    if (code == KBD_CODE_SYM + 1) return KeyAct::SymUp;
    if (code == KBD_CODE_SHIFT) return KeyAct::Shift;
    if (code == KBD_CODE_ALT) return KeyAct::Alt;
    if (code == 0x08) return KeyAct::Backspace;
    if (code == '\r' || code == '\n') return KeyAct::Enter;
    if (code == ' ') return KeyAct::Space;
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
