// Chirp — voice terminal for a Hermes agent on the T-Embed CC1101 Plus
// (ESP32-S3).
//
// Hold the encoder key to talk (push-to-talk), double-tap it for hands-free
// voice mode; the turn runs mic → shim STT → Hermes (the paired profile) → shim
// TTS → speaker. Side key: tap = Chats → Theme → Setup → back to the chat,
// hold = volume. Wheel: scroll the transcript / select in the menu / volume.
//
// Character mode (Setup tab) swaps the transcript for an animated avatar of
// the agent and its newest answer; the wheel then scrolls that answer.
//
// This file: setup, the loop, the input dispatcher (what each gesture means
// in each mode) and push-to-talk capture. Module map and execution contexts:
// app.h.

#include <Arduino.h>
#include <LittleFS.h>
#include <WiFi.h>

#include "app.h"
#include "capture.h"
#include "character.h"
#include "config.h"
#include "console.h"
#include "hermes.h"
#include "input.h"
#include "logo_draw.h"
#include "menu.h"
#include "ota.h"
#include "portal.h"
#include "power.h"
#include "route.h"
#include "selftest.h"
#include "settings.h"
#include "speech.h"
#include "turn.h"
#include "ui.h"
#include "voice.h"
#if CHIRP_HAS_KEYBOARD
#include "keyboard.h"
#include "keymap.h"
#endif

UiState uiState = UI_BOOT;

static bool captureShown() { return millis() - captureStartMs() >= RECORD_SHOW_MS; }
bool appListeningShown() { return (uiState == UI_RECORDING || voiceHearing()) && captureShown(); }
bool appVoiceWaiting() {
    return voiceOn() && uiState == UI_IDLE && !(voiceHearing() && captureShown());
}

static uint32_t wifiRetryMs = 0;
static bool volMode = false;                      // side-key hold: wheel = volume
static uint32_t volModeUntilMs = 0;
static bool sideHoldActed = false;                // this press's hold armed volume

// Wheel speed → transcript rows per click.
static const int SCROLL_ROWS_SLOW = 4;            // deliberate clicks
static const int SCROLL_ROWS_MED = 8;             // ~one screen per click
static const int SCROLL_ROWS_FAST = 16;           // flick through long chats
static const uint32_t WHEEL_MED_MS = 90;          // ms per click below this: medium
static const uint32_t WHEEL_FAST_MS = 40;         // ms per click below this: fast

