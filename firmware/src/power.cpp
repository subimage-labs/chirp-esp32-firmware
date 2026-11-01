#include "power.h"

#include <WiFi.h>
#include <Wire.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>

#include "app.h"
#include "capture.h"
#include "config.h"
#include "input.h"
#include "keyboard.h"
#include "menu.h"
#include "battery.h"
#include "power_policy.h"
#include "selftest.h"
#include "settings.h"
#include "speech.h"
#include "ui.h"
#include "voice.h"
#include "wifi_join.h"

static PowerPolicy policy(ANIM_SLOW_MS, SCREEN_DIM_MS, SCREEN_OFF_MS, LIGHT_SLEEP_MS,
                          DEEP_SLEEP_MS);
static PowerSource source = PowerSource::Unknown;
static Backlight backlightNow = Backlight::Full;
static bool wifiRejoining = false;                // after a light-sleep wake
// A plug or unplug can dip the rail: the display chip comes back reset (asleep,
// factory settings) and the backlight chip off, while this side counts the
// screen lit (T-Deck, seen twice: backlight on, no picture). readSource raises
// it; powerTick sets the panel up again.
static bool panelResync = false;                  // loop
static bool keyTyped = false;                     // loop: powerKeyTyped since the last tick

void powerKeyTyped() { keyTyped = true; }
static BatteryGauge gauge;
static BatteryView batteryShown;                  // what ui last got

static uint8_t backlightDuty(Backlight b) {
    return b == Backlight::Full ? 255 : b == Backlight::Dim ? BACKLIGHT_DIM_DUTY : 0;
}

static void backlightSet(Backlight b) {
    if (b == backlightNow) return;
    const bool wasOff = backlightNow == Backlight::Off;
    backlightNow = b;
    if (wasOff) {                                 // set the panel up from scratch: it may
        uiPanelOn();                              // have been reset while off (see panelResync)
        appRedraw();
    }
    uiBacklight(backlightDuty(b));
    if (b == Backlight::Off) uiPanelBlank(true);  // not just dark: not driving the image
    Serial.printf("[Power] backlight %s\n", b == Backlight::Full  ? "full"
                                            : b == Backlight::Dim ? "dim"
                                                                  : "off");
}

#if CHIRP_BATTERY_ADC
// The battery sits on a divider behind an ADC pin, with no charger to ask
// about USB power. The S3's own USB block answers the part that matters: a
// USB host sends a frame every millisecond (HWCDC::isPlugged), so Usb while
// one is attached (no sleep, like a charger-reported USB). A charger-only
// plug sends no frames and counts as battery: the device sleeps, any key
// wakes it. Changes are logged.
static PowerSource readSource(bool) {
    const PowerSource prev = source;
    source = HWCDC::isPlugged() ? PowerSource::Usb
             : gauge.known()    ? PowerSource::Battery
                                : PowerSource::Unknown;
    if (source != prev) Serial.printf("[Power] source %s\n", powerSourceName());
    if (source != prev && prev != PowerSource::Unknown) panelResync = true;
    return source;
}
#else
static int chargerRead(uint8_t reg) {
    Wire.beginTransmission(CHARGER_I2C_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return -1;
    if (Wire.requestFrom((uint8_t)CHARGER_I2C_ADDR, (uint8_t)1) != 1) return -1;
    return Wire.read();
}

static void chargerWrite(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(CHARGER_I2C_ADDR);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
}

// BQ25896: REG11 bit 7 VBUS_GD or REG0B bit 2 PG_STAT = USB power present.
// Usb if any successful read shows it; Battery only when both reads succeed
// without it; anything else is Unknown. Polled every 2 s; `fresh` bypasses
// the cache. Changes are logged.
static PowerSource readSource(bool fresh) {
    static uint32_t lastMs = 0;
    static bool polled = false;
    if (!fresh && polled && millis() - lastMs < 2000) return source;
    const PowerSource prev = polled ? source : (PowerSource)0xFF;
    polled = true;
    lastMs = millis();
    const int r11 = chargerRead(0x11), r0b = chargerRead(0x0B);
    if ((r11 >= 0 && (r11 & 0x80)) || (r0b >= 0 && (r0b & 0x04)))
        source = PowerSource::Usb;
    else if (r11 >= 0 && r0b >= 0)
        source = PowerSource::Battery;
    else
        source = PowerSource::Unknown;
    if (source != prev)
        Serial.printf("[Power] source %s (r11=%d r0b=%d)\n", powerSourceName(), r11, r0b);
    return source;
}
#endif

Backlight powerBacklight() { return backlightNow; }

const char* powerSourceName() {
    return source == PowerSource::Usb       ? "usb"
           : source == PowerSource::Battery ? "battery"
                                            : "unknown: no sleep";
}

// Radio, mic, panel and peripheral rail down; rail and backlight pins are
// held low through sleep.
static void powerDown() {
    micRemove();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    uiPanelOff();
    digitalWrite(BOARD_PWR_EN_PIN, LOW);
    gpio_hold_en((gpio_num_t)BOARD_PWR_EN_PIN);
}

// Either key wakes; the chip then reboots through setup().
static void enterDeepSleep() {
    Serial.println("[Power] deep sleep");
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);  // drop light-sleep timer
    powerDown();
    gpio_deep_sleep_hold_en();
    for (const int pin : CHIRP_WAKE_KEYS) {
        rtc_gpio_pullup_en((gpio_num_t)pin);
        rtc_gpio_pulldown_dis((gpio_num_t)pin);
    }
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);  // keep pull-ups
    uint64_t wakeMask = 0;
    for (const int pin : CHIRP_WAKE_KEYS) wakeMask |= 1ULL << pin;
    esp_sleep_enable_ext1_wakeup(wakeMask, ESP_EXT1_WAKEUP_ANY_LOW);
    Serial.flush();
    delay(20);
    esp_deep_sleep_start();
}

