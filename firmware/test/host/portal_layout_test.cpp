// portal_layout_test.cpp — the setup screen on every board's panel: every
// element inside the screen, the column clear of the code, and every string
// in the Fixed cut (measured from the real font data) inside its slot, the
// status messages on at most two lines. Run: firmware/test/host/run.sh
#include "portal_layout.h"

#include <cstdio>
#include <string>

#include "fontpick.h"
#include "setup_form.h"

#include "../../src/fonts/font_data.cpp"

using namespace portal_layout;

static int failures = 0;
#define CHECK(cond, ...)                                    \
    do {                                                    \
        if (!(cond)) {                                      \
            printf("FAIL %s:%d: %s — ", __FILE__, __LINE__, #cond); \
            printf(__VA_ARGS__);                            \
            printf("\n");                                   \
            failures++;                                     \
        }                                                   \
    } while (0)

static const FontCut& fixedCut() {
    return FONT_FAMILIES[fontFind(FONT_FIXED_FAMILY)].size[FONT_SIZE_M];
}
static int width(const char* s, int n) { return vlwWidth(fixedCut(), std::string(s, n).c_str()); }
static int width(const std::string& s) { return width(s.data(), (int)s.size()); }

// The widest string a slot can get: n copies of the alphabet's widest glyph.
static std::string widest(const char* alphabet, size_t n) {
    char best = alphabet[0];
    for (const char* p = alphabet; *p; p++)
        if (width(p, 1) > width(&best, 1)) best = *p;
    return std::string(n, best);
}

static void checkPanel(const char* board, int w, int h, int modules) {
    const Layout l = compute(w, h, modules);
    const int bodyTop = l.logoH + HEADLINE_H;
    // Inside the screen, clear of each other.
    CHECK(l.qrX >= 0 && l.qrY >= bodyTop && l.qrY + l.qrSide <= h, "%s %dx%d v%d: QR %d..%d",
          board, w, h, modules, l.qrY, l.qrY + l.qrSide);
    CHECK(l.colX >= l.qrX + l.qrSide + GUTTER, "%s: column over the code", board);
    CHECK(l.colW >= COL_MIN, "%s: column %d < %d", board, l.colW, COL_MIN);
    CHECK(l.labelY >= bodyTop, "%s: column over the headline (%d < %d)", board, l.labelY, bodyTop);
    CHECK(l.statusY + STATUS_LINES * LINE <= h, "%s %dx%d v%d: two status lines end at %d > %d",
          board, w, h, modules, l.statusY + STATUS_LINES * LINE, h);
    CHECK(l.qrScale >= 3, "%s: QR modules %d px: too small to scan reliably", board, l.qrScale);
    // Every string in its slot.
    CHECK(width(HEADLINE) <= w - 2 * M, "%s: headline %d px > %d", board, width(HEADLINE), w - 2 * M);
    CHECK(width(LABEL) <= l.colW, "%s: label %d px > %d", board, width(LABEL), l.colW);
    const std::string ssid = "Chirp-" + widest("0123456789ABCDEF", 4);   // portal.cpp's %02X%02X
    CHECK(width(ssid) <= l.cardRoom, "%s: network name %d px > %d", board, width(ssid), l.cardRoom);
    const std::string pass = widest(PORTAL_PASS_ALPHABET, PORTAL_PASS_LEN);
    CHECK(width(pass) <= l.cardRoom, "%s: worst password %d px > %d", board, width(pass), l.cardRoom);
    for (const char* msg : {MSG_CONNECTED, MSG_SAVED, MSG_FINISH, MSG_CANCEL_SIDE_KEY,
                            MSG_CANCEL_BACKSPACE}) {
        const int split = statusSplit(msg, (int)std::string(msg).size(), l.statusRoom,
                                      [](const char* s, int n) { return width(s, n); });
        CHECK(split != -2, "%s: \"%s\" does not fit two lines of %d px", board, msg, l.statusRoom);
    }
}

static void testWrap() {
    auto chars = [](const char*, int n) { return n * 10; };   // 10 px a byte
    CHECK(statusSplit("ab cd", 5, 50, chars) == -1, "fits on one line");
    CHECK(statusSplit("ab cd", 5, 30, chars) == 2, "wraps at the space");
    CHECK(statusSplit("abc defgh", 9, 40, chars) == -2, "second line too long");
    CHECK(statusSplit("abcdef", 6, 30, chars) == -2, "no space to wrap at");
    CHECK(statusSplit("a b c", 5, 30, chars) == 3, "last space that fits wins");
}

int main() {
    testWrap();
    for (int modules : {29, 33}) {             // QR versions 3 and 4
        checkPanel("T-Deck", 320, 240, modules);
        checkPanel("T-Embed", 320, 170, modules);
    }
    if (failures) {
        printf("portal_layout: %d check(s) failed\n", failures);
        return 1;
    }
    printf("portal_layout: all checks passed\n");
    return 0;
}
