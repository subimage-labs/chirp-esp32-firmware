// balltap.h — the trackball's vertical roll as the wheel's detents. Pure;
// host-tested (balltap_test.cpp). Loop task only (input.cpp).
//
// BallSteps: a set number of pulses per detent. A ball pulses far faster
// than a wheel clicks; one detent per pulse read every roll as a fast spin
// (2 menu rows, 16 chat rows per pulse). Sideways rolls are not read.
#pragma once

#include <stdint.h>

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
