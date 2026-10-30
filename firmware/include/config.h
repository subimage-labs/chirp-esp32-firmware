#pragma once
// config.h — build-time app constants: timing, limits, policy. Pins and
// per-board hardware live in board.h (one header per target, picked by the
// platformio.ini environment). Device settings (WiFi, Hermes host and key,
// audio shim) are entered on the device and live in NVS — settings.h.
#include "board.h"

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

// --- Microphone (I2S0 RX; kind and pins per board) ---
#define SAMPLE_RATE      16000
#define REC_MAX_SECS     30
#define REC_MAX_PCM      (SAMPLE_RATE * 2 * REC_MAX_SECS)   // 960000 bytes
#define MIN_TURN_PCM     (SAMPLE_RATE)                      // 0.5s of PCM
#define MIC_DMA_BUFS     8
#define MIC_DMA_LEN      512
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

// --- Speaker (I2S1 TX, ESP32-audioI2S; pins per board) ---
#define DEFAULT_VOLUME   21     // 0..21 (21 = unity); decoder adds fixed +6 dB w/ limiter
#define MAX_VOLUME       21
// phase 2: loudness target adjustable in a prefs screen (encoder center press)

// --- Controls (pins per board; timing shared) ---
#define DEBOUNCE_MS      30     // key must stay up this long to count as released
#define BTN_DBL_MS       400    // double-tap window
#define BTN_TAP_MS       300    // encoder press shorter than this: never sent as speech
#define RECORD_SHOW_MS   500    // a capture shows Listening only once this old (push-to-talk, voice)
#define BTN_HOLD_MS      500    // side-key hold enters volume mode

// --- Power (battery only; USB keeps the screen on, never sleeps) ---
// Overridable at build time (-D) for short-timer bench tests.
#ifndef SCREEN_DIM_MS
#define SCREEN_DIM_MS      60000UL    // idle → backlight dims
#endif
#ifndef SCREEN_OFF_MS
#define SCREEN_OFF_MS     180000UL   // idle → backlight off (next input only wakes)
#endif
#ifndef LIGHT_SLEEP_MS
#define LIGHT_SLEEP_MS     600000UL   // idle → light sleep (state kept); a key wakes
#endif
#ifndef DEEP_SLEEP_MS
#define DEEP_SLEEP_MS     7200000UL  // idle → deep sleep (reboot on wake)
#endif
#define BACKLIGHT_DIM_DUTY 40         // of 255: LEDC PWM duty, or level 3 of 16 (CHIRP_BL_PULSE)
#ifndef ANIM_SLOW_MS
#define ANIM_SLOW_MS       20000UL    // idle → character animation drops to a trickle
#endif
#define ANIM_SLOW_FRAME_MS 250        // 4 fps while slow (full rate is avatar::FRAME_MS)
#define BATTERY_POLL_MS    10000UL    // charger ADC read interval (battery.h smooths it)

// --- WAV ---
#define WAV_HDR          44
