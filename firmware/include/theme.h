// theme.h — screen colors by role, the Solarized dark/light palettes, RGB565
// blending, and the status-text shimmer curve. Pure (no Arduino),
// host-tested; ui.cpp owns the current theme (loop task).
#pragma once

#include <stdint.h>

// What a color is for; every screen draws through these, never raw colors.
enum class Ink : uint8_t {
    Bg,       // background
    Surface,  // selection bar, highlighted row
    Muted,    // hints, rules, inactive tab, quiet status
    Text,     // body text: questions, unselected rows
    Strong,   // emphasis: answers, selected rows, values
    Accent,   // headings, active tab, voice mode
    Listen,   // recording / hearing
    Busy,     // working (the shimmer highlight)
    Ok,       // ready / saved
    Err,      // failures
    Count,    // the palette roles above; derived roles follow
    Faint,    // derived: Muted at 25 % over Bg (menu dividers)
};

// 0xRRGGBB → RGB565.
constexpr uint16_t rgb565(uint32_t c) {
    return (uint16_t)(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F));
}

// One palette: a color per Ink, in Ink order.
struct Palette {
    uint16_t ink[(int)Ink::Count];
};

#define PALETTE(bg, surface, muted, text, strong, accent, listen, busy, ok, err)         \
    {{rgb565(bg), rgb565(surface), rgb565(muted), rgb565(text), rgb565(strong),           \
      rgb565(accent), rgb565(listen), rgb565(busy), rgb565(ok), rgb565(err)}}

// A theme family has a dark and a light palette; the device picks the family
// and the mode separately. Colors are each scheme's published palette, with
// readability adjustments for the small screen outdoors: Everforest light's
// body text uses its fg tones (the grey ones fall to 3:1); the light
// Catppuccin, Rosé Pine and Everforest listen color is the scheme's blue
// (their yellows are near-invisible on cream); Solarized dark's text moves
// up a step (questions base1, answers base2) because base0/base1 read washed
// out on the panel.
struct ThemeFamily {
    const char* name;
    Palette dark, light;
};

// Alphabetical: the Theme tab lists them in this order. The device stores
// the choice by name, so the order can change freely.
static const ThemeFamily THEMES[] = {
    {"Catppuccin",                                  // Mocha / Latte
     PALETTE(0x1e1e2e, 0x313244, 0x6c7086, 0xbac2de, 0xcdd6f4, 0xcba6f7, 0xf9e2af, 0xfab387,
             0xa6e3a1, 0xf38ba8),
     PALETTE(0xeff1f5, 0xccd0da, 0x9ca0b0, 0x5c5f77, 0x4c4f69, 0x8839ef, 0x1e66f5, 0xfe640b,
             0x40a02b, 0xd20f39)},
    {"Everforest",
     PALETTE(0x2d353b, 0x3d484d, 0x859289, 0x9da9a0, 0xd3c6aa, 0x83c092, 0xdbbc7f, 0xe69875,
             0xa7c080, 0xe67e80),
     PALETTE(0xfdf6e3, 0xefebd4, 0x939f91, 0x5c6a72, 0x2d353b, 0x35a77c, 0x3a94c5, 0xf57d26,
             0x8da101, 0xf85552)},
    {"Gruvbox",
     PALETTE(0x282828, 0x3c3836, 0x928374, 0xd5c4a1, 0xfbf1c7, 0x8ec07c, 0xfabd2f, 0xfe8019,
             0xb8bb26, 0xfb4934),
     PALETTE(0xfbf1c7, 0xebdbb2, 0x928374, 0x504945, 0x282828, 0x427b58, 0xb57614, 0xaf3a03,
             0x79740e, 0x9d0006)},
    {"Rose Pine",                                   // Rosé Pine main / Dawn
     PALETTE(0x191724, 0x26233a, 0x6e6a86, 0x908caa, 0xe0def4, 0xc4a7e7, 0xf6c177, 0xebbcba,
             0x9ccfd8, 0xeb6f92),
     PALETTE(0xfaf4ed, 0xf2e9e1, 0x9893a5, 0x797593, 0x575279, 0x907aa9, 0x286983, 0xd7827e,
             0x56949f, 0xb4637a)},
    {"Solarized",
     PALETTE(0x002b36, 0x073642, 0x586e75, 0x93a1a1, 0xeee8d5, 0x2aa198, 0xb58900, 0xcb4b16,
             0x859900, 0xdc322f),
     PALETTE(0xfdf6e3, 0xeee8d5, 0x93a1a1, 0x657b83, 0x586e75, 0x2aa198, 0xb58900, 0xcb4b16,
             0x859900, 0xdc322f)},
    {"Tokyo Night",                                 // Night / Day
     PALETTE(0x1a1b26, 0x292e42, 0x565f89, 0xa9b1d6, 0xc0caf5, 0x7aa2f7, 0xe0af68, 0xff9e64,
             0x9ece6a, 0xf7768e),
     PALETTE(0xe1e2e7, 0xc4c8da, 0x848cb5, 0x6172b0, 0x3760bf, 0x2e7de9, 0x8c6c3e, 0xb15c00,
             0x587539, 0xf52a65)},
};
#undef PALETTE
static const uint8_t THEME_COUNT = sizeof(THEMES) / sizeof(THEMES[0]);
static const char* const THEME_DEFAULT = "Solarized";   // dark

