#include "keyboard.h"

#include "config.h"      // board.h: CHIRP_HAS_KEYBOARD and the board's pins

#if CHIRP_HAS_KEYBOARD
#include <Arduino.h>
#include <Wire.h>

#include <atomic>

#include "keymap.h"

// The C3 rescans its matrix about every 35 ms; a read every 10 ms sees every
// press and keeps the mic key's level close to the finger.
static const uint32_t KBD_POLL_MS = 10;
static const uint8_t KBD_CMD_BRIGHTNESS = 0x01;   // LILYGO_KB_BRIGHTNESS_CMD
static const uint8_t KBD_CMD_RAW = 0x03;          // LILYGO_KB_MODE_RAW_CMD

static bool online = false;
static uint32_t lastPollMs = 0;
static keymap::Decoder decoder;
static std::atomic<bool> micDown{false};          // loop writes, the key sampler reads
static KbdEvent pending[8];                       // decoded, not yet taken by keyboardPoll
static int pendingHead = 0, pendingLen = 0;
#ifdef BENCH_CMDS
static bool echo = false;
#endif

static void command(uint8_t cmd, int arg = -1) {
    Wire.beginTransmission(KBD_I2C_ADDR);
    Wire.write(cmd);
    if (arg >= 0) Wire.write((uint8_t)arg);
    Wire.endTransmission();
}

bool keyboardBegin() {
    Wire.begin(CHIRP_I2C_SDA, CHIRP_I2C_SCL, 400000);
    delay(50);                                  // the C3 needs a moment after rail-on
    Wire.requestFrom((uint8_t)KBD_I2C_ADDR, (uint8_t)1);
    online = Wire.available() > 0;
    while (Wire.available()) Wire.read();
    if (online) {
        command(KBD_CMD_RAW);
        keyboardSetBacklight(96);
    }
    Serial.printf("[Kbd] %s\n", online ? "ok" : "not online");
    return online;
}

bool keyboardOnline() { return online; }

void keyboardSetBacklight(uint8_t v) { command(KBD_CMD_BRIGHTNESS, v); }

bool keyboardMicDown() { return micDown.load(std::memory_order_relaxed); }

#ifdef BENCH_CMDS
void keyboardEcho(bool on) { echo = on; }
bool keyboardEchoOn() { return echo; }
#endif

static void queue(const KbdEvent& e) {
    if (pendingLen == (int)(sizeof pending / sizeof pending[0])) return;   // 8 keys in 10 ms: drop
    pending[(pendingHead + pendingLen) % (sizeof pending / sizeof pending[0])] = e;
    pendingLen++;
}

static void pollMatrix() {
    keymap::Matrix m;
    Wire.requestFrom((uint8_t)KBD_I2C_ADDR, (uint8_t)keymap::COLS);
    if (Wire.available() < keymap::COLS) {
        while (Wire.available()) Wire.read();
        return;                                 // a short read: keep the last state
    }
    for (int c = 0; c < keymap::COLS; c++) m.col[c] = (uint8_t)Wire.read();
#ifdef BENCH_CMDS
    static keymap::Matrix shown;
    if (echo && memcmp(m.col, shown.col, sizeof m.col)) {
        shown = m;
        Serial.printf("[Kbd] raw %02X %02X %02X %02X %02X\n", m.col[0], m.col[1], m.col[2],
                      m.col[3], m.col[4]);
    }
#endif
    decoder.feed(m, [](const keymap::KeyEvent& k) {
        KbdEvent e;
        e.kind = k.key == keymap::Key::Char        ? KbdEv::Char
                 : k.key == keymap::Key::Backspace ? KbdEv::Backspace
                                                   : KbdEv::Enter;
        e.c = k.c;
        queue(e);
    });
    micDown.store(decoder.micDown(), std::memory_order_relaxed);
}

bool keyboardPoll(KbdEvent& ev) {
    if (!online) return false;
    const uint32_t now = millis();
    if (!pendingLen && now - lastPollMs >= KBD_POLL_MS) {
        lastPollMs = now;
        pollMatrix();
    }
    if (!pendingLen) return false;
    ev = pending[pendingHead];
    pendingHead = (pendingHead + 1) % (int)(sizeof pending / sizeof pending[0]);
    pendingLen--;
    return true;
}

#endif
