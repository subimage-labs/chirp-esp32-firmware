// turntext.h — bounded worker→UI text publication for the turn pipeline.
//
// One publisher (turn worker task, core 0), one reader (Arduino UI loop,
// core 1), guarded by a std::mutex (FreeRTOS-backed on target, pthread on
// host tests). Everything copied under the lock is fixed-size memcpy —
// allocation never happens inside, so hold time is bounded (~µs) and
// independent of heap state: the worker prepares its Strings before
// publish(), the UI builds Strings from the view after read().
#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>

static constexpr size_t SNAP_Q_CAP = 2048;  // covers ~60s of STT text
static constexpr size_t SNAP_A_CAP = 4096;  // 1-3 sentence answers + margin

// Reader-owned stable copy of one published state; always NUL-terminated.
struct TurnSnapView {
    char q[SNAP_Q_CAP] = {};
    char a[SNAP_A_CAP] = {};
    bool qTrunc = false, aTrunc = false;  // source exceeded cap (display only)
};

class TurnSnap {
public:
    void publish(const char* q, const char* a);
    void read(TurnSnapView& out, uint32_t& revOut) const;
    uint32_t currentRev() const;

private:
    mutable std::mutex m;
    uint32_t rev = 0;
    char q[SNAP_Q_CAP] = {};
    char a[SNAP_A_CAP] = {};
    bool qTrunc = false, aTrunc = false;
};

// UI-side cache: what the THINKING screen has folded into pendingQ/pendingA.
// pin() starts a turn — it forgets every publish made before now (stale text
// can never replay under a new question) and clears the unapplied flag.
// poll() refreshes the view and raises dirty() on unseen content; the
// consumer clearDirty()s once it has rendered the text.
class TurnReader {
public:
    explicit TurnReader(const TurnSnap& snap) : snap_(snap) {}

    void pin() {
        applied_ = snap_.currentRev();
        dirty_ = false;
    }

    // Returns true when it copied a new revision into view().
    bool poll() {
        if (snap_.currentRev() == applied_) return false;  // no 6KB copy
        uint32_t r;
        snap_.read(view_, r);
        applied_ = r;
        dirty_ = true;
        return true;
    }

    bool dirty() const { return dirty_; }
    void clearDirty() { dirty_ = false; }
    const TurnSnapView& view() const { return view_; }

private:
    const TurnSnap& snap_;
    TurnSnapView view_;
    uint32_t applied_ = 0;
    bool dirty_ = false;
};
