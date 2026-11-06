// Host tests for GestureDecoder. Run: firmware/test/host/run.sh
#include "gesture.h"

#include <cstdio>
#include <vector>

static int failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,   \
                         __LINE__, #cond);                                \
            failures++;                                                   \
        }                                                                 \
    } while (0)

static constexpr uint32_t REL = 30, DBL = 300, HOLD = 500;

// Drives both keys over [from, to) in 1 ms steps; collects events.
struct Rig {
    GestureDecoder g{REL, DBL, HOLD};
    uint32_t t = 1000;
    std::vector<InputEvent> ev;
    void run(bool enc, bool side, uint32_t ms) {
        for (uint32_t i = 0; i < ms; i++, t++) {
            GestureOut o;
            g.update(enc, side, t, o);
            for (int k = 0; k < o.n; k++) ev.push_back(o.ev[k]);
        }
    }
    int count(InputType ty) const {
        int n = 0;
        for (auto& e : ev) n += e.type == ty;
        return n;
    }
};

static void pressIsImmediateReleaseDebounced() {
    Rig r;
    r.run(true, false, 1);
    CHECK(r.ev.size() == 1 && r.ev[0].type == InputType::EncPress);
    r.run(true, false, 200);
    r.run(false, false, REL - 1);
    CHECK(r.count(InputType::EncRelease) == 0);
    r.run(false, false, 2);
    CHECK(r.count(InputType::EncRelease) == 1);
}

// A brief contact blip while held must not release (or re-press).
static void blipWhileHeldIgnored() {
    Rig r;
    r.run(true, false, 100);
    r.run(false, false, 20);
    r.run(true, false, 100);
    CHECK(r.count(InputType::EncPress) == 1);
    CHECK(r.count(InputType::EncRelease) == 0);
}

static void doubleTapWithinWindow() {
    Rig r;
    r.run(true, false, 60);
    r.run(false, false, 100);
    r.run(true, false, 60);   // second press ~160 ms after the first
    CHECK(r.count(InputType::EncPress) == 1);
    CHECK(r.count(InputType::EncDouble) == 1);
}

// The loop samples keys only every ~64 ms while recording. A fast
// double-tap seen as down/up/down/up at that rate must still be a double,
// not one merged press.
static void sparseSamplingDoubleTap() {
    GestureDecoder g{REL, DBL, HOLD};
    std::vector<InputEvent> ev;
    const bool levels[] = {true, false, true, false, false};
    uint32_t t = 1000;
    for (bool lv : levels) {
        GestureOut o;
        g.update(lv, false, t, o);
        for (int k = 0; k < o.n; k++) ev.push_back(o.ev[k]);
        t += 64;
    }
    CHECK(ev.size() == 4 && ev[0].type == InputType::EncPress &&
          ev[1].type == InputType::EncRelease && ev[2].type == InputType::EncDouble &&
          ev[3].type == InputType::EncRelease);
}

// A third press right after a double starts fresh (single), never another double.
static void tripleIsDoubleThenSingle() {
    Rig r;
    for (int i = 0; i < 3; i++) {
        r.run(true, false, 40);
        r.run(false, false, 90);
    }
    CHECK(r.count(InputType::EncDouble) == 1);
    CHECK(r.count(InputType::EncPress) == 2);
}

static void slowSecondPressIsSingle() {
    Rig r;
    r.run(true, false, 60);
    r.run(false, false, DBL + 50);
    r.run(true, false, 60);
    CHECK(r.count(InputType::EncPress) == 2);
    CHECK(r.count(InputType::EncDouble) == 0);
}

static void cancelDoublePreventsPairing() {
    Rig r;
    r.run(true, false, 40);
    r.run(false, false, 90);
    r.g.cancelDouble();
    r.run(true, false, 40);
    CHECK(r.count(InputType::EncDouble) == 0);
    CHECK(r.count(InputType::EncPress) == 2);
}

