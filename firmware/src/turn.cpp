#include "turn.h"

#include "app.h"
#include "capture.h"
#include "character.h"
#include "hermes.h"
#include "speech.h"
#include "turntext.h"
#include "ui.h"
#include "voice.h"

#include <atomic>

// Stage cell: enum + 24-char label shared by the worker and the loop under
// stageMux. Only plain stores and strncpy happen inside — never allocation.
enum WorkerStage : uint8_t { WS_IDLE, WS_STT, WS_LLM, WS_OK, WS_FAIL, WS_SILENCE, WS_STOPPED };
static WorkerStage workerStage = WS_IDLE;
static char stageLabel[24];
static portMUX_TYPE stageMux = portMUX_INITIALIZER_UNLOCKED;

static TaskHandle_t turnTaskHandle = nullptr;
static String workerQ;                            // worker-private until the stage handoff
static String lastAnswer;                         // worker-private until the stage handoff
static TurnSnap turnSnap;                         // worker -> loop progressive text
static TurnReader turnReader(turnSnap);           // loop-side cache, pinned per turn
#ifdef BENCH_CMDS
static String injectedQ;                          // bench: set before the notify
static String injectedA;                          // bench: a canned answer instead of Hermes
#endif

static std::atomic<uint32_t> latT0{0};             // worker writes, any context reads
// Stop request: the loop clears it before waking the worker and sets it from
// turnStop; the worker polls it after STT and while the stream waits.
static std::atomic<bool> stopReq{false};
static bool stopRequested() { return stopReq.load(); }

void turnLatMark(const char* event) {
    Serial.printf("[Lat] %s +%ums\n", event, (unsigned)(millis() - latT0.load()));
}

// Lock latency per turn (µs, includes wait for the other side). pubLat is
// worker-owned, readLat loop-owned; each logs its own.
struct LatStat {
    uint32_t n = 0, maxUs = 0;
    uint64_t sumUs = 0;
    void add(uint32_t us) { n++; sumUs += us; if (us > maxUs) maxUs = us; }
    void log(const char* tag) const {
        Serial.printf("[Timing] %s n=%u avg=%uus max=%uus\n", tag, (unsigned)n,
                      (unsigned)(n ? sumUs / n : 0), (unsigned)maxUs);
    }
};
static LatStat pubLat, readLat;

// ---------------------------------------------------------------- worker
static void setStage(WorkerStage s, const char* label) {
    portENTER_CRITICAL(&stageMux);
    workerStage = s;
    if (label) {
        strncpy(stageLabel, label, sizeof(stageLabel) - 1);
        stageLabel[sizeof(stageLabel) - 1] = '\0';
    }
    portEXIT_CRITICAL(&stageMux);
}

// Every turn ends here: telemetry first, then the stage handoff (after
// setStage the loop owns the turn strings until the next turnBegin).
static void endTurn(WorkerStage s, const char* label) {
    pubLat.log("publish");
    logHeap("turn");
    setStage(s, label);
}

// Mirror turn text into the bounded loop snapshot. The lock is held only
// for fixed-size memcpy — no allocation inside, cheap per delta.
static void publishTurnText() {
    const uint32_t t = micros();
    turnSnap.publish(workerQ.c_str(), lastAnswer.c_str());
    pubLat.add(micros() - t);
}

static void onDelta(const char* delta) {
    if (lastAnswer.length() == 0) turnLatMark("first_token");
    lastAnswer += delta;
    publishTurnText();
    speechFeed(delta);
}

#ifdef BENCH_CMDS
// Bench: streams a canned answer word by word through the same path as
// Hermes deltas (transcript, speech chunks), so playback and the character
// view's speech-following run for real without asking anyone. The first word
// comes 1.5 s late, like a model's first token.
static AskEnd cannedAnswer(const String& a) {
    for (int w = 0; w < 75; w++) {
        if (stopRequested()) return AskEnd::Stopped;
        delay(20);
    }
    int i = 0;
    while (i < (int)a.length()) {
        if (stopRequested()) return AskEnd::Stopped;
        int sp = a.indexOf(' ', i + 1);
        if (sp < 0) sp = a.length();
        onDelta(a.substring(i, sp).c_str());
        i = sp;
        delay(20);
    }
    return AskEnd::Answered;
}
#endif

static void turnTask(void*) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        const uint32_t t0 = millis();
        pubLat = LatStat();
        workerQ = "";
        lastAnswer = "";
        bool sttOk;
#ifdef BENCH_CMDS
        const String canned = injectedA;          // this turn's only, whichever way it ends
        injectedA = "";
        if (injectedQ.length()) {
            workerQ = injectedQ;
            injectedQ = "";
            sttOk = true;
        } else
#endif
            sttOk = captureTranscribe(workerQ);
        if (!sttOk) { endTurn(WS_FAIL, "STT failed"); continue; }
        publishTurnText();   // question visible from the first THINKING frame
        if (workerQ.length() == 0) { endTurn(WS_SILENCE, "Heard nothing"); continue; }
        if (stopRequested()) { endTurn(WS_STOPPED, "Stopped"); continue; }
        Serial.printf("[Timing] stt=%ums heard=%.40s\n",
                      (unsigned)(millis() - t0), workerQ.c_str());
        const uint32_t t1 = millis();
        latT0.store(t1);

        if (!speechStart()) { endTurn(WS_FAIL, "Speech busy - retry"); continue; }
        setStage(WS_LLM, "Thinking");             // after the reset: the loop may now play
#ifdef BENCH_CMDS
        const AskEnd end = canned.length() ? cannedAnswer(canned)
                                           : hermesStreamAsk(workerQ, onDelta, stopRequested);
#else
        const AskEnd end = hermesStreamAsk(workerQ, onDelta, stopRequested);
