// Host tests for the wheel quadrature decoder. Run: firmware/test/host/run.sh
#include "wheel.h"

#include <cstdio>
#include <initializer_list>

static int failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,   \
                         __LINE__, #cond);                                \
            failures++;                                                   \
        }                                                                 \
    } while (0)

static void feed(QuadDecoder& d, std::initializer_list<uint8_t> states) {
    for (uint8_t s : states) d.step(s);
}

// Clockwise 3→1→0→2→3 is +1 click, the reverse is -1 (menu/scroll/volume
// directions depend on this sign).
static void fullClickDirections() {
    QuadDecoder d;
    feed(d, {1, 0, 2, 3});
    CHECK(d.detent() == 1);
    feed(d, {2, 0, 1, 3});
    CHECK(d.detent() == 0);
    feed(d, {2, 0, 1, 3, 2, 0, 1, 3});
    CHECK(d.detent() == -2);
}

// A fast spin of many clicks counts every one.
static void spinCountsEveryClick() {
    QuadDecoder d;
    for (int i = 0; i < 250; i++) feed(d, {1, 0, 2, 3});
    CHECK(d.detent() == 250);
}

// Contact bounce at rest must never register a click.
static void restBounceIsNotAClick() {
    QuadDecoder d;
    for (int i = 0; i < 20; i++) feed(d, {1, 3, 2, 3});
    CHECK(d.detent() == 0);
}

// Only the rest state settles a click: mid-rotation states report false.
static void settlesOnlyAtRest() {
    QuadDecoder d;
    CHECK(!d.step(1));
    CHECK(!d.step(0));
    CHECK(!d.step(2));
    CHECK(d.step(3));
    CHECK(!d.step(3));  // no edge
}

// Turning partway and back returns to the same click.
static void partialTurnAndBack() {
    QuadDecoder d;
    feed(d, {1, 0, 1, 3});
    CHECK(d.detent() == 0);
}

int main() {
    fullClickDirections();
    spinCountsEveryClick();
    restBounceIsNotAClick();
    settlesOnlyAtRest();
    partialTurnAndBack();
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("wheel: all checks passed\n");
    return 0;
}
