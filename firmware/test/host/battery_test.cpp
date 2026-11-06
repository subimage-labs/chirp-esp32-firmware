// Host tests for battery.h: register decode, the voltage → percent curve,
// the gauge's smoothing and hysteresis, the low-battery threshold and the
// glyph geometry. Run: firmware/test/host/run.sh
#include "battery.h"

#include <cstdio>
#include <cstdlib>

static int failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,   \
                         __LINE__, #cond);                                \
            failures++;                                                   \
        }                                                                 \
    } while (0)

static void registerDecode() {
    CHECK(batteryMvFromReg0E(0) == 2304);
    CHECK(batteryMvFromReg0E(1) == 2324);
    CHECK(batteryMvFromReg0E(0x5F) == 4204);                 // 95 steps
    CHECK(batteryMvFromReg0E(0x80 | 50) == batteryMvFromReg0E(50));   // bit 7 is THERM_STAT
    CHECK(batteryMvFromReg0E(0x7F) == 4844);
    CHECK(!batteryMvPlausible(batteryMvFromReg0E(0)));       // no conversion yet
    CHECK(batteryMvPlausible(3700) && batteryMvPlausible(4200));
    CHECK(!batteryMvPlausible(4700));

    CHECK(batteryChargeFromReg0B(0x00) == Charge::Idle);
    CHECK(batteryChargeFromReg0B(0x08) == Charge::Charging);   // pre-charge
    CHECK(batteryChargeFromReg0B(0x10) == Charge::Charging);   // fast charge
    CHECK(batteryChargeFromReg0B(0x18) == Charge::Done);
    CHECK(batteryChargeFromReg0B(0xE7) == Charge::Idle);       // other bits are not CHRG_STAT
    CHECK(batteryChargeFromReg0B(0xFF) == Charge::Done);
}

static void curve() {
    CHECK(batteryPercentFromMv(3300) == 0);
    CHECK(batteryPercentFromMv(2500) == 0);
    CHECK(batteryPercentFromMv(4200) == 100);
    CHECK(batteryPercentFromMv(4500) == 100);
    for (int i = 0; i < BATTERY_CURVE_N; i++)                 // every table point is exact
        CHECK(batteryPercentFromMv(BATTERY_CURVE[i].mv) == BATTERY_CURVE[i].pct);
    // The table itself: ascending in both columns, so the curve is a function.
    for (int i = 1; i < BATTERY_CURVE_N; i++) {
        CHECK(BATTERY_CURVE[i].mv > BATTERY_CURVE[i - 1].mv);
    }
    // Monotonic, 0..100, and no jump larger than the steepest segment allows.
    int prev = 0;
    for (int mv = 2800; mv <= 4400; mv++) {
        const int p = batteryPercentFromMv(mv);
        CHECK(p >= 0 && p <= 100);
        CHECK(p >= prev);
        CHECK(p - prev <= 1);
        prev = p;
    }
    CHECK(batteryPercentFromMv(3825) == 46);                   // between 3820 (45) and 3840 (50): 46.25
    CHECK(batteryPercentFromMv(3650) == 8);                    // between 3600 (5) and 3690 (10)
}

static void chargeRise() {
    CHECK(batteryChargeRiseMv(3700) == 60);
    CHECK(batteryChargeRiseMv(4100) == 60);
    CHECK(batteryChargeRiseMv(4150) == 30);
    CHECK(batteryChargeRiseMv(4200) == 0);
    CHECK(batteryChargeRiseMv(4300) == 0);
    int prev = 1000;
    for (int mv = 3500; mv <= 4300; mv++) {                    // never grows with voltage
        const int r = batteryChargeRiseMv(mv);
        CHECK(r >= 0 && r <= prev);
        prev = r;
    }
}

static void lowThreshold() {
    CHECK(batteryLowNext(false, 14, false));
    CHECK(!batteryLowNext(false, 15, false));                  // enters below 15
    CHECK(!batteryLowNext(false, 17, false));
    CHECK(batteryLowNext(true, 15, false));                    // stays through the gap
    CHECK(batteryLowNext(true, 17, false));
    CHECK(!batteryLowNext(true, 18, false));                   // leaves at 18
    CHECK(!batteryLowNext(true, 3, true));                     // USB clears it
    CHECK(!batteryLowNext(false, 3, true));
    CHECK(BATTERY_LOW_ENTER_PCT < BATTERY_LOW_EXIT_PCT);
}