// Index of the family with that name; -1 when there is none (a stored name
// from a build that dropped it).
inline int themeFind(const char* name) {
    if (!name) return -1;
    for (uint8_t i = 0; i < THEME_COUNT; i++) {
        const char* a = THEMES[i].name;
        const char* b = name;
        while (*a && *a == *b) a++, b++;
        if (!*a && !*b) return i;
    }
    return -1;
}

// Bench `theme <n>`: family * 2 + (light ? 1 : 0), in list order.
inline uint8_t themeCode(uint8_t family, bool light) { return (uint8_t)(family * 2 + light); }
inline bool themeCodeValid(uint8_t code) { return code < THEME_COUNT * 2; }

// a → b by level/255, per channel.
inline uint16_t blend565(uint16_t a, uint16_t b, uint8_t level) {
    const uint32_t l = level, m = 255 - level;
    const uint32_t r = (((a >> 11) & 0x1F) * m + ((b >> 11) & 0x1F) * l + 127) / 255;
    const uint32_t g = (((a >> 5) & 0x3F) * m + ((b >> 5) & 0x3F) * l + 127) / 255;
    const uint32_t bl = ((a & 0x1F) * m + (b & 0x1F) * l + 127) / 255;
    return (uint16_t)((r << 11) | (g << 5) | bl);
}

// Shimmer: a soft bright band sweeps left to right through a label of n
// characters, then rests before the next pass. Returns how lit character i
// is at time tMs (0 = base color, 255 = full highlight).
static const uint32_t SHIMMER_CHAR_MS = 55;     // band speed: ms per character
static const uint32_t SHIMMER_REST_MS = 350;    // pause between passes
static const int SHIMMER_HALF = 3;              // band half-width, in characters

inline uint8_t shimmerLevel(int i, int n, uint32_t tMs) {
    if (n <= 0 || i < 0 || i >= n) return 0;
    // The centre travels from SHIMMER_HALF before the first character to
    // SHIMMER_HALF past the last, in 1/16-character steps.
    const uint32_t travel = (uint32_t)(n + 2 * SHIMMER_HALF) * SHIMMER_CHAR_MS;
    const uint32_t t = tMs % (travel + SHIMMER_REST_MS);
    if (t >= travel) return 0;
    const int32_t centre16 = (int32_t)(t * 16 / SHIMMER_CHAR_MS) - SHIMMER_HALF * 16;
    int32_t d16 = (int32_t)i * 16 + 8 - centre16;          // to the character's middle
    if (d16 < 0) d16 = -d16;
    if (d16 >= SHIMMER_HALF * 16) return 0;
    // Smoothstep falloff: bright core, soft edges.
    const int32_t x = 256 - d16 * 256 / (SHIMMER_HALF * 16);   // 0..256
    const int32_t s = x * x * (768 - 2 * x) / 65536;            // 3x² - 2x³, 0..256
    return (uint8_t)(s > 255 ? 255 : s);
}
