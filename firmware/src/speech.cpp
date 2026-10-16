#include "speech.h"

#include <ArduinoJson.h>
#include <FS.h>
#include <FSImpl.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include <memory>

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "Audio.h"
#include "chirp_mp3.h"   // embedded release blip (host-generated)
#include "config.h"
#include "error_mp3.h"   // embedded failure tone (host-generated)
#include "mem_source.h"
#include "sentence_packer.h"
#include "settings.h"
#include "turn.h"

static Audio audio(false, 3, I2S_NUM_1);          // playback owns I2S1
static uint8_t volume = DEFAULT_VOLUME;           // loop
static bool playing = false;                      // loop (eof callback runs in audio.loop)
static bool chirping = false;                     // loop: what plays is the chirp

// --- chunk pipeline ---
static SentencePacker packer(CHUNK_MIN_SENTS, CHUNK_MIN_WORDS, CHUNK_MAX_WORDS,
                             CHUNK_FIRST_MIN_WORDS, CHUNK_SECOND_MAX_WORDS,
                             CHUNK_LATER_MIN_WORDS);   // worker

static std::mutex qMutex;                         // guards the four below
// Ring: chunk i lives in chunkQ[i % CHUNK_QUEUE]; its slot is free again once
// the fetcher has copied the text (claimedUpTo >= i), so long answers keep
// flowing while earlier chunks play.
static std::string chunkQ[CHUNK_QUEUE];
static int chunkQLen = 0;                         // chunks enqueued this answer
static int claimedUpTo = -1;                      // highest chunk the fetcher copied
static bool fetchTaskRunning = false;

// PSRAM slot handoff (slot = chunk % kSpeechSlots, mem_source.h). The fetch
// task fills staged[slot] then publishes fetchedUpTo (release). The loop
// acquires it and the decoder plays the bytes in place, so a slot stays
// occupied from download until the decoder has closed the file; only then
// does the loop free it and publish consumedUpTo (release). The fetch task
// acquires that before refilling the slot (speechSlotFree). The loop alone
// frees slots during a turn; speechStart (worker) frees leftovers only after
// the fetch task has exited, and speechChirp (loop, always before the
// worker is woken) has already released the decoder's slot.
static std::atomic<int> fetchedUpTo{-1};
static std::atomic<int> consumedUpTo{-1};
static std::atomic<bool> fetchFailed{false};
static std::atomic<bool> chunkAbort{false};
static uint8_t* staged[kSpeechSlots] = {nullptr, nullptr};
static size_t stagedSize[kSpeechSlots] = {0, 0};
// Where each staged chunk's text starts in this answer's spoken text and how
// long it is (chars), published with the slot; fetchChars is the fetch task's
// running count (reset by speechStart once the task has exited).
static uint32_t stagedStart[kSpeechSlots] = {0, 0};
static uint32_t stagedLen[kSpeechSlots] = {0, 0};
static uint32_t fetchChars = 0;
static int curChunk = -1;                         // loop: chunk playing / played
static int curStart = -1, curLen = 0;             // loop: its text span (stagedStart/Len)
static uint32_t curBytes = 0, curPlayMs = 0;      // loop: its MP3 size, when it started
static int heldChunk = -1;                        // loop: chunk whose slot the decoder reads
static bool endReported = false;                  // loop: Done logged for this answer
static std::atomic<bool> producing{false};        // worker: speechStart until speechFinish

void audio_eof_mp3(const char*) {
    Serial.printf("[Audio] eof t=%u\n", (unsigned)millis());
    turnLatMark("eof");
    playing = false;
}

