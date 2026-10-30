// board_tembed.h — LILYGO T-Embed CC1101 Plus (ESP32-S3, ST7789 170x320 run
// landscape, PDM mic, I2S speaker amp, wheel + two keys, BQ25896 charger).
#pragma once

#define CHIRP_BOARD_NAME "T-Embed CC1101 Plus"

#define CHIRP_BOARD_TEMBED 1
#define CHIRP_ROTATION 3          // landscape 320x170

// The PDM mic and the BQ25896 charger are the defaults: capture.cpp and
// power.cpp take another path only when a board defines CHIRP_MIC_ES7210 /
// CHIRP_BATTERY_ADC.

#define CHIRP_I2C_SDA 8
#define CHIRP_I2C_SCL 18

// --- Microphone (PDM via I2S0 RX) ---
#define MIC_CLK_PIN      39
#define MIC_DATA_PIN     42
#define MIC_PORT         I2S_NUM_0
#define MIC_GAIN         6      // software PDM boost (~+15.6dB)

// --- Speaker (I2S1 TX, ESP32-audioI2S) ---
#define SPK_BCLK_PIN     46
#define SPK_LRCLK_PIN    40
#define SPK_DIN_PIN      7

// --- Controls ---
#define PTT_BTN_PIN      6      // side key: tap=conversations menu, hold=volume
#define ENCODER_A_PIN    4
#define ENCODER_B_PIN    5
#define ENCODER_BTN_PIN  0      // encoder: hold=push-to-talk, double-tap=voice mode

// --- Board ---
#define BOARD_PWR_EN_PIN 15     // peripheral power rail, HIGH = on
#define RADIO_CS_PIN     12     // CC1101 on the panel's SPI bus: held high (never selected)
#define SD_CS_PIN        13     // microSD slot on the same bus: held high
#define CHARGER_I2C_ADDR 0x6B

// Keys that may wake the chip from deep sleep (RTC-capable, active low).
#define CHIRP_WAKE_KEYS {PTT_BTN_PIN, ENCODER_BTN_PIN}

// The two controls' names in the hints ("Wheel=select  Encoder=open"); the
// tabs' hints name no next-tab control.
#define CHIRP_TURN "Wheel"
#define CHIRP_PRESS "Encoder"
#define CHIRP_NEXT ""
#define CHIRP_WIFI_HELP "WiFi failed; side key 3x = Setup"

// The READY screen's hint lines.
#define CHIRP_HINT1 "Hold encoder to talk"
#define CHIRP_HINT2 "Double-tap for voice mode"

// The setup screen's way back out (the words are portal_layout.h's, so its
// test measures them).
#define CHIRP_CANCEL_HINT portal_layout::MSG_CANCEL_SIDE_KEY
