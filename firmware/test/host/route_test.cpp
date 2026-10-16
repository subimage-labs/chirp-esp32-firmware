// Host tests for the input router. Run: firmware/test/host/run.sh
#include "route.h"
#include "gesture.h"

#include <cstdio>
#include <initializer_list>

static int failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,   \
                         __LINE__, #cond);                                \
            failures++;                                                   \
        }                                                                 \
    } while (0)

static RouteCtx ctx(UiState s, bool voice = false, bool menu = false) {
    return RouteCtx{s, menu, voice, true, false, false, 500, 300};
}
static InputEvent ev(InputType t, uint32_t heldMs = 100) {
    return InputEvent{t, 1, 200, heldMs, false, false};
}
static InputEvent chord() {   // side tap that began with the encoder key down
    return InputEvent{InputType::SideRelease, 0, 0, 150, false, true};
}

// Actions that touch the capture (mic, recording buffer, STT stream).
static bool touchesCapture(Act a) {
    return a == Act::StartRecording || a == Act::StopSpeechRecord ||
           a == Act::EndRecording || a == Act::ToggleVoice ||
           a == Act::CancelRecordToggle || a == Act::StopSpeechToggle ||
           a == Act::OpenMenu;   // opening the menu turns voice off
}

// While the worker owns the capture, no event in any voice/menu
// combination may touch it.
static void thinkingNeverTouchesCapture() {
    const InputType all[] = {InputType::Wheel, InputType::EncPress, InputType::EncDouble,
                             InputType::EncRelease, InputType::SideHold, InputType::SideRelease};
    for (bool voice : {false, true})
        for (bool menu : {false, true}) {
            for (InputType t : all)
                CHECK(!touchesCapture(route(ev(t), ctx(UI_THINKING, voice, menu))));
            CHECK(!touchesCapture(route(chord(), ctx(UI_THINKING, voice, menu))));
        }
}

// Turning voice off by double-tap during STT is deferred, then applied once
// the turn leaves THINKING (speaking or idle).
static void voiceOffDuringSttIsDeferred() {
    CHECK(route(ev(InputType::EncDouble), ctx(UI_THINKING, true)) == Act::DeferVoice);
    CHECK(!voiceToggleDue(true, UI_THINKING));
    CHECK(voiceToggleDue(true, UI_SPEAKING));
    CHECK(voiceToggleDue(true, UI_IDLE));
    CHECK(!voiceToggleDue(false, UI_IDLE));
}

static void doubleTapPerState() {
    CHECK(route(ev(InputType::EncDouble), ctx(UI_IDLE)) == Act::ToggleVoice);
    CHECK(route(ev(InputType::EncDouble), ctx(UI_RECORDING)) == Act::CancelRecordToggle);
    CHECK(route(ev(InputType::EncDouble), ctx(UI_SPEAKING)) == Act::StopSpeechToggle);
    CHECK(route(ev(InputType::EncDouble), ctx(UI_IDLE, false, true)) == Act::None);
}

static void pushToTalk() {
    CHECK(route(ev(InputType::EncPress), ctx(UI_IDLE)) == Act::StartRecording);
    CHECK(route(ev(InputType::EncPress), ctx(UI_SPEAKING)) == Act::StopSpeechRecord);
    CHECK(route(ev(InputType::EncPress), ctx(UI_IDLE, true)) == Act::None);
    RouteCtx offline = ctx(UI_IDLE);
    offline.wifi = false;
    CHECK(route(ev(InputType::EncPress), offline) == Act::NoWifi);
    CHECK(route(ev(InputType::EncRelease, 900), ctx(UI_RECORDING)) == Act::EndRecording);
    CHECK(route(ev(InputType::EncRelease), ctx(UI_IDLE)) == Act::None);
}

// A tap-short press is never sent off as speech. The view flips on the chord
// (encoder held, side tap) in every state outside the menu; a plain side tap
// still opens the menu.
static void chordTogglesView() {
    CHECK(route(ev(InputType::EncRelease, 120), ctx(UI_RECORDING)) == Act::DiscardRecording);
    CHECK(route(ev(InputType::EncRelease, 299), ctx(UI_RECORDING)) == Act::DiscardRecording);
    CHECK(route(ev(InputType::EncRelease, 300), ctx(UI_RECORDING)) == Act::EndRecording);
    for (UiState s : {UI_IDLE, UI_RECORDING, UI_THINKING, UI_SPEAKING}) {
        CHECK(route(chord(), ctx(s)) == Act::ToggleView);
        CHECK(route(chord(), ctx(s, true)) == Act::ToggleView);          // voice mode
    }
    CHECK(route(chord(), ctx(UI_IDLE, false, true)) == Act::MenuNext);   // menu: the side key's own
    CHECK(route(ev(InputType::SideRelease, 150), ctx(UI_IDLE)) == Act::OpenMenu);
    RouteCtx vol = ctx(UI_IDLE);
    vol.holdActed = true;                                                 // held into volume mode
    CHECK(route(chord(), vol) == Act::ClearHold);
}