// Read-only fs::FS over one memory range, so connecttoFS decodes straight
// from PSRAM (chunks) or flash (chirp, error tone): no file system round
// trip. It serves one range at a time, whatever the path; open() hands out a
// handle that reads the range current at that moment.
namespace {
struct MemFileImpl : fs::FileImpl {
    explicit MemFileImpl(MemReader r) : r_(r), open_(true) {}
    size_t write(const uint8_t*, size_t) override { return 0; }
    size_t read(uint8_t* buf, size_t n) override { return r_.read(buf, n); }
    void flush() override {}
    bool seek(uint32_t pos, fs::SeekMode mode) override {
        return r_.seek(pos, mode == fs::SeekCur ? MemSeek::Cur
                          : mode == fs::SeekEnd ? MemSeek::End : MemSeek::Set);
    }
    size_t position() const override { return r_.position(); }
    size_t size() const override { return r_.size(); }
    bool setBufferSize(size_t) override { return false; }
    void close() override { r_.clear(); open_ = false; }
    time_t getLastWrite() override { return 0; }
    const char* path() const override { return "/sound.mp3"; }
    const char* name() const override { return "sound.mp3"; }   // the decoder picks the codec by extension
    boolean isDirectory() override { return false; }
    fs::FileImplPtr openNextFile(const char*) override { return fs::FileImplPtr(); }
    boolean seekDir(long) override { return false; }
    String getNextFileName() override { return String(); }
    String getNextFileName(bool*) override { return String(); }
    void rewindDirectory() override {}
    operator bool() override { return open_; }

private:
    MemReader r_;
    bool open_;
};

struct MemFSImpl : fs::FSImpl {
    MemReader src;                                 // loop: set by openMemory before connecttoFS
    fs::FileImplPtr open(const char*, const char*, const bool) override {
        return std::make_shared<MemFileImpl>(src);
    }
    bool exists(const char*) override { return src.size() > 0; }
    bool rename(const char*, const char*) override { return false; }
    bool remove(const char*) override { return false; }
    bool mkdir(const char*) override { return false; }
    bool rmdir(const char*) override { return false; }
};
}  // namespace

static std::shared_ptr<MemFSImpl> memFsImpl = std::make_shared<MemFSImpl>();
static fs::FS memFs(memFsImpl);

void speechBegin() {
    audio.setPinout(SPK_BCLK_PIN, SPK_LRCLK_PIN, SPK_DIN_PIN);
    audio.setVolume(volume);   // 21 = unity in the lib's gain table
}

// ---------------------------------------------------------------- loop side
uint8_t speechVolume() { return volume; }

void speechSetVolume(uint8_t v) {
    volume = v;
    if (playing) audio.setVolume(volume);
}

bool speechPlaying() { return playing; }
bool speechSpeaking() { return playing && !chirping; }
// Within a chunk (one sentence can be several lines), the position advances
// with playback time over the chunk's duration estimated from its size and
// the stream's bitrate. Not the decoder's read position: it buffers ahead.
int speechSpokenChars() {
    if (!speechSpeaking() || curStart < 0) return -1;
    const uint32_t bitrate = audio.getBitRate();
    if (bitrate == 0 || curBytes == 0) return curStart;
    const uint32_t durMs = (uint32_t)((uint64_t)curBytes * 8000u / bitrate);
    const uint32_t el = millis() - curPlayMs;
    if (durMs == 0 || el >= durMs) return curStart + curLen;
    return curStart + (int)((uint64_t)curLen * el / durMs);
}

// Decoded-audio level for the character's mouth. The audio library calls this
// hook from audio.loop() (always the loop task) with each decoded buffer
// before volume and gain; leaving *continueI2S true keeps the output going.
static uint8_t levelPeak = 0;

uint8_t speechLevel() {
    const uint8_t v = levelPeak;
    levelPeak = 0;
    return v;
}

void audio_process_extern(int16_t* buff, uint16_t len, bool* continueI2S) {
    *continueI2S = true;
    int32_t peak = 0;
    for (uint16_t i = 0; i < len; i++) {
        const int32_t a = buff[i] < 0 ? -(int32_t)buff[i] : buff[i];
        if (a > peak) peak = a;
    }
    const int32_t lvl = (peak * 5) >> 9;           // 32767 -> 320, clamped
    const uint8_t v = lvl > 255 ? 255 : (uint8_t)lvl;
    if (v > levelPeak) levelPeak = v;
}

void speechPump() {
    if (playing) audio.loop();
}

// Hands a finished (or abandoned) chunk's slot back to the fetcher. Only
// valid once the decoder has closed its file: after the eof callback, or
// after audio.stopSong()/connecttoFS (both close the previous file).
static void releaseHeld() {
    if (heldChunk < 0) return;
    const int slot = speechSlot(heldChunk);
    heap_caps_free(staged[slot]);
    staged[slot] = nullptr;
    stagedSize[slot] = 0;
    consumedUpTo.store(heldChunk, std::memory_order_release);   // slot reusable
    heldChunk = -1;
}

// Opens MP3 bytes for the decoder; the bytes must outlive the playback.
static bool openMemory(const uint8_t* data, size_t size) {
    audio.setVolume(volume);
    memFsImpl->src = MemReader(data, size);
    return audio.connecttoFS(memFs, "/sound.mp3");
}

// Whatever plays is over: close the decoder's file, free a held chunk.
static void silence() {
    if (playing) {
        audio.stopSong();
        playing = false;
    }
    releaseHeld();
}

