// keymap.h — the T-Deck keyboard's key matrix decoded into typing and the
// mic key. Pure (no Arduino), host-tested; keyboard.cpp feeds it every
// matrix it reads.
//
// The keyboard is an ESP32-C3 running LILYGO's firmware
// (examples/Keyboard_ESP32C3, MIT), switched to raw mode (command 0x03,
// firmware from 2025-06): each read answers 5 bytes, one per column, bit r
// set while row r's key is down. The decoder does what the C3 does in its
// character mode (the tables below are its layout) and also reports keys
// held, which character mode cannot: the mic key is a push-to-talk button.
// Sym held types the symbol layer; shift held upper-cases letters; alt
// combinations type nothing (alt+B is the C3's own backlight toggle).
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace keymap {

const int COLS = 5, ROWS = 7;

struct Matrix {
    uint8_t col[COLS] = {0, 0, 0, 0, 0};
    bool down(int c, int r) const { return (col[c] >> r) & 1; }
};

// Keys with a role rather than a character: {column, row}.
struct Pos {
    int c, r;
};
const Pos SYM = {0, 2}, ALT = {0, 4}, MIC = {0, 6}, LSHIFT = {1, 6}, RSHIFT = {2, 3},
          ENTER = {3, 3}, BACKSPACE = {4, 3};

// [column][row]; 0 = no character on that layer.
const char PLAIN[COLS][ROWS] = {
    {'q', 'w', 0, 'a', 0, ' ', 0},
    {'e', 's', 'd', 'p', 'x', 'z', 0},
    {'r', 'g', 't', 0, 'v', 'c', 'f'},
    {'u', 'h', 'y', 0, 'b', 'n', 'j'},
    {'o', 'l', 'i', 0, '$', 'm', 'k'},
};
const char SYMBOL[COLS][ROWS] = {
    {'#', '1', 0, '*', 0, 0, '0'},
    {'2', '4', '5', '@', '8', '7', 0},
    {'3', '/', '(', 0, '?', '9', '6'},
    {'_', ':', ')', 0, '!', ',', ';'},
    {'+', '"', '-', 0, 0, '.', '\''},
};

enum class Key : uint8_t { Char, Backspace, Enter };

struct KeyEvent {
    Key key;
    char c;      // Key::Char
};

class Decoder {
public:
    // The newest matrix: out(KeyEvent) for every key that went down since the
    // last one, in column/row order.
    template <class Out>
    void feed(const Matrix& m, Out out) {
        const bool sym = m.down(SYM.c, SYM.r), alt = m.down(ALT.c, ALT.r);
        const bool shift = m.down(LSHIFT.c, LSHIFT.r) || m.down(RSHIFT.c, RSHIFT.r);
        for (int c = 0; c < COLS; c++) {
            const uint8_t pressed = m.col[c] & ~prev_.col[c];
            for (int r = 0; r < ROWS; r++) {
                if (!((pressed >> r) & 1)) continue;
                if (c == ENTER.c && r == ENTER.r) out(KeyEvent{Key::Enter, 0});
                else if (c == BACKSPACE.c && r == BACKSPACE.r) out(KeyEvent{Key::Backspace, 0});
                else if (alt) continue;
                else if (char ch = (sym ? SYMBOL : PLAIN)[c][r]) {
                    if (shift && ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
                    out(KeyEvent{Key::Char, ch});
                }
            }
        }
        prev_ = m;
    }

    // The mic key held, as push-to-talk (with sym held it types '0' instead).
    bool micDown() const { return prev_.down(MIC.c, MIC.r) && !prev_.down(SYM.c, SYM.r); }

private:
    Matrix prev_;
};

}  // namespace keymap

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
