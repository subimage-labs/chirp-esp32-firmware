// balltap.h — trackball motion as the controls the router knows. Pure;
// host-tested (balltap_test.cpp). Loop task only (input.cpp).
//
// BallTap: a sideways flick as one step (T-Deck: right = next menu tab,
// left = the one before; input.cpp keeps one per direction). A flick is a
// burst of pulses: a burst of at least MIN_PULSES is one step, and the next
// needs QUIET_MS without pulses first, so one flick never counts twice and a
// stray pulse while scrolling counts as nothing.
//
// BallSteps: the vertical roll as wheel detents, a set number of pulses per
// detent. A ball pulses far faster than a wheel clicks; one detent per pulse
// read every roll as a fast spin (2 menu rows, 16 chat rows per pulse).
#pragma once

#include <stdint.h>

class BallTap {
public:
    static const int32_t MIN_PULSES = 2;
    static const uint32_t QUIET_MS = 150;

    // pulses: sideways pulses since the last call. True once per tap.
    bool feed(uint32_t now, int32_t pulses) {
        if (pulses <= 0) return false;
        if (count_ > 0 && now - last_ >= QUIET_MS) {   // a new burst
            count_ = 0;
            fired_ = false;
        }
        last_ = now;
        count_ += pulses;
        if (fired_ || count_ < MIN_PULSES) return false;
        fired_ = true;
        return true;
    }

private:
    uint32_t last_ = 0;
    int32_t count_ = 0;
    bool fired_ = false;
};

class BallSteps {
public:
    explicit BallSteps(int32_t pulsesPerStep) : per_(pulsesPerStep > 0 ? pulsesPerStep : 1) {}

    // net: up minus down pulses since the last call. -> detents (+ = up). The
    // remainder carries over in the same direction; a reversal drops it, so
    // turning back always takes a full step's pulses.
    int32_t feed(int32_t net) {
        if (!net) return 0;
        if (acc_ && (net > 0) != (acc_ > 0)) acc_ = 0;
        acc_ += net;
        const int32_t steps = acc_ / per_;
        acc_ -= steps * per_;
        return steps;
    }

private:
    int32_t per_;
    int32_t acc_ = 0;
};
