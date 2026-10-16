// Host tests for the wrapping-clock expiry window (expiry.h).
// Run: firmware/test/host/run.sh
#include "expiry.h"

#include <initializer_list>
#include <cstdio>

static int failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,   \
                         __LINE__, #cond);                                \
            failures++;                                                   \
        }                                                                 \
    } while (0)

static void neverArmed() {
    Expiry e;
    for (uint32_t now : {0u, 1u, 2500u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu, 2147483648u + 2500u})
        CHECK(!expiryActive(e, now));
    // The old bug: a zero deadline read as live once the clock passed 2^31.
    CHECK(!expiryActive(e, 0x80000001u));
    CHECK(!e.armed);
}

static void normalExpiry() {
    Expiry e;
    expiryArm(e, 1000, 2500);
    CHECK(expiryActive(e, 1000));
    CHECK(expiryActive(e, 3499));
    CHECK(e.armed);
    CHECK(!expiryActive(e, 3500));                  // the deadline itself is over
    CHECK(!e.armed);                                // and the window is disarmed
    CHECK(!expiryActive(e, 3499));                  // stays off, even for an earlier clock
    expiryArm(e, 5000, 100);                        // re-arming works
    CHECK(expiryActive(e, 5099) && !expiryActive(e, 5100));
}

static void signedHalfway() {
    // Past the deadline by up to 2^31 - 1 ms the window is off; exactly 2^31 it is too.
    Expiry e;
    expiryArm(e, 0, 2500);
    CHECK(!expiryActive(e, 2500 + 0x7FFFFFFFu));
    expiryArm(e, 0, 2500);
    CHECK(!expiryActive(e, 2500 + 0x80000000u));
    // Disarmed at expiry, a late query half a clock period on cannot revive it.
    expiryArm(e, 0, 2500);
    CHECK(!expiryActive(e, 2500));
    CHECK(!expiryActive(e, 2500 + 0x80000001u));
    CHECK(!expiryActive(e, 2500 + 0x80000000u + 12345u));
    // Armed on a clock that is itself past the signed halfway point.
    expiryArm(e, 0x80000000u, 2500);
    CHECK(expiryActive(e, 0x80000000u + 2499) && !expiryActive(e, 0x80000000u + 2500));
}

static void rollover() {
    Expiry e;
    expiryArm(e, 0xFFFFFF00u, 2500);                // deadline wraps to 2244
    CHECK(expiryActive(e, 0xFFFFFF00u));
    CHECK(expiryActive(e, 0xFFFFFFFFu));
    CHECK(expiryActive(e, 0));                      // across the wrap
    CHECK(expiryActive(e, 2243));
    CHECK(!expiryActive(e, 2244));
    CHECK(!expiryActive(e, 2245));

    // Polled every second across 30 days of uptime: one error, active exactly
    // for its window, never again afterwards.
    Expiry w;
    const uint32_t start = 0xFFFFF000u;             // so the run crosses 2^32 too
    uint32_t now = start;
    expiryArm(w, now, 2500);
    uint32_t activeSeconds = 0;
    for (uint64_t s = 0; s < 30ull * 24 * 3600; s++, now += 1000) {
        if (expiryActive(w, now)) activeSeconds++;
    }
    CHECK(activeSeconds == 3);                      // polls at +0, +1 s, +2 s
}

int main() {
    neverArmed();
    normalExpiry();
    signedHalfway();
    rollover();
    if (failures) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::puts("expiry_test: ok");
    return 0;
}
