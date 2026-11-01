#include "keyboard.h"

#include "config.h"      // board.h: CHIRP_HAS_KEYBOARD and the board's pins

#if CHIRP_HAS_KEYBOARD
#include <Arduino.h>
#include <Wire.h>
#include <freertos/queue.h>

#include <atomic>

#include "i2c_bus.h"
#include "keymap.h"

// The C3 rescans its matrix about every 35 ms; a read every 10 ms sees every
// press. The reads run on their own task: the mic key's level must follow the
// finger while the loop is busy (starting the mic, drawing), or a tap reads
// as a hold and a double-tap is lost.
static const uint32_t KBD_POLL_MS = 10;
static const uint8_t KBD_CMD_BRIGHTNESS = 0x01;   // LILYGO_KB_BRIGHTNESS_CMD
static const uint8_t KBD_CMD_RAW = 0x03;          // LILYGO_KB_MODE_RAW_CMD
static const uint32_t KBD_BOOT_MS = 50;           // the C3 needs this long after rail-on
static const int KBD_PROBE_TRIES = 10;            // keyboardReinit waits at most
static const uint32_t KBD_PROBE_GAP_MS = 25;      //   BOOT + TRIES x GAP for the C3's answer
static const uint8_t KBD_BACKLIGHT_DEFAULT = 96;

static bool online = false;                       // set in keyboardBegin, before the task exists
static uint8_t backlight = KBD_BACKLIGHT_DEFAULT; // loop: the level the C3 gets after a re-init
// decoder and typed's sender are owned by the kbd task, but only inside
// pollMatrix's I2cBusLock: keyboardReinit takes the same lock to reset them.
static keymap::Decoder decoder;
static std::atomic<bool> micDown{false};          // kbd task writes, the key sampler reads
static QueueHandle_t typed = nullptr;             // KbdEvent: kbd task → loop
#ifdef BENCH_CMDS
static std::atomic<bool> echo{false};             // loop writes, kbd task reads
#endif

static void command(uint8_t cmd, int arg = -1) {
    I2cBusLock bus;
    Wire.beginTransmission(KBD_I2C_ADDR);
    Wire.write(cmd);
    if (arg >= 0) Wire.write((uint8_t)arg);
    Wire.endTransmission();
}

static void pollMatrix() {
    I2cBusLock bus;                                 // held through the decode: see decoder
    keymap::Matrix m;
    {
        Wire.requestFrom((uint8_t)KBD_I2C_ADDR, (uint8_t)keymap::COLS);
        const bool whole = Wire.available() >= keymap::COLS;
        for (int c = 0; c < keymap::COLS && whole; c++) m.col[c] = (uint8_t)Wire.read();
        while (Wire.available()) Wire.read();
        if (!whole) return;                     // a short read: keep the last state
    }
#ifdef BENCH_CMDS
    static keymap::Matrix shown;
    if (echo.load(std::memory_order_relaxed) && memcmp(m.col, shown.col, sizeof m.col)) {
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
        xQueueSend(typed, &e, 0);                   // full (16 unread keys): dropped
    });
    micDown.store(decoder.micDown(), std::memory_order_relaxed);
}

static void kbdTask(void*) {
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        pollMatrix();
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(KBD_POLL_MS));
    }
}

// True when the C3 answers a read (in either of its modes).
static bool probe() {
    I2cBusLock bus;
    Wire.requestFrom((uint8_t)KBD_I2C_ADDR, (uint8_t)1);
    const bool answered = Wire.available() > 0;
    while (Wire.available()) Wire.read();
    return answered;
}

// A booted C3 starts in character mode; this firmware needs raw mode.
static void configure() {
    command(KBD_CMD_RAW);
    command(KBD_CMD_BRIGHTNESS, backlight);
}

bool keyboardBegin() {
    if (typed) return online;                       // the task runs already (setup mode re-enters)
    Wire.begin(CHIRP_I2C_SDA, CHIRP_I2C_SCL, 400000);
    delay(KBD_BOOT_MS);
    online = probe();
    if (online) {
        configure();
        typed = xQueueCreate(16, sizeof(KbdEvent));
        // Above the loop (priority 1) on its core, so a busy loop never delays a read.
        const bool started = typed && xTaskCreatePinnedToCore(kbdTask, "kbd", 4096, nullptr, 2,
                                                              nullptr, 1) == pdPASS;
        if (!started) {                             // offline: keyboardPoll stays empty and
            Serial.println("[Kbd] task start failed");   // micDown never latches
            if (typed) vQueueDelete(typed);
            typed = nullptr;
            online = false;
        }
    }
    Serial.printf("[Kbd] %s\n", online ? "ok" : "not online");
    return online;
}

bool keyboardReinit() {
    if (!online) return false;
    // The lock keeps the kbd task off the bus (and out of the decoder) from
    // before the C3 answers until it is in raw mode with a clean decoder: a
    // C3 still in character mode must never be read as a matrix.
    I2cBusLock bus;
    delay(KBD_BOOT_MS);
    bool answered = probe();
    for (int i = 1; !answered && i < KBD_PROBE_TRIES; i++) {
        delay(KBD_PROBE_GAP_MS);
        answered = probe();
    }
    if (answered) configure();
    decoder = keymap::Decoder{};                    // keys "down" before the reset are not down now
    micDown.store(false, std::memory_order_relaxed);
    xQueueReset(typed);                             // typed ahead of the reset: stale
    Serial.printf("[Kbd] reinit %s\n", answered ? "ok" : "no answer");
    return answered;
}

bool keyboardOnline() { return online; }

void keyboardSetBacklight(uint8_t v) {
    backlight = v;
    command(KBD_CMD_BRIGHTNESS, v);
}

bool keyboardMicDown() { return micDown.load(std::memory_order_relaxed); }

#ifdef BENCH_CMDS
void keyboardEcho(bool on) { echo.store(on, std::memory_order_relaxed); }
bool keyboardEchoOn() { return echo.load(std::memory_order_relaxed); }
#endif

bool keyboardPoll(KbdEvent& ev) { return typed && xQueueReceive(typed, &ev, 0) == pdTRUE; }

#endif