// The real decoder feeding the router, with a minimal model of what main.cpp
// does with the actions: key levels per 5 ms sample (the timer's rate).
struct Device {
    GestureDecoder g{30, 400, 500};
    UiState state = UI_IDLE;
    bool menu = false;
    int views = 0, sent = 0, opens = 0;
    uint32_t t = 1000;
    void step(bool enc, bool side, uint32_t ms) {
        for (uint32_t i = 0; i < ms; i += 5, t += 5) {
            GestureOut o;
            g.update(enc, side, t, o);
            for (int k = 0; k < o.n; k++) act(route(o.ev[k], RouteCtx{state, menu, false, true, false,
                                                                     false, 500, 300}));
        }
    }
    void act(Act a) {
        switch (a) {
        case Act::StartRecording: state = UI_RECORDING; break;
        case Act::EndRecording: sent++; state = UI_THINKING; break;
        case Act::DiscardRecording: state = UI_IDLE; break;
        case Act::ToggleView: if (state == UI_RECORDING) state = UI_IDLE; views++; break;
        case Act::OpenMenu: opens++; break;
        default: break;
        }
    }
};

// The chord after a dial hold well past the tap limit flips the view once and
// never sends the recording, whichever key comes up first, or both at once.
static void chordReleaseOrders() {
    Device sideFirst;
    sideFirst.step(true, false, 400);
    sideFirst.step(true, true, 120);
    sideFirst.step(true, false, 200);
    sideFirst.step(false, false, 100);
    CHECK(sideFirst.views == 1 && sideFirst.sent == 0 && sideFirst.opens == 0);
    CHECK(sideFirst.state == UI_IDLE);

    Device dialFirst;
    dialFirst.step(true, false, 400);
    dialFirst.step(true, true, 100);
    dialFirst.step(false, true, 150);              // dial up while the side key is still down
    dialFirst.step(false, false, 100);
    CHECK(dialFirst.views == 1 && dialFirst.sent == 0 && dialFirst.opens == 0);
    CHECK(dialFirst.state == UI_IDLE);

    Device together;
    together.step(true, false, 400);
    together.step(true, true, 150);
    together.step(false, false, 100);              // both up in the same sample
    CHECK(together.views == 1 && together.sent == 0 && together.opens == 0);
    CHECK(together.state == UI_IDLE);

    Device talk;                                   // a plain hold still asks
    talk.step(true, false, 600);
    talk.step(false, false, 100);
    CHECK(talk.sent == 1 && talk.views == 0);

    Device dbl;                                    // and a double-tap after a chord is not one
    dbl.step(true, false, 100);
    dbl.step(true, true, 60);
    dbl.step(false, false, 60);
    dbl.step(true, false, 60);
    dbl.step(false, false, 100);
    CHECK(dbl.views == 1);
}

static void menuKeys() {
    const RouteCtx m = ctx(UI_IDLE, false, true);
    CHECK(route(ev(InputType::EncPress), m) == Act::CancelDouble);
    CHECK(route(ev(InputType::EncRelease), m) == Act::MenuActivate);
    CHECK(route(ev(InputType::SideHold), m) == Act::None);       // no volume in menu
    CHECK(route(ev(InputType::SideRelease, 900), m) == Act::MenuNext);  // even long
    CHECK(route(ev(InputType::Wheel), m) == Act::MenuMove);
}

static void sideKey() {
    CHECK(route(ev(InputType::SideRelease, 200), ctx(UI_IDLE)) == Act::OpenMenu);
    CHECK(route(ev(InputType::SideRelease, 200), ctx(UI_THINKING)) == Act::StopTurn);
    CHECK(route(ev(InputType::SideRelease, 200), ctx(UI_THINKING, true)) == Act::StopTurn);
    CHECK(route(ev(InputType::SideRelease, 600), ctx(UI_THINKING)) == Act::None);  // after a hold
    CHECK(route(ev(InputType::SideRelease, 600), ctx(UI_IDLE)) == Act::None);
    CHECK(route(ev(InputType::SideHold), ctx(UI_IDLE)) == Act::VolumeMode);
    RouteCtx acted = ctx(UI_IDLE);
    acted.holdActed = true;
    CHECK(route(ev(InputType::SideRelease, 900), acted) == Act::ClearHold);
}

static void wheel() {
    CHECK(route(ev(InputType::Wheel), ctx(UI_IDLE)) == Act::Scroll);
    RouteCtx vol = ctx(UI_SPEAKING);
    vol.volumeActive = true;
    CHECK(route(ev(InputType::Wheel), vol) == Act::Volume);
}

int main() {
    thinkingNeverTouchesCapture();
    voiceOffDuringSttIsDeferred();
    doubleTapPerState();
    pushToTalk();
    chordTogglesView();
    chordReleaseOrders();
    menuKeys();
    sideKey();
    wheel();
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("route: all checks passed\n");
    return 0;
}
