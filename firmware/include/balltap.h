// balltap.h — a sideways trackball flick as one step (T-Deck: right = next
// menu tab, left = the one before; input.cpp keeps one per direction). A
// flick is a burst of pulses: a burst of at least MIN_PULSES is one step,
// and the next needs QUIET_MS without pulses first, so one flick never counts
// twice and a stray pulse while scrolling counts as nothing. Pure;
// host-tested (balltap_test.cpp). Loop task only (input.cpp).
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