void speechChirp() {
    silence();
    playing = openMemory(CHIRP_MP3, CHIRP_MP3_LEN);
    chirping = playing;
    Serial.printf("[Chirp] open=%d\n", (int)playing);
}

void speechErrorTone() {
    silence();
    playing = openMemory(ERROR_MP3, ERROR_MP3_LEN);
    chirping = false;
}

void speechStop() {
    chunkAbort = true;                             // fetch task stops between chunks
    silence();
}

static bool playChunk(int idx) {
    const int slot = speechSlot(idx);
    if (!staged[slot]) return false;
    heldChunk = idx;                               // the decoder reads the slot in place
    curStart = (int)stagedStart[slot];
    curLen = (int)stagedLen[slot];
    curBytes = stagedSize[slot];
    curPlayMs = millis();
    playing = openMemory(staged[slot], stagedSize[slot]);
    char ev[8];
    snprintf(ev, sizeof(ev), "play%d", idx);
    turnLatMark(ev);
    Serial.printf("[Chunk] play n=%d sz=%u playing=%d\n", idx,
                  (unsigned)stagedSize[slot], (int)playing);
    if (!playing) releaseHeld();                   // a failed open closed the file
    return playing;
}

SpeakStep speechTick() {
    // The chirp's end-of-file arrives ~3 s after it starts; once speech is
    // staged, the acknowledgement has done its job and gives way.
    if (playing && chirping && curChunk < fetchedUpTo.load(std::memory_order_acquire)) {
        audio.stopSong();
        playing = false;
    }
    if (playing) {
        audio.loop();                              // eof clears playing in here
        if (playing) return SpeakStep::Busy;
    }
    chirping = false;
    releaseHeld();                                 // the decoder closed its file
    if (!chunkAbort && !fetchFailed &&
        curChunk < fetchedUpTo.load(std::memory_order_acquire)) {
        curChunk++;
        if (!playChunk(curChunk)) chunkAbort = true;
        return SpeakStep::Busy;
    }
    if (endReported) return SpeakStep::Done;
    // Flag first, then the queue: speechFinish enqueues the last chunks
    // before clearing the flag, so "finished" implies the final length.
    const bool more = producing.load();
    int queued;
    {
        std::lock_guard<std::mutex> g(qMutex);
        queued = chunkQLen;
    }
    if (!chunkAbort && !fetchFailed && (more || curChunk + 1 < queued))
        return SpeakStep::Busy;                    // Hermes still talking / next chunk synthesizing
    endReported = true;
    Serial.printf("[Speak] end %s chunk=%d/%d\n",
                  chunkAbort ? "abort" : fetchFailed ? "fetch-fail" : "done",
                  curChunk + 1, queued);
    return SpeakStep::Done;
}

// ---------------------------------------------------------------- fetch task
static void ttsFetchPSRAM(const std::string& text, uint8_t*& out, size_t& outSize) {
    out = nullptr;
    outSize = 0;
    JsonDocument req;
    req["input"] = text.c_str();
    req["voice"] = TTS_VOICE;
    String body;
    serializeJson(req, body);
    HTTPClient http;
    http.begin(settingsEndpoints().ttsUrl);
    http.setTimeout(TTS_TIMEOUT_MS);
    http.addHeader("Content-Type", "application/json");
    const int code = http.POST(body);
    if (code != HTTP_CODE_OK) {
        Serial.printf("[TTS] HTTP %d: %s\n", code,
                      code > 0 ? http.getString().c_str() : "(conn)");
        http.end();
        return;
    }
    const int len = http.getSize();
    if (len < 1000 || len > (int)CHUNK_MP3_CAP) {
        Serial.printf("[TTS] bad size %d\n", len);
        http.end();
        return;
    }
    WiFiClient* s = http.getStreamPtr();
    if (!s) { http.end(); return; }
    s->setTimeout(TTS_TIMEOUT_MS);
    uint8_t* buf = (uint8_t*)heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
    if (!buf) { http.end(); return; }
    size_t got = 0;
    while (got < (size_t)len) {
        const size_t r = s->readBytes(buf + got, (size_t)len - got);
        if (r == 0) break;
        got += r;
    }
    http.end();
    if (got == (size_t)len) { out = buf; outSize = got; }
    else heap_caps_free(buf);
}

