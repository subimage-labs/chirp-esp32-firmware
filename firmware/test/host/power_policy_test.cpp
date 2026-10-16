// Host tests for PowerPolicy. Run: firmware/test/host/run.sh
#include "power_policy.h"

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

static constexpr uint32_t SLOW = 20000, DIM = 60000, OFF = 180000, LIGHT = 600000, DEEP = 7200000;
using PS = PowerSource;

static PowerPolicy make() { return PowerPolicy(SLOW, DIM, OFF, LIGHT, DEEP); }

static void batteryTimelineBoundaries() {
    PowerPolicy p = make();
    p.setSource(PS::Battery, 1000);
    CHECK(p.backlight(1000 + DIM - 1) == Backlight::Full);
    CHECK(p.backlight(1000 + DIM) == Backlight::Dim);
    CHECK(p.backlight(1000 + OFF - 1) == Backlight::Dim);
    CHECK(p.backlight(1000 + OFF) == Backlight::Off);
    CHECK(p.sleepDue(1000 + LIGHT - 1) == SleepDue::None);
    CHECK(p.sleepDue(1000 + LIGHT) == SleepDue::Light);
    CHECK(p.sleepDue(1000 + DEEP - 1) == SleepDue::Light);
    CHECK(p.sleepDue(1000 + DEEP) == SleepDue::Deep);
}

// Slow animation: battery only, after slowMs idle (before the screen dims),
// and any activity restores full rate.
static void animationSlowsOnBatteryWhenIdle() {
    PowerPolicy p = make();
    p.setSource(PS::Battery, 1000);
    CHECK(!p.animSlow(1000));
    CHECK(!p.animSlow(1000 + SLOW - 1));
    CHECK(p.animSlow(1000 + SLOW));
    CHECK(SLOW < DIM && p.backlight(1000 + SLOW) == Backlight::Full);   // slows before it dims
    CHECK(p.animSlow(1000 + DIM) && p.animSlow(1000 + OFF));            // and stays slow
    p.activity(1000 + SLOW + 5);
    CHECK(!p.animSlow(1000 + SLOW + 5));                                // any input: full rate
    CHECK(!p.animSlow(1000 + SLOW + 5 + SLOW - 1));
    CHECK(p.animSlow(1000 + SLOW + 5 + SLOW));
}

static void animationNeverSlowsOnUsbOrUnknown() {
    PowerPolicy p = make();
    p.setSource(PS::Usb, 0);
    CHECK(!p.animSlow(DEEP * 3));
    p.setSource(PS::Unknown, DEEP * 4);
    CHECK(!p.animSlow(DEEP * 8));
}

static void animationRestartsOnUnplugAndWrap() {
    PowerPolicy p = make();
    p.setSource(PS::Usb, 0);
    p.setSource(PS::Battery, 500000);                  // unplugged after a long idle: clock restarts
    CHECK(!p.animSlow(500000));
    CHECK(p.animSlow(500000 + SLOW));
    PowerPolicy w = make();
    const uint32_t last = 0xFFFFFFFFu - 1000;
    w.setSource(PS::Battery, last);
    CHECK(!w.animSlow(last + 500));
    CHECK(w.animSlow(last + SLOW));                    // across the millis() wrap
}

// The light-sleep timer must fire exactly when deep sleep becomes due.
static void lightSleepTimerTargetsDeepThreshold() {
    PowerPolicy p = make();
    p.setSource(PS::Battery, 0);
    CHECK(p.msUntilDeep(LIGHT) == DEEP - LIGHT);
    CHECK(p.sleepDue(LIGHT + p.msUntilDeep(LIGHT)) == SleepDue::Deep);
    CHECK(p.msUntilDeep(DEEP + 5) == 0);
}

// A key wake from light sleep is activity: back to Full, no re-sleep until
// a full light period passes.
static void activityRestartsTimeline() {
    PowerPolicy p = make();
    p.setSource(PS::Battery, 0);
    CHECK(p.sleepDue(LIGHT) == SleepDue::Light);
    p.activity(LIGHT + 5);
    CHECK(p.backlight(LIGHT + 5) == Backlight::Full);
    CHECK(p.sleepDue(LIGHT + 5 + LIGHT - 1) == SleepDue::None);
}

static void usbPinsScreenOnAndNeverSleeps() {
    PowerPolicy p = make();
    p.setSource(PS::Usb, 0);
    CHECK(p.backlight(DEEP * 10) == Backlight::Full);
    CHECK(p.sleepDue(DEEP * 10) == SleepDue::None);
}

