#include "input.h"

#include <Arduino.h>
#include <esp_timer.h>

#include <atomic>

#include "config.h"
#include "gesture.h"
#include "keyring.h"
#include "wheel.h"

static GestureDecoder gestures(DEBOUNCE_MS, BTN_DBL_MS, BTN_HOLD_MS);
static int32_t wheelConsumed = 0;     // detents already turned into events
static uint32_t lastWheelMs = 0;

// Key levels, sampled every 5 ms by an esp_timer (its task) and replayed into
// the decoder by the loop (keyring.h): a loop stall cannot merge presses.
static const uint32_t KEY_SAMPLE_US = 5000;
static KeyRing<32> keyRing;
static KeySample keyLast = {0, false, false};   // loop: the last replayed levels

static InputEvent queue[16];
static int qHead = 0, qLen = 0;

#ifdef BENCH_CMDS
static std::atomic<bool> simEnc{false}, simSide{false};   // loop writes, the sampler reads
static int32_t simClicks = 0;         // injected detents, folded into the wheel
static uint32_t simPerClickMs = 0;    // forced spin speed for injected clicks
#endif

static bool encRaw() {
#ifdef BENCH_CMDS
    if (simEnc.load()) return true;
#endif
    return digitalRead(ENCODER_BTN_PIN) == LOW;
}

static bool sideRaw() {
#ifdef BENCH_CMDS
    if (simSide.load()) return true;
#endif
    return digitalRead(PTT_BTN_PIN) == LOW;
}

static void sampleKeys(void*) { keyRing.sample(millis(), encRaw(), sideRaw()); }

static int32_t wheelPosition() {
#ifdef BENCH_CMDS
    return wheelDetents() + simClicks;
#else
    return wheelDetents();
#endif
}

static void push(const InputEvent& e) {
    if (qLen == (int)(sizeof(queue) / sizeof(queue[0]))) return;
    queue[(qHead + qLen) % (sizeof(queue) / sizeof(queue[0]))] = e;
    qLen++;
}

static void update() {
    const uint32_t now = millis();
    keyReplay<GestureOut>(keyRing, gestures, now, keyLast, [](const GestureOut& out) {
        for (int i = 0; i < out.n; i++) push(out.ev[i]);
    });

    const int32_t pos = wheelPosition();
    if (pos != wheelConsumed) {
        const int32_t clicks = pos - wheelConsumed;
        wheelConsumed = pos;
        // Spin speed averaged over the batch: a blocked loop delivers several
        // clicks at once and still reads as fast.
        uint32_t perClick = (now - lastWheelMs) / (uint32_t)abs(clicks);
#ifdef BENCH_CMDS
        if (simPerClickMs) { perClick = simPerClickMs; simPerClickMs = 0; }
#endif
        lastWheelMs = now;
        push({InputType::Wheel, clicks, perClick, 0, false, false});
    }
}

void inputBegin() {
    pinMode(PTT_BTN_PIN, INPUT_PULLUP);
    pinMode(ENCODER_BTN_PIN, INPUT_PULLUP);
    if (!wheelBegin(ENCODER_A_PIN, ENCODER_B_PIN))
        Serial.println("[Wheel] encoder pins unsupported (need GPIO < 32)");
    wheelConsumed = wheelPosition();
    keyLast.t = millis();
    esp_timer_create_args_t args = {};
    args.callback = sampleKeys;
    args.name = "keys";
    esp_timer_handle_t timer = nullptr;
    if (esp_timer_create(&args, &timer) != ESP_OK ||
        esp_timer_start_periodic(timer, KEY_SAMPLE_US) != ESP_OK)
        Serial.println("[Input] key sampler failed to start");
}

bool inputPoll(InputEvent& ev) {
    if (qLen == 0) update();
    if (qLen == 0) return false;
    ev = queue[qHead];
    qHead = (qHead + 1) % (sizeof(queue) / sizeof(queue[0]));
    qLen--;
    return true;
}

bool inputKeyDown() { return encRaw() || sideRaw(); }
bool inputWheelPending() { return wheelPosition() != wheelConsumed; }
void inputDiscardWheel() { wheelConsumed = wheelPosition(); }
void inputSwallowKeys() { gestures.swallow(); }
void inputCancelDouble() { gestures.cancelDouble(); }

#ifdef BENCH_CMDS
// A bench key change is held for two sampler periods before the next console
// line runs: consolePoll applies every queued line in one pass, and a
// "side down" / "side up" pair landing between two 5 ms samples would never
// be seen (the view chord then reads as a plain encoder press).
void inputSimKey(bool encoder, bool down) {
    std::atomic<bool>& sim = encoder ? simEnc : simSide;
    if (sim.load() == down) return;
    sim = down;
    delay(2 * KEY_SAMPLE_US / 1000 + 2);
}

void inputSimWheel(int32_t clicks, uint32_t perClickMs) {
    simClicks += clicks;
    simPerClickMs = perClickMs;
}
#endif
