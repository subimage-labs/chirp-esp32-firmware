// Host tests for the chirp! logo: the art's shape, the rendered frame's
// outline and rim rules, the wordmark part, the palette on every theme
// background, scale fitting.
// Run: firmware/test/host/run.sh
#include "logo.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace logo;

static int failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,   \
                         __LINE__, #cond);                                \
            failures++;                                                   \
        }                                                                 \
    } while (0)

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
static int chroma(uint16_t c) {                   // spread of the 8-bit channels
    const int r = (c >> 11) * 255 / 31, g = ((c >> 5) & 0x3F) * 255 / 63, b = (c & 0x1F) * 255 / 31;
    return std::max(r, std::max(g, b)) - std::min(r, std::min(g, b));
}
static double colorDistance(uint16_t a, uint16_t b) {   // between the 8-bit expansions
    const int ra = (a >> 11) * 255 / 31, ga = ((a >> 5) & 0x3F) * 255 / 63, ba = (a & 0x1F) * 255 / 31;
    const int rb = (b >> 11) * 255 / 31, gb = ((b >> 5) & 0x3F) * 255 / 63, bb = (b & 0x1F) * 255 / 31;
    return std::sqrt((double)((ra - rb) * (ra - rb) + (ga - gb) * (ga - gb) + (ba - bb) * (ba - bb)));
}

// Every theme background, dark and light.
static std::vector<uint16_t> themeBackgrounds() {
    std::vector<uint16_t> v;
    for (uint8_t f = 0; f < THEME_COUNT; f++) {
        v.push_back(THEMES[f].dark.ink[(int)Ink::Bg]);
        v.push_back(THEMES[f].light.ink[(int)Ink::Bg]);
    }
    return v;
}

static std::vector<uint8_t> logoFrame() {
    std::vector<uint8_t> f(PIXELS);
    renderLogo(f.data());
    return f;
}

static bool isShape(uint8_t s) { return s != Clear && s != Outline && s != Rim; }

// ---------------------------------------------------------------- art
static void artShape() {
    CHECK(W == ART_W + 2 * MARGIN && H == ART_H + 2 * MARGIN && PIXELS == W * H);
    int used[SlotCount] = {0};
    for (int y = 0; y < ART_H; y++) {
        CHECK((int)std::strlen(ROWS[y]) == ART_W);
        for (int x = 0; x < ART_W; x++) {
            const uint8_t s = slotOf(ROWS[y][x]);
            CHECK(s < SlotCount);
            if (s < SlotCount) used[s]++;
        }
    }
    CHECK(slotOf('?') == SlotCount && slotOf(' ') == SlotCount);   // unknown characters are rejected
    // Every art letter names its own slot.
    const char* letters = ".odbhfraepwc";
    bool seen[SlotCount] = {false};
    int named = 0;
    for (const char* c = letters; *c; c++) {
        const uint8_t s = slotOf(*c);
        CHECK(s < SlotCount && !seen[s]);
        seen[s] = true;
        named++;
    }
    CHECK(named == SlotCount - 1 && !seen[Rim]);    // every slot but Rim has an art letter
    // The fills Perch draws: the yellow letters, the orange beak and "!", the
    // chick's eyes. Rim only comes from the render. (Belly, white and cheek stay
    // in the palette for the other designs in firmware/tools/logo_options/.)
    for (int s : {BodyD, Body, BodyL, AccD, Acc, AccL, Pupil}) CHECK(used[s] > 0);
    CHECK(used[Rim] == 0);
    // The art touches its own top, bottom, left and right: nothing cropped, nothing wasted.
    bool top = false, bottom = false, left = false, right = false;
    for (int x = 0; x < ART_W; x++) top |= ROWS[0][x] != '.', bottom |= ROWS[ART_H - 1][x] != '.';
    for (int y = 0; y < ART_H; y++) left |= ROWS[y][0] != '.', right |= ROWS[y][ART_W - 1] != '.';
    CHECK(top && bottom && left && right);
    // Layout: the chick sits on top (alone in its first rows), the eyes are only in the chick,
    // the letters are the yellow ramp, the "!" the orange one after a blank column.
    CHECK(CHICK_X + CHICK_W <= BANG_X - 1 && BANG_X + 2 == ART_W && CHICK_H < ART_H);
    for (int y = 0; y < ART_H; y++)
        for (int x = 0; x < ART_W; x++) {
            const uint8_t s = slotOf(ROWS[y][x]);
            if (s == Clear) continue;
            const bool chick = x >= CHICK_X && x < CHICK_X + CHICK_W && y < CHICK_H;
            if (y < CHICK_H - 1) CHECK(chick);
            if (chick) continue;
            if (x < BANG_X - 1) CHECK(s == BodyD || s == Body || s == BodyL);
            else if (x == BANG_X - 1) CHECK(false);        // the gap before the exclamation mark
            else CHECK(s == AccD || s == Acc || s == AccL);
        }
}

