#include "capture.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <driver/i2s.h>

#include "config.h"
#include "settings.h"
#if CHIRP_MIC_ES7210
#include <Wire.h>

#include "es7210.h"
#endif

static uint8_t* wavBuf = nullptr;                 // PSRAM: WAV header + PCM
static size_t pcmLen = 0;
static int16_t clipPeak = 0;
static uint8_t lastLevel = 0;                     // loop: latest chunk peak, for the meter
static uint32_t recStartMs = 0;
static bool micUp = false;                        // driver actually installed
static WiFiClient wavStream;                      // streaming capture upload
static bool wavStreamLive = false;

static void fillWavHeader(uint8_t* h, uint32_t pcmBytes) {
    const uint32_t sr = SAMPLE_RATE, br = SAMPLE_RATE * 2;
    const uint32_t chunk = pcmBytes + 36;
    memcpy(h + 0,  "RIFF", 4);
    memcpy(h + 4,  &chunk, 4);
    memcpy(h + 8,  "WAVE", 4);
    memcpy(h + 12, "fmt ", 4);
    memcpy(h + 16, "\x10\x00\x00\x00", 4);        // Subchunk1Size
    memcpy(h + 20, "\x01\x00", 2);                // PCM
    memcpy(h + 22, "\x01\x00", 2);                // mono
    memcpy(h + 24, &sr, 4);
    memcpy(h + 28, &br, 4);
    memcpy(h + 32, "\x02\x00", 2);                // block align
    memcpy(h + 34, "\x10\x00", 2);                // 16 bits
    memcpy(h + 36, "data", 4);
    memcpy(h + 40, &pcmBytes, 4);
}

bool captureBegin() {
    wavBuf = (uint8_t*)heap_caps_malloc(WAV_HDR + REC_MAX_PCM, MALLOC_CAP_SPIRAM);
    return wavBuf != nullptr;
}

// ---------------------------------------------------------------- mic
void micRemove() {
    if (micUp) {
        i2s_driver_uninstall(MIC_PORT);
        micUp = false;
    }
}

bool micInstall() {
    micRemove();
#if CHIRP_MIC_ES7210
    // The codec sits on the keyboard's I2C bus (keyboardBegin started it)
    // and clocks from our I2S0: S3 master, ES7210 slave, MCLK 256 x fs.
    audio_hal_codec_config_t cfg = {};
    cfg.adc_input = AUDIO_HAL_ADC_INPUT_ALL;
    cfg.codec_mode = AUDIO_HAL_CODEC_MODE_ENCODE;
    cfg.i2s_iface.mode = AUDIO_HAL_MODE_SLAVE;
    cfg.i2s_iface.fmt = AUDIO_HAL_I2S_NORMAL;
    cfg.i2s_iface.samples = AUDIO_HAL_16K_SAMPLES;
    cfg.i2s_iface.bits = AUDIO_HAL_BIT_LENGTH_16BITS;
    if (es7210_adc_init(&Wire, &cfg) != ESP_OK) return false;
    if (es7210_adc_config_i2s(cfg.codec_mode, &cfg.i2s_iface) != ESP_OK) return false;
    if (es7210_mic_select((es7210_input_mics_t)ES7210_INPUT_MIC1) != ESP_OK) return false;
    if (es7210_adc_set_gain((es7210_input_mics_t)ES7210_INPUT_MIC1, GAIN_30DB) != ESP_OK)
        return false;
    if (es7210_adc_ctrl_state(cfg.codec_mode, AUDIO_HAL_CTRL_START) != ESP_OK) return false;
    i2s_config_t icfg = {};
    icfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
    icfg.sample_rate = SAMPLE_RATE;
    icfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    icfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;   // SDOUT1's slot: MIC1, mono
    icfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    icfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL2;
    icfg.dma_buf_count = MIC_DMA_BUFS;
    icfg.dma_buf_len = MIC_DMA_LEN;
    icfg.use_apll = false;
    icfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    i2s_pin_config_t pins = {};
    pins.mck_io_num = MIC_MCLK_PIN;
    pins.bck_io_num = MIC_SCK_PIN;
    pins.ws_io_num = MIC_WS_PIN;
    pins.data_out_num = I2S_PIN_NO_CHANGE;
    pins.data_in_num = MIC_DIN_PIN;
    if (i2s_driver_install(MIC_PORT, &icfg, 0, nullptr) != ESP_OK) return false;
    if (i2s_set_pin(MIC_PORT, &pins) != ESP_OK) {
        i2s_driver_uninstall(MIC_PORT);
        return false;
    }
    i2s_zero_dma_buffer(MIC_PORT);
    micUp = true;
    return true;
#else
    i2s_config_t cfg = {};
    cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM);
    cfg.sample_rate = SAMPLE_RATE;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL2;
    cfg.dma_buf_count = MIC_DMA_BUFS;
    cfg.dma_buf_len = MIC_DMA_LEN;
    cfg.use_apll = false;
    i2s_pin_config_t pins = {};
    pins.mck_io_num = I2S_PIN_NO_CHANGE;
    pins.bck_io_num = I2S_PIN_NO_CHANGE;
    pins.ws_io_num = MIC_CLK_PIN;                 // PDM clock out
    pins.data_out_num = I2S_PIN_NO_CHANGE;
    pins.data_in_num = MIC_DATA_PIN;
    if (i2s_driver_install(MIC_PORT, &cfg, 0, nullptr) != ESP_OK) return false;
    if (i2s_set_pin(MIC_PORT, &pins) != ESP_OK) {
        i2s_driver_uninstall(MIC_PORT);
        return false;
    }
    i2s_zero_dma_buffer(MIC_PORT);
    micUp = true;
    return true;