// RAM, transcript and UI state survive. Either key wakes (GPIO level wake —
// the keys have no ISR, so a held key cannot storm; the wheel pins do, so
// they are not wake sources). A timer wakes when deep sleep is due.
static void enterLightSleep() {
    const uint32_t timerMs = policy.msUntilDeep(millis());
    Serial.printf("[Power] light sleep (deep in %us)\n", (unsigned)(timerMs / 1000));
    Serial.flush();
    powerDown();
    for (const int pin : CHIRP_WAKE_KEYS)
        gpio_wakeup_enable((gpio_num_t)pin, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    esp_sleep_enable_timer_wakeup((uint64_t)max(timerMs, (uint32_t)1000) * 1000ULL);
    esp_light_sleep_start();

    const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    for (const int pin : CHIRP_WAKE_KEYS) gpio_wakeup_disable((gpio_num_t)pin);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    if (cause == ESP_SLEEP_WAKEUP_TIMER) {
        // Deep sleep only on a fresh, valid battery reading: USB attached or
        // a charger fault during light sleep restarts the clock and wakes.
        const uint32_t now = millis();
        policy.setSource(readSource(true), now);
        if (policy.sleepDue(now) == SleepDue::Deep) enterDeepSleep();   // no return
    }

    Serial.println("[Power] woke from light sleep");
    gpio_hold_dis((gpio_num_t)BOARD_PWR_EN_PIN);
    digitalWrite(BOARD_PWR_EN_PIN, HIGH);
#if CHIRP_HAS_KEYBOARD
    keyboardReinit();                               // the rail cut the C3: back to character mode
#endif
    uiPanelOn();
    backlightNow = Backlight::Full;
    inputSwallowKeys();                             // the waking press only wakes
    inputDiscardWheel();
    policy.activity(millis());
    appRedraw();
    uiMsg("Joining WiFi", Ink::Muted);
    wifiJoinBegin();                                // the loop's wifiJoinTick joins the pick
    wifiRejoining = true;
}

bool powerBoot() {
    gpio_hold_dis((gpio_num_t)TFT_BL);             // released from deep-sleep hold
    gpio_hold_dis((gpio_num_t)BOARD_PWR_EN_PIN);
    gpio_deep_sleep_hold_dis();
    pinMode(BOARD_PWR_EN_PIN, OUTPUT);
    digitalWrite(BOARD_PWR_EN_PIN, HIGH);         // peripheral power rail
    const bool byKey = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1;
    if (byKey) Serial.println("[Power] woke by key");
    return byKey;
}

void powerBegin() {
#if !CHIRP_BATTERY_ADC
    Wire.begin(CHIRP_I2C_SDA, CHIRP_I2C_SCL);
#endif
}

#if CHIRP_BATTERY_ADC
// Battery level: the divider pin, oversampled 8x (its source impedance is
// high), into the same gauge (battery.h). Skipped while the speaker plays:
// the amplifier's load sags the cell and the gauge would smooth that in as
// charge.
static bool batterySample() {
    uint32_t sum = 0;
    for (int i = 0; i < 8; i++) sum += analogReadMilliVolts(BAT_ADC_PIN);
    const int mv = (int)(sum / 8) * BAT_ADC_DIV;
    if (!batteryMvPlausible(mv)) return false;
    gauge.update(mv, Charge::Idle, false);
    return true;
}
#else
// Battery level: VBAT (REG0E) and the charge state (REG0B) into the gauge
// (battery.h) every BATTERY_POLL_MS (1.5 s until the first valid sample). The
// charger's ADC is one-shot unless asked: continuous conversion (REG02 bit 6,
// once a second) keeps BATV fresh with no trigger; it is re-asserted on every
// sample because a charger watchdog expiry restores the default. Skipped
// while the speaker plays: the amplifier's load sags the cell and the gauge
// would smooth that in as charge. About four short I2C transactions.
static bool batterySample() {
    const int r02 = chargerRead(0x02);
    if (r02 < 0) return false;
    if (!(r02 & 0x40))      // bit 7 CONV_START and bit 1 FORCE_DPDM are triggers: never write them back
        chargerWrite(0x02, (uint8_t)((r02 | 0x40) & ~0x82));
    const int r0e = chargerRead(0x0E), r0b = chargerRead(0x0B);
    if (r0e < 0 || r0b < 0) return false;
    const int mv = batteryMvFromReg0E((uint8_t)r0e);
    if (!batteryMvPlausible(mv)) return false;      // code 0: no conversion yet
    gauge.update(mv, batteryChargeFromReg0B((uint8_t)r0b), source == PowerSource::Usb);
    return true;
}
#endif

static void batteryPublish() {
    const BatteryView v = gauge.view();
    if (v == batteryShown) return;
    batteryShown = v;
    uiBattery(v);
}

static void batteryTick(uint32_t now) {
    static uint32_t lastMs = 0;
    static bool polled = false;
    if (polled && now - lastMs < (gauge.known() ? BATTERY_POLL_MS : 1500UL)) return;
    if (speechPlaying()) return;
    polled = true;
    lastMs = now;
    if (batterySample()) batteryPublish();
}

BatteryView powerBattery() { return gauge.view(); }

bool powerAnimSlow() { return policy.animSlow(millis()); }

static const char* chargeName() {
    if (gauge.charge() == Charge::Charging) return "charging";
    if (gauge.charge() == Charge::Done) return "charged";
    return gauge.usb() ? "on USB, not charging" : "discharging";
}

bool powerBatteryText(char* buf, size_t n) {
    if (!gauge.known()) return false;
    snprintf(buf, n, "%d%%  %d.%02d V  %s", gauge.percent(), gauge.millivolts() / 1000,
             gauge.millivolts() % 1000 / 10, chargeName());
    return true;
}

#ifdef BENCH_CMDS
// A fresh sample, then the raw reading and everything derived from it.
void powerBatteryReport() {
#if CHIRP_BATTERY_ADC
    readSource(true);
    const bool fed = batterySample();
    if (fed) batteryPublish();
    const int mv = gauge.millivolts();
    Serial.printf("[Bench] battery adc mv=%d curve=%d%% sample=%s known=%d pct=%d "
                  "state=%s usb=%d low=%d\n",
                  mv, batteryPercentFromMv(mv), fed ? "ok" : "rejected",
                  (int)gauge.known(), gauge.percent(), chargeName(), (int)gauge.usb(),
                  (int)gauge.low());
#else
    const int r02 = chargerRead(0x02), r0b = chargerRead(0x0B), r0c = chargerRead(0x0C),
              r0e = chargerRead(0x0E), r11 = chargerRead(0x11);
    readSource(true);
    const bool fed = batterySample();
    if (fed) batteryPublish();
    const int mv = r0e >= 0 ? batteryMvFromReg0E((uint8_t)r0e) : -1;
    Serial.printf("[Bench] battery r02=0x%02X r0b=0x%02X r0c=0x%02X r0e=0x%02X r11=0x%02X "
                  "mv=%d curve=%d%% sample=%s known=%d pct=%d state=%s usb=%d low=%d\n",
                  r02 & 0xFF, r0b & 0xFF, r0c & 0xFF, r0e & 0xFF, r11 & 0xFF, mv,
                  mv >= 0 ? batteryPercentFromMv(mv) : -1, fed ? "ok" : "rejected",
                  (int)gauge.known(), gauge.percent(), chargeName(), (int)gauge.usb(),
                  (int)gauge.low());
#endif
}
#endif

void powerTick() {
    const uint32_t now = millis();
    policy.setSource(readSource(false), now);
    if (panelResync) {
        panelResync = false;
#if CHIRP_HAS_KEYBOARD
        keyboardReinit();                         // the dip may have reset the C3 too
#endif
        if (backlightNow != Backlight::Off) {     // off: the next wake sets it up anyway
            uiPanelOn();
            uiBacklight(backlightDuty(backlightNow));
            appRedraw();
            Serial.println("[Power] panel set up again (power source changed)");
        }
    }
    batteryTick(now);
    const bool keyDown = inputKeyDown() || keyTyped;
    keyTyped = false;
    const bool wheelMoved = inputWheelPending();
    if (policy.backlight(now) == Backlight::Off && (keyDown || wheelMoved)) {
        inputDiscardWheel();                        // the waking turn only wakes
        if (keyDown) inputSwallowKeys();            // the waking press only wakes
    }
    const bool busy = uiState != UI_IDLE || voiceHearing() || speechPlaying();
    if (keyDown || wheelMoved || busy) policy.activity(now);
    backlightSet(policy.backlight(now));
    if (wifiRejoining && WiFi.status() == WL_CONNECTED) {
        wifiRejoining = false;
        Serial.println("[Power] WiFi back");
        if (uiState == UI_IDLE && !menuOpen()) {     // the host may have changed meanwhile
            const char* problem = selfTestRun();
            uiMsg(problem ? problem : "Ready", problem ? Ink::Err : Ink::Ok);
            if (problem) speechErrorTone();
        }
    }
    if (keyDown || busy || voiceOn()) return;
    if (policy.sleepDue(now) == SleepDue::None) return;
    policy.setSource(readSource(true), now);       // fresh reading decides
    const SleepDue due = policy.sleepDue(now);
    if (due == SleepDue::Deep) enterDeepSleep();
    else if (due == SleepDue::Light) enterLightSleep();
}
