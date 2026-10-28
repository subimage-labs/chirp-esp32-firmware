#include "trackball.h"

#include "config.h"      // board.h: CHIRP_HAS_TRACKBALL and the board's pins

#if CHIRP_HAS_TRACKBALL

#include <Arduino.h>

#include <atomic>

// ISR-owned pulse counts. The GPIO ISR service is IRAM-only, so everything
// the handler touches lives in IRAM (code) or DRAM (.bss).
static std::atomic<int32_t> pulses[4];
static volatile uint32_t lastUs[4] = {0, 0, 0, 0};
static const uint32_t GLITCH_US = 150;    // ball encoder pulses are ms-scale

template <int I>
static void IRAM_ATTR ballIsr() {
    const uint32_t now = micros();
    if (now - lastUs[I] < GLITCH_US) return;   // contact bounce
    lastUs[I] = now;
    pulses[I].fetch_add(1, std::memory_order_relaxed);
}

static int32_t take(std::atomic<int32_t>& a) {
    return a.exchange(0, std::memory_order_relaxed);
}

bool trackballBegin() {
    const int pins[4] = {TRACKBALL_UP_PIN, TRACKBALL_DOWN_PIN, TRACKBALL_LEFT_PIN,
                         TRACKBALL_RIGHT_PIN};
    pinMode(pins[0], INPUT_PULLUP);
    pinMode(pins[1], INPUT_PULLUP);
    pinMode(pins[2], INPUT_PULLUP);
    pinMode(pins[3], INPUT_PULLUP);
    attachInterrupt(pins[0], ballIsr<0>, FALLING);
    attachInterrupt(pins[1], ballIsr<1>, FALLING);
    attachInterrupt(pins[2], ballIsr<2>, FALLING);
    attachInterrupt(pins[3], ballIsr<3>, FALLING);
    return true;
}

int32_t trackballUp() { return take(pulses[0]); }
int32_t trackballDown() { return take(pulses[1]); }
int32_t trackballLeft() { return take(pulses[2]); }
int32_t trackballRight() { return take(pulses[3]); }

#endif  // CHIRP_HAS_TRACKBALL