#endif
}

bool micInstalled() { return micUp; }

size_t micRead(int16_t* buf, size_t bytes, uint32_t timeoutMs) {
    size_t br = 0;
    if (i2s_read(MIC_PORT, buf, bytes, &br, pdMS_TO_TICKS(timeoutMs)) != ESP_OK) return 0;
    const size_t n = br / 2;
    for (size_t i = 0; i < n; i++) {               // software gain for the PDM mic
        int32_t s = (int32_t)buf[i] * MIC_GAIN;
        if (s > 32767) s = 32767;
        else if (s < -32768) s = -32768;
        buf[i] = (int16_t)s;
    }
    return br;
}

// ---------------------------------------------------------------- recording
// One chunked-encoding write to the streaming STT upload.
static bool chunkedWrite(const uint8_t* data, size_t len) {
    if (wavStream.printf("%X\r\n", (unsigned)len) <= 0) return false;
    if (len && wavStream.write(data, len) != (int)len) return false;
    return wavStream.print("\r\n") > 0;
}

void captureReset(int16_t peak) {
    pcmLen = 0;
    clipPeak = peak;
    lastLevel = 0;
    recStartMs = millis();
    fillWavHeader(wavBuf, 0);
}

void captureStreamOpen() {
    wavStreamLive = false;
    const Endpoints& ep = settingsEndpoints();
    if (WiFi.status() != WL_CONNECTED ||
        !wavStream.connect(ep.shimHost.c_str(), ep.shimPort, 1500))
        return;
    const String head = "POST /v1/audio/transcriptions/stream HTTP/1.1\r\n"
                        "Host: " + ep.shimHost + "\r\n"
                        "Content-Type: audio/wav\r\n"
                        "Transfer-Encoding: chunked\r\nConnection: close\r\n\r\n";
    if (wavStream.print(head) == (int)head.length() && chunkedWrite(wavBuf, WAV_HDR)) {
        wavStream.setTimeout(15000);
        wavStreamLive = true;
    } else {
        wavStream.stop();
    }
}

void captureStreamClose() {
    if (!wavStreamLive) return;
    wavStream.print("0\r\n\r\n");
    wavStream.stop();
    wavStreamLive = false;
}

bool captureAppend(const int16_t* pcm, size_t bytes, int16_t peak) {
    if (pcmLen + bytes > REC_MAX_PCM) return false;
    memcpy(wavBuf + WAV_HDR + pcmLen, pcm, bytes);
    pcmLen += bytes;
    if (peak > clipPeak) clipPeak = peak;
    lastLevel = (uint8_t)min(255, peak >> 5);
    if (wavStreamLive && !chunkedWrite(reinterpret_cast<const uint8_t*>(pcm), bytes)) {
        wavStream.stop();                         // shim gone: buffered POST later
        wavStreamLive = false;
    }
    return true;
}