static BatteryGauge feed(int mv, Charge c, bool usb, int n) {
    BatteryGauge g;
    for (int i = 0; i < n; i++) g.update(mv, c, usb);
    return g;
}

static void firstSampleSnaps() {
    BatteryGauge g;
    CHECK(!g.known());
    CHECK(!g.view().known);
    g.update(3850, Charge::Idle, false);
    CHECK(g.known() && g.percent() == 55 && g.millivolts() == 3850);
    CHECK(g.view().pct == 55 && !g.view().usb && !g.view().charging && !g.view().low);
    CHECK(feed(3600, Charge::Idle, false, 1).percent() == 5);
    CHECK(feed(3200, Charge::Idle, false, 1).percent() == 0);
}

// Reading noise (load, ADC step of 20 mV) must not move the shown number.
static void noiseDoesNotJitter() {
    BatteryGauge g;
    unsigned seed = 12345;
    int changes = 0, lastChange = -100, lastDir = 0, flicker = 0;
    int last = -1;
    for (int i = 0; i < 400; i++) {
        seed = seed * 1103515245u + 12345u;
        const int noise = (int)((seed >> 16) % 41) - 20;       // ±20 mV: a full ADC step either way
        g.update(3850 + noise, Charge::Idle, false);
        if (i >= 20) {                                         // after the first-sample settle
            CHECK(g.percent() >= 52 && g.percent() <= 57);     // true level 55
            if (g.percent() != last) {
                changes++;
                const int dir = g.percent() > last ? 1 : -1;
                if (dir != lastDir && i - lastChange < 10) flicker++;   // up-down-up within 10 samples
                lastDir = dir;
                lastChange = i;
            }
        }
        last = g.percent();
    }
    CHECK(changes <= 25);                                      // a step every 15 samples at worst
    CHECK(flicker <= 2);                                       // reversals are rare, never a flicker
}

// A steady discharge shows a steady fall: never up on battery, and each
// update moves at most one point.
static void dischargeIsMonotonic() {
    BatteryGauge g;
    unsigned seed = 777;
    int prev = 101;
    for (int i = 0; i < 1500; i++) {
        seed = seed * 1103515245u + 12345u;
        const int noise = (int)((seed >> 16) % 31) - 15;
        const int mv = 4150 - i * 700 / 1500;                  // 4150 → 3450 mV
        g.update(mv + noise, Charge::Idle, false);
        CHECK(g.percent() <= prev);
        CHECK(prev - g.percent() <= 1 || prev == 101);
        prev = g.percent();
    }
    CHECK(g.percent() < 10);
}

// A load sag (the speaker, a WiFi burst) that recovers shows at most a small
// dip, then the number comes back.
static void sagRecovers() {
    BatteryGauge g = feed(3850, Charge::Idle, false, 1);
    CHECK(g.percent() == 55);
    int lowest = 100;
    for (int i = 0; i < 5; i++) {                              // 150 mV of sag for 5 samples
        g.update(3700, Charge::Idle, false);
        if (g.percent() < lowest) lowest = g.percent();
    }
    CHECK(lowest >= 55 - 5);                                   // one point per sample
    for (int i = 0; i < 40; i++) g.update(3850, Charge::Idle, false);
    CHECK(g.percent() >= 53 && g.percent() <= 55);             // back (a rise needs 3: within 2)
}

// Rising on battery needs real evidence (3 points), not the 2 of a jitter.
static void riseOnBatteryNeedsMore() {
    BatteryGauge g = feed(3850, Charge::Idle, false, 1);       // 55
    for (int i = 0; i < 20; i++) g.update(3871, Charge::Idle, false);   // raw 60 (+5): rises
    CHECK(g.percent() > 55);
    BatteryGauge h = feed(3850, Charge::Idle, false, 1);
    for (int i = 0; i < 30; i++) h.update(3857, Charge::Idle, false);   // raw 57 (+2): holds
    CHECK(h.percent() == 55);
}