// The release carries how long the key was down (press to first raw-up).
static void releaseCarriesHeldTime() {
    Rig r;
    r.run(true, false, 100);
    r.run(false, false, REL + 2);
    CHECK(r.count(InputType::EncRelease) == 1);
    for (auto& e : r.ev)
        if (e.type == InputType::EncRelease) CHECK(e.heldMs >= 99 && e.heldMs <= 101);
}

static bool lastSideChord(const Rig& r) {
    for (auto it = r.ev.rbegin(); it != r.ev.rend(); ++it)
        if (it->type == InputType::SideRelease) return it->chord;
    return false;
}

// The view chord: a side press that begins while the encoder key is down,
// whatever happens to the encoder key before the side release.
static void sideTapWithEncoderHeldIsChord() {
    Rig r;
    r.run(true, false, 80);                        // encoder down first
    r.run(true, true, 120);                        // side tap while it is held
    r.run(true, false, REL + 5);
    CHECK(r.count(InputType::SideRelease) == 1 && lastSideChord(r));
    r.run(false, false, REL + 5);
    Rig both;                                      // encoder released before the side key
    both.run(true, false, 80);
    both.run(true, true, 100);
    both.run(false, true, 60);
    both.run(false, false, REL + 5);
    CHECK(both.count(InputType::SideRelease) == 1 && lastSideChord(both));
    Rig plain;                                     // side alone, and side first then encoder
    plain.run(false, true, 120);
    plain.run(false, false, REL + 5);
    CHECK(plain.count(InputType::SideRelease) == 1 && !lastSideChord(plain));
    plain.run(false, true, 60);
    plain.run(true, true, 60);
    plain.run(false, false, REL + 5);
    CHECK(plain.count(InputType::SideRelease) == 2 && !lastSideChord(plain));
}

// Side tap: release without hold; held time is press → first raw-up, so the
// release debounce does not inflate it.
static void sideTapReportsHeldTime() {
    Rig r;
    r.run(false, true, 450);
    r.run(false, false, REL + 5);
    CHECK(r.count(InputType::SideHold) == 0);
    CHECK(r.count(InputType::SideRelease) == 1);
    CHECK(r.ev.back().heldMs == 450 && !r.ev.back().holdFired);
}

static void sideHoldFiresOnceThenRelease() {
    Rig r;
    r.run(false, true, HOLD + 400);
    CHECK(r.count(InputType::SideHold) == 1);
    r.run(false, false, REL + 5);
    CHECK(r.ev.back().type == InputType::SideRelease && r.ev.back().holdFired);
}

// The waking press is swallowed whole (press, hold, release), then input
// re-arms for the next press.
static void swallowSuppressesWholePress() {
    Rig r;
    r.g.swallow();
    r.run(true, true, HOLD + 100);
    r.run(false, false, REL + 5);
    CHECK(r.ev.empty());
    r.run(true, false, 10);
    CHECK(r.count(InputType::EncPress) == 1);
}

// A swallowed press must not pair with the next one into a double.
static void swallowedPressDoesNotPrimeDouble() {
    Rig r;
    r.g.swallow();
    r.run(true, false, 40);
    r.run(false, false, 90);
    r.run(true, false, 40);
    CHECK(r.count(InputType::EncDouble) == 0);
    CHECK(r.count(InputType::EncPress) == 1);
}

int main() {
    pressIsImmediateReleaseDebounced();
    blipWhileHeldIgnored();
    doubleTapWithinWindow();
    sparseSamplingDoubleTap();
    tripleIsDoubleThenSingle();
    slowSecondPressIsSingle();
    cancelDoublePreventsPairing();
    releaseCarriesHeldTime();
    sideTapWithEncoderHeldIsChord();
    sideTapReportsHeldTime();
    sideHoldFiresOnceThenRelease();
    swallowSuppressesWholePress();
    swallowedPressDoesNotPrimeDouble();
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("gesture: all checks passed\n");
    return 0;
}
