// power.h — battery power management (policy: power_policy.h). Loop task
// only. Owns the charger reading, the idle clock, the backlight level and
// the sleep paths; the board peripheral rail is switched here.
//
// Battery level (battery.h): read from the charger's ADC every few seconds
// and handed to the status strip (ui.h uiBattery); the same gauge feeds the
// Setup tab's info line. Slow animation (power_policy.h): powerAnimSlow() is
// true on battery once idle ANIM_SLOW_MS, and the character view drops its
// frame rate.
//
// Battery: dim → backlight off → light sleep (state kept, a key wakes, WiFi
// rejoins) → deep sleep (reboot). USB: screen on, never sleeps. Charger
// unreadable: screen timeline only, never sleeps. Every sleep decision uses
// a fresh charger reading. The input that wakes a dark screen or the chip is
// swallowed so waking never acts.
#pragma once

#include <stddef.h>

#include "battery.h"
#include "power_policy.h"

bool powerBoot();      // first thing in setup: release sleep holds, rail on;
                       // true when a key woke the chip from deep sleep
void powerBegin();     // charger I2C
void powerTick();      // every loop, before input dispatch
void powerKeyTyped();  // loop: a keyboard key (typing counts as activity, wakes the screen)
const char* powerSourceName();   // bench status
Backlight powerBacklight();      // the level the backlight is at now (character mode animates by it)
bool powerAnimSlow();            // battery, idle long enough: animations run at a trickle
BatteryView powerBattery();      // the smoothed level for the screen
// "87%  3.92 V  discharging" into buf; false before the first valid reading.
bool powerBatteryText(char* buf, size_t n);
#ifdef BENCH_CMDS
void powerBatteryReport();       // bench: fresh sample, raw registers, derived values
#endif
