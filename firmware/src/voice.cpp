#include "voice.h"

#include <Arduino.h>

#include "capture.h"
#include "config.h"
#include "turn.h"
#include "ui.h"

static bool voiceMode = false;
static bool vSpeech = false;
static bool vAnyDip = false;                      // saw quiet during this turn
static uint32_t vLoudMs = 0, vSilenceMs = 0, vSpeechMs = 0;
static uint32_t vLastActMs = 0, vMuteUntilMs = 0, vDrainReads = 0;
static float vFloor = VOICE_FLOOR_SEED;           // room ambient (shared with idle)
static float vMean = 0.0f;                        // DC blocker state (shared)
static int16_t vPreRoll[VOICE_PREROLL_MS * SAMPLE_RATE / 1000];
static size_t vPreIdx = 0, vPreCount = 0;
static const size_t PREROLL_CAP = sizeof(vPreRoll) / sizeof(vPreRoll[0]);

bool voiceOn() { return voiceMode; }
bool voiceHearing() { return vSpeech; }

// DC blocker in place (far off zero; decisions and captures use AC);
// returns the AC peak.
static int16_t removeDc(int16_t* pcm, size_t n) {
    int16_t peak = 0;
    for (size_t i = 0; i < n; i++) {
        vMean = vMean * 0.998f + (float)pcm[i] * 0.002f;
        const int16_t ac = (int16_t)constrain((int32_t)(pcm[i] - vMean), -32768, 32767);
        pcm[i] = ac;
        const int16_t a = ac < 0 ? -ac : ac;
        if (a > peak) peak = a;
    }
    return peak;
}

static void trackFloor(int16_t peak) {
    if ((float)peak < vFloor) vFloor = (float)peak;         // min-latch: instant
    else vFloor = min(VOICE_FLOOR_MAX, vFloor * 1.008f);    // slow creep up
}

// Idle ambient tracking: the main screen silently measures the room (local
// energy statistics only — nothing is captured or sent), so voice mode
// enters warm: no calibration pause, no status text on the READY screen.
void ambientTick() {
    if (!micInstalled() && !micInstall()) return;
    static int16_t tmp[READ_CHUNK / 2];
    const size_t br = micRead(tmp, READ_CHUNK, 20);
    if (!br) return;
    trackFloor(removeDc(tmp, br / 2));
}

void voiceResume() {
    if (!voiceMode) return;
    vMuteUntilMs = millis() + VOICE_SETTLE_MS;
    vDrainReads = 3;                              // toss stale DMA audio
    vLastActMs = millis();
    uiMsg("Voice - double-tap to exit", Ink::Accent);
}

void voiceSet(bool on) {
    if (on == voiceMode) return;
    voiceMode = on;
    vSpeech = false;
    vLoudMs = vSilenceMs = vSpeechMs = 0;
    vPreIdx = vPreCount = 0;
    vLastActMs = millis();
    vMuteUntilMs = 0;
    if (on) {
        if (!micInstall()) {
            voiceMode = false;
            uiMsg("Mic init failed", Ink::Err);
            return;
        }
        vDrainReads = 3;
        uiMsg("Voice mode on - double-tap to exit", Ink::Accent);
        Serial.println("[Voice] on");
    } else {
        captureStreamClose();
        micRemove();
        uiMsg("Voice mode off", Ink::Muted);
        Serial.println("[Voice] off");
    }
}

// Speech detected: open the turn with the pre-roll in front.
static void openTurn(int16_t peak) {
    vSpeech = true;
    vSpeechMs = 0;
    vSilenceMs = 0;
    captureReset(peak);
    uiMsg("Listening", Ink::Listen);
    captureStreamOpen();
    const size_t start = (vPreIdx + PREROLL_CAP - vPreCount) % PREROLL_CAP;  // oldest
    const size_t first = min(vPreCount, PREROLL_CAP - start);   // ring: two runs
    if (first) captureAppend(vPreRoll + start, first * 2, peak);
    if (vPreCount > first) captureAppend(vPreRoll, (vPreCount - first) * 2, peak);
    vPreIdx = vPreCount = 0;
}

