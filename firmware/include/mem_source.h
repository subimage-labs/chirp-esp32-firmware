// mem_source.h — pure logic behind playing MP3 straight from memory.
//
// MemReader is the read-only "file" the decoder sees (speech.cpp wraps it in
// an fs::FS so ESP32-audioI2S's connecttoFS reads RAM, PSRAM or flash-mapped
// data with no copy). Slot policy says when a PSRAM chunk slot may be refilled.
// No Arduino includes: host-tested (test/host/mem_source_test.cpp).
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum class MemSeek : uint8_t { Set, Cur, End };

// A byte range with a read position. Does not own the bytes; the owner keeps
// them alive until the decoder has closed its file (speech.cpp: slot lifetime).
class MemReader {
public:
    MemReader() : data_(nullptr), size_(0), pos_(0) {}
    MemReader(const uint8_t* data, size_t size) : data_(data), size_(size), pos_(0) {}

    size_t size() const { return size_; }
    size_t position() const { return pos_; }

    // Copies up to n bytes from the position; 0 at the end.
    size_t read(uint8_t* out, size_t n) {
        const size_t left = size_ - pos_;
        if (n > left) n = left;
        if (n) memcpy(out, data_ + pos_, n);
        pos_ += n;
        return n;
    }

    // Like fseek, except a target outside [0, size] fails and leaves the
    // position alone.
    bool seek(uint32_t off, MemSeek mode) {
        size_t base = 0;
        if (mode == MemSeek::Cur) base = pos_;
        else if (mode == MemSeek::End) base = size_;
        if (mode == MemSeek::End) {
            if (off > size_) return false;
            pos_ = size_ - off;                    // End: backwards from the end
            return true;
        }
        if (off > size_ - base) return false;
        pos_ = base + off;
        return true;
    }

    // Forget the bytes: later reads return 0, so a stale handle can never
    // touch memory its owner has since freed.
    void clear() { *this = MemReader(); }

private:
    const uint8_t* data_;
    size_t size_;
    size_t pos_;
};

// PSRAM chunk slots: chunk i lives in slot i % kSpeechSlots. A slot holds its
// chunk until the decoder has finished with it (not merely started), so the
// next chunk downloads into the other slot while this one plays.
static const int kSpeechSlots = 2;

inline int speechSlot(int chunk) { return chunk % kSpeechSlots; }

// May the fetcher fill chunk `chunk`? Its slot's previous occupant is chunk
// `chunk - kSpeechSlots`; consumedUpTo is the highest chunk whose playback
// finished and whose slot was released (-1: none).
inline bool speechSlotFree(int chunk, int consumedUpTo) {
    return chunk - kSpeechSlots <= consumedUpTo;
}
