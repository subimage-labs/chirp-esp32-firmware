// balltap_test.cpp — trackball motion: sideways flicks become tab steps (one
// per flick, none for a stray pulse), the vertical roll becomes wheel
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

// Feeds one pulse every `gap` ms, `n` times, from t; returns the taps.
static int flick(BallTap& b, uint32_t& t, int n, uint32_t gap = 10) {
    int taps = 0;
    for (int i = 0; i < n; i++, t += gap) taps += b.feed(t, 1);
    return taps;
}

int main() {
    {
        BallTap b;
        uint32_t t = 1000;
        CHECK(flick(b, t, 8) == 1, "one flick of 8 pulses is one tap");
    }
    {
        BallTap b;
        uint32_t t = 1000;
        CHECK(flick(b, t, 1) == 0, "a lone pulse is no tap");
        t += BallTap::QUIET_MS;
        CHECK(flick(b, t, 1) == 0, "nor is another lone pulse after a pause");
    }
    {
        BallTap b;
        uint32_t t = 1000;
        int taps = 0;
        for (int k = 0; k < 3; k++) {
            taps += flick(b, t, 4);
            t += BallTap::QUIET_MS;              // the pause between flicks
        }
        CHECK(taps == 3, "three flicks with pauses are three taps");
    }
    {
        BallTap b;
        uint32_t t = 1000;
        int taps = flick(b, t, 4);
        t += BallTap::QUIET_MS - 20;            // too soon: the same flick still
        taps += flick(b, t, 4);
        CHECK(taps == 1, "pulses before the pause ends belong to the same flick");
    }
    {
        BallTap b;
        CHECK(!b.feed(1000, 0), "no pulses, no tap");
        CHECK(b.feed(1000, 3), "a batch of pulses in one call is a tap");
    }
    {
        BallTap b;                              // millis() wrap inside a flick
        uint32_t t = 0xFFFFFFF0u;
        CHECK(flick(b, t, 4) == 1, "a flick across the millis wrap is one tap");
    }
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
