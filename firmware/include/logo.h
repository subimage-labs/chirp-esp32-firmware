// logo.h — the chirp! mark, "Perch": the hand-pixelled wordmark "chirp!" with
// a tiny chick perched as the dot of the i. Pure (no Arduino), host-tested;
// logo_draw.cpp paints it (loop task): the boot splash, the empty chat's
// title, the Setup tab, and (baked into logo_web.h) the setup web page.
//
// The art is ASCII rows of fills (ROWS below, the editable source); one
// character is one logical pixel. render() turns it into a frame of palette
// slots the way the characters are drawn (avatar.h): a deep outline all the
// way round every shape, and a one-pixel glow ring (Rim) outside it whose
// color follows the theme: the rim tint at half strength over dark
// backgrounds, the background itself (no glow) on light ones. paletteFor(bg) gives the slot
// colors; the logo's own colors are the same on every theme. Other designs
// in the same format: firmware/tools/logo_options/.
//
// Preview on all theme backgrounds: firmware/tools/logo_preview.py.
#pragma once

#include <stdint.h>
#include <string.h>

#include "avatar.h"
#include "theme.h"

namespace logo {

enum Slot : uint8_t {
    Clear,                  // the theme background
    Outline,                // deep, hue-tinted: lines between parts
    Rim,                    // the glow beside the outline: follows the theme background
    BodyD, Body, BodyL,     // the yellow ramp: the chick's body, the letters
    Belly,
    AccD, Acc, AccL,        // the orange ramp: the beak, feet, the exclamation mark
    White, Pupil, Cheek,
    SlotCount,
};

// Art characters: '.' the background, 'o' an outline line drawn by hand, and
// one letter per color slot.
inline uint8_t slotOf(char c) {
    switch (c) {
    case '.': return Clear;
    case 'o': return Outline;
    case 'd': return BodyD;
    case 'b': return Body;
    case 'h': return BodyL;
    case 'f': return Belly;
    case 'r': return AccD;
    case 'a': return Acc;
    case 'e': return AccL;
    case 'w': return White;
    case 'p': return Pupil;
    case 'c': return Cheek;
    default: return SlotCount;     // not an art character
    }
}

// ---------------------------------------------------------------- art
// Legend: h/b/d body light/mid/dark · f belly · e/a/r orange light/mid/dark ·
// w white · p pupil · c cheek · o outline line. c h i r p ! with a two-pixel
// stroke, x-height 7, lit from the top left; the chick (columns 12-17, rows
// 0-4) is the i's dot; the orange "!" is the last two columns.
const int ART_W = 32, ART_H = 18;
const int CHICK_X = 12, CHICK_W = 6, CHICK_H = 5;   // the chick: its columns and rows
const int BANG_X = 30;                              // the exclamation mark: columns BANG_X .. BANG_X + 1
static const char* const ROWS[ART_H] = {
    ".............hbbh...............",
    "............hbbbbd..............",
    "............hpbbpd..............",
    "............hbaabd..............",
    ".......hh....dddd.............ee",
    ".......hd.....................er",
    ".......hd.....................er",
    ".......hd.....................er",
    ".hhhh..hbhhh..hh.hhhh..hhhhh..er",
    "hd..dh.hbddbh.hd.hd.dh.hd..hh.er",
    "hd.....hd..hd.hd.hd....hd..hd.rr",
    "hd.....hd..hd.hd.hd....hd..hd...",
    "hd.....hd..hd.hd.hd....hd..hd...",
    "dd..hh.hd..hd.hd.hd....hd..hd.ee",
    ".dhhd..dd..dd.dd.dd....hbhhd..rr",
    ".......................hd.......",
    ".......................hd.......",
    ".......................dd.......",
};

// The margin around the art, in logical pixels: the outline, then the glow.
const int MARGIN = 2;

inline int frameW(int artW) { return artW + 2 * MARGIN; }
inline int frameH(int artH) { return artH + 2 * MARGIN; }

// The logo's frame: the art inside its margin.
const int W = ART_W + 2 * MARGIN;
const int H = ART_H + 2 * MARGIN;
const int PIXELS = W * H;

// ---------------------------------------------------------------- render
// Art rows -> out[(w + 4) * (h + 4)] slots: the art inside a two-pixel
// margin; every background pixel next to a shape becomes Outline, then every
// background pixel next to the outline becomes the glow (Rim), as on the
// characters (avatar.h silhouetteGlow).
inline void render(const char* const* rows, int w, int h, uint8_t* out) {
    const int fw = frameW(w), fh = frameH(h);
    memset(out, Clear, (size_t)fw * fh);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const uint8_t s = slotOf(rows[y][x]);
            out[(y + MARGIN) * fw + x + MARGIN] = s < SlotCount ? s : (uint8_t)Clear;
        }
    static const int DX[4] = {-1, 1, 0, 0}, DY[4] = {0, 0, -1, 1};
    for (int y = 0; y < fh; y++)
        for (int x = 0; x < fw; x++) {
            if (out[y * fw + x] != Clear) continue;
            for (int k = 0; k < 4; k++) {
                const int nx = x + DX[k], ny = y + DY[k];
                if ((unsigned)nx >= (unsigned)fw || (unsigned)ny >= (unsigned)fh) continue;
                const uint8_t n = out[ny * fw + nx];
                if (n != Clear && n != Outline) {      // a shape pixel
                    out[y * fw + x] = Outline;
                    break;
                }
            }
        }
    for (int y = 0; y < fh; y++)
        for (int x = 0; x < fw; x++) {
            if (out[y * fw + x] != Clear) continue;
            for (int k = 0; k < 4; k++) {
                const int nx = x + DX[k], ny = y + DY[k];
                if ((unsigned)nx >= (unsigned)fw || (unsigned)ny >= (unsigned)fh) continue;
                if (out[ny * fw + nx] == Outline) {    // glow never feeds more glow
                    out[y * fw + x] = Rim;
                    break;
                }
            }
        }
}

