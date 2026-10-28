// keymap_test.cpp — the T-Keyboard's scancode classification and the compose
// line's editor (keymap.h). Pure, host-side.
#include <cstdio>
#include <cstring>

#include "keymap.h"

static int fails = 0;
static void ok(bool cond, const char* what) {
    if (!cond) {
        printf("FAIL %s\n", what);
        fails++;
    }
}

static void testClassification() {
    ok(keyAct(0x00) == KeyAct::None, "idle byte is None");
    ok(keyAct(0xFF) == KeyAct::None, "0xFF is None");
    ok(keyAct('a') == KeyAct::Print, "letter prints");
    ok(keyAct('Z') == KeyAct::Print, "capital prints");
    ok(keyAct('5') == KeyAct::Print, "digit prints");
    ok(keyAct('.') == KeyAct::Print, "punctuation prints");
    ok(keyAct(0x7F) == KeyAct::Unknown, "DEL is Unknown");
    ok(keyAct(0x01) == KeyAct::Unknown, "control code is Unknown");
    ok(keyAct(0x80) == KeyAct::Unknown, "high bit is Unknown");
    ok(keyAct(0x08) == KeyAct::Backspace, "0x08 backspaces");
    ok(keyAct('\r') == KeyAct::Enter, "CR enters");
    ok(keyAct('\n') == KeyAct::Enter, "LF enters");
    ok(keyAct(' ') == KeyAct::Space, "space is the talk key");
    ok(keyAct(KBD_CODE_SYM) == KeyAct::SymDown, "sym press");
    ok(keyAct(KBD_CODE_SYM + 1) == KeyAct::SymUp, "sym release");
    ok(keyAct(KBD_CODE_SHIFT) == KeyAct::Shift, "shift classifies");
    ok(keyAct(KBD_CODE_ALT) == KeyAct::Alt, "alt classifies");
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
    testClassification();
    testCompose();
    if (fails) {
        printf("%d keymap test(s) failed\n", fails);
        return 1;
    }
    printf("keymap tests passed\n");
    return 0;
}
