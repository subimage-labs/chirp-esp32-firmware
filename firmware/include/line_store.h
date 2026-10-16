// line_store.h — wrapped transcript lines kept as one text arena.
//
// Each line is {offset, length, kind} into a single byte buffer, the text
// NUL-terminated in place, so a window of rows is drawn straight from the
// arena without copying and a whole chat costs two allocations however many
// lines it has (instead of one small block per line). Both containers take
// their memory from the allocator `Alloc`: PsramAlloc on the device
// (psram_alloc.h), anything on the host. Owner: the loop task (ui.cpp).
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

template <template <class> class Alloc>
class BasicLineStore {
public:
    size_t size() const { return lines_.size(); }
    bool empty() const { return lines_.empty(); }

    // Appends a line of `n` bytes (UTF-8, no NUL inside) of the given kind.
    void push(const char* s, size_t n, uint8_t kind) {
        Line l;
        l.off = (uint32_t)text_.size();
        l.len = (uint32_t)n;
        l.kind = kind;
        text_.append(s, n);
        text_.push_back('\0');
        lines_.push_back(l);
    }

    uint8_t kind(size_t i) const { return lines_[i].kind; }
    // The line as a NUL-terminated string; valid until the next change.
    const char* text(size_t i) const { return text_.data() + lines_[i].off; }
    size_t length(size_t i) const { return lines_[i].len; }
    uint8_t lastKind() const { return lines_.back().kind; }

    void popBack() {
        text_.resize(lines_.back().off);
        lines_.pop_back();
    }

    // Keeps the buffers' capacity: re-wrapping a streaming answer on every
    // delta reuses them instead of churning the heap.
    void clear() {
        text_.clear();
        lines_.clear();
    }

    size_t textBytes() const { return text_.size(); }

private:
    struct Line {
        uint32_t off;
        uint32_t len;
        uint8_t kind;
    };
    std::basic_string<char, std::char_traits<char>, Alloc<char> > text_;
    std::vector<Line, Alloc<Line> > lines_;
};
