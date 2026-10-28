#include "keyboard.h"

#include "config.h"      // board.h: CHIRP_HAS_KEYBOARD and the board's pins

#if CHIRP_HAS_KEYBOARD
#include <Arduino.h>
#include <Wire.h>

#include "keymap.h"

static bool online = false;
static volatile bool encDown = false;    // space held (input.cpp samples)
static volatile bool sideDown = false;   // sym held
#ifdef BENCH_CMDS
static bool echo = false;
#endif

bool keyboardBegin() {
    pinMode(KBD_INT_PIN, INPUT_PULLUP);
    Wire.begin(CHIRP_I2C_SDA, CHIRP_I2C_SCL, 400000);
    delay(50);                                  // the C3 needs a moment after rail-on
    Wire.requestFrom((uint8_t)KBD_I2C_ADDR, (uint8_t)1);
    online = Wire.available() > 0;
    if (online) keyboardSetBacklight(96);
    Serial.printf("[Kbd] %s\n", online ? "ok" : "not online");
    return online;
}

bool keyboardOnline() { return online; }

void keyboardSetBacklight(uint8_t v) {
    Wire.beginTransmission(KBD_I2C_ADDR);
    Wire.write(0x01);                           // LILYGO_KB_BRIGHTNESS_CMD
    Wire.write(v);
    Wire.endTransmission();
}

bool keyboardEncDown() { return encDown; }
bool keyboardSideDown() { return sideDown; }

#ifdef BENCH_CMDS
void keyboardEcho(bool on) { echo = on; }
bool keyboardEchoOn() { return echo; }
#endif

// One byte per transaction: the C3's firmware queues one keycode per read
// and answers 0x00 when the queue is empty. INT low means a queue exists.
static int readKey() {
    if (digitalRead(KBD_INT_PIN) == HIGH) return -1;
    Wire.requestFrom((uint8_t)KBD_I2C_ADDR, (uint8_t)1);
    if (!Wire.available()) return -1;
    return Wire.read();
}

bool keyboardPoll(KbdEvent& ev) {
    ev.kind = KbdEv::None;
    ev.c = 0;
    ev.raw = 0;
    if (!online) return false;
    const int k = readKey();
    if (k <= 0) {
        // The C3 queues presses only: an idle queue while space is held
        // means the key went up (there are no release codes).
        if (encDown && digitalRead(KBD_INT_PIN) == HIGH) encDown = false;
        return false;
    }
    const uint8_t code = (uint8_t)k;
#ifdef BENCH_CMDS
    if (echo) Serial.printf("[Kbd] raw %02X\n", code);
#endif
    switch (keyAct(code)) {
    case KeyAct::None:
        break;
    case KeyAct::Print:
        ev.kind = KbdEv::Char;
        ev.c = (char)code;
        break;
    case KeyAct::Backspace:
        ev.kind = KbdEv::Backspace;
        break;
    case KeyAct::Enter:
        ev.kind = KbdEv::Enter;
        break;
    case KeyAct::Space:
        encDown = true;
        break;
    case KeyAct::SymDown:
        sideDown = true;
        break;
    case KeyAct::SymUp:
        sideDown = false;
        break;
    case KeyAct::Shift:
    case KeyAct::Alt:
        break;                                  // nothing yet; shift applies in the C3
    case KeyAct::Unknown:
        ev.kind = KbdEv::Raw;
        ev.raw = code;
        break;
    }
    return ev.kind != KbdEv::None;
}

#endif

