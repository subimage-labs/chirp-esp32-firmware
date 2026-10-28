// board_tdeck.h — LILYGO T-Deck (ESP32-S3, ST7789 2.8" 320x240 landscape
// above the keyboard, ES7210 mic codec + I2S speaker amp, I2C keyboard on
// the ESP32-C3 coprocessor, trackball, battery ADC).
#pragma once

#define CHIRP_BOARD_NAME "T-Deck"

#define CHIRP_BOARD_TDECK 1
#define CHIRP_ROTATION 1          // landscape 320x240, upright (LILYGO's examples use 1)
#define CHIRP_MIC_ES7210 1        // ES7210 ADC, configured over I2S-bus I2C
#define CHIRP_BATTERY_ADC 1       // divider on an ADC pin, no charger comms

// The setup screen's way back out (the portal polls the keyboard for it;
// the words are portal_layout.h's, so its test measures them).
#define CHIRP_CANCEL_HINT portal_layout::MSG_CANCEL_BACKSPACE

#define CHIRP_HAS_KEYBOARD 1
#define CHIRP_HAS_TRACKBALL 1
#define CHIRP_BANNER 1            // companion banner in the chat view
#define CHIRP_COMPOSE 1           // typed compose line (chat view)

#define CHIRP_I2C_SDA 18
#define CHIRP_I2C_SCL 8

// --- Microphone (ES7210 codec, standard I2S RX on I2S0, S3 = master) ---
#define MIC_SCK_PIN      47
#define MIC_WS_PIN       21
#define MIC_DIN_PIN      14
#define MIC_MCLK_PIN     48       // 256 x fs (4.096 MHz at 16 kHz)
#define MIC_PORT         I2S_NUM_0
#define MIC_GAIN         1        // codec PGA does the gain (set in capture.cpp)

// --- Speaker (I2S1 TX, ESP32-audioI2S) ---
#define SPK_BCLK_PIN     7
#define SPK_LRCLK_PIN    5
#define SPK_DIN_PIN      6

// --- Controls ---
#define PTT_BTN_PIN      -1      // no side key: the keyboard's sym key stands in
#define ENCODER_A_PIN    -1      // no wheel: the trackball feeds the same events
#define ENCODER_B_PIN    -1
#define ENCODER_BTN_PIN  0       // trackball click (shares GPIO0/BOOT with the mic)

// --- Trackball ---
#define TRACKBALL_UP_PIN    3
#define TRACKBALL_DOWN_PIN  15
#define TRACKBALL_LEFT_PIN  1
#define TRACKBALL_RIGHT_PIN 2

// --- Keyboard (T-Keyboard firmware on the ESP32-C3, I2C) ---
#define KBD_I2C_ADDR     0x55
#define KBD_INT_PIN      46

// --- Board ---
#define BOARD_PWR_EN_PIN 10     // peripheral power rail, HIGH = on
#define RADIO_CS_PIN     9      // SX1262 on the panel's SPI bus: held high (never selected)
#define SD_CS_PIN        39     // microSD slot on the same bus: held high
#define BAT_ADC_PIN      4      // battery through a 2:1 divider
#define BAT_ADC_DIV      2

// Keys that may wake the chip from deep sleep (RTC-capable, active low).
#define CHIRP_WAKE_KEYS {ENCODER_BTN_PIN}

// The READY screen's hint lines.
#define CHIRP_HINT1 "Hold space to talk"
#define CHIRP_HINT2 "Type, enter to send"
