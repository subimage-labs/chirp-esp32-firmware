// battery.h — battery level from the BQ25896's VBAT reading: register decode,
// a 1S Li-ion/LiPo voltage → percent curve, smoothing so the number does not
// jitter, the low-battery threshold, and the geometry of the status-strip
// glyph. Pure (no Arduino), host-tested; power.cpp feeds it readings and
// ui.cpp draws the BatteryView it yields.
//
// The percentage is an estimate from voltage alone: the curve is a typical
// single-cell discharge curve at light load, the charge-time correction is a
// fixed rule of thumb, and neither is calibrated against this cell. Treat the
// number as a gauge, not a fuel meter.
#pragma once

#include <stdint.h>

// REG0B bits 4:3 (CHRG_STAT): 0 not charging, 1 pre-charge, 2 fast charge,
// 3 charge done.
enum class Charge : uint8_t { Idle, Charging, Done };

inline Charge batteryChargeFromReg0B(uint8_t reg) {
    const uint8_t stat = (uint8_t)((reg >> 3) & 3);
    return stat == 3 ? Charge::Done : stat == 0 ? Charge::Idle : Charge::Charging;
}

// REG0E bits 6:0 (BATV): 2.304 V + 20 mV per step. Code 0 is what the ADC
// reads before its first conversion: no reading yet.
inline int batteryMvFromReg0E(uint8_t reg) { return 2304 + 20 * (reg & 0x7F); }
inline bool batteryMvPlausible(int mv) { return mv >= 2500 && mv <= 4600; }

// Typical 1S Li-ion/LiPo discharge curve, (mV, percent), ascending. The knee
// below 3.7 V is real: the last 15 % spans a few tens of millivolts.
struct BatteryCurvePoint { int mv; int pct; };
static const BatteryCurvePoint BATTERY_CURVE[] = {
    {3300, 0},  {3600, 5},  {3690, 10}, {3710, 15}, {3730, 20}, {3750, 25}, {3770, 30},
    {3790, 35}, {3800, 40}, {3820, 45}, {3840, 50}, {3850, 55}, {3870, 60}, {3910, 65},
    {3950, 70}, {3980, 75}, {4020, 80}, {4080, 85}, {4110, 90}, {4150, 95}, {4200, 100},
};
static const int BATTERY_CURVE_N = (int)(sizeof(BATTERY_CURVE) / sizeof(BATTERY_CURVE[0]));

// Linear between the points, clamped to 0..100.
inline int batteryPercentFromMv(int mv) {
    if (mv <= BATTERY_CURVE[0].mv) return 0;
    if (mv >= BATTERY_CURVE[BATTERY_CURVE_N - 1].mv) return 100;
    int i = 1;
    while (mv > BATTERY_CURVE[i].mv) i++;
    const BatteryCurvePoint& lo = BATTERY_CURVE[i - 1];
    const BatteryCurvePoint& hi = BATTERY_CURVE[i];
    return lo.pct + ((mv - lo.mv) * (hi.pct - lo.pct) + (hi.mv - lo.mv) / 2) / (hi.mv - lo.mv);
}

// While charging, the terminal voltage reads above the cell's resting voltage
// (charge current through its internal resistance): up to 60 mV in the
// constant-current phase, fading to none at 4.2 V where the taper begins.
inline int batteryChargeRiseMv(int mv) {
    if (mv <= 4100) return 60;
    if (mv >= 4200) return 0;
    return (4200 - mv) * 60 / 100;
}

// Low-battery warning: set below LOW_ENTER_PCT, cleared at LOW_EXIT_PCT (the
// gap keeps it from flickering at the edge) or whenever USB is present.
static const int BATTERY_LOW_ENTER_PCT = 15;
static const int BATTERY_LOW_EXIT_PCT = 18;

inline bool batteryLowNext(bool wasLow, int pct, bool usb) {
    if (usb) return false;
    if (pct < BATTERY_LOW_ENTER_PCT) return true;
    return wasLow && pct < BATTERY_LOW_EXIT_PCT;
}