// Fetches queued chunks in order, one PSRAM slot ahead of playback. Exits
// when the queue is drained; the exit decision and the producer's
// "enqueue, then spawn if not running" both happen under qMutex, so a chunk
// queued at the moment of exit always gets a fresh task.
static void chunkFetchTask(void*) {
    for (;;) {
        const int i = fetchedUpTo.load(std::memory_order_relaxed) + 1;   // sole writer
        {
            std::lock_guard<std::mutex> g(qMutex);
            if (chunkAbort || i >= chunkQLen) {
                fetchTaskRunning = false;
                break;
            }
        }
        while (!speechSlotFree(i, consumedUpTo.load(std::memory_order_acquire)) &&
               !chunkAbort)
            delay(10);                             // wait for the slot to free
        if (chunkAbort) {
            std::lock_guard<std::mutex> g(qMutex);
            fetchTaskRunning = false;
            break;
        }
        std::string text;
        {
            std::lock_guard<std::mutex> g(qMutex);
            text = chunkQ[i % CHUNK_QUEUE];
            claimedUpTo = i;                       // no more merges into chunk i
        }
        const uint32_t start = fetchChars;
        fetchChars += text.size() + 1;
        ttsFetchPSRAM(text, staged[i & 1], stagedSize[i & 1]);
        stagedStart[i & 1] = start;
        stagedLen[i & 1] = text.size();
        if (!staged[i & 1]) {
            Serial.printf("[TTSF] chunk %d failed\n", i);
            fetchFailed = true;
            std::lock_guard<std::mutex> g(qMutex);
            fetchTaskRunning = false;
            break;
        }
        Serial.printf("[TTSF] chunk %d staged %uB\n", i, (unsigned)stagedSize[i & 1]);
        if (i == 0) turnLatMark("chunk0_staged");
        fetchedUpTo.store(i, std::memory_order_release);   // slot i&1 published
    }
    vTaskDelete(NULL);
}

// ---------------------------------------------------------------- worker side
static void enqueue(const std::string& c) {
    bool spawn = false, first = false;
    {
        std::lock_guard<std::mutex> g(qMutex);
        const int n = chunkQLen;
        if (n - CHUNK_QUEUE <= claimedUpTo) {      // slot n % Q already copied out
            chunkQ[n % CHUNK_QUEUE] = c;
            chunkQLen = n + 1;
            first = n == 0;
        } else {                                   // ring full: the newest chunk is
            std::string& last = chunkQ[(n - 1) % CHUNK_QUEUE];   // still unclaimed
            last += ' ';
            last += c;
        }
        if (!fetchTaskRunning && fetchedUpTo < chunkQLen - 1 && !fetchFailed) {
            fetchTaskRunning = true;
            spawn = true;
        }
    }
    if (first) turnLatMark("chunk0_queued");
    if (spawn && xTaskCreatePinnedToCore(chunkFetchTask, "chunkfetch", 16384, nullptr,
                                         2, nullptr, 0) != pdPASS) {
        std::lock_guard<std::mutex> g(qMutex);
        fetchTaskRunning = false;                  // task never started
    }
}

bool speechStart() {
    // Stop and reap the previous turn's fetch task before touching its state.
    // If it is still blocked in a download, nothing is reset: the stale task
    // would otherwise publish into this turn or race the slot frees.
    chunkAbort = true;
    const uint32_t reapStart = millis();
    for (;;) {
        {
            std::lock_guard<std::mutex> g(qMutex);
            if (!fetchTaskRunning) break;
        }
        if (millis() - reapStart > 4000) {
            Serial.println("[TTSF] previous fetch task still running; turn refused");
            return false;
        }
        delay(5);
    }
    // The fetch task has exited: every remaining field is ours alone.
    {
        std::lock_guard<std::mutex> g(qMutex);
        for (auto& c : chunkQ) c.clear();
        chunkQLen = 0;
        claimedUpTo = -1;
    }
    curChunk = -1;                    // nothing playing yet; speechTick starts chunk 0
    curStart = -1;
    curLen = 0;
    fetchChars = 0;
    endReported = false;
    producing = true;
    fetchedUpTo = -1;
    consumedUpTo = -1;
    fetchFailed = false;
    for (int s = 0; s < kSpeechSlots; s++) {      // both slots idle: see the handoff note
        if (staged[s]) { heap_caps_free(staged[s]); staged[s] = nullptr; stagedSize[s] = 0; }
    }
    packer.reset();
    chunkAbort = false;
    return true;
}

void speechFeed(const char* delta) {
    std::vector<std::string> chunks;
    packer.feed(delta, chunks);
    for (const auto& c : chunks) enqueue(c);
}

void speechFinish() {
    std::vector<std::string> chunks;
    packer.finish(chunks);
    for (const auto& c : chunks) enqueue(c);
    producing = false;
}

int speechChunks() {
    std::lock_guard<std::mutex> g(qMutex);
    return chunkQLen;
}
