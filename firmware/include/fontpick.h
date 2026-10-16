// fontpick.h — the font choice and the layout derived from it. Pure (no
// Arduino), host-tested; ui.cpp owns the current choice (loop task).
//
// A font family (font_data.h) comes in three sizes, S M L. The choice is
// stored by family NAME and size LETTER, never by table position, so
// reordering or adding families never changes what a device shows. Every
// text height on screen derives from the line box of the active cut
// (uiMetricsFor), and text that may not fit is cut to width here.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <string>

#include "font_data.h"
#include "textfold.h"

enum : uint8_t { FONT_SIZE_S, FONT_SIZE_M, FONT_SIZE_L };
static const uint8_t FONT_SIZE_COUNT = 3;
static const uint8_t FONT_SIZE_DEFAULT = FONT_SIZE_M;

inline char fontSizeLetter(uint8_t s) { return "SML"[s < FONT_SIZE_COUNT ? s : FONT_SIZE_DEFAULT]; }

// "S", "M" or "L" (any case) → size; -1 for anything else.
inline int fontSizeFind(const char* s) {
    if (!s || !s[0] || s[1]) return -1;
    const char c = (char)(s[0] & ~0x20);
    for (uint8_t i = 0; i < FONT_SIZE_COUNT; i++)
        if (c == fontSizeLetter(i)) return i;
    return -1;
}

inline uint8_t fontSizeNext(uint8_t s) { return (uint8_t)((s + 1) % FONT_SIZE_COUNT); }

// Family by exact name → table index; -1 when unknown.
inline int fontFind(const char* name) {
    if (!name) return -1;
    for (size_t i = 0; i < FONT_FAMILY_COUNT; i++)
        if (strcmp(FONT_FAMILIES[i].name, name) == 0) return (int)i;
    return -1;
}

// The stored choice → what to show: unknown names or sizes fall back to the
// default, each on its own (a good name keeps its family).
struct FontChoice {
    uint8_t family;
    uint8_t size;
};
inline FontChoice fontChoiceFrom(const char* name, const char* size) {
    int f = fontFind(name);
    if (f < 0) f = fontFind(FONT_DEFAULT_FAMILY);
    int s = fontSizeFind(size);
    if (s < 0) s = FONT_SIZE_DEFAULT;
    return {(uint8_t)f, (uint8_t)s};
}

// ---------------------------------------------------------------- VLW reads
// The same table TFT_eSPI::loadMetrics reads: 24 header bytes, then one
// 28-byte record per glyph (code, height, width, advance, dY, dX, 0), all
// big-endian. Glyph codes ascend, so lookups are a binary search.
inline uint32_t vlwBe32(const uint8_t* d, size_t at) {
    return (uint32_t)d[at] << 24 | (uint32_t)d[at + 1] << 16 | (uint32_t)d[at + 2] << 8 | d[at + 3];
}

inline bool vlwFind(const FontCut& f, uint32_t cp, size_t& index) {
    size_t lo = 0, hi = f.glyphs;
    while (lo < hi) {
        const size_t mid = (lo + hi) / 2;
        if (vlwBe32(f.vlw, 24 + 28 * mid) < cp) lo = mid + 1;
        else hi = mid;
    }
    index = lo;
    return lo < f.glyphs && vlwBe32(f.vlw, 24 + 28 * lo) == cp;
}

inline bool vlwHas(const FontCut& f, uint32_t cp) {
    size_t i;
    return vlwFind(f, cp, i);
}

// Advance of a code point; the UI draws spaces as U+00A0 (the font's own space
// width). -1 when the font lacks it.
inline int vlwAdvance(const FontCut& f, uint32_t cp) {
    size_t i;
    if (cp == ' ') cp = 0xA0;
    return vlwFind(f, cp, i) ? (int)vlwBe32(f.vlw, 24 + 28 * i + 12) : -1;
}

// Width of UTF-8 text as the UI measures it (glyphs the font lacks add nothing).
inline int vlwWidth(const FontCut& f, const char* s) {
    int w = 0;
    const char* p = s;
    const char* end = s + strlen(s);
    while (p < end) {
        const int a = vlwAdvance(f, utf8Next(p, end));
        if (a > 0) w += a;
    }
    return w;
}

// ---------------------------------------------------------------- layout
// Every text height derives from the body line box in one place.
struct UiMetrics {
    int line;     // transcript line pitch
    int row;      // menu row pitch
    int status;   // bottom status strip
    int header;   // tab names + rule
    int button;   // a menu action drawn as a button (outline, padding, gap)
    int pinned;   // the Chats tab's pinned "+ New chat" button and the gap under it
};

inline UiMetrics uiMetricsFor(int box) {
    return {box, box + 2, box + 4, box + 6, box + 10, box + 16};
}

// Cuts a string to `avail` px, marking the cut with "…". `width` measures a
// UTF-8 string. Never splits a UTF-8 sequence; returns s untouched when it fits.
template <class Width>
std::string fitText(std::string s, int avail, Width width) {
    if (width(s.c_str()) <= avail) return s;
    const int limit = avail - width("\xE2\x80\xA6");
    while (!s.empty() && width(s.c_str()) > limit) {
        size_t n = s.size();
        while (n > 0 && ((uint8_t)s[n - 1] & 0xC0) == 0x80) n--;   // continuation bytes
        s.erase(n > 0 ? n - 1 : 0);
    }
    return s + "\xE2\x80\xA6";
}

// The tab header: names with a bar between each, `pad` px either side of the
// bar. The widest pad in [minPad, maxPad] whose row fits `avail`; -1 when even
// minPad does not.
inline int headerPad(const int* nameW, int n, int barW, int avail, int maxPad, int minPad) {
    int names = 0;
    for (int i = 0; i < n; i++) names += nameW[i];
    for (int pad = maxPad; pad >= minPad; pad--)
        if (names + (n - 1) * (barW + 2 * pad) <= avail) return pad;
    return -1;
}