static void charging() {
    // The terminal voltage reads high while charging: 3900 mV charging is
    // about what 3840 mV is at rest.
    BatteryGauge g = feed(3900, Charge::Charging, true, 1);
    CHECK(g.percent() == batteryPercentFromMv(3840));
    CHECK(g.view().usb && g.view().charging && !g.view().low);
    // Charging does not show a small dip: falling needs 3.
    for (int i = 0; i < 20; i++) g.update(3880, Charge::Charging, true);   // raw 3820 → 45 (-5)
    CHECK(g.percent() < 50 && g.percent() >= 45);
    BatteryGauge h = feed(3900, Charge::Charging, true, 1);    // 50
    for (int i = 0; i < 30; i++) h.update(3890, Charge::Charging, true);   // -10 mV: -2 points
    CHECK(h.percent() == 50);
    // The top of the charge: 99 until the charger says Done.
    BatteryGauge top = feed(4200, Charge::Charging, true, 1);
    CHECK(top.percent() == 99);
    for (int i = 0; i < 10; i++) top.update(4210, Charge::Charging, true);
    CHECK(top.percent() == 99);
    top.update(4195, Charge::Done, true);
    CHECK(top.percent() == 100 && !top.view().charging && top.view().usb);
}

static void sourceChangesSnap() {
    BatteryGauge g = feed(4190, Charge::Done, true, 5);
    CHECK(g.percent() == 100);
    g.update(3900, Charge::Idle, false);                        // unplugged: the real level
    CHECK(g.percent() == batteryPercentFromMv(3900));
    g.update(3900, Charge::Charging, true);                     // plugged in: charging compensation
    CHECK(g.percent() == batteryPercentFromMv(3840));
    CHECK(g.view().charging);
}

static void lowFollowsPercentAndUsb() {
    BatteryGauge g = feed(3600, Charge::Idle, false, 1);        // 5 %
    CHECK(g.low() && g.view().low);
    g.update(3600, Charge::Charging, true);                     // plug in: not low
    CHECK(!g.low());
    g.update(3600, Charge::Idle, false);                        // unplug: low again
    CHECK(g.low());
    // Between 15 and 18 it holds its state: no flicker at the edge.
    BatteryGauge h = feed(3700, Charge::Idle, false, 1);        // 12 %: low
    CHECK(h.low());
    for (int i = 0; i < 200; i++) h.update(3720, Charge::Idle, false);   // 17 %
    CHECK(h.percent() >= 15 && h.percent() <= 17);
    CHECK(h.low());
    for (int i = 0; i < 200; i++) h.update(3790, Charge::Idle, false);   // 35 %
    CHECK(!h.low());
    BatteryGauge k = feed(3720, Charge::Idle, false, 1);        // 17 % from the start: not low
    CHECK(!k.low());
    CHECK(feed(3850, Charge::Idle, false, 1).low() == false);
}

static void viewEquality() {
    BatteryView a, b;
    CHECK(a == b);
    b.known = true;
    CHECK(a != b);
    a = b;
    b.pct = 5;
    CHECK(a != b);
}

static void layout() {
    for (int box = 10; box <= 40; box++) {
        const BatteryLayout l = batteryLayout(box);
        CHECK(l.glyphH >= 8 && l.glyphH <= box);
        CHECK(l.glyphW > l.glyphH && l.nubH > 0 && l.nubH < l.glyphH);
        CHECK(l.boltW >= 4);
        CHECK(l.glyphH - 4 >= 4);                              // room for the bar
    }
}

int main() {
    registerDecode();
    curve();
    chargeRise();
    lowThreshold();
    firstSampleSnaps();
    noiseDoesNotJitter();
    dischargeIsMonotonic();
    sagRecovers();
    riseOnBatteryNeedsMore();
    charging();
    sourceChangesSnap();
    lowFollowsPercentAndUsb();
    viewEquality();
    layout();
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("battery: all checks passed\n");
    return 0;
}
