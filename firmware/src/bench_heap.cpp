#include "bench.h"

#ifdef BENCH_CMDS

#include <esp_heap_caps.h>

#include <vector>

#include "app.h"
#include "menu.h"
#include "speech.h"
#include "ui.h"

// The workloads use only the public ui.h transcript/status calls, so this
// file builds unchanged against images with other text engines; the font
// switch workload exists only where there is a font API to switch.
#if __has_include("font_data.h")
#define HEAPBENCH_FONT_API 1
#endif

static const uint32_t INT_CAPS = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
static uint32_t intFree() { return heap_caps_get_free_size(INT_CAPS); }
static uint32_t intLargest() { return heap_caps_get_largest_free_block(INT_CAPS); }
static uint32_t psramFree() { return heap_caps_get_free_size(MALLOC_CAP_SPIRAM); }

// One workload: times every step and samples the internal heap right after
// it, so transient dips inside the run show as "low" even when the heap
// recovers by the end. done() prints one line.
class Probe {
public:
    explicit Probe(const char* name)
        : name_(name), free0_(intFree()), largest0_(intLargest()), psram0_(psramFree()),
          lowFree_(free0_), lowLargest_(largest0_) {}

    template <class F>
    void step(F f) {
        const uint32_t t = micros();
        f();
        const uint32_t us = micros() - t;
        n_++;
        sumUs_ += us;
        if (us > maxUs_) maxUs_ = us;
        sample();
        delay(1);   // untimed: lets the idle task feed the watchdog on long runs
    }

    void sample() {
        lowFree_ = min(lowFree_, intFree());
        lowLargest_ = min(lowLargest_, intLargest());
    }

    void done() {
        Serial.printf("[HeapBench] %-8s n=%u avg=%uus max=%uus | int free %u->%u low=%u "
                      "largest %u->%u low=%u | psram free %u->%u | min=%u\n",
                      name_, (unsigned)n_, (unsigned)(n_ ? sumUs_ / n_ : 0), (unsigned)maxUs_,
                      (unsigned)free0_, (unsigned)intFree(), (unsigned)lowFree_,
                      (unsigned)largest0_, (unsigned)intLargest(), (unsigned)lowLargest_,
                      (unsigned)psram0_, (unsigned)psramFree(),
                      (unsigned)heap_caps_get_minimum_free_size(INT_CAPS));
    }

private:
    const char* name_;
    uint32_t free0_, largest0_, psram0_, lowFree_, lowLargest_;
    uint32_t n_ = 0, sumUs_ = 0, maxUs_ = 0;
};

// A fixed, repeatable chat: five question/answer pairs with curly quotes,
// dashes and an accent, cycled with a running number so every turn differs.
static const char* const QS[] = {
    "What's the weather like in Lisbon this weekend?",
    "Can you explain how a heat pump works?",
    "Give me a quick recipe for sourdough starter",
    "Why does my garage door opener keep reversing?",
    "Summarize the plot of Dune in a few sentences",
};
static const char* const AS[] = {
    "Lisbon looks sunny on Saturday, around 24 degrees, with a light breeze off the Tagus. "
    "Sunday cools to 19 and clouds roll in by evening, so yes \xE2\x80\x94 a light jacket for "
    "the night. It\xE2\x80\x99s \xE2\x80\x9Cunlikely\xE2\x80\x9D to rain before Monday, and "
    "the caf\xC3\xA9 terraces stay open late. Pack sunscreen for the day and comfortable shoes "
    "for the hills; the trams are charming but crowded around noon.",
    "A heat pump moves heat instead of making it. A refrigerant absorbs warmth outdoors, a "
    "compressor raises its pressure and temperature, and a coil indoors releases the heat. "
    "Reverse the cycle and it cools the house. Because it relocates energy, it delivers "
    "three to four units of heat for every unit of electricity it uses \xE2\x80\x94 far "
    "better than resistance heating, though efficiency falls as it gets very cold outside.",
    "Mix equal weights of whole-wheat flour and lukewarm water in a clean jar, stir well, "
    "cover loosely and leave it warm. Each day discard half and feed it the same amount of "
    "flour and water. After five to seven days it should double within hours of feeding and "
    "smell pleasantly tangy; that is when it\xE2\x80\x99s ready to bake with.",
    "Usually the safety eyes are misaligned or blocked, or the travel limit is set too short "
    "so the door thinks it hit an obstacle. Clean both sensor lenses, check the LEDs are "
    "steady, then re-run the limit setup. If it still reverses near the floor, lubricate the "
    "rollers and inspect the springs before calling a technician.",
    "Paul Atreides, heir of a noble house, is sent to the desert planet Arrakis, the only "
    "source of the spice melange. After his family is betrayed he flees into the desert, "
    "joins the Fremen, and grows into the prophesied leader who will free the planet "
    "\xE2\x80\x94 at a terrible cost.",
};
static const int NQ = sizeof(QS) / sizeof(QS[0]);

