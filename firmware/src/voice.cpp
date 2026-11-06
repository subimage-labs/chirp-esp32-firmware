#include "voice.h"

#include <Arduino.h>

#include "capture.h"
#include "config.h"
#include "turn.h"
#include "ui.h"
#include "voice_detect.h"

static bool voiceMode = false;
static VoiceDetector det;                         // DC blocker, floor, detector, pre-roll
static uint32_t vLastActMs = 0, vMuteUntilMs = 0, vDrainReads = 0;

bool voiceOn() { return voiceMode; }
bool voiceHearing() { return det.hearing(); }

// Idle ambient tracking: the main screen silently measures the room (local
// energy statistics only — nothing is captured or sent), so voice mode
// enters warm: no calibration pause, no status text on the READY screen.
void ambientTick() {
    if (!micInstalled() && !micInstall()) return;
    static int16_t tmp[READ_CHUNK / 2];
    const size_t br = micRead(tmp, READ_CHUNK, 20);
    if (!br) return;
    det.ambient(tmp, br / 2);
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
    det.reset();
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
static void openTurn(const VoiceDetector::Result& r) {
    captureReset(r.peak);
    uiMsg("Listening", Ink::Listen);
    captureStreamOpen();
    for (const VoiceDetector::Span& s : r.preroll)         // ring: two runs, oldest first
        if (s.n) captureAppend(s.data, s.n * 2, r.peak);
}

void voiceTick() {
    if (millis() <= vMuteUntilMs) return;
    static int16_t tmp[READ_CHUNK / 2];
    const size_t br = micRead(tmp, READ_CHUNK, 100);
    if (!br) return;
    if (vDrainReads) { vDrainReads--; return; }   // stale audio after playback
    const size_t n = br / 2;
    const bool wasHearing = det.hearing();
    const float floorBefore = det.floor();        // the floor is frozen while hearing
    const VoiceDetector::Result r =
        det.feed(tmp, n, [](const int16_t* p, size_t samples, int16_t pk) {
            return captureAppend(p, samples * 2, pk);
        });
    if (r.active) vLastActMs = millis();

    if (!wasHearing) {
        static uint32_t vLastLogMs = 0;            // room tuning telemetry
        if (millis() - vLastLogMs > 1000) {
            vLastLogMs = millis();
            Serial.printf("[Voice] idle ac=%d mean=%d floor=%d th=%d/%d loud=%ums\n",
                          (int)r.peak, (int)det.mean(), (int)det.floor(), (int)r.startTh,
                          (int)r.endTh, (unsigned)det.loudMs());
        }
        if (r.event == VoiceDetector::Event::None) {
            if (millis() - vLastActMs > VOICE_IDLE_EXIT_MS)
                voiceSet(false);                  // been quiet: leave voice mode
            return;
        }
        openTurn(r);                               // triggering chunk already in pre-roll
        return;
    }

    if (r.event == VoiceDetector::Event::CapClose) {   // cap: close the turn now
        turnBegin(true);                          // keep mic for the next turn
        return;
    }
    static uint32_t vLastUiMs = 0;                  // interactive countdown
    if (millis() - vLastUiMs > 100) {
        vLastUiMs = millis();
        char vb[28];
        if (r.peak < r.endTh && det.speechMs() >= VOICE_MIN_SPEECH_MS)
            snprintf(vb, sizeof(vb), "Closing %us",
                     (unsigned)((VOICE_END_MS > det.silenceMs()
                                 ? VOICE_END_MS - det.silenceMs() : 0) + 999) / 1000);
        else
            snprintf(vb, sizeof(vb), "Listening %us",
                     (unsigned)((millis() - captureStartMs()) / 1000));
        uiMsg(vb, Ink::Listen);
    }
    if (r.event == VoiceDetector::Event::Reject) {    // constant noise, not speech
        Serial.printf("[Voice] sustained noise: floor %d->%d, rejecting\n",
                      (int)floorBefore, (int)r.peak);
        captureReset(0);
        captureStreamClose();
        return;
    }
    if (r.event == VoiceDetector::Event::Close) {
        Serial.printf("[Voice] turn close speech=%ums sil=%ums peak=%d\n",
                      (unsigned)det.speechMs(), (unsigned)det.silenceMs(), (int)r.peak);
        turnBegin(true);                          // keep mic for the next turn
    }
}