size_t captureBytes() { return pcmLen; }
int16_t capturePeak() { return clipPeak; }
uint8_t captureLevel() { return lastLevel; }
uint32_t captureStartMs() { return recStartMs; }
bool captureWorthSending() { return pcmLen >= MIN_TURN_PCM && clipPeak >= SILENCE_PEAK; }

// ---------------------------------------------------------------- STT
static bool sttTranscribe(String& text) {
    const char* B = "ChirpClip";
    String head = String("--") + B + "\r\n"
                  "Content-Disposition: form-data; name=\"file\"; filename=\"clip.wav\"\r\n"
                  "Content-Type: audio/wav\r\n\r\n";
    String tail = String("\r\n--") + B + "--\r\n";
    const size_t wavLen = WAV_HDR + pcmLen;
    const size_t total = head.length() + wavLen + tail.length();

    uint8_t* body = (uint8_t*)heap_caps_malloc(total, MALLOC_CAP_SPIRAM);
    if (!body) return false;
    memcpy(body, head.c_str(), head.length());
    memcpy(body + head.length(), wavBuf, wavLen);
    memcpy(body + head.length() + wavLen, tail.c_str(), tail.length());

    HTTPClient http;
    http.begin(settingsEndpoints().sttUrl);
    http.setTimeout(STT_TIMEOUT_MS);
    http.addHeader("Content-Type", "multipart/form-data; boundary=" + String(B));
    const int code = http.POST(body, total);
    heap_caps_free(body);
    if (code != HTTP_CODE_OK) {
        Serial.printf("[STT] HTTP %d: %s\n", code,
                      code > 0 ? http.getString().c_str() : "(conn)");
        http.end();
        return false;
    }
    String resp = http.getString();
    http.end();
    JsonDocument doc;
    if (deserializeJson(doc, resp)) {
        Serial.printf("[STT] bad json: %.80s\n", resp.c_str());
        return false;
    }
    const char* t = doc["text"] | "";
    text = String(t);
    text.trim();
    if (text.length() == 0)
        Serial.printf("[STT] empty text (pcm=%u bytes, %.1fs) resp=%.80s\n",
                      (unsigned)pcmLen, (float)(millis() - recStartMs) / 1000.0f,
                      resp.c_str());
    return true;
}

// Finalizes the streamed capture: ends the chunked body, reads the result.
// Reads with explicit delay() — Arduino WiFiClient::read busy-spins without
// yielding when no data is ready, which starves IDLE0 and trips the task WDT.
static bool streamFinishSTT(String& text) {
    wavStream.print("0\r\n\r\n");
    String all;
    all.reserve(1024);
    const uint32_t deadline = millis() + STT_TIMEOUT_MS;
    while (millis() < deadline) {
        while (wavStream.available()) {
            const int c = wavStream.read();
            if (c < 0) break;
            all += (char)c;
        }
        if (!wavStream.connected() && wavStream.available() == 0) break;
        if (wavStream.available() == 0) delay(2);  // yield: feed IDLE0
    }
    wavStream.stop();
    wavStreamLive = false;
    const int bodyAt = all.indexOf("\r\n\r\n");
    if (bodyAt < 0) return false;
    if (all.indexOf(" 200 ") < 0) {
        Serial.printf("[STT] stream status: %.30s\n", all.c_str());
        return false;
    }
    JsonDocument doc;
    if (deserializeJson(doc, all.substring(bodyAt + 4))) return false;
    const char* t = doc["text"] | "";
    text = String(t);
    text.trim();
    return true;
}

bool captureTranscribe(String& text) {
    fillWavHeader(wavBuf, (uint32_t)pcmLen);
    if (wavStreamLive) {                          // capture was streamed live
        const bool ok = streamFinishSTT(text);
        Serial.printf("[STT] path=stream ok=%d\n", (int)ok);
        return ok;
    }
    return sttTranscribe(text);
}
