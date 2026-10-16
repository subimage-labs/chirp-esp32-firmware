// Host tests for KeyRing (TSan). Run: firmware/test/host/run.sh
#include "keyring.h"
#include "gesture.h"

#include <vector>

#include <cstdio>
#include <thread>

static int failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,   \
                         __LINE__, #cond);                                \
            failures++;                                                   \
        }                                                                 \
    } while (0)

// Only changes are queued, oldest first, with their sample times.
static void changesInOrder() {
    KeyRing<8> r;
    r.sample(10, false, false);                    // first sample always counts
    r.sample(15, false, false);                    // unchanged: skipped
    r.sample(20, true, false);
    r.sample(25, true, false);
    r.sample(30, false, false);
    r.sample(35, true, false);                     // the second press of a double-tap
    KeySample s;
    const uint32_t ts[] = {10, 20, 30, 35};
    const bool enc[] = {false, true, false, true};
    for (int i = 0; i < 4; i++) {
        CHECK(r.pop(s) && s.t == ts[i] && s.enc == enc[i] && !s.side);
    }
    CHECK(!r.pop(s));
}

// A full ring drops the newest change and counts it; a later tick that finds
// the level back at the last queued one has nothing to add.
static void overflowDropsNewest() {
    KeyRing<4> r;
    for (uint32_t i = 0; i < 6; i++) r.sample(i, i % 2 == 0, false);
    CHECK(r.dropped() == 1);                       // t=4 dropped; t=5 equals the last queued
    KeySample s;
    for (uint32_t i = 0; i < 4; i++) CHECK(r.pop(s) && s.t == i);
    CHECK(!r.pop(s));
    r.sample(9, false, true);                      // room again
    CHECK(r.pop(s) && s.t == 9 && s.side);
}

static int countOf(const std::vector<InputEvent>& ev, InputType t) {
    int n = 0;
    for (const InputEvent& e : ev) n += e.type == t;
    return n;
}

// A double-tap made while the loop is stalled (the first press opened the
// STT stream): the 5 ms samples replayed afterwards still decode as a double.
// Sampling only at the end of the stall saw one long press.
static void doubleTapDuringStall() {
    KeyRing<32> ring;
    GestureDecoder dec{30, 400, 500};
    KeySample last = {1000, false, false};
    std::vector<InputEvent> ev;
    auto emit = [&ev](const GestureOut& o) { for (int i = 0; i < o.n; i++) ev.push_back(o.ev[i]); };
    // press 0..90, up 90..180, press 180..260, up from 260 (ms after 1000)
    for (uint32_t t = 1000; t < 1600; t += 5) {
        const uint32_t d = t - 1000;
        ring.sample(t, (d < 90) || (d >= 180 && d < 260), false);
        if (t == 1005) keyReplay<GestureOut>(ring, dec, t, last, emit);   // the press is seen at once
    }
    keyReplay<GestureOut>(ring, dec, 1600, last, emit);    // the loop wakes up 595 ms later
    CHECK(countOf(ev, InputType::EncPress) == 1);
    CHECK(countOf(ev, InputType::EncDouble) == 1);
    CHECK(countOf(ev, InputType::EncRelease) == 2);

    GestureDecoder old{30, 400, 500};                      // the loop-only sampling it replaces
    std::vector<InputEvent> oev;
    GestureOut o;
    old.update(true, false, 1005, o);
    old.update(true, false, 1240, o);                      // stalled: the next look sees it down
    old.update(false, false, 1600, o);
    old.update(false, false, 1640, o);
    for (int i = 0; i < o.n; i++) oev.push_back(o.ev[i]);
    CHECK(countOf(oev, InputType::EncDouble) == 0);
}

// A sample that raced the loop's tick never moves the decoder's time back.
static void replayClampsTime() {
    struct Probe {
        uint32_t lastT = 0;
        bool back = false;
        void update(bool, bool, uint32_t t, GestureOut&) {
            if ((int32_t)(t - lastT) < 0) back = true;
            lastT = t;
        }
    } p;
    KeyRing<8> ring;
    KeySample last = {0, false, false};
    keyReplay<GestureOut>(ring, p, 500, last, [](const GestureOut&) {});
    ring.sample(480, true, false);                         // taken before that tick
    keyReplay<GestureOut>(ring, p, 520, last, [](const GestureOut&) {});
    CHECK(!p.back && p.lastT == 520);
}

// One producer thread, one consumer: every change arrives once, in order.
static void concurrent() {
    KeyRing<16> r;
    const uint32_t n = 20000;
    std::thread prod([&] {
        for (uint32_t t = 1; t <= n; t++) {
            while (true) {
                const uint32_t before = r.dropped();
                r.sample(t, t % 2 == 1, false);
                if (r.dropped() == before) break;
                std::this_thread::yield();         // ring full: retry this level
            }
        }
    });
    uint32_t got = 0, last = 0;
    KeySample s;
    while (got < n) {
        if (!r.pop(s)) {
            std::this_thread::yield();
            continue;
        }
        if (s.t != last + 1 || s.enc != (s.t % 2 == 1)) {
            failures++;
            break;
        }
        last = s.t;
        got++;
    }
    prod.join();
    CHECK(got == n);
}

int main() {
    changesInOrder();
    overflowDropsNewest();
    doubleTapDuringStall();
    replayClampsTime();
    concurrent();
    if (failures) {
        std::fprintf(stderr, "keyring_test: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("keyring_test: ok\n");
    return 0;
}