// ---------------------------------------------------------------- frame
static void frameRules() {
    const std::vector<uint8_t> f = logoFrame();
    static const int DX[4] = {-1, 1, 0, 0}, DY[4] = {0, 0, -1, 1};
    int counts[SlotCount] = {0};
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            const uint8_t s = f[y * W + x];
            CHECK(s < SlotCount);
            if (s >= SlotCount) continue;
            counts[s]++;
            const bool edge = x == 0 || y == 0 || x == W - 1 || y == H - 1;
            if (edge) CHECK(s == Clear || s == Rim);          // the margin's outer ring: glow or background
            bool touchesClear = false, touchesOutline = false;
            for (int k = 0; k < 4; k++) {
                const int nx = x + DX[k], ny = y + DY[k];
                if ((unsigned)nx >= (unsigned)W || (unsigned)ny >= (unsigned)H) continue;
                const uint8_t n = f[ny * W + nx];
                touchesClear |= n == Clear;
                touchesOutline |= n == Outline;
            }
            if (s == Rim) CHECK(touchesOutline);              // the glow sits beside the outline
            if (s == Clear) CHECK(!touchesOutline);           // the outline runs all the way round
            if (isShape(s)) CHECK(!touchesClear);             // every shape is outlined
        }
    // The background, the outline between parts, the rim, and each fill the art draws.
    for (int s : {Clear, Outline, Rim, BodyD, Body, BodyL, AccD, Acc, AccL, Pupil}) CHECK(counts[s] > 0);
    // The art's own pixels are kept as drawn (the fills are not touched by the outline).
    for (int y = 0; y < ART_H; y++)
        for (int x = 0; x < ART_W; x++) {
            const uint8_t a = slotOf(ROWS[y][x]);
            if (isShape(a)) CHECK(f[(y + MARGIN) * W + x + MARGIN] == a);
        }
    // Two pieces, not one: the chick, and the wordmark with its outlines joined
    // (the glow, background-colored on light themes, does not join them).
    std::vector<int> label(PIXELS, 0);
    int components = 0;
    for (int i = 0; i < PIXELS; i++) {
        if (f[i] == Clear || f[i] == Rim || label[i]) continue;
        components++;
        std::vector<int> stack = {i};
        label[i] = components;
        while (!stack.empty()) {
            const int p = stack.back();
            stack.pop_back();
            const int x = p % W, y = p / W;
            for (int k = 0; k < 4; k++) {
                const int nx = x + DX[k], ny = y + DY[k];
                if ((unsigned)nx >= (unsigned)W || (unsigned)ny >= (unsigned)H) continue;
                const int q = ny * W + nx;
                if (f[q] != Clear && f[q] != Rim && !label[q]) {
                    label[q] = components;
                    stack.push_back(q);
                }
            }
        }
    }
    CHECK(components == 2);
    // The render is deterministic and independent of what the buffer held.
    std::vector<uint8_t> g(PIXELS, 0xAA);
    renderLogo(g.data());
    CHECK(g == f);
}

// A tiny hand art checks the rule on its own: a shape gets an outline on its
// four sides (not the corners), and the background beside that outline the
// glow; a hand-drawn outline pixel is outline too and gets glow beside it.
static void renderRule() {
    const char* rows[3] = {"bo.", "...", ".d."};
    uint8_t out[7 * 7];
    memset(out, 0xAA, sizeof out);                // a pixel render() skipped would show
    render(rows, 3, 3, out);
    #define AT(x, y) out[(y) * 7 + (x)]
    CHECK(AT(2, 2) == Body && AT(3, 2) == Outline);                // art 'b' and the hand-drawn 'o'
    CHECK(AT(1, 2) == Outline && AT(2, 1) == Outline && AT(2, 3) == Outline);   // outline round 'b'
    CHECK(AT(0, 2) == Rim && AT(2, 0) == Rim && AT(4, 2) == Rim);   // glow beside the outline
    CHECK(AT(1, 1) == Rim && AT(0, 0) == Clear && AT(6, 6) == Clear);   // glow turns the corner; far corners background
    CHECK(AT(3, 4) == BodyD && AT(3, 5) == Outline && AT(3, 6) == Rim);   // the 'd', its outline, its glow
    #undef AT
    // Two shapes one pixel apart share the outline column between them; the
    // background above it becomes glow.
    const char* two[3] = {"b.b", "b.b", "b.b"};
    uint8_t o2[7 * 7];
    memset(o2, 0xAA, sizeof o2);
    render(two, 3, 3, o2);
    CHECK(o2[2 * 7 + 3] == Outline && o2[3 * 7 + 3] == Outline && o2[4 * 7 + 3] == Outline);
    CHECK(o2[1 * 7 + 3] == Rim);
}

