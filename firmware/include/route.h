// route.h — what an input event means in the current mode. Pure (no
// Arduino), host-tested; main.cpp executes the returned action.
//
// Ownership rule enforced here: while the turn worker owns the capture
// (UI_THINKING), no event may touch it — recording, the STT upload stream
// or the mic. A voice-mode toggle there is deferred (Act::DeferVoice) and
// applied by voiceToggleDue() once the worker has handed the turn back.
#pragma once

#include <stdint.h>

#include "app_state.h"
#include "input.h"

enum class Act : uint8_t {
    None,
    CancelDouble,        // menu click: the press must not pair into a double
    NoWifi,              // can't start a turn
    StartRecording,
    StopSpeechRecord,    // interrupt the answer and talk
    EndRecording,        // release: hand the capture to the worker
    DiscardRecording,    // release of a tap-short press: drop it silently
    ToggleView,          // encoder held + side tap: character view <-> chat view
    ToggleVoice,
    CancelRecordToggle,  // double-tap after the first press began recording
    StopSpeechToggle,
    DeferVoice,          // worker owns the capture: toggle after the turn
    MenuActivate,
    MenuNext,            // side tap in the menu: next tab, then back to chat
    OpenMenu,
    StopTurn,            // side tap while the worker runs: end the turn
    VolumeMode,
    ClearHold,           // side release after a hold that acted
    MenuMove,
    Volume,
    Scroll,
};

struct RouteCtx {
    UiState state;
    bool menu;
    bool voice;
    bool wifi;
    bool volumeActive;   // side-hold volume window still open
    bool holdActed;      // this side press's hold armed volume mode
    uint32_t holdMs;     // BTN_HOLD_MS
    uint32_t tapMs;      // BTN_TAP_MS: an encoder press shorter than this is a tap
};

inline Act route(const InputEvent& ev, const RouteCtx& c) {
    switch (ev.type) {
    case InputType::Wheel:
        if (c.menu) return Act::MenuMove;
        return c.volumeActive ? Act::Volume : Act::Scroll;
    case InputType::EncPress:                       // push-to-talk starts instantly
        if (c.menu) return Act::CancelDouble;       // click resolves on release
        if (c.voice) return Act::None;
        if (c.state == UI_IDLE) return c.wifi ? Act::StartRecording : Act::NoWifi;
        if (c.state == UI_SPEAKING) return Act::StopSpeechRecord;
        return Act::None;
    case InputType::EncDouble:                      // voice mode toggle
        if (c.menu) return Act::None;
        if (c.state == UI_THINKING) return Act::DeferVoice;
        if (c.state == UI_RECORDING) return Act::CancelRecordToggle;
        if (c.state == UI_SPEAKING) return Act::StopSpeechToggle;
        return Act::ToggleVoice;
    case InputType::EncRelease:
        if (ev.chord)                               // the chord's press: never speech, never a click
            return c.state == UI_RECORDING ? Act::DiscardRecording : Act::None;
        if (c.menu) return Act::MenuActivate;
        if (c.state != UI_RECORDING) return Act::None;
        // Too short to be speech: the first half of a tap or a double-tap.
        return ev.heldMs < c.tapMs ? Act::DiscardRecording : Act::EndRecording;
    case InputType::SideHold:
        return c.menu ? Act::None : Act::VolumeMode;
    case InputType::SideRelease:
        if (c.holdActed) return Act::ClearHold;
        if (c.menu) return Act::MenuNext;           // Chats → Theme → Setup → chat
        if (ev.chord) return Act::ToggleView;       // the chord, in any state
        if (ev.heldMs >= c.holdMs + 60) return Act::None;
        return c.state == UI_THINKING ? Act::StopTurn : Act::OpenMenu;
    }
    return Act::None;
}

// A deferred voice toggle applies once the worker no longer owns the capture.
inline bool voiceToggleDue(bool pending, UiState state) {
    return pending && state != UI_THINKING;
}
