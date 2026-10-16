// gesture.h — key debounce and gesture decoding. Pure (no Arduino),
// host-tested; input.cpp feeds raw key levels and forwards the events.
#pragma once

#include <stdint.h>

#include "input.h"

// Press is accepted immediately (push-to-talk starts instantly). Release
// needs the key seen up for releaseMs, which absorbs contact bounce: a
// shorter blip while held is ignored. The loop samples sparsely (~64 ms
// while recording), so a key seen up at one sample and down again at a
// later one at least releaseMs apart was released in between: that is
// Repressed (release, then a new press) — a fast double-tap never merges
// into one long press.
class KeyDebounce {
public:
    enum Edge : uint8_t { None, Pressed, Released, Repressed };

    explicit KeyDebounce(uint32_t releaseMs) : releaseMs_(releaseMs) {}

    Edge update(bool raw, uint32_t now) {
        if (!down_) {
            if (!raw) return None;
            press(now);
            return Pressed;
        }
        if (raw) {
            if (!upPending_) return None;
            upPending_ = false;
            if (now - upSince_ < releaseMs_) return None;   // blip while held
            release();
            press(now);
            return Repressed;
        }
        if (!upPending_) {
            upPending_ = true;
            upSince_ = now;
        }
        if (now - upSince_ < releaseMs_) return None;
        release();
        return Released;
    }

    bool isDown() const { return down_; }
    uint32_t downAt() const { return downAt_; }
    uint32_t heldMs() const { return heldMs_; }   // last press: down → first seen up

private:
    void press(uint32_t now) {
        down_ = true;
        downAt_ = now;
        upPending_ = false;
    }
    void release() {
        down_ = false;
        upPending_ = false;
        heldMs_ = upSince_ - downAt_;
    }

    uint32_t releaseMs_;
    bool down_ = false, upPending_ = false;
    uint32_t downAt_ = 0, upSince_ = 0, heldMs_ = 0;
};

struct GestureOut {
    InputEvent ev[4];
    int n = 0;
    void push(const InputEvent& e) { if (n < 4) ev[n++] = e; }
};

// Encoder key: EncPress / EncDouble (second press within doubleMs) /
// EncRelease (carrying the held time).
// Side key: SideHold once past holdMs, then SideRelease carrying the held
// time (press to first raw-up) and whether SideHold fired.
// The view chord: a side press that begins while the encoder key is down
// marks both releases (chord), whichever comes first, so the encoder's is
// never taken for speech.
// swallow(): every event is suppressed until both keys are up — the press
// that wakes the device never acts.
class GestureDecoder {
public:
    GestureDecoder(uint32_t releaseMs, uint32_t doubleMs, uint32_t holdMs)
        : enc_(releaseMs), side_(releaseMs), doubleMs_(doubleMs), holdMs_(holdMs) {}

    void update(bool encRaw, bool sideRaw, uint32_t now, GestureOut& out) {
        const KeyDebounce::Edge e = enc_.update(encRaw, now);
        if ((e == KeyDebounce::Released || e == KeyDebounce::Repressed) && !swallow_)
            out.push({InputType::EncRelease, 0, 0, enc_.heldMs(), false, encChord_});
        if (e == KeyDebounce::Pressed || e == KeyDebounce::Repressed) encChord_ = false;
        if ((e == KeyDebounce::Pressed || e == KeyDebounce::Repressed) && !swallow_) {
            if (primed_ && now - lastEncPress_ < doubleMs_) {
                primed_ = false;
                out.push({InputType::EncDouble, 0, 0, 0, false, false});
            } else {
                primed_ = true;
                lastEncPress_ = now;
                out.push({InputType::EncPress, 0, 0, 0, false, false});
            }
        }

        const KeyDebounce::Edge s = side_.update(sideRaw, now);
        if ((s == KeyDebounce::Released || s == KeyDebounce::Repressed) && !swallow_)
            out.push({InputType::SideRelease, 0, 0, side_.heldMs(), holdFired_, sideChord_});
        if (s == KeyDebounce::Pressed || s == KeyDebounce::Repressed) {
            holdFired_ = false;
            sideChord_ = enc_.isDown();
            if (sideChord_) {
                encChord_ = true;
                primed_ = false;                   // the chord's press pairs with nothing
            }
        }
        // Physically held: the release debounce window must not count as hold.
        if (sideRaw && side_.isDown() && !holdFired_ && now - side_.downAt() > holdMs_) {
            holdFired_ = true;
            if (!swallow_) out.push({InputType::SideHold, 0, 0, 0, false, false});
        }

        if (swallow_ && !encRaw && !sideRaw && !enc_.isDown() && !side_.isDown())
            swallow_ = false;
    }

    void swallow() {
        swallow_ = true;
        primed_ = false;
    }
    void cancelDouble() { primed_ = false; }

private:
    KeyDebounce enc_, side_;
    uint32_t doubleMs_, holdMs_;
    bool primed_ = false;           // last encoder press can pair into a double
    uint32_t lastEncPress_ = 0;
    bool holdFired_ = false;
    bool sideChord_ = false;        // this side press began with the encoder key down
    bool encChord_ = false;         // a side press began during this encoder press
    bool swallow_ = false;
};