// The logo's own frame (W x H slots).
inline void renderLogo(uint8_t* out) { render(ROWS, ART_W, ART_H, out); }

// ---------------------------------------------------------------- palette
struct Look {
    uint32_t body, bodyD, bodyL, belly;    // the yellow ramp and the belly
    uint32_t acc, accD, accL;              // the orange ramp
    uint32_t cheek;
    uint32_t outline;                      // deep, hue-tinted: the outline round every shape, on every theme
    uint32_t rim;                          // the glow outside the outline on dark backgrounds (half strength)
};

static const Look LOOK = {
    0xf5b324, 0xc47a12, 0xffe07a, 0xfff1c2,
    0xec4a28, 0xa62e18, 0xff8a56,
    0xff8f7a,
    0x34120a,
    0xffc94a,
};

// The slot colors (RGB565) of the logo on background bg.
inline void paletteFor(uint16_t bg, uint16_t* p) {
    p[Clear] = bg;
    p[Outline] = rgb565(LOOK.outline);
    p[Rim] = avatar::bgIsLight(bg) ? bg : blend565(bg, rgb565(LOOK.rim), 128);
    p[BodyD] = rgb565(LOOK.bodyD);
    p[Body] = rgb565(LOOK.body);
    p[BodyL] = rgb565(LOOK.bodyL);
    p[Belly] = rgb565(LOOK.belly);
    p[AccD] = rgb565(LOOK.accD);
    p[Acc] = rgb565(LOOK.acc);
    p[AccL] = rgb565(LOOK.accL);
    p[White] = rgb565(avatar::CHAR_WHITE);
    p[Pupil] = rgb565(avatar::CHAR_PUPIL);
    p[Cheek] = rgb565(LOOK.cheek);
}

// The background the setup web page puts behind the logo (a light cream, so
// the logo takes the dark-outline edge of the light themes): firmware/tools/
// logo_web.py bakes the logo on it into include/logo_web.h.
const uint32_t WEB_BG = 0xfff6dc;

// The largest integer scale <= maxScale at which the logo's frame fits in
// maxW x maxH px; 1 when none does.
inline int fitScale(int maxW, int maxH, int maxScale) {
    int s = maxScale;
    while (s > 1 && (W * s > maxW || H * s > maxH)) s--;
    return s;
}

}  // namespace logo
