// wheel.h — rotary encoder decoded in a GPIO interrupt.
//
// The Arduino loop blocks in i2s_read (20-100 ms) and on full repaints, far
// longer than the gap between quadrature edges of a spinning wheel; polling
// dropped clicks. The ISR sees every edge and publishes the detent count.
//
// Detents (FOUR3): the wheel rests with both lines high (state 3); one
// click is 4 valid edges: clockwise 3→1→0→2→3 counts up.
#pragma once

#include <cstdint>

namespace wheel_detail {
// Direction per transition, index = newState | oldState << 2: +1, -1 or 0
// (no change / invalid double-edge). Packed 2 bits per entry (1=+1, 2=-1)
// into an immediate, so the ISR needs no table in flash.
constexpr int8_t kDir[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};
constexpr uint32_t pack(int i = 0) {  // single-return recursion: C++11 constexpr
    return i == 16 ? 0u
                   : ((uint32_t)(kDir[i] == 1 ? 1 : kDir[i] == -1 ? 2 : 0) << (2 * i)) |
                         pack(i + 1);
}
constexpr uint32_t kPacked = pack();
static_assert(kPacked == 0x24428118u, "transition table packing");
}  // namespace wheel_detail

// Pure decoder; the ISR owns one instance. Host-tested.
struct QuadDecoder {
    uint8_t oldState = 3;
    int32_t quarter = 0;  // quarter steps; detent = quarter >> 2 at rest

    // Feeds the 2-bit pin state (a | b << 1). Returns true when the wheel
    // reached the rest state, i.e. detent() is a settled click count.
    __attribute__((always_inline)) inline bool step(uint8_t state) {
        if (state == oldState) return false;
        const uint32_t code =
            (wheel_detail::kPacked >> (2 * (state | (oldState << 2)))) & 3u;
        quarter += (code == 1) - (code == 2);
        oldState = state;
        return state == 3;
    }
    int32_t detent() const { return quarter >> 2; }
};

// Device only. Pins must be GPIO < 32 (read from GPIO_IN_REG).
bool wheelBegin(int pinA, int pinB);
int32_t wheelDetents();  // settled click count since boot
