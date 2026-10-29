// power_policy.h — idle timeline for the screen and sleep. Pure (no
// Arduino), host-tested; main.cpp feeds it activity and the power source and
// applies the result.
//
// Battery: Slow animation (slowMs idle) → Full → Dim (dimMs idle) → Off
//          (offMs) → light sleep (lightMs: RAM and screen state kept, instant
//          wake) → deep sleep (deepMs: reboot on wake). Slow animation is not
//          a backlight level: animated screens drop to a trickle frame rate
//          well before the screen dims, and any activity restores it.
// Usb:     the same screen timeline (Dim, then Off: an idle static screen
//          would otherwise ghost on the panel; any input restores it),
//          full-rate animation, never sleeps.
// Unknown: charger unreadable (or not yet read): the screen timeline runs
//          (reversible by any input) but sleep is inhibited until a valid
//          battery reading — a read fault must never sleep a device that
//          may be on USB. Animation stays at full rate.
// Any source change restarts the idle clock, so unplugging (or a recovered
// reading) never blanks or sleeps instantly. millis() keeps counting through
// light sleep, so the idle clock spans it.
#pragma once

#include <cstdint>

enum class Backlight : uint8_t { Full, Dim, Off };
enum class PowerSource : uint8_t { Unknown, Usb, Battery };
enum class SleepDue : uint8_t { None, Light, Deep };

class PowerPolicy {
public:
    PowerPolicy(uint32_t slowMs, uint32_t dimMs, uint32_t offMs, uint32_t lightMs,
                uint32_t deepMs)
        : slowMs_(slowMs), dimMs_(dimMs), offMs_(offMs), lightMs_(lightMs), deepMs_(deepMs) {}

    void activity(uint32_t now) { last_ = now; }

    void setSource(PowerSource s, uint32_t now) {
        if (s == source_) return;
        source_ = s;
        last_ = now;
    }

    // Unsigned subtraction: correct across the 49-day millis() wrap.
    Backlight backlight(uint32_t now) const {
        const uint32_t idle = now - last_;
        if (idle >= offMs_) return Backlight::Off;
        if (idle >= dimMs_) return Backlight::Dim;
        return Backlight::Full;
    }

    // True when animated screens should run at the trickle rate.
    bool animSlow(uint32_t now) const {
        return source_ == PowerSource::Battery && now - last_ >= slowMs_;
    }

    SleepDue sleepDue(uint32_t now) const {
        if (source_ != PowerSource::Battery) return SleepDue::None;
        const uint32_t idle = now - last_;
        if (idle >= deepMs_) return SleepDue::Deep;
        if (idle >= lightMs_) return SleepDue::Light;
        return SleepDue::None;
    }

    // Light-sleep timer: how long until deep sleep is due (0 if already).
    uint32_t msUntilDeep(uint32_t now) const {
        const uint32_t idle = now - last_;
        return idle >= deepMs_ ? 0 : deepMs_ - idle;
    }

private:
    uint32_t slowMs_, dimMs_, offMs_, lightMs_, deepMs_;
    uint32_t last_ = 0;
    PowerSource source_ = PowerSource::Unknown;
};
