// balltap_test.cpp — trackball motion: the vertical roll becomes wheel
// detents at a set number of pulses each.
// Run: firmware/test/host/run.sh
#include "balltap.h"

#include <cstdio>

static int failures = 0;
#define CHECK(cond, msg)                                         \
    do {                                                         \
        if (!(cond)) {                                           \
            printf("FAIL %s:%d: %s — %s\n", __FILE__, __LINE__, #cond, msg); \
            failures++;                                          \
        }                                                        \
    } while (0)

int main() {
    {
        BallSteps s(2);                         // two pulses per wheel detent
        CHECK(s.feed(1) == 0, "half a step: nothing yet");
        CHECK(s.feed(1) == 1, "the second pulse completes the step");
        CHECK(s.feed(5) == 2, "a batch: whole steps, the rest kept");
        CHECK(s.feed(1) == 1, "the kept pulse counts toward the next step");
        CHECK(s.feed(-1) == 0, "turning back drops the kept half: no step yet");
        CHECK(s.feed(-1) == -1, "a full step down");
        CHECK(s.feed(0) == 0, "no pulses, no step");
    }
    {
        BallSteps s(3);
        CHECK(s.feed(2) == 0 && s.feed(-2) == 0 && s.feed(-1) == -1,
              "a reversal never borrows the other direction's pulses");
        BallSteps one(0);
        CHECK(one.feed(4) == 4, "a nonsense divider acts as one pulse per step");
    }
    if (failures) {
        printf("balltap: %d check(s) failed\n", failures);
        return 1;
    }
    printf("balltap: all checks passed\n");
    return 0;
}
