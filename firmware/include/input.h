// input.h — keys and wheel decoded into gesture events. Loop task only,
// except the key sampler (esp_timer task, every 5 ms), which only pushes key
// level changes into the key ring (keyring.h) that the loop replays. Owns key
// debounce/gesture state and the consumed wheel position; it never decides
// what an event means — main.cpp's dispatcher routes events to the state
// owner (menu, volume, transcript, capture).
#pragma once

#include <stdint.h>

enum class InputType : uint8_t {
    Wheel,       // clicks (+ = clockwise), perClickMs = spin speed
    EncPress,    // encoder key down (single)
    EncDouble,   // second encoder press within BTN_DBL_MS
    EncRelease,  // encoder key up (debounced); heldMs = how long it was down; chord
    SideHold,    // side key held past BTN_HOLD_MS (fires once per press)
    SideRelease, // side key up; holdFired = SideHold was emitted; chord
    Tab,         // a sideways trackball flick: clicks = +1 next tab, -1 the tab before
};

struct InputEvent {
    InputType type;
    int32_t clicks;       // Wheel
    uint32_t perClickMs;  // Wheel
    uint32_t heldMs;      // EncRelease, SideRelease
    bool holdFired;       // SideRelease
    bool chord;           // SideRelease: the encoder key was down when the side key went down;
                          // EncRelease: the side key went down during this encoder press
};

void inputBegin();
bool inputPoll(InputEvent& ev);   // next pending event; false when none

bool inputKeyDown();              // either key physically (or bench) down
bool inputWheelPending();         // wheel turned since last consumed
void inputDiscardWheel();         // drop pending clicks (a waking turn)
void inputSwallowKeys();          // suppress events until both keys are up
void inputCancelDouble();         // the next encoder press cannot pair

#ifdef BENCH_CMDS
void inputSimKey(bool encoder, bool down);
void inputSimWheel(int32_t clicks, uint32_t perClickMs);
void inputSimTab(int32_t dir);           // a sideways flick: +1 next tab, -1 the tab before
#endif
