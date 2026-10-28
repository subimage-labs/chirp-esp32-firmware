#include "keyboard.h"

#include "config.h"      // board.h: CHIRP_HAS_KEYBOARD and the board's pins

#if CHIRP_HAS_KEYBOARD
#include <Arduino.h>
#include <Wire.h>

#include "keymap.h"

// The C3 scans its matrix about every 35 ms and keeps only the last key, so
// a read every 10 ms never loses one.
static const uint32_t KBD_POLL_MS = 10;

static bool online = false;
static uint32_t lastPollMs = 0;
#ifdef BENCH_CMDS
static bool echo = false;
#endif

bool keyboardBegin() {
    Wire.begin(CHIRP_I2C_SDA, CHIRP_I2C_SCL, 400000);
    delay(50);                                  // the C3 needs a moment after rail-on
    Wire.requestFrom((uint8_t)KBD_I2C_ADDR, (uint8_t)1);
    online = Wire.available() > 0;
    while (Wire.available()) Wire.read();       // a key pressed during boot is dropped
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

#ifdef BENCH_CMDS
void keyboardEcho(bool on) { echo = on; }
bool keyboardEchoOn() { return echo; }
#endif

static int readKey() {
    Wire.requestFrom((uint8_t)KBD_I2C_ADDR, (uint8_t)1);
    if (!Wire.available()) return -1;
    return Wire.read();
}

bool keyboardPoll(KbdEvent& ev) {
    ev.kind = KbdEv::None;
    ev.c = 0;
    ev.raw = 0;
    if (!online) return false;
    const uint32_t now = millis();
    if (now - lastPollMs < KBD_POLL_MS) return false;
    lastPollMs = now;
    const int k = readKey();
    if (k < 0) return false;
    const uint8_t code = (uint8_t)k;
#ifdef BENCH_CMDS
    if (echo && code) Serial.printf("[Kbd] raw %02X\n", code);
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
    case KeyAct::Unknown:
        ev.kind = KbdEv::Raw;
        ev.raw = code;
        break;
    }
    return ev.kind != KbdEv::None;
}

#endif
