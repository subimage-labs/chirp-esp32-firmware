// expiry.h — a one-shot "until" window on a wrapping 32-bit millisecond clock.
//
// An unarmed window is never active (a zero deadline would read as live for
// 24.9 days at every wrap). Asking whether it is still active disarms it once
// the deadline has passed, so a stale deadline cannot come back half a clock
// period later; whoever owns a window must therefore ask at least once per
// ~24 days (character.cpp polls it on every loop pass). The clock is passed
// in, so this is host-tested. Owner: whoever holds the window.
#pragma once

#include <stdint.h>

struct Expiry {
    bool armed = false;
    uint32_t until = 0;
};

inline void expiryArm(Expiry& e, uint32_t now, uint32_t forMs) {
    e.armed = true;
    e.until = now + forMs;                        // wraps with the clock
}

// Still inside the window? Disarms when the deadline has passed.
inline bool expiryActive(Expiry& e, uint32_t now) {
    if (!e.armed) return false;
    if ((int32_t)(e.until - now) > 0) return true;
    e.armed = false;
    return false;
}