// ---------------------------------------------------------------- palette
static void palette() {
    const std::vector<uint16_t> bgs = themeBackgrounds();
    CHECK(bgs.size() == (size_t)THEME_COUNT * 2);
    for (const uint16_t bg : bgs) {
        uint16_t p[SlotCount];
        paletteFor(bg, p);
        CHECK(p[Clear] == bg);
        // The glow beside the outline: nothing (the background) on light backgrounds,
        // the rim tint at half strength over dark ones (the characters' rule).
        if (avatar::bgIsLight(bg)) CHECK(p[Rim] == bg && contrast(p[Outline], bg) >= 3.0);
        else CHECK(p[Rim] == blend565(bg, rgb565(LOOK.rim), 128) && contrast(p[Rim], bg) >= 2.0);
        // The fills read on the outline (lines between parts), whatever the theme.
        for (int s : {BodyD, Body, BodyL, Belly, AccD, Acc, AccL, White, Cheek})
            CHECK(contrast(p[s], p[Outline]) >= 2.2);
        // A lit glow over dark never melts into the letters' own colour.
        if (!avatar::bgIsLight(bg)) CHECK(p[Rim] != p[Body] && p[Rim] != p[BodyL]);
    }
    uint16_t p[SlotCount];
    paletteFor(bgs[0], p);
    // The yellow ramp steps in lightness, the orange one too.
    CHECK(luminance(p[BodyD]) < luminance(p[Body]) && luminance(p[Body]) < luminance(p[BodyL]));
    CHECK(luminance(p[AccD]) < luminance(p[Acc]) && luminance(p[Acc]) < luminance(p[AccL]));
    CHECK(contrast(p[BodyD], p[Body]) >= 1.3 && contrast(p[BodyL], p[Body]) >= 1.3);
    CHECK(contrast(p[AccD], p[Acc]) >= 1.3 && contrast(p[AccL], p[Acc]) >= 1.2);
    // What sits on what: pupils and the white on the body, the beak on the belly,
    // the cheeks on the body.
    CHECK(contrast(p[Pupil], p[Body]) >= 3.0 && contrast(p[Pupil], p[White]) >= 3.0);
    CHECK(contrast(p[Acc], p[Belly]) >= 2.0);
    CHECK(contrast(p[Outline], p[Belly]) >= 3.0);              // the open beak's mouth line on the belly
    CHECK(colorDistance(p[Cheek], p[Body]) >= 40.0);
    CHECK(colorDistance(p[Acc], p[Body]) >= 100.0);            // the exclamation mark is another hue than the letters
    // A deliberate outline: deep and hue-tinted, never grey; the rim a bright saturated tint.
    CHECK(luminance(p[Outline]) <= 0.02 && chroma(p[Outline]) >= 25);
    CHECK(chroma(rgb565(LOOK.rim)) >= 80 && luminance(rgb565(LOOK.rim)) >= 0.30);
    // The logo's colors are its own: the same on every theme background but for the
    // background and the glow.
    uint16_t a[SlotCount], b[SlotCount];
    paletteFor(bgs[0], a);
    for (const uint16_t bg : bgs) {
        paletteFor(bg, b);
        for (int s = 0; s < SlotCount; s++)
            if (s != Clear && s != Rim) CHECK(a[s] == b[s]);
    }
    // The silhouette on any gray background never disappears: the outline on the
    // light side, the glow on the dark side.
    for (int v = 0; v < 256; v += 5) {
        const uint16_t bg = rgb565(((uint32_t)v << 16) | ((uint32_t)v << 8) | (uint32_t)v);
        uint16_t q[SlotCount];
        paletteFor(bg, q);
        CHECK(avatar::bgIsLight(bg) ? contrast(q[Outline], bg) >= 2.3 : contrast(q[Rim], bg) >= 1.5);
    }
}

// The setup web page puts the logo on a light background: no glow, the deep outline.
static void webBackground() {
    const uint16_t bg = rgb565(WEB_BG);
    CHECK(avatar::bgIsLight(bg));
    uint16_t p[SlotCount];
    paletteFor(bg, p);
    CHECK(p[Rim] == bg);
    CHECK(contrast(p[Outline], bg) >= 10.0);
}

// ---------------------------------------------------------------- scale
static void scaling() {
    // The empty chat screen and the boot splash: 4x (144 x 88) fits both.
    CHECK(W * 4 <= 320 && H * 4 == 88);
    CHECK(fitScale(320, 88, 4) == 4);
    CHECK(fitScale(320, 79, 4) == 3);
    CHECK(fitScale(320, 142, 4) == 4);
    CHECK(fitScale(320, H * 2, 4) == 2);
    CHECK(fitScale(W * 3 - 1, 1000, 4) == 2);       // width limits too
    CHECK(fitScale(1, 1, 4) == 1);                  // nothing fits: the smallest
    CHECK(fitScale(1000, 1000, 4) == 4);            // never above the maximum
    CHECK(fitScale(1000, 1000, 1) == 1);
}

int main() {
    artShape();
    frameRules();
    renderRule();
    palette();
    webBackground();
    scaling();
    if (failures) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::puts("logo_test ok");
    return 0;
}
