#pragma once
// Build-time constants: pins, timing, limits. Device settings (WiFi, Hermes
// host and key, audio shim) are entered on the device and live in NVS —
// settings.h.

#define TTS_VOICE       "af_sky"   // ignored by shim: local Paradee TTS is single-voice af_heart

// --- Timing / limits ---
#define WIFI_TIMEOUT_MS      15000UL
#define PORTAL_IDLE_MS       600000UL  // setup mode with no phone → back to the app
#define PORTAL_MAX_MS        900000UL  // ...and at most this long in all, phone or not
#define STT_TIMEOUT_MS       60000UL
#define HERMES_TIMEOUT_MS    60000UL   // HTTPClient::setTimeout is uint16 ms (65.5s max)
#define HERMES_RETRY_MS      700UL     // pause before the one retry of a refused connection
#define TTS_TIMEOUT_MS       60000UL   // ditto — larger values silently truncate
#define SSE_IDLE_LIMIT_MS    90000UL   // no data event (tool progress counts, keepalive comments don't)
#define HERMES_STREAM_BUDGET_MS 240000UL  // overall SSE budget (uint32; setTimeout caps at 65.5s)
#define CHUNK_FIRST_MIN_WORDS 3        // first chunk: one sentence of >= this many words
#define CHUNK_MIN_SENTS      2         // second chunk: >= this many sentences
#define CHUNK_MIN_WORDS      14        // ...and >= this many words (>=6s playback)
#define CHUNK_SECOND_MAX_WORDS 30      // ...closed at once at this many (keeps it short)
#define CHUNK_LATER_MIN_WORDS 30       // later chunks grow: >= this many words
#define CHUNK_MAX_WORDS      80        // force-close before chunks get huge
#define CHUNK_MP3_CAP        (512 * 1024)
#define CHUNK_QUEUE          4
#define WIFI_RETRY_MS        5000UL

// --- Microphone (PDM via I2S0 RX) ---
#define MIC_CLK_PIN      39
#define MIC_DATA_PIN     42
#define MIC_PORT         I2S_NUM_0
#define SAMPLE_RATE      16000
#define REC_MAX_SECS     30
#define REC_MAX_PCM      (SAMPLE_RATE * 2 * REC_MAX_SECS)   // 960000 bytes
#define MIN_TURN_PCM     (SAMPLE_RATE)                      // 0.5s of PCM
#define MIC_DMA_BUFS     8
#define MIC_DMA_LEN      512
#define MIC_GAIN         6      // software PDM boost (~+15.6dB); raise if far, lower if clipped
#define READ_CHUNK       2048
#define SILENCE_PEAK     1200   // 16-bit peak below this across the clip = silence

// --- Voice mode (double-tap encoder key): half-duplex hands-free ---
#define VOICE_START_RATIO   3.0f   // turn opens at floor x this
#define VOICE_END_RATIO     1.7f   // silence when below floor x this
#define VOICE_START_MIN     700    // absolute guard: never open below this
#define VOICE_END_MIN       900    // absolute guard: never count silence below
#define VOICE_FLOOR_SEED    600.0f // initial ambient estimate (post-gain)
#define VOICE_START_MS      400     // net loud (leaky) needed to open a turn
#define VOICE_END_MS        2000    // trailing silence that closes a turn
#define VOICE_RECOVER_MS    6000    // no-dip speech this long = noise, reject
#define VOICE_FLOOR_MAX     8000.0f // floor ceiling (startTh sanity)
#define VOICE_MIN_SPEECH_MS 1200    // ignore sub-second blips
#define VOICE_PREROLL_MS    300     // ring kept before speech detection fires
#define VOICE_SETTLE_MS     400     // mute after playback before listening
#define VOICE_IDLE_EXIT_MS  180000UL  // leave voice mode after this much quiet

// --- Speaker (I2S1 TX, ESP32-audioI2S) ---
#define SPK_BCLK_PIN     46
#define SPK_LRCLK_PIN    40
#define SPK_DIN_PIN      7
#define DEFAULT_VOLUME   21     // 0..21 (21 = unity); decoder adds fixed +6 dB w/ limiter
#define MAX_VOLUME       21
// phase 2: loudness target adjustable in a prefs screen (encoder center press)

// --- Controls ---
#define PTT_BTN_PIN      6      // side key: tap=conversations menu, hold=volume
#define ENCODER_A_PIN    4
#define ENCODER_B_PIN    5
#define ENCODER_BTN_PIN  0      // encoder: hold=push-to-talk, double-tap=voice mode, +side tap=view
#define DEBOUNCE_MS      30     // key must stay up this long to count as released
#define BTN_DBL_MS       400    // double-tap window
#define BTN_TAP_MS       300    // encoder press shorter than this: never sent as speech
#define RECORD_SHOW_MS   500    // a capture shows Listening only once this old (push-to-talk, voice)
#define BTN_HOLD_MS      500    // side-key hold enters volume mode

// --- Board ---
#define BOARD_PWR_EN_PIN 15     // peripheral power rail, HIGH = on
#define RADIO_CS_PIN     12     // CC1101 on the panel's SPI bus: held high (never selected)
#define SD_CS_PIN        13     // microSD slot on the same bus: held high
#define CHARGER_I2C_SDA  8      // BQ25896 charger: reports USB power present
#define CHARGER_I2C_SCL  18
#define CHARGER_I2C_ADDR 0x6B

// --- Power (battery only; USB keeps the screen on, never sleeps) ---
// Overridable at build time (-D) for short-timer bench tests.
#ifndef SCREEN_DIM_MS
#define SCREEN_DIM_MS      60000UL    // idle → backlight dims
#endif
#ifndef SCREEN_OFF_MS
#define SCREEN_OFF_MS      180000UL   // idle → backlight off (next input only wakes)
#endif
#ifndef LIGHT_SLEEP_MS
#define LIGHT_SLEEP_MS     600000UL   // idle → light sleep (state kept); a key wakes
#endif
#ifndef DEEP_SLEEP_MS
#define DEEP_SLEEP_MS      7200000UL  // idle → deep sleep (reboot on wake)
#endif
#define BACKLIGHT_DIM_DUTY 40         // of 255 (LEDC PWM on TFT_BL)
#ifndef ANIM_SLOW_MS
#define ANIM_SLOW_MS       20000UL    // idle → character animation drops to a trickle
#endif
#define ANIM_SLOW_FRAME_MS 250        // 4 fps while slow (full rate is avatar::FRAME_MS)
#define BATTERY_POLL_MS    10000UL    // charger ADC read interval (battery.h smooths it)

// --- WAV ---
#define WAV_HDR          44