void logHeap(const char* tag) {
    const uint32_t ic = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    Serial.printf("[Heap] %s int free=%u largest=%u min=%u psram free=%u largest=%u\n",
                  tag, (unsigned)heap_caps_get_free_size(ic),
                  (unsigned)heap_caps_get_largest_free_block(ic),
                  (unsigned)heap_caps_get_minimum_free_size(ic),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
}

void appRedraw() {
    uiClear();
    if (menuOpen()) menuRender();
    else transcriptRender();
}

// ---------------------------------------------------------------- push-to-talk
// The mic starts on the press (no first syllable lost), but nothing shows
// until the press is RECORD_SHOW_MS old: the first tap of a double-tap, or
// the dial press of the view chord, never flashes the listening screen.
static bool recShown = false;

static void startRecording() {
    speechStop();                     // an idle sound (error tone) must not be recorded
    if (!micInstall()) {
        uiMsg("Mic init failed", Ink::Err);
        return;
    }
    captureReset(0);
    captureStreamOpen();
    recShown = false;
    uiState = UI_RECORDING;
}

static void showRecording() {
    recShown = true;
    transcriptFollow();               // transcript stays; countdown in the status line
    uiMsg("Listening 0s", Ink::Listen);
}

static void dropRecording() {
    captureStreamClose();
    micRemove();
    captureReset(0);
    uiState = UI_IDLE;
}

static void cancelRecording() {
    dropRecording();
    uiMsg("Cancelled", Ink::Muted);
}

static void recordingTick() {
    static int16_t tmp[READ_CHUNK / 2];
    const size_t br = micRead(tmp, READ_CHUNK, 100);
    if (br) {
        int16_t peak = 0;
        for (size_t i = 0; i < br / 2; i++) {
            const int16_t a = tmp[i] < 0 ? -tmp[i] : tmp[i];
            if (a > peak) peak = a;
        }
        if (!captureAppend(tmp, br, peak)) {      // cap reached: treat as release
            turnBegin(false);
            return;
        }
    }
    const uint32_t now = millis();
    if (!recShown) {
        if (captureShown()) showRecording();
        return;
    }
    static uint32_t lastUi = 0;
    if (now - lastUi > 500) {
        lastUi = now;
        char buf[24];
        snprintf(buf, sizeof(buf), "Listening %us",
                 (unsigned)((now - captureStartMs()) / 1000));
        uiMsg(buf, Ink::Listen);
    }
}

// ---------------------------------------------------------------- dispatch
static void showVolume() {
    char b[12];
    snprintf(b, sizeof(b), "Vol %u", speechVolume());
    uiNotice(b, Ink::Accent);
}

// No live capture, playback or voice listening under the menu.
static void openMenu() {
    if (uiState == UI_RECORDING) cancelRecording();
    if (uiState == UI_SPEAKING) uiState = UI_IDLE;
    speechStop();                     // speech, or an idle error tone
    if (voiceOn()) voiceSet(false);
    menuEnter();
}

static bool voiceTogglePending = false;          // double-tap while the worker owned capture

static void doAct(Act a, const InputEvent& ev) {
    switch (a) {
    case Act::None:
        break;
    case Act::CancelDouble:
        inputCancelDouble();
        break;
    case Act::NoWifi:
        uiMsg("No WiFi", Ink::Err);
        break;
    case Act::StartRecording:
        startRecording();
        break;
    case Act::StopSpeechRecord:
        speechStop();
        startRecording();
        break;
    case Act::EndRecording:
        turnBegin(false);
        break;
    case Act::DiscardRecording: {                   // a tap, not speech
        const bool shown = recShown;
        dropRecording();
        if (shown) uiMsg(transcriptEmpty() ? "" : "Ready", Ink::Ok);
        break;
    }
    case Act::ToggleView: {                         // the chord's encoder press was not speech
        const bool dropped = uiState == UI_RECORDING;
        if (dropped) dropRecording();
        characterSetOn(!characterOn());
        appRedraw();
        if (dropped) uiMsg(transcriptEmpty() ? "" : "Ready", Ink::Ok);
        break;
    }
    case Act::ToggleVoice:
        voiceSet(!voiceOn());
        break;
    case Act::CancelRecordToggle:
        cancelRecording();
        voiceSet(!voiceOn());
        break;
    case Act::StopSpeechToggle:
        speechStop();
        voiceSet(!voiceOn());
        break;
    case Act::DeferVoice:
        voiceTogglePending = !voiceTogglePending;   // a second double-tap undoes it
        Serial.printf("[Voice] toggle deferred until the turn ends (%d)\n",
                      (int)voiceTogglePending);
        break;
    case Act::MenuActivate:
        menuActivate();
        break;
    case Act::MenuNext:
        menuNext();
        break;
    case Act::OpenMenu:
        openMenu();
        break;
    case Act::StopTurn:
        turnStop();
        break;
    case Act::VolumeMode:
        sideHoldActed = true;
        volMode = true;
        volModeUntilMs = millis() + 1500;
        showVolume();
        break;
    case Act::ClearHold:
        sideHoldActed = false;
        break;
    case Act::MenuMove:
        menuMove(ev.clicks, ev.perClickMs);
        break;
    case Act::Volume:
        speechSetVolume((uint8_t)constrain((int)speechVolume() - (int)ev.clicks, 0,
                                           (int)MAX_VOLUME));
        volModeUntilMs = millis() + 1500;
        showVolume();
        break;
    case Act::Scroll: {
        volMode = false;
        const int rows = ev.perClickMs < WHEEL_FAST_MS  ? SCROLL_ROWS_FAST
                         : ev.perClickMs < WHEEL_MED_MS ? SCROLL_ROWS_MED
                                                        : SCROLL_ROWS_SLOW;
        transcriptScroll((int)ev.clicks * rows);
        break;
    }
    }
}

static void dispatch(const InputEvent& ev) {
    const RouteCtx ctx{uiState,
                       menuOpen(),
                       voiceOn(),
                       WiFi.status() == WL_CONNECTED,
                       volMode && millis() < volModeUntilMs,
                       sideHoldActed,
                       BTN_HOLD_MS,
                       BTN_TAP_MS};
    doAct(route(ev, ctx), ev);
}

// A voice toggle deferred during THINKING applies once the worker has handed
// the capture back (SPEAKING or IDLE).
static void deferredVoiceTick() {
    if (!voiceToggleDue(voiceTogglePending, uiState)) return;
    voiceTogglePending = false;
    voiceSet(!voiceOn());
}

// Volume mode times out on its own, independent of wheel activity.
static void volModeTick() {
    if (volMode && millis() > volModeUntilMs) {
        volMode = false;
        uiMsg(uiState == UI_SPEAKING ? "" : "Ready", Ink::Ok);   // speaking: the chat's wave row
    }
}

// ---------------------------------------------------------------- arduino
void setup() {
    Serial.setRxBufferSize(2048);                  // a whole provisioning line (console.h)
    Serial.begin(115200);
    delay(200);
    Serial.printf("[Board] %s\n", CHIRP_BOARD_NAME);   // which image this is (board.h)
    const bool wokeByKey = powerBoot();
    uiBegin();
    powerBegin();
    uiClear();
    logoSplash();                                  // the boot messages show under it
    uiNotice("Booting", Ink::Strong);              // notices: shown under the splash in either view

    settingsLoad();
    if (portalRequested() || !settingsReady()) portalRun();   // never returns
    characterBegin();                              // the saved view, this agent's character
    characterPaint();                              // that view boots straight to the character
    uiPanelHold();                                 // loop() holds it from here on (ui.cpp)

    if (!captureBegin()) {
        uiMsg("PSRAM alloc failed", Ink::Err);
        while (true) delay(1000);
    }
    if (!LittleFS.begin(true)) {
        Serial.println("[FS] LittleFS mount failed");
        uiMsg("LittleFS failed", Ink::Err);
    }
    speechBegin();
    hermesSessionRestore();
    turnTaskStart();
#if CHIRP_HAS_KEYBOARD
    keyboardBegin();                               // the T-Deck's keyboard (I2C)
#endif
    inputBegin();
    if (wokeByKey) inputSwallowKeys();             // the waking press only wakes

    settingsWifiBegin();
    uiNotice("Joining WiFi", Ink::Strong);
    const uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_TIMEOUT_MS) {
        delay(250);
        uiPanelHold();
        Serial.print('.');
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[WiFi] %s (rssi %d)\n",
                      WiFi.localIP().toString().c_str(), WiFi.RSSI());
        otaMarkHealthy();                          // this image boots and networks
        uiNotice("Checking Hermes", Ink::Strong);
        const char* problem = selfTestRun();
        uiMsg(problem ? problem : "", problem ? Ink::Err : Ink::Ok);
        uiPanelHold();
        if (problem) speechErrorTone();
        if (!problem) otaBootCheck();               // restarts into a new image
    } else {
        Serial.println("[WiFi] join failed, retrying in background");
        selfTestRun();                             // logs the result for host/pair.py
        uiMsg(CHIRP_WIFI_HELP, Ink::Err);
        speechErrorTone();
    }
    transcriptRender();                            // boot into the READY screen
    logHeap("boot");
    uiState = UI_IDLE;
}

