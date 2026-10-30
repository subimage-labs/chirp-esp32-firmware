#include "input.h"

#include <Arduino.h>
#include <esp_timer.h>

#include <atomic>

#include "balltap.h"
#include "config.h"
#include "gesture.h"
#include "keyboard.h"
#include "keyring.h"
#include "trackball.h"
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
#if CHIRP_HAS_KEYBOARD
    return keyboardMicDown();                 // the mic key (keyboard.h)
#elif ENCODER_BTN_PIN >= 0
    return digitalRead(ENCODER_BTN_PIN) == LOW;
#else
    return false;
#endif
}

static bool sideRaw() {
#ifdef BENCH_CMDS
    if (simSide.load()) return true;
#endif
#if PTT_BTN_PIN >= 0
    return digitalRead(PTT_BTN_PIN) == LOW;
#else
    return false;
#endif
}

static void sampleKeys(void*) { keyRing.sample(millis(), encRaw(), sideRaw()); }

// The wheel's detent source: the encoder on wheel boards, the trackball's
// vertical axis on ball boards (roll up = clockwise), CHIRP_BALL_PULSES_PER_STEP
// pulses per detent (balltap.h); sideways rolls are not read.
#if CHIRP_HAS_TRACKBALL
static int32_t ballNet = 0;                  // detents not yet turned into events
static BallSteps ballSteps(CHIRP_BALL_PULSES_PER_STEP);
static int32_t ballPosition() {
    const int32_t up = trackballUp(), down = trackballDown();
#if defined(BENCH_CMDS) && CHIRP_HAS_KEYBOARD
    if ((up || down) && keyboardEchoOn())    // bench `keys on`: tuning data
        Serial.printf("[Ball] t=%lu up=%ld down=%ld\n", (unsigned long)millis(), (long)up, (long)down);
#endif
    ballNet += ballSteps.feed(up - down);
    return ballNet;
}
#endif

static int32_t wheelPosition() {
#if CHIRP_HAS_TRACKBALL
    const int32_t pos = ballPosition();
#else
    const int32_t pos = wheelDetents();
#endif
#ifdef BENCH_CMDS
    return pos + simClicks;
#else
    return pos;
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
#if PTT_BTN_PIN >= 0
    pinMode(PTT_BTN_PIN, INPUT_PULLUP);
#endif
#if ENCODER_BTN_PIN >= 0
    pinMode(ENCODER_BTN_PIN, INPUT_PULLUP);
#endif
#if CHIRP_HAS_TRACKBALL
    trackballBegin();
#else
    if (!wheelBegin(ENCODER_A_PIN, ENCODER_B_PIN))
        Serial.println("[Wheel] encoder pins unsupported (need GPIO < 32)");
#endif
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
