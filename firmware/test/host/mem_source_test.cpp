// Host tests for mem_source.h: the memory "file" the decoder reads and the
// PSRAM slot-reuse rule.
#include "mem_source.h"

#include <stdio.h>

#include <vector>

static int failures = 0;
#define CHECK(c)                                                              \
    do {                                                                      \
        if (!(c)) {                                                           \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);               \
            failures++;                                                       \
        }                                                                     \
    } while (0)

static void testRead() {
    uint8_t src[10];
    for (int i = 0; i < 10; i++) src[i] = (uint8_t)(i + 1);
    MemReader r(src, sizeof(src));
    CHECK(r.size() == 10);
    CHECK(r.position() == 0);
    uint8_t buf[16] = {0};
    CHECK(r.read(buf, 4) == 4);
    CHECK(buf[0] == 1 && buf[3] == 4);
    CHECK(r.position() == 4);
    CHECK(r.read(buf, 100) == 6);                  // clipped to what is left
    CHECK(buf[0] == 5 && buf[5] == 10);
    CHECK(r.position() == 10);
    CHECK(r.read(buf, 1) == 0);                    // end of file
    CHECK(r.read(buf, 0) == 0);
}

static void testSeek() {
    uint8_t src[10];
    for (int i = 0; i < 10; i++) src[i] = (uint8_t)i;
    MemReader r(src, sizeof(src));
    uint8_t b = 0;
    CHECK(r.seek(7, MemSeek::Set));
    CHECK(r.read(&b, 1) == 1 && b == 7);
    CHECK(r.seek(10, MemSeek::Set));               // the end itself is valid
    CHECK(r.read(&b, 1) == 0);
    CHECK(!r.seek(11, MemSeek::Set));              // past the end fails...
    CHECK(r.position() == 10);                     // ...and leaves the position
    CHECK(r.seek(2, MemSeek::Set));
    CHECK(r.seek(3, MemSeek::Cur));                // forward from 2
    CHECK(r.position() == 5);
    CHECK(!r.seek(6, MemSeek::Cur));               // 5 + 6 > 10
    CHECK(r.position() == 5);
    CHECK(r.seek(4, MemSeek::End));                // backwards from the end
    CHECK(r.position() == 6);
    CHECK(r.read(&b, 1) == 1 && b == 6);
    CHECK(r.seek(0, MemSeek::End));
    CHECK(r.position() == 10);
    CHECK(!r.seek(11, MemSeek::End));              // before the start
    CHECK(r.position() == 10);
}

static void testClearAndEmpty() {
    uint8_t src[4] = {1, 2, 3, 4};
    MemReader r(src, sizeof(src));
    uint8_t buf[4];
    r.clear();                                     // owner is about to free the bytes
    CHECK(r.size() == 0);
    CHECK(r.read(buf, 4) == 0);
    CHECK(!r.seek(1, MemSeek::Set));
    MemReader e;                                   // default: empty, never touches memory
    CHECK(e.size() == 0 && e.read(buf, 4) == 0);
    MemReader z(nullptr, 0);
    CHECK(z.read(buf, 4) == 0);
}

static void testSlotRule() {
    CHECK(speechSlot(0) == 0 && speechSlot(1) == 1);
    CHECK(speechSlot(2) == 0 && speechSlot(7) == 1);
    // Nothing consumed yet: the first two chunks may fill, a third may not
    // (it would overwrite chunk 0 while that plays).
    CHECK(speechSlotFree(0, -1));
    CHECK(speechSlotFree(1, -1));
    CHECK(!speechSlotFree(2, -1));
        CHECK(speechSlotFree(2, 0));                   // chunk 0 finished and released
    CHECK(!speechSlotFree(3, 0));
    CHECK(speechSlotFree(3, 1));
}

// Replays the loop/fetch protocol: the fetcher fills chunks in order whenever
// the slot rule allows; the player plays a chunk to completion, then
// releases. The slot a chunk occupies must never be written while it plays.
static void testPipelineNeverOverwritesPlayingSlot() {
    const int total = 9;
    int fetched = -1, consumed = -1;
    int playing = -1;                              // chunk the decoder reads, -1: none
    int slotOwner[kSpeechSlots] = {-1, -1};        // chunk occupying each slot
    bool aheadWhilePlaying = false;
    // One download per call, like the real task (each takes a while).
    auto fetchOne = [&]() {
        const int c = fetched + 1;
        if (c >= total || !speechSlotFree(c, consumed)) return;
        const int s = speechSlot(c);
        CHECK(slotOwner[s] == -1);                 // slot was released first
        if (playing >= 0 && c > playing) aheadWhilePlaying = true;
        slotOwner[s] = c;
        fetched = c;
    };
    for (int step = 0; consumed < total - 1 && step < 1000; step++) {
        fetchOne();
        if (playing < 0 && consumed < fetched) playing = consumed + 1;
        fetchOne();                                // concurrent with playback
        if (playing >= 0) {
            CHECK(slotOwner[speechSlot(playing)] == playing);   // intact until done
            slotOwner[speechSlot(playing)] = -1;   // decoder closed: release
            consumed = playing;
            playing = -1;
        }
    }
    CHECK(consumed == total - 1);
    CHECK(aheadWhilePlaying);                      // the next chunk downloads during playback
}

int main() {
    testRead();
    testSeek();
    testClearAndEmpty();
    testSlotRule();
    testPipelineNeverOverwritesPlayingSlot();
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("mem_source_test: ok\n");
    return 0;
}
