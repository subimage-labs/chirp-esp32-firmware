// textfold.h — makes arbitrary UTF-8 safe to draw with the device font: every
// code point the font has passes through, look-alikes replace the ones it
// lacks, the rest are dropped, so nothing ever renders as a box. Pure (no
// Arduino), host-tested.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <string>

// The next code point of a UTF-8 string, advancing p. Malformed input yields
// U+FFFD and consumes one byte.
inline uint32_t utf8Next(const char*& p, const char* end) {
    const uint8_t c = (uint8_t)*p++;
    if (c < 0x80) return c;
    int extra;
    uint32_t cp;
    if ((c & 0xE0) == 0xC0) { extra = 1; cp = c & 0x1F; }
    else if ((c & 0xF0) == 0xE0) { extra = 2; cp = c & 0x0F; }
    else if ((c & 0xF8) == 0xF0) { extra = 3; cp = c & 0x07; }
    else return 0xFFFD;
    if (end - p < extra) return 0xFFFD;
    for (int i = 0; i < extra; i++) {
        const uint8_t n = (uint8_t)p[i];
        if ((n & 0xC0) != 0x80) return 0xFFFD;
        cp = (cp << 6) | (n & 0x3F);
    }
    p += extra;
    return cp;
}

inline void utf8Append(std::string& out, uint32_t cp) {
    if (cp < 0x80) out += (char)cp;
    else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) {
        out += (char)(0xE0 | (cp >> 12));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    } else {
        out += (char)(0xF0 | (cp >> 18));
        out += (char)(0x80 | ((cp >> 12) & 0x3F));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    }
}

// ASCII bases of U+0100..U+017F (accents dropped). U+0132/0133/0152/0153 are
// the ligatures IJ ij OE oe, handled apart.
static const char LATIN_EXT_A[] =
    "AaAaAaCcCcCcCcDdDd"       // 0100..0111
    "EeEeEeEeEe"               // 0112..011B
    "GgGgGgGg"                 // 011C..0123
    "HhHh"                     // 0124..0127
    "IiIiIiIiIi"               // 0128..0131
    "Ii"                       // 0132..0133
    "Jj" "Kkk"                 // 0134..0138
    "LlLlLlLlLl"               // 0139..0142
    "NnNnNnnNn"                // 0143..014B
    "OoOoOo" "Oo"              // 014C..0153
    "RrRrRr"                   // 0154..0159
    "SsSsSsSs"                 // 015A..0161
    "TtTtTt"                   // 0162..0167
    "UuUuUuUuUuUu"             // 0168..0173
    "Ww" "YyY" "ZzZzZz" "s";   // 0174..017F
static_assert(sizeof(LATIN_EXT_A) - 1 == 0x80, "one base letter per U+0100..U+017F");

// A plain-ASCII stand-in for a code point the font lacks; "" when there is
// none (the code point is dropped).
inline const char* foldSubstitute(uint32_t cp, char (&one)[2]) {
    one[1] = 0;
    switch (cp) {
    case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2015:
    case 0x2212: return "-";
    case 0x2018: case 0x2019: case 0x201B: case 0x2032: case 0x00B4: case 0x02BC: return "'";
    case 0x201C: case 0x201D: case 0x201F: case 0x201E: case 0x2033: return "\"";
    case 0x201A: return ",";
    case 0x2026: return "...";
    case 0x2022: case 0x00B7: case 0x25CF: case 0x25E6: case 0x2023: return "*";
    case 0x2190: return "<-";
    case 0x2192: return "->";
    case 0x20AC: return "EUR";
    case 0x2122: return "TM";
    case 0x00D7: return "x";
    case 0x2039: return "<";
    case 0x203A: return ">";
    case 0x0132: return "IJ";
    case 0x0133: return "ij";
    case 0x0152: return "OE";
    case 0x0153: return "oe";
    default: break;
    }
    if (cp >= 0x100 && cp <= 0x17F) { one[0] = LATIN_EXT_A[cp - 0x100]; return one; }
    return "";
}

// Spaces of every width and the line separators become one space; invisible
// formatting and combining marks vanish.
inline bool foldIsSpace(uint32_t cp) {
    return cp == 0x09 || cp == 0xA0 || (cp >= 0x2000 && cp <= 0x200A) || cp == 0x202F ||
           cp == 0x205F || cp == 0x3000 || cp == 0x2028 || cp == 0x2029;
}
inline bool foldIsInvisible(uint32_t cp) {
    return (cp >= 0x200B && cp <= 0x200F) || (cp >= 0x202A && cp <= 0x202E) ||
           (cp >= 0x2060 && cp <= 0x206F) || (cp >= 0xFE00 && cp <= 0xFE0F) ||
           (cp >= 0x0300 && cp <= 0x036F) || cp == 0xFEFF || cp == 0xAD ||
           (cp >= 0x1F3FB && cp <= 0x1F3FF) || cp == 0xFFFD;
}

// has(cp): does the font draw this code point? Called only for code points
// above U+0020. Output holds '\n', ' ' and code points the font has.
template <class Has>
std::string foldText(const char* in, size_t n, Has has) {
    std::string out;
    out.reserve(n);
    const char* p = in;
    const char* end = in + n;
    while (p < end) {
        const uint32_t cp = utf8Next(p, end);
        if (cp == '\n' || cp == ' ') { out += (char)cp; continue; }
        if (foldIsSpace(cp)) { out += ' '; continue; }
        if (cp < 0x20 || (cp >= 0x7F && cp < 0xA0) || foldIsInvisible(cp)) continue;
        if (has(cp)) { utf8Append(out, cp); continue; }
        char one[2];
        out += foldSubstitute(cp, one);
    }
    return out;
}
