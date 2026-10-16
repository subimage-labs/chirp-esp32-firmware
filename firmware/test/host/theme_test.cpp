// Host tests for theme colors and the status shimmer. Run: firmware/test/host/run.sh
#include "theme.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>

static int failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,   \
                         __LINE__, #cond);                                \
            failures++;                                                   \
        }                                                                 \
    } while (0)

static void rgb() {
    CHECK(rgb565(0xFFFFFF) == 0xFFFF);
    CHECK(rgb565(0x000000) == 0x0000);
    CHECK(rgb565(0xFF0000) == 0xF800);
    CHECK(rgb565(0x00FF00) == 0x07E0);
    CHECK(rgb565(0x0000FF) == 0x001F);
}

static void blending() {
    const uint16_t a = rgb565(0x586e75), b = rgb565(0xcb4b16);
    CHECK(blend565(a, b, 0) == a);
    CHECK(blend565(a, b, 255) == b);
    // Halfway lands between the two on every channel.
    const uint16_t m = blend565(0x0000, 0xFFFF, 128);
    CHECK(((m >> 11) & 0x1F) >= 15 && ((m >> 11) & 0x1F) <= 16);
    CHECK(((m >> 5) & 0x3F) >= 31 && ((m >> 5) & 0x3F) <= 32);
}

// WCAG relative luminance of an RGB565 color.
static double luminance(uint16_t c) {
    const double ch[3] = {((c >> 11) & 0x1F) / 31.0, ((c >> 5) & 0x3F) / 63.0, (c & 0x1F) / 31.0};
    double lin[3];
    for (int i = 0; i < 3; i++)
        lin[i] = ch[i] <= 0.03928 ? ch[i] / 12.92 : std::pow((ch[i] + 0.055) / 1.055, 2.4);
    return 0.2126 * lin[0] + 0.7152 * lin[1] + 0.0722 * lin[2];
}

static double contrast(uint16_t a, uint16_t b) {
    double la = luminance(a), lb = luminance(b);
    if (la < lb) std::swap(la, lb);
    return (la + 0.05) / (lb + 0.05);
}

// Every palette stays readable on the small screen: body text, answers, the
// listening and error states against the background; the selection bar and
// the shimmer highlight visibly differ from what they sit on.
static void palettesReadable() {
    for (uint8_t f = 0; f < THEME_COUNT; f++) {
        const Palette* both[2] = {&THEMES[f].dark, &THEMES[f].light};
        for (const Palette* p : both) {
            const uint16_t* ink = p->ink;
            const uint16_t bg = ink[(int)Ink::Bg];
            CHECK(contrast(ink[(int)Ink::Text], bg) >= 3.0);
            CHECK(contrast(ink[(int)Ink::Strong], bg) >= 4.0);
            if (p == both[0])                         // dark: answers must be crisp
                CHECK(contrast(ink[(int)Ink::Strong], bg) >= 7.0);
            CHECK(contrast(ink[(int)Ink::Listen], bg) >= 2.8);
            CHECK(contrast(ink[(int)Ink::Err], bg) >= 2.8);
            CHECK(ink[(int)Ink::Surface] != bg);
            CHECK(ink[(int)Ink::Busy] != ink[(int)Ink::Muted]);
        }
        // "Light" is the light one.
        CHECK(luminance(THEMES[f].light.ink[(int)Ink::Bg]) >
              luminance(THEMES[f].dark.ink[(int)Ink::Bg]) + 0.5);
    }
}

// The Theme tab lists THEMES in order: it must stay alphabetical, and every
// stored name must find its family again.
static void themeNames() {
    for (uint8_t f = 0; f + 1 < THEME_COUNT; f++)
        CHECK(std::strcmp(THEMES[f].name, THEMES[f + 1].name) < 0);   // sorted, no duplicates
    for (uint8_t f = 0; f < THEME_COUNT; f++) CHECK(themeFind(THEMES[f].name) == f);
    CHECK(themeFind(THEME_DEFAULT) >= 0);
    CHECK(themeFind("Solarize") < 0 && themeFind("Solarized ") < 0);   // exact names only
    CHECK(themeFind("") < 0 && themeFind(nullptr) < 0);
}

static uint32_t passMs(int n) {
    return (uint32_t)(n + 2 * SHIMMER_HALF) * SHIMMER_CHAR_MS;
}

// One pass lights every character fully, left to right, then rests dark.
static void shimmerSweeps() {
    const int n = 8;
    int prevPeakT = -1;
    for (int i = 0; i < n; i++) {
        int peak = 0, peakT = 0;
        for (uint32_t t = 0; t < passMs(n); t++) {
            const int l = shimmerLevel(i, n, t);
            if (l > peak) { peak = l; peakT = (int)t; }
        }
        CHECK(peak >= 240);
        CHECK(peakT > prevPeakT);
        prevPeakT = peakT;
    }
    for (uint32_t t = passMs(n); t < passMs(n) + SHIMMER_REST_MS; t += 7)
        for (int i = 0; i < n; i++) CHECK(shimmerLevel(i, n, t) == 0);
    // The next pass starts again from the left.
    const uint32_t cycle = passMs(n) + SHIMMER_REST_MS;
    for (uint32_t t = 0; t < passMs(n); t += 13)
        CHECK(shimmerLevel(3, n, t) == shimmerLevel(3, n, t + cycle));
}

// The band is a band: never more than its width lit at once.
static void shimmerBandWidth() {
    const int n = 12;
    for (uint32_t t = 0; t < passMs(n); t += 5) {
        int lit = 0;
        for (int i = 0; i < n; i++) lit += shimmerLevel(i, n, t) > 0;
        CHECK(lit <= 2 * SHIMMER_HALF);
    }
}

static void shimmerBounds() {
    CHECK(shimmerLevel(0, 0, 100) == 0);
    CHECK(shimmerLevel(-1, 5, 100) == 0);
    CHECK(shimmerLevel(5, 5, 100) == 0);
    CHECK(shimmerLevel(0, 1, 0xFFFFFFF0u) <= 255);    // millis() wrap-safe
}

int main() {
    rgb();
    blending();
    palettesReadable();
    themeNames();
    shimmerSweeps();
    shimmerBandWidth();
    shimmerBounds();
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("theme: all checks passed\n");
    return 0;
}