#endif
        turnLatMark("stream_end");
        speechFinish();                           // short answers: the only chunk
        if (end == AskEnd::Stopped) { endTurn(WS_STOPPED, "Stopped"); continue; }
        if (end == AskEnd::Silent) { endTurn(WS_FAIL, "Hermes not answering"); continue; }
        if (end != AskEnd::Answered || speechChunks() == 0) {
            endTurn(WS_FAIL, "Hermes unreachable");
            continue;
        }
        Serial.printf("[Timing] hermes=%ums\n[Turn] heard: %s\n[Turn] answer: %s\n",
                      (unsigned)(millis() - t1), workerQ.c_str(), lastAnswer.c_str());
        endTurn(WS_OK, "");
    }
}

void turnTaskStart() {
    if (xTaskCreatePinnedToCore(turnTask, "turn", 16384, nullptr, 1,
                                &turnTaskHandle, 0) != pdPASS) {
        Serial.println("[Task] turn worker failed to start");
        uiMsg("Task start failed", Ink::Err);
    }
}

// ---------------------------------------------------------------- loop
static void startWorker() {
    speechChirp();                    // instant audible ack: the wait feels shorter
    turnReader.pin();                 // no text from the previous turn leaks in
    readLat = LatStat();
    transcriptPendingBegin();
    uiMsg("Thinking", Ink::Busy);
    uiState = UI_THINKING;
    stopReq.store(false);
    setStage(WS_STT, "Transcribing");
    xTaskNotifyGive(turnTaskHandle);
}

void turnStop() {
    if (uiState != UI_THINKING || stopReq.load()) return;
    stopReq.store(true);
    Serial.println("[Turn] stop requested");
}

void turnBegin(bool keepMic) {
    if (!keepMic) micRemove();
    if (!captureWorthSending()) {
        captureStreamClose();                     // rejected: terminate upload
        Serial.printf("[Turn] capture rejected: pcm=%u peak=%d\n",
                      (unsigned)captureBytes(), (int)capturePeak());
        uiMsg("Nothing heard", Ink::Muted);
        characterReact(avatar::Puzzled);
        uiState = UI_IDLE;
        return;
    }
    startWorker();
}

#ifdef BENCH_CMDS
void turnAskText(const String& q) {
    injectedQ = q;                    // read by the worker after the notify
    startWorker();
}

void turnSpeakText(const String& a) {
    injectedQ = "(bench) speak";
    injectedA = a;                    // read by the worker after the notify
    startWorker();
}
#endif

static void finishThinking(WorkerStage st, const char* lbl) {
    readLat.log("snapread");
    // Final text, full fidelity (no snapshot cap): the worker is parked, so
    // its strings are stable — plain read, no lock.
    if (st == WS_OK && lastAnswer.length() > 0) {
        transcriptCommit(workerQ, lastAnswer);
        hermesSessionSave();          // persist for next boot
        uiMsg("", Ink::Ok);           // the chat's live row shows the speech
        uiState = UI_SPEAKING;
        return;
    }
    speechStop();                     // a failed stream may have started speaking
    if (st == WS_FAIL) {
        speechErrorTone();
        characterError(workerQ.c_str(), lbl);
    }
    if (st == WS_SILENCE) characterReact(avatar::Puzzled);
    transcriptPendingCancel();
    uiMsg(lbl, st == WS_FAIL ? Ink::Err : Ink::Muted);
    uiState = UI_IDLE;
    voiceResume();
}

static void thinkingTick() {
    char lbl[24];
    WorkerStage st;
    portENTER_CRITICAL(&stageMux);
    st = workerStage;
    strncpy(lbl, stageLabel, sizeof(lbl) - 1);
    lbl[sizeof(lbl) - 1] = '\0';
    portEXIT_CRITICAL(&stageMux);

    if (st == WS_OK || st == WS_FAIL || st == WS_SILENCE || st == WS_STOPPED) {
        finishThinking(st, lbl);
        return;
    }
    // Speak the first chunks while Hermes is still streaming; before the LLM
    // stage the speech state still belongs to the worker's reset.
    if (st == WS_LLM) speechTick();
    else speechPump();                // the release chirp
    // Progressive answer: fold worker text into the pending block, full
    // repaint at most 5 fps so the screen stays calm.
    static uint32_t lastStream = 0;
    const uint32_t tr = micros();
    if (turnReader.poll()) readLat.add(micros() - tr);
    if (turnReader.dirty() && millis() - lastStream > 200) {
        lastStream = millis();
        turnReader.clearDirty();
        const TurnSnapView& v = turnReader.view();
        transcriptPendingSet(v.q, v.a);
    }
    // The status label shimmers while the turn works (~30 fps; each frame
    // repaints only the label's characters).
    static uint32_t lastShimmer = 0;
    static bool waveCleared = false;             // the status is emptied once for the wave row
    if (millis() - lastShimmer >= 33) {
        lastShimmer = millis();
        const bool wave = !stopReq.load() && speechPlaying() && st == WS_LLM;
        if (wave && !waveCleared) uiMsg("", Ink::Ok);   // the chat's wave row shows it speaks
        else if (!wave) uiShimmer(stopReq.load() ? "Stopping" : lbl);
        waveCleared = wave;
    }
}

bool turnTranscribing() {
    portENTER_CRITICAL(&stageMux);
    const bool stt = workerStage == WS_STT;
    portEXIT_CRITICAL(&stageMux);
    return stt;
}

void turnTick() {
    if (uiState == UI_THINKING) {
        thinkingTick();
    } else if (uiState == UI_SPEAKING && speechTick() == SpeakStep::Done) {
        uiMsg("Ready", Ink::Ok);
        uiState = UI_IDLE;
        voiceResume();
    }
}
