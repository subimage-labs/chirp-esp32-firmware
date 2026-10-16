#include "wheel.h"

#include <Arduino.h>
#include <soc/gpio_reg.h>

#include <atomic>

// ISR-owned; the GPIO ISR service is IRAM-only, so everything the handler
// touches lives in IRAM (code) or DRAM (.bss) — safe while flash cache is off.
static QuadDecoder decoder;
static uint32_t maskA = 0, maskB = 0;
static std::atomic<int32_t> settled{0};

static void IRAM_ATTR wheelIsr() {
    const uint32_t in = REG_READ(GPIO_IN_REG);
    const uint8_t s = ((in & maskA) ? 1 : 0) | ((in & maskB) ? 2 : 0);
    if (decoder.step(s)) settled.store(decoder.detent(), std::memory_order_relaxed);
}

bool wheelBegin(int pinA, int pinB) {
    if (pinA < 0 || pinA >= 32 || pinB < 0 || pinB >= 32) return false;
    pinMode(pinA, INPUT_PULLUP);
    pinMode(pinB, INPUT_PULLUP);
    maskA = 1u << pinA;
    maskB = 1u << pinB;
    const uint32_t in = REG_READ(GPIO_IN_REG);
    decoder.oldState = ((in & maskA) ? 1 : 0) | ((in & maskB) ? 2 : 0);
    attachInterrupt(pinA, wheelIsr, CHANGE);
    attachInterrupt(pinB, wheelIsr, CHANGE);
    return true;
}

int32_t wheelDetents() { return settled.load(std::memory_order_relaxed); }