static void closeTurn() {
    vSpeech = false;
    vAnyDip = false;
    vLoudMs = 0;
    turnBegin(true);                              // keep mic for the next turn
}

void voiceTick() {
    if (millis() <= vMuteUntilMs) return;
    static int16_t tmp[READ_CHUNK / 2];
    const size_t br = micRead(tmp, READ_CHUNK, 100);
    if (!br) return;
    if (vDrainReads) { vDrainReads--; return; }   // stale audio after playback
    const size_t n = br / 2;
    const int16_t peak = removeDc(tmp, n);
    const uint32_t durMs = (uint32_t)br * 1000UL / (SAMPLE_RATE * 2UL);
    const int32_t startTh = max((int32_t)VOICE_START_MIN,
                                (int32_t)(vFloor * VOICE_START_RATIO));
    const int32_t endTh = max((int32_t)VOICE_END_MIN,
                              (int32_t)(vFloor * VOICE_END_RATIO));
    if (peak >= endTh) vLastActMs = millis();

    if (!vSpeech) {
        for (size_t i = 0; i < n; i++) {          // rolling pre-roll ring
            vPreRoll[vPreIdx] = tmp[i];
            vPreIdx = (vPreIdx + 1) % PREROLL_CAP;
            if (vPreCount < PREROLL_CAP) vPreCount++;
        }
        vLoudMs = (peak >= startTh)                        // leaky integrator:
                    ? vLoudMs + durMs                       // dips between syllables
                    : (vLoudMs > durMs ? vLoudMs - durMs : 0);  // decay, not reset
        trackFloor(peak);
        static uint32_t vLastLogMs = 0;            // room tuning telemetry
        if (millis() - vLastLogMs > 1000) {
            vLastLogMs = millis();
            Serial.printf("[Voice] idle ac=%d mean=%d floor=%d th=%d/%d loud=%ums\n",
                          (int)peak, (int)vMean, (int)vFloor, (int)startTh,
                          (int)endTh, (unsigned)vLoudMs);
        }
        if (vLoudMs < VOICE_START_MS) {
            if (millis() - vLastActMs > VOICE_IDLE_EXIT_MS)
                voiceSet(false);                  // been quiet: leave voice mode
            return;
        }
        openTurn(peak);
        return;                                    // triggering chunk already in pre-roll
    }

    if (!captureAppend(tmp, br, peak)) {          // cap: close the turn now
        closeTurn();
        return;
    }
    vSpeechMs += durMs;
    if (peak < endTh) { vSilenceMs += durMs; vAnyDip = true; }
    else vSilenceMs = 0;
    static uint32_t vLastUiMs = 0;                  // interactive countdown
    if (millis() - vLastUiMs > 100) {
        vLastUiMs = millis();
        char vb[28];
        if (peak < endTh && vSpeechMs >= VOICE_MIN_SPEECH_MS)
            snprintf(vb, sizeof(vb), "Closing %us",
                     (unsigned)((VOICE_END_MS > vSilenceMs
                                 ? VOICE_END_MS - vSilenceMs : 0) + 999) / 1000);
        else
            snprintf(vb, sizeof(vb), "Listening %us",
                     (unsigned)((millis() - captureStartMs()) / 1000));
        uiMsg(vb, Ink::Listen);
    }
    if (vSpeechMs > VOICE_RECOVER_MS && !vAnyDip) {   // constant noise, not speech
        Serial.printf("[Voice] sustained noise: floor %d->%d, rejecting\n",
                      (int)vFloor, (int)peak);
        vFloor = min(VOICE_FLOOR_MAX, (float)peak);
        vSpeech = false;
        vAnyDip = false;
        vLoudMs = 0;
        captureReset(0);
        captureStreamClose();
        return;
    }
    if (vSpeechMs >= VOICE_MIN_SPEECH_MS && vSilenceMs >= VOICE_END_MS) {
        Serial.printf("[Voice] turn close speech=%ums sil=%ums peak=%d\n",
                      (unsigned)vSpeechMs, (unsigned)vSilenceMs, (int)peak);
        closeTurn();
    }
}