// Unplugging after a long idle on USB must not blank or sleep at once.
static void unplugRestartsIdleClock() {
    PowerPolicy p = make();
    p.setSource(PS::Usb, 0);
    const uint32_t unplug = DEEP * 3;
    p.setSource(PS::Battery, unplug);
    CHECK(p.backlight(unplug) == Backlight::Full);
    CHECK(p.sleepDue(unplug + LIGHT - 1) == SleepDue::None);
    CHECK(p.backlight(unplug + DIM) == Backlight::Dim);
}

// Repeated identical readings must not keep resetting the clock.
static void steadyReadingIsNotActivity() {
    PowerPolicy p = make();
    p.setSource(PS::Battery, 0);
    p.setSource(PS::Battery, OFF);
    CHECK(p.backlight(OFF) == Backlight::Off);
}

// Charger unreadable from boot: screen timeline runs, never sleeps.
static void bootReadFailureNeverSleeps() {
    PowerPolicy p = make();  // starts Unknown
    p.setSource(PS::Unknown, 0);
    CHECK(p.backlight(OFF) == Backlight::Off);
    CHECK(p.sleepDue(DEEP * 10) == SleepDue::None);
}

// Valid USB → read fault → recovery: never sleeps along the way.
static void usbReadFaultThenRecovery() {
    PowerPolicy p = make();
    p.setSource(PS::Usb, 0);
    const uint32_t fault = DEEP * 2;
    p.setSource(PS::Unknown, fault);
    CHECK(p.backlight(fault) == Backlight::Full);  // clock restarted
    CHECK(p.sleepDue(fault + DEEP * 5) == SleepDue::None);
    const uint32_t back = fault + DEEP * 5;
    p.setSource(PS::Usb, back);
    CHECK(p.backlight(back + DEEP) == Backlight::Full);
    CHECK(p.sleepDue(back + DEEP) == SleepDue::None);
}

// Unknown → Battery: sleep only a full idle period after the valid reading.
static void recoveredBatteryReadingStartsFreshClock() {
    PowerPolicy p = make();
    p.setSource(PS::Unknown, 0);
    const uint32_t ok = DEEP * 3;
    p.setSource(PS::Battery, ok);
    CHECK(p.sleepDue(ok + LIGHT - 1) == SleepDue::None);
    CHECK(p.sleepDue(ok + LIGHT) == SleepDue::Light);
}

static void millisWrap() {
    PowerPolicy p = make();
    const uint32_t last = 0xFFFFFFFFu - 1000;
    p.setSource(PS::Battery, last);
    CHECK(p.backlight(last + 500) == Backlight::Full);        // no wrap yet
    CHECK(p.backlight(last + DIM) == Backlight::Dim);         // wrapped
    CHECK(p.sleepDue(last + LIGHT) == SleepDue::Light);
    CHECK(p.msUntilDeep(last + LIGHT) == DEEP - LIGHT);
}

// Light-sleep timer wake: main.cpp re-reads the charger, feeds setSource,
// and deep-sleeps only if sleepDue still says Deep.
static void timerWakeStillOnBatteryGoesDeep() {
    PowerPolicy p = make();
    p.setSource(PS::Battery, 0);
    p.setSource(PS::Battery, DEEP);   // fresh reading, unchanged
    CHECK(p.sleepDue(DEEP) == SleepDue::Deep);
}

static void usbAttachedDuringLightSleepCancelsDeep() {
    PowerPolicy p = make();
    p.setSource(PS::Battery, 0);
    p.setSource(PS::Usb, DEEP);
    CHECK(p.sleepDue(DEEP) == SleepDue::None);
    CHECK(p.backlight(DEEP) == Backlight::Full);
}

static void readFaultDuringLightSleepCancelsDeep() {
    PowerPolicy p = make();
    p.setSource(PS::Battery, 0);
    p.setSource(PS::Unknown, DEEP);
    CHECK(p.sleepDue(DEEP) == SleepDue::None);
    CHECK(p.sleepDue(DEEP * 3) == SleepDue::None);
}

int main() {
    batteryTimelineBoundaries();
    animationSlowsOnBatteryWhenIdle();
    animationNeverSlowsOnUsbOrUnknown();
    animationRestartsOnUnplugAndWrap();
    lightSleepTimerTargetsDeepThreshold();
    activityRestartsTimeline();
    usbPinsScreenOnAndNeverSleeps();
    unplugRestartsIdleClock();
    steadyReadingIsNotActivity();
    bootReadFailureNeverSleeps();
    usbReadFaultThenRecovery();
    recoveredBatteryReadingStartsFreshClock();
    millisWrap();
    timerWakeStillOnBatteryGoesDeep();
    usbAttachedDuringLightSleepCancelsDeep();
    readFaultDuringLightSleepCancelsDeep();
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("power_policy: all checks passed\n");
    return 0;
}
