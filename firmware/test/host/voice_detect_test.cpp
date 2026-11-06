#define CHIRP_BOARD_TEMBED 1
#include "voice_detect.h"
#include <cmath>
#include <vector>
#include <algorithm>

#include <cstdio>
static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { \
    std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
    ++failures; } } while (0)

using Event = VoiceDetector::Event;
static bool accept(const int16_t*, size_t, int16_t) { return true; }
static std::vector<int16_t> loud(size_t n) {
    std::vector<int16_t> pcm(n);
    for (size_t i = 0; i < n; ++i) pcm[i] = i % 2 ? -20000 : 20000;
    return pcm;
}
// Documented zero-floor latch fixes thresholds at 700/900 for timing tests.
static void pinFloor(VoiceDetector& d) {
    int16_t zero = 0; d.ambient(&zero, 1); CHECK(d.floor() == 0);
}
static VoiceDetector::Result chunk(VoiceDetector& d, size_t n, bool high = true) {
    auto pcm = high ? loud(n) : std::vector<int16_t>(n, 0);
    return d.feed(pcm.data(), pcm.size(), accept);
}
static void open(VoiceDetector& d) {
    for (int i = 0; i < 6; ++i) CHECK(chunk(d, 1024).event == Event::None);
    CHECK(chunk(d, 1024).event == Event::Open);
    CHECK(d.hearing() && d.loudMs() == 448 && d.speechMs() == 0 && d.silenceMs() == 0);
}
static void dcAndFloor() {
    VoiceDetector d;
    CHECK(d.floor() == 600 && d.mean() == 0);
    int16_t one = 1000;
    CHECK(d.ambient(&one, 1) == 998 && one == 998 && d.mean() == 2.0f);
    VoiceDetector dc;
    std::vector<int16_t> constant(8000, 5000);
    dc.ambient(constant.data(), constant.size());
    CHECK(constant[0] == 4990 && constant[499] == 1837);
    CHECK(constant[1999] == 91 && constant[7999] == 0);
    CHECK(dc.mean() == 4999.9423828125f);
    VoiceDetector shared;
    one = 1000; shared.ambient(&one, 1);
    one = 1000; shared.feed(&one, 1, accept);
    CHECK(one == 996); // Second sample uses the mean established by ambient().
    VoiceDetector ac;
    auto alternating = loud(8000);
    const int16_t peak = ac.ambient(alternating.data(), alternating.size());
    CHECK(peak >= 19900 && peak <= 20100);
    VoiceDetector clamp;
    std::vector<int16_t> low(20000, -32768);
    clamp.ambient(low.data(), low.size()); // Do not pin the -32768 peak quirk.
    one = 32767;
    CHECK(clamp.ambient(&one, 1) == 32767 && one == 32767);

    VoiceDetector latch;
    one = 301; CHECK(latch.ambient(&one, 1) == 300 && latch.floor() == 300);
    one = 302; CHECK(latch.ambient(&one, 1) == 300);
    CHECK(std::fabs(latch.floor() - 302.4f) < 0.001f);
    one = 1003; latch.ambient(&one, 1);
    CHECK(latch.floor() > 302.4f && latch.floor() < 305.0f);
    VoiceDetector zero; pinFloor(zero);
    for (int i = 0; i < 10; ++i) { auto pcm = loud(512); zero.ambient(pcm.data(), pcm.size()); }
    CHECK(zero.floor() == 0 && zero.loudMs() == 0 && !zero.hearing());
    VoiceDetector creep;
    for (int i = 0; i < 10; ++i) { auto pcm = loud(512); creep.ambient(pcm.data(), pcm.size()); }
    CHECK(creep.floor() == 649.7655639648438f);
    auto r = chunk(creep, 512);
    CHECK(r.startTh == 1949 && r.endTh == 1104);
    VoiceDetector maximum;
    for (int i = 0; i < 326; ++i) { auto pcm = loud(512); maximum.ambient(pcm.data(), pcm.size()); }
    CHECK(maximum.floor() == 8000.0f);
    for (int i = 0; i < 4; ++i) { auto pcm = loud(512); maximum.ambient(pcm.data(), pcm.size()); }
    CHECK(maximum.floor() == 8000.0f);
    r = chunk(maximum, 512);
    CHECK(r.startTh == 24000 && r.endTh == 13600);
    VoiceTuning minima; minima.startRatio = 0; minima.endRatio = 0;
    minima.startMin = 2500; minima.endMin = 2700;
    VoiceDetector minimums(minima);
    r = chunk(minimums, 512);
    CHECK(r.startTh == 2500 && r.endTh == 2700);
    VoiceDetector seed;
    r = chunk(seed, 512, false);
    CHECK(r.startTh == 1800 && r.endTh == 1020); // Before this chunk latches the floor.
}
static void openingAndEdges() {
    VoiceDetector d; pinFloor(d); open(d);
    VoiceDetector exact; pinFloor(exact);
    for (int i = 0; i < 7; ++i) CHECK(chunk(exact, 800).event == Event::None);
    CHECK(exact.loudMs() == 350);
    CHECK(chunk(exact, 800).event == Event::Open && exact.loudMs() == 400);
    VoiceDetector leak; pinFloor(leak);
    chunk(leak, 1024); chunk(leak, 1024); chunk(leak, 1024, false);
    CHECK(leak.loudMs() == 64);
    for (int i = 0; i < 5; ++i) CHECK(chunk(leak, 1024).event == Event::None);
    CHECK(leak.loudMs() == 384 && chunk(leak, 1024).event == Event::Open);
    VoiceDetector drain; pinFloor(drain);
    chunk(drain, 1024); chunk(drain, 1024, false); CHECK(drain.loudMs() == 0);
    chunk(drain, 1024, false); CHECK(drain.loudMs() == 0);
    // Independent raw/AC reference pairs. With a one-sample chunk duration is zero;
    // startMs=0 is unsuitable for a threshold check, so use 16 samples with trailing zeros.
    struct Edge { int16_t raw, peak; bool active; uint32_t loudMs; };
    for (const Edge e : {Edge{701, 699, false, 0}, Edge{702, 700, false, 1},
                         Edge{901, 899, false, 1}, Edge{902, 900, true, 1}}) {
        VoiceDetector edge; pinFloor(edge);
        std::vector<int16_t> pcm(16, 0); pcm[0] = e.raw;
        auto r = edge.feed(pcm.data(), pcm.size(), accept);
        CHECK(r.peak == e.peak && r.active == e.active && edge.loudMs() == e.loudMs);
        CHECK(r.startTh == 700 && r.endTh == 900);
    }
    VoiceDetector shortChunk; pinFloor(shortChunk);
    chunk(shortChunk, 15); CHECK(shortChunk.loudMs() == 0);
    chunk(shortChunk, 16); CHECK(shortChunk.loudMs() == 1);
}
static void preroll(size_t n, int chunks, uint32_t startMs, size_t first, size_t second,
                    bool resetFirst = false) {
    VoiceTuning t; t.startMs = startMs;
    VoiceDetector d(t); pinFloor(d);
    if (resetFirst) { chunk(d, 512); d.reset(); }
    std::vector<int16_t> expected;
    int calls = 0;
    for (int i = 0; i < chunks; ++i) {
        auto pcm = loud(n);
        auto r = d.feed(pcm.data(), pcm.size(), [&](const int16_t*, size_t, int16_t) { ++calls; return true; });
        expected.insert(expected.end(), pcm.begin(), pcm.end());
        if (i + 1 < chunks) CHECK(r.event == Event::None);
        else {
            CHECK(r.event == Event::Open);
            CHECK(r.preroll[0].n == first && r.preroll[1].n == second);
            std::vector<int16_t> actual;
            for (const auto& span : r.preroll)
                if (span.n) actual.insert(actual.end(), span.data, span.data + span.n);
            if (expected.size() > 4800) expected.erase(expected.begin(), expected.end() - 4800);
            CHECK(actual == expected);
        }
    }
    CHECK(calls == 0);
}
static void hearing() {
    VoiceDetector frozen; open(frozen);
    const float nonzeroFloor = frozen.floor();
    CHECK(nonzeroFloor > 600);
    chunk(frozen, 512); CHECK(frozen.floor() == nonzeroFloor);
    chunk(frozen, 512, false); CHECK(frozen.floor() == nonzeroFloor);
    VoiceDetector d; pinFloor(d); open(d);
    const float floor = d.floor();
    chunk(d, 512); chunk(d, 512, false);
    CHECK(d.speechMs() == 64 && d.silenceMs() == 32);
    chunk(d, 512); CHECK(d.silenceMs() == 0);
    for (int i = 1; i <= 62; ++i) {
        CHECK(chunk(d, 512, false).event == Event::None);
        CHECK(d.silenceMs() == static_cast<uint32_t>(i * 32));
    }
    CHECK(chunk(d, 512, false).event == Event::Close);
    CHECK(!d.hearing() && d.loudMs() == 0 && d.silenceMs() == 2016 && d.speechMs() == 2112);
    CHECK(d.floor() == floor);
    // Default silence cannot exceed speech: use custom tuning to isolate the minimum-speech gate.
    VoiceTuning t; t.endMs = 64; t.minSpeechMs = 128;
    VoiceDetector gated(t); pinFloor(gated); open(gated);
    CHECK(chunk(gated, 512, false).event == Event::None);
    CHECK(chunk(gated, 512, false).event == Event::None && gated.silenceMs() == 64);
    CHECK(chunk(gated, 512, false).event == Event::None);
    CHECK(chunk(gated, 512, false).event == Event::Close && gated.speechMs() == 128);
    VoiceDetector reject; pinFloor(reject); open(reject);
    for (int i = 0; i < 93; ++i) CHECK(chunk(reject, 1024).event == Event::None);
    CHECK(reject.speechMs() == 5952 && reject.floor() == 0);
    CHECK(chunk(reject, 1024).event == Event::Reject);
    CHECK(reject.speechMs() == 6016 && reject.floor() == 8000 && !reject.hearing() && reject.loudMs() == 0);
    VoiceDetector lowReject; pinFloor(lowReject); open(lowReject);
    for (int i = 0; i < 94; ++i) {
        std::vector<int16_t> pcm(1024);
        for (size_t j = 0; j < pcm.size(); ++j) pcm[j] = j % 2 ? -2000 : 2000;
        const auto r = lowReject.feed(pcm.data(), pcm.size(), accept);
        if (i < 93) CHECK(r.event == Event::None && lowReject.floor() == 0);
        else {
            CHECK(r.event == Event::Reject && r.peak > 1900 && r.peak < 2100);
            CHECK(lowReject.floor() == r.peak);
        }
    }
    // Exact recovery limit is still allowed.
    VoiceDetector strict; pinFloor(strict); open(strict);
    for (int i = 0; i < 120; ++i) CHECK(chunk(strict, 800).event == Event::None);
    CHECK(strict.speechMs() == 6000 && chunk(strict, 800).event == Event::Reject);
    VoiceDetector dip; pinFloor(dip); open(dip); chunk(dip, 1024, false);
    for (int i = 0; i < 110; ++i) CHECK(chunk(dip, 1024).event == Event::None);
    CHECK(dip.hearing() && dip.floor() == 0);
    VoiceDetector cap; pinFloor(cap); open(cap);
    int calls = 0;
    for (int i = 1; i <= 3; ++i) {
        auto pcm = loud(512);
        auto r = cap.feed(pcm.data(), pcm.size(), [&](const int16_t* p, size_t n, int16_t peak) {
            CHECK(p == pcm.data() && n == 512 && peak >= 19900);
            ++calls; CHECK(cap.speechMs() == static_cast<uint32_t>((calls - 1) * 32));
            return calls < 3;
        });
        CHECK(r.event == (i == 3 ? Event::CapClose : Event::None));
    }
    CHECK(calls == 3 && !cap.hearing() && cap.loudMs() == 0 && cap.speechMs() == 64 && cap.silenceMs() == 0);
    VoiceDetector reset; pinFloor(reset); open(reset); chunk(reset, 512, false);
    const float mean = reset.mean(), savedFloor = reset.floor(); reset.reset();
    CHECK(!reset.hearing() && reset.loudMs() == 0 && reset.speechMs() == 0 && reset.silenceMs() == 0);
    CHECK(reset.mean() == mean && reset.floor() == savedFloor);
}
int main() {
    dcAndFloor(); openingAndEdges();
    preroll(512, 13, 400, 2944, 1856);
    preroll(4800, 2, 400, 4800, 0);
    preroll(512, 2, 64, 1024, 0);
    preroll(512, 2, 64, 1024, 0, true);
    hearing();
    return failures ? 1 : 0;
}
