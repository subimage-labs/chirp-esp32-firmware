// voice_detect.h — voice-activity detection for hands-free voice mode: the DC
// blocker, the room-floor tracker, the thresholds, the leaky loudness
// integrator that opens a turn, the pre-roll ring and the rules that close
// or reject it. Pure (no Arduino types), host-tested by voice_detect_test.cpp.
//
// All arithmetic is float32 exactly as the firmware runs it. The mute
// window, the drain count, the idle exit, the UI text, the serial logs and
// every capture / turn call stay in voice.cpp, which reads the Result of
// each chunk and the accessors.
//
// Ownership: one VoiceDetector, owned by voice.cpp and used only from the
// loop context (ambientTick, voiceTick, voiceSet).
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <algorithm>

#include "config.h"          // VOICE_*, SAMPLE_RATE

struct VoiceTuning {         // defaults = config.h
    float    startRatio = VOICE_START_RATIO;     // turn opens at floor x this
    float    endRatio   = VOICE_END_RATIO;       // silence when below floor x this
    int32_t  startMin   = VOICE_START_MIN;       // never open below this
    int32_t  endMin     = VOICE_END_MIN;         // never count silence below this
    float    floorSeed  = VOICE_FLOOR_SEED;      // initial ambient estimate
    float    floorMax   = VOICE_FLOOR_MAX;       // floor ceiling
    uint32_t startMs    = VOICE_START_MS;        // net loud (leaky) needed to open
    uint32_t endMs      = VOICE_END_MS;          // trailing silence that closes
    uint32_t recoverMs  = VOICE_RECOVER_MS;      // no-dip speech this long = noise
    uint32_t minSpeechMs = VOICE_MIN_SPEECH_MS;  // ignore sub-second blips
};

class VoiceDetector {
public:
    static constexpr size_t PREROLL_CAP = VOICE_PREROLL_MS * SAMPLE_RATE / 1000;   // samples
    struct Span { const int16_t* data = nullptr; size_t n = 0; };
    enum class Event : uint8_t { None, Open, Close, CapClose, Reject };
    struct Result {
        Event   event = Event::None;
        int16_t peak = 0;          // AC peak of this chunk
        bool    active = false;    // peak >= endTh (this chunk): refresh the last-activity time
        int32_t startTh = 0, endTh = 0;   // thresholds used for this chunk (before the floor update)
        Span    preroll[2];        // Event::Open only: oldest-first, two runs; valid until the next call
    };

    explicit VoiceDetector(const VoiceTuning& t = VoiceTuning())
        : t_(t), floor_(t.floorSeed) {}

    // Leaving or entering voice mode: forget the turn state. The DC mean, the
    // floor and the dip flag carry over.
    void reset() {
        hearing_ = false;
        loudMs_ = silenceMs_ = speechMs_ = 0;
        idx_ = count_ = 0;
    }

    // Idle ambient tracking: DC-block in place and track the floor only.
    // Returns the AC peak.
    int16_t ambient(int16_t* pcm, size_t n) {
        const int16_t peak = removeDc(pcm, n);
        trackFloor(peak);
        return peak;
    }

    // pcm: n >= 1 raw samples, converted in place to AC. `append(const int16_t*
    // pcm, size_t samples, int16_t peak) -> bool` is called only while
    // hearing(), before the counters update; false = capture buffer full.
    template <class Append>
    Result feed(int16_t* pcm, size_t n, Append&& append) {
        Result r;
        r.peak = removeDc(pcm, n);
        const uint32_t durMs = (uint32_t)(n * 2) * 1000UL / (SAMPLE_RATE * 2UL);
        r.startTh = std::max(t_.startMin, (int32_t)(floor_ * t_.startRatio));
        r.endTh = std::max(t_.endMin, (int32_t)(floor_ * t_.endRatio));
        r.active = r.peak >= r.endTh;

        if (!hearing_) {
            for (size_t i = 0; i < n; i++) {          // rolling pre-roll ring
                ring_[idx_] = pcm[i];
                idx_ = (idx_ + 1) % PREROLL_CAP;
                if (count_ < PREROLL_CAP) count_++;
            }
            loudMs_ = (r.peak >= r.startTh)                       // leaky integrator:
                        ? loudMs_ + durMs                          // dips between syllables
                        : (loudMs_ > durMs ? loudMs_ - durMs : 0);  // decay, not reset
            trackFloor(r.peak);
            if (loudMs_ < t_.startMs) return r;
            hearing_ = true;
            speechMs_ = 0;
            silenceMs_ = 0;
            const size_t start = (idx_ + PREROLL_CAP - count_) % PREROLL_CAP;   // oldest
            const size_t first = std::min(count_, PREROLL_CAP - start);         // ring: two runs
            r.preroll[0].data = ring_ + start;
            r.preroll[0].n = first;
            r.preroll[1].data = ring_;
            r.preroll[1].n = count_ - first;
            idx_ = count_ = 0;
            r.event = Event::Open;
            return r;
        }

        if (!append(pcm, n, r.peak)) {                // cap: close the turn now
            endTurn();
            r.event = Event::CapClose;
            return r;
        }
        speechMs_ += durMs;
        if (r.peak < r.endTh) { silenceMs_ += durMs; anyDip_ = true; }
        else silenceMs_ = 0;
        if (speechMs_ > t_.recoverMs && !anyDip_) {   // constant noise, not speech
            floor_ = std::min(t_.floorMax, (float)r.peak);
            endTurn();
            r.event = Event::Reject;
            return r;
        }
        if (speechMs_ >= t_.minSpeechMs && silenceMs_ >= t_.endMs) {
            endTurn();
            r.event = Event::Close;
        }
        return r;
    }

    bool     hearing()   const { return hearing_; }
    float    floor()     const { return floor_; }
    float    mean()      const { return mean_; }
    uint32_t loudMs()    const { return loudMs_; }
    uint32_t speechMs()  const { return speechMs_; }
    uint32_t silenceMs() const { return silenceMs_; }

private:
    // DC blocker in place (far off zero; decisions and captures use AC);
    // returns the AC peak.
    int16_t removeDc(int16_t* pcm, size_t n) {
        int16_t peak = 0;
        for (size_t i = 0; i < n; i++) {
            mean_ = mean_ * 0.998f + (float)pcm[i] * 0.002f;
            const int32_t raw = (int32_t)(pcm[i] - mean_);
            const int16_t ac = (int16_t)std::min<int32_t>(std::max<int32_t>(raw, -32768), 32767);
            pcm[i] = ac;
            const int16_t a = ac < 0 ? -ac : ac;
            if (a > peak) peak = a;
        }
        return peak;
    }

    void trackFloor(int16_t peak) {
        if ((float)peak < floor_) floor_ = (float)peak;         // min-latch: instant
        else floor_ = std::min(t_.floorMax, floor_ * 1.008f);   // slow creep up
    }

    void endTurn() {
        hearing_ = false;
        anyDip_ = false;
        loudMs_ = 0;
    }

    VoiceTuning t_;
    bool hearing_ = false;
    bool anyDip_ = false;                      // saw quiet during this turn
    uint32_t loudMs_ = 0, silenceMs_ = 0, speechMs_ = 0;
    float floor_;                              // room ambient (shared with idle)
    float mean_ = 0.0f;                        // DC blocker state (shared)
    int16_t ring_[PREROLL_CAP];
    size_t idx_ = 0, count_ = 0;
};
