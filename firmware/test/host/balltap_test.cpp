// balltap_test.cpp — sideways trackball flicks become tab steps: one per
// flick, none for a stray pulse, three for three flicks with pauses.
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
    if (failures) {
        printf("balltap: %d check(s) failed\n", failures);
        return 1;
    }
    printf("balltap: all checks passed\n");
    return 0;
}