static std::vector<Turn> fixedTurns(int count) {
    std::vector<Turn> turns;
    for (int i = 0; i < count; i++)
        turns.push_back({String(QS[i % NQ]) + " #" + String(i), String(AS[i % NQ])});
    return turns;
}

// Ends every workload on the same screen with the transcript emptied, so the
// next one starts from the same heap and a leak shows in free before/after.
static void settle() { transcriptClear(); }

static void wlRender(int n) {
    transcriptLoad(fixedTurns(20));
    Probe p("render");
    for (int i = 0; i < n; i++) {
        p.step([] { transcriptScroll(4); });
        p.step([] { transcriptFollow(); });
    }
    p.done();
    settle();
}

static void wlCommit(int n) {
    transcriptClear();
    Probe p("commit");
    for (int i = 0; i < n; i++)
        p.step([i] { transcriptCommit(String(QS[i % NQ]) + " #" + String(i), AS[i % NQ]); });
    p.done();
    settle();
}

// Streaming: the answer grows by 12 bytes per step, each step re-wrapping
// and repainting the pending block the way the loop does per delta.
static void wlPending(int n) {
    transcriptClear();
    transcriptPendingBegin();
    const String a = AS[1];
    Probe p("pending");
    for (int i = 0; i < n; i++) {
        const String part = a.substring(0, min((int)a.length(), 12 * (i + 1)));
        p.step([&] { transcriptPendingSet(QS[1], part.c_str()); });
    }
    p.done();
    transcriptPendingCancel();
    settle();
}

// The empty chat screen: the wordmark, then Body, on every render.
static void wlIdle(int n) {
    transcriptClear();
    Probe p("idle");
    for (int i = 0; i < n; i++) p.step([] { transcriptRender(); });
    p.done();
}

static void wlMsg(int n) {
    static const char* const MSGS[] = {"Ready", "Wheel=select  Encoder=open  Side=Theme",
                                       "Switching to Alpha...", "Mic init failed"};
    Probe p("msg");
    for (int i = 0; i < n; i++)
        p.step([i] { uiMsg(MSGS[i % 4], i % 2 ? Ink::Muted : Ink::Ok); });
    p.done();
    uiMsg("Ready", Ink::Ok);
}

// The status shimmer at the loop's ~30 fps for `seconds`.
static void wlShimmer(int seconds) {
    Probe p("shimmer");
    const uint32_t end = millis() + (uint32_t)seconds * 1000;
    while ((int32_t)(millis() - end) < 0) {
        const uint32_t frame = millis();
        p.step([] { uiShimmer("Thinking..."); });
        while (millis() - frame < 33) delay(1);
    }
    p.done();
    uiMsg("Ready", Ink::Ok);
}

#ifdef HEAPBENCH_FONT_API
// Body <-> Title reloads, plus what one loaded font costs in internal heap.
static void wlFont(int n) {
    uiFont(UiFont::Body);
    const uint32_t body = intFree();
    uiFont(UiFont::Title);
    const uint32_t title = intFree();
    uiFont(UiFont::Body);
    Serial.printf("[HeapBench] font-cost body_loaded_free=%u title_loaded_free=%u "
                  "(title minus body %d B)\n",
                  (unsigned)body, (unsigned)title, (int)title - (int)body);
    Probe p("font");
    for (int i = 0; i < n; i++) {
        p.step([] { uiFont(UiFont::Title); });
        p.step([] { uiFont(UiFont::Body); });
    }
    p.done();
}
#endif

void benchHeapRun(const String& args) {
    if (!args.length()) {
        logHeap("bench");
        return;
    }
    if (uiState != UI_IDLE || menuOpen() || speechPlaying()) {
        Serial.println("[HeapBench] refused: needs idle, no menu, nothing playing");
        return;
    }
    const int sp = args.indexOf(' ');
    const String what = sp < 0 ? args : args.substring(0, sp);
    const int n = sp < 0 ? 0 : (int)args.substring(sp + 1).toInt();
    settle();                                    // bench-start and bench-end are both logged with the transcript empty
    logHeap("bench-start");
    if (what == "all") {
        wlRender(40);
        wlCommit(40);
        wlPending(60);
        wlIdle(40);
        wlMsg(40);
#ifdef HEAPBENCH_FONT_API
        wlFont(40);
#endif
        wlShimmer(5);
    } else if (what == "render") wlRender(n ? n : 40);
    else if (what == "commit") wlCommit(n ? n : 40);
    else if (what == "pending") wlPending(n ? n : 60);
    else if (what == "idle") wlIdle(n ? n : 40);
    else if (what == "msg") wlMsg(n ? n : 40);
    else if (what == "shimmer") wlShimmer(n ? n : 5);
    else if (what == "keep") transcriptLoad(fixedTurns(n ? n : 4));   // left loaded, to look at
#ifdef HEAPBENCH_FONT_API
    else if (what == "font") wlFont(n ? n : 40);
#endif
    else {
        Serial.println("[HeapBench] heap [all|render|commit|pending|idle|msg|shimmer|font|keep] [n]");
        return;
    }
    appRedraw();
    logHeap("bench-end");
}

#endif