// What the screen shows. known = false until a plausible reading exists.
struct BatteryView {
    bool known = false;
    int pct = 0;
    bool usb = false;       // power present (charging or not)
    bool charging = false;
    bool low = false;
    bool operator==(const BatteryView& o) const {
        return known == o.known && pct == o.pct && usb == o.usb && charging == o.charging &&
               low == o.low;
    }
    bool operator!=(const BatteryView& o) const { return !(*this == o); }
};

// Smooths the readings into a stable percentage. Feed it a sample every few
// seconds.
//   - The voltage goes through an exponential average, 1/8 per sample; the
//     first samples after a snap weigh 1/2, 1/3, ... (a plain mean) so one
//     noisy first reading does not linger.
//   - The shown percentage steps one point at a time toward the estimate and
//     only once it differs by 2 or more; a move against the expected
//     direction (up on battery, down while charging) needs 3: load sag and
//     its recovery must not show as a charge changing.
//   - The first sample and any change of USB/charge state snap straight to
//     the estimate (and reseed the average): plugging in or unplugging is a
//     real step.
//   - While charging the display tops out at 99; 100 is Charge::Done.
class BatteryGauge {
public:
    void update(int mv, Charge charge, bool usb) {
        const bool snap = !known_ || usb != usb_ || charge != charge_;
        if (snap) {
            ema16_ = mv * 16;
            samples_ = 1;
        } else {
            const int weight = samples_ < 8 ? samples_ + 1 : 8;
            ema16_ += (mv * 16 - ema16_) / weight;
            if (samples_ < 8) samples_++;
        }
        const int ema = (ema16_ + 8) / 16;
        usb_ = usb;
        charge_ = charge;
        mv_ = mv;
        int raw;
        if (charge == Charge::Done) {
            raw = 100;
        } else {
            int v = ema;
            if (charge == Charge::Charging) v -= batteryChargeRiseMv(v);
            raw = batteryPercentFromMv(v);
            if (charge == Charge::Charging && raw > 99) raw = 99;
        }
        if (snap) {
            pct_ = raw;
        } else {
            const int diff = raw - pct_;
            const bool againstUp = !usb_ && charge_ != Charge::Charging;   // rising unexpected
            const bool againstDown = charge_ == Charge::Charging;          // falling unexpected
            const int need = diff > 0 ? (againstUp ? 3 : 2) : (againstDown ? 3 : 2);
            if (diff >= need) pct_++;
            else if (-diff >= need) pct_--;
        }
        known_ = true;
        low_ = batteryLowNext(low_, pct_, usb_);
    }

    bool known() const { return known_; }
    int percent() const { return pct_; }
    int millivolts() const { return mv_; }       // the last raw sample
    Charge charge() const { return charge_; }
    bool usb() const { return usb_; }
    bool low() const { return low_; }

    BatteryView view() const {
        BatteryView v;
        v.known = known_;
        v.pct = pct_;
        v.usb = usb_;
        v.charging = charge_ == Charge::Charging;
        v.low = low_;
        return v;
    }

private:
    bool known_ = false;
    bool usb_ = false;
    bool low_ = false;
    Charge charge_ = Charge::Idle;
    int ema16_ = 0;                              // average, in 1/16 mV
    int samples_ = 0;                            // since the last snap, capped at 8
    int mv_ = 0;
    int pct_ = 0;
};

// Geometry of the status-strip block, at the strip's right edge:
//   [bolt] [glyph][nub]
// All derived from the status text's line box so every font size reflows. The
// glyph is a battery outline whose inner bar shows the level (no number: the
// Setup tab's Battery row gives the percentage).
struct BatteryLayout {
    int glyphH, glyphW;     // body outline
    int nubW, nubH;         // the terminal at the right
    int boltW;              // the charging bolt slot, left of the glyph
    int gap;                // between bolt and glyph
    int width;              // whole block
};

inline BatteryLayout batteryLayout(int lineBox) {
    BatteryLayout l;
    int h = lineBox * 5 / 9;
    if (h < 8) h = 8;
    if (h > lineBox) h = lineBox;
    l.glyphH = h;
    l.glyphW = h * 2 + 2;
    l.nubW = 2;
    l.nubH = h / 2;
    l.boltW = h / 2 + 2;
    l.gap = 3;
    l.width = l.boltW + l.gap + l.glyphW + l.nubW;
    return l;
}