// The chat view's live row: who has the floor, and that side's level. The
// playback level is read only while the chat is up: the character view reads
// it (the peak since its last read) for its own wave and mouth.
static void liveRowTick() {
    const bool shown = !menuOpen();
    const bool chat = shown && !characterOn();
    const bool answering = uiState == UI_SPEAKING || (uiState == UI_THINKING && !turnTranscribing());
    if (appListeningShown()) transcriptLive(LiveRow::Bars, captureLevel(), shown);
    else if (answering && speechSpeaking()) transcriptLive(LiveRow::Wave, chat ? speechLevel() : 0, shown);
    else if (appVoiceWaiting()) transcriptLive(LiveRow::Dots, 0, shown);
    else transcriptLive(LiveRow::None, 0, shown);
}

// ---------------------------------------------------------------- typed chat
// The compose line (keyboard boards): printable keys append, backspace
// edits, enter sends the line as a turn through the same path bench `ask`
// uses (STT skipped). Space and ?123 never reach here: they hold the
// encoder / side key roles and are sampled by input.cpp.
#if CHIRP_COMPOSE
static ComposeBuffer compose;
static void composeSync() { uiComposeSet(compose.text()); }
#endif

#if CHIRP_HAS_KEYBOARD
static void keyboardTick() {
    KbdEvent ev;
    while (keyboardPoll(ev)) {
        switch (ev.kind) {
        case KbdEv::Char:
            if (uiState == UI_IDLE && !menuOpen()) {
                compose.put(ev.c);
                composeSync();
            }
            break;
        case KbdEv::Backspace:                    // in the menu: straight back to chat
            if (menuOpen()) menuClose();
            else if (compose.backspace()) composeSync();
            break;
        case KbdEv::Enter:                        // in the menu: pick the row
            if (menuOpen()) {
                menuActivate();
            } else if (!compose.empty() && uiState == UI_IDLE && WiFi.status() == WL_CONNECTED) {
                turnAskText(String(compose.text()));
                compose.clear();
                composeSync();
            }
            break;
        default:
            break;
        }
    }
}
#endif

void loop() {
    consolePoll(true);
    powerTick();
#if CHIRP_HAS_KEYBOARD
    keyboardTick();                               // drain the T-Keyboard: compose, send, key levels
#endif
    InputEvent ev;
    while (inputPoll(ev)) dispatch(ev);
    volModeTick();
    menuTick();
    speechPreviewTick();                           // a voice preview (Setup tab), menu open or not
    uiTick();                                      // scrolls a status text that doesn't fit

    if (uiState == UI_RECORDING) recordingTick();
    if (uiState == UI_IDLE && !menuOpen()) {
        if (speechPlaying()) speechPump();         // error tone; the mic waits for it
        else if (voiceOn()) voiceTick();
        else ambientTick();                        // silently warm the noise floor
    }
    turnTick();
    characterTick();
    liveRowTick();
    deferredVoiceTick();

    if (WiFi.status() != WL_CONNECTED &&
        uiState == UI_IDLE && millis() - wifiRetryMs > WIFI_RETRY_MS) {
        wifiRetryMs = millis();
        WiFi.reconnect();
    }
    uiPanelHold();                                 // after this pass's drawing (ui.cpp)
    delay(2);
}
