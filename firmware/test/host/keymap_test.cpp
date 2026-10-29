// keymap_test.cpp — the T-Deck keyboard's matrix decoder and the compose
// line's editor (keymap.h). Pure, host-side.
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>

#include "keymap.h"

using namespace keymap;

static int fails = 0;
static void ok(bool cond, const char* what) {
    if (!cond) {
        printf("FAIL %s\n", what);
        fails++;
    }
}

// The matrix with these keys down.
static Matrix keys(std::initializer_list<Pos> down) {
    Matrix m;
    for (const Pos& p : down) m.col[p.c] |= (uint8_t)(1 << p.r);
    return m;
}

// What one feed typed: characters as themselves, '<' backspace, '\n' enter.
static std::string typed(Decoder& d, const Matrix& m) {
    std::string s;
    d.feed(m, [&](const KeyEvent& e) {
        s += e.key == Key::Char ? e.c : e.key == Key::Backspace ? '<' : '\n';
    });
    return s;
}

const Pos A = {0, 3}, Q = {0, 0}, SPACE = {0, 5}, DOLLAR = {4, 4}, B = {3, 4};

static void testDecoder() {
    {
        Decoder d;
        ok(typed(d, keys({A})) == "a", "a press types a");
        ok(typed(d, keys({A})) == "", "held: no repeat");
        ok(typed(d, keys({})) == "", "release types nothing");
        ok(typed(d, keys({A})) == "a", "pressed again: types again");
    }
    {
        Decoder d;
        ok(typed(d, keys({LSHIFT, A})) == "A", "shift upper-cases");
        typed(d, keys({}));
        ok(typed(d, keys({RSHIFT, DOLLAR})) == "$", "shift leaves $ alone");
        typed(d, keys({}));
        ok(typed(d, keys({LSHIFT, SPACE})) == " ", "shift+space is a space");
    }
    {
        Decoder d;
        ok(typed(d, keys({SYM, Q})) == "#", "sym types the symbol layer");
        typed(d, keys({}));
        ok(typed(d, keys({SYM, SPACE})) == "", "no symbol on space: nothing");
        typed(d, keys({}));
        ok(typed(d, keys({SYM, MIC})) == "0" && !d.micDown(), "sym+mic types 0, never talks");
    }
    {
        Decoder d;
        ok(typed(d, keys({MIC})) == "" && d.micDown(), "mic held: talk, no character");
        ok(d.micDown(), "still held");
        typed(d, keys({}));
        ok(!d.micDown(), "mic released");
    }
    {
        Decoder d;
        ok(typed(d, keys({ALT, B})) == "", "alt+B types nothing (the C3's backlight)");
        typed(d, keys({}));
        ok(typed(d, keys({ENTER})) == "\n", "enter");
        typed(d, keys({}));
        ok(typed(d, keys({BACKSPACE})) == "<", "backspace");
        typed(d, keys({}));
        ok(typed(d, keys({ALT, ENTER})) == "\n", "alt does not swallow enter");
    }
    {
        Decoder d;
        ok(typed(d, keys({Q, A})) == "qa", "two keys in one read: both, in matrix order");
    }
    int seen[26] = {0};
    for (int c = 0; c < COLS; c++)
        for (int r = 0; r < ROWS; r++)
            if (PLAIN[c][r] >= 'a' && PLAIN[c][r] <= 'z') seen[PLAIN[c][r] - 'a']++;
    bool once = true;
    for (int i = 0; i < 26; i++) once = once && seen[i] == 1;
    ok(once, "the plain layer has every letter exactly once");
}

static void testCompose() {
    ComposeBuffer c;
    ok(c.empty(), "starts empty");
    ok(!strcmp(c.text(), ""), "starts as empty string");
    ok(c.put('h') && c.put('i'), "letters append");
    ok(!strcmp(c.text(), "hi"), "text is what was typed");
    ok(c.put('\t') == false, "control characters refused");
    ok(c.put(0xE9) == false, "non-ASCII refused (single-byte line)");
    ok(c.backspace(), "backspace edits");
    ok(!strcmp(c.text(), "h"), "backspace removed one char");
    ok(c.put(' ') && c.put('!'), "space and punctuation append");
    ok(!strcmp(c.text(), "h !"), "space and punctuation appended");
    for (size_t i = 0; i < ComposeBuffer::CAP + 10; i++) c.put('x');
    ok(c.len == ComposeBuffer::CAP, "caps at CAP");
    ok(c.text()[ComposeBuffer::CAP] == 0, "stays NUL-terminated at the cap");
    c.clear();
    ok(c.empty() && !c.backspace(), "clear empties; backspace on empty fails");
}

int main() {
    testDecoder();
    testCompose();
    if (fails) {
        printf("%d keymap test(s) failed\n", fails);
        return 1;
    }
    printf("keymap tests passed\n");
    return 0;
}
