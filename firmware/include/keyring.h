// keyring.h — key level changes, sampled off the loop and replayed in it.
// Pure (no Arduino), host-tested under TSan.
//
// The loop can stall for a few hundred ms (opening the STT stream, a full
// repaint, an I2S read), and keys sampled only there merge a quick
// release-and-press into one long press: a double-tap becomes a recording.
// So a 5 ms timer (input.cpp, esp_timer task) samples both keys and pushes
// each change with its time; the loop pops them in order into the gesture
// decoder. One producer (the timer), one consumer (the loop); a full ring
// drops the newest change and counts it.
#pragma once

#include <stdint.h>

#include <atomic>

struct KeySample {
    uint32_t t;        // millis() when sampled
    bool enc, side;    // pressed
};

template <unsigned N>
class KeyRing {
    static_assert((N & (N - 1)) == 0, "N must be a power of two");

public:
    // Producer: record the levels at t if they differ from the last pushed.
    void sample(uint32_t t, bool enc, bool side) {
        if (primed_ && enc == lastEnc_ && side == lastSide_) return;
        const unsigned head = head_.load(std::memory_order_relaxed);
        if (head - tail_.load(std::memory_order_acquire) == N) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return;                                // keep lastEnc_: retried next tick
        }
        buf_[head % N] = {t, enc, side};
        head_.store(head + 1, std::memory_order_release);
        primed_ = true;
        lastEnc_ = enc;
        lastSide_ = side;
    }

    // Consumer: the oldest unread change; false when none.
    bool pop(KeySample& out) {
        const unsigned tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) return false;
        out = buf_[tail % N];
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    uint32_t dropped() const { return dropped_.load(std::memory_order_relaxed); }

private:
    KeySample buf_[N];
    std::atomic<unsigned> head_{0}, tail_{0};
    std::atomic<uint32_t> dropped_{0};
    bool primed_ = false, lastEnc_ = false, lastSide_ = false;   // producer-only
};

// Consumer side: feeds every queued change to dec (anything with
// update(enc, side, now, out)), oldest first, then one tick at `now` with the
// last levels so time-based events (release debounce, side hold) still fire
// while nothing changes. emit(out) is called after each update. Time never
// runs backwards into the decoder: a sample taken before the previous tick
// (the timer raced the loop) is fed at that tick's time.
template <class Out, unsigned N, class Decoder, class Emit>
void keyReplay(KeyRing<N>& ring, Decoder& dec, uint32_t now, KeySample& last, Emit emit) {
    KeySample s;
    while (ring.pop(s)) {
        if ((int32_t)(s.t - last.t) < 0) s.t = last.t;
        last = s;
        Out out;
        dec.update(s.enc, s.side, s.t, out);
        emit(out);
    }
    if ((int32_t)(now - last.t) > 0) last.t = now;
    Out out;
    dec.update(last.enc, last.side, last.t, out);
    emit(out);
}