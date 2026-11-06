// ui_layout_test.cpp — the chat screen on every common ESP32 panel, at every
// font family and size: the companion goes where it was designed to go, every
// region is inside the screen and clear of the others, the chat keeps enough
// lines and width to read, and the rail's avatar/name/state/meter stack fits
// its rail. Run: firmware/test/host/run.sh
#include "ui_layout.h"

#include <cstdio>

#include "fontpick.h"
#include "avatar.h"

#include "../../src/fonts/font_data.cpp"

using namespace ui_layout;

static int failures = 0;
#define CHECK(cond, ...)                                                    \
    do {                                                                    \
        if (!(cond)) {                                                      \
            printf("FAIL %s:%d: %s — ", __FILE__, __LINE__, #cond);         \
            printf(__VA_ARGS__);                                            \
            printf("\n");                                                   \
            failures++;                                                     \
        }                                                                   \
    } while (0)

struct Panel {
    const char* boards;
    int w, h;
    Companion expected;
};

// Sizes as the device is held.
static const Panel PANELS[] = {
    {"T-Deck, M5Stack Core, CYD", 320, 240, Companion::Rail},
    {"Waveshare 1.69\"", 280, 240, Companion::Rail},
    {"T-Embed, T-Display S3", 320, 170, Companion::None},
    {"Waveshare 1.47\"", 320, 172, Companion::None},
    {"Cardputer, T-Display, StickC", 240, 135, Companion::None},
    {"T-Watch S3, M5 Dial, round", 240, 240, Companion::None},
    {"T-HMI, CYD held tall", 240, 320, Companion::Top},
    {"T-Display S3 held tall", 170, 320, Companion::Top},
    {"StickC held tall", 135, 240, Companion::None},
};

const int CONTENT_Y = 2;                 // ui.cpp
const int MIN_LINES = 3;                 // any panel, at size M
const int MIN_LINES_WITH_COMPANION = 5;  // a companion must not cost a readable chat
const int MIN_CHAT_W = 135;              // the narrowest panel's whole width

static bool inside(const Rect& r, int w, int h) {
    return r.x >= 0 && r.y >= 0 && r.right() <= w && r.bottom() <= h && r.w >= 0 && r.h >= 0;
}

static void checkPanel(const Panel& p) {
    for (size_t f = 0; f < FONT_FAMILY_COUNT; f++) {
        const FontFamily& fam = FONT_FAMILIES[f];
        for (int s = 0; s < FONT_SIZE_COUNT; s++) {
            const UiMetrics m = uiMetricsFor(fam.size[s].box);
            const int small = fam.size[FONT_SIZE_S].box;
            const int bottom = p.h - m.status - m.status;   // the typing line + the status strip
            const Chat c = chatLayout(p.w, p.h, CONTENT_Y, bottom, true);
            const char* tag = fam.name;
            const char sz = fontSizeLetter((uint8_t)s);
            CHECK(c.companion == p.expected, "%s %dx%d %s %c: companion %d, want %d", p.boards, p.w,
                  p.h, tag, sz, (int)c.companion, (int)p.expected);
            CHECK(inside(c.chat, p.w, p.h), "%s %dx%d: chat outside the screen", p.boards, p.w, p.h);
            CHECK(c.chat.w >= MIN_CHAT_W, "%s %dx%d: chat %d px wide", p.boards, p.w, p.h, c.chat.w);
            const int lines = c.chat.h / m.line;
            if (s == FONT_SIZE_M)
                CHECK(lines >= (c.companion == Companion::None ? MIN_LINES : MIN_LINES_WITH_COMPANION),
                      "%s %dx%d %s M: %d chat lines", p.boards, p.w, p.h, tag, lines);
            CHECK(lines >= 2, "%s %dx%d %s %c: %d chat lines", p.boards, p.w, p.h, tag, sz, lines);
            if (c.companion == Companion::None) {
                CHECK(c.scale == 0 && c.comp.w == 0, "%s: a companion area with none", p.boards);
                continue;
            }
            CHECK(inside(c.comp, p.w, p.h), "%s %dx%d: companion outside the screen", p.boards, p.w, p.h);
            CHECK(AV * c.scale + 2 * PAD <= c.comp.w && AV * c.scale + 2 * PAD <= c.comp.h,
                  "%s: avatar %d px does not fit its area", p.boards, AV * c.scale);
            if (c.companion == Companion::Rail) {
                CHECK(c.chat.x >= c.comp.right(), "%s: chat over the rail", p.boards);
                CHECK(railStackH(c.scale, m.line, small) <= c.comp.h,
                      "%s %dx%d %s %c: rail stack %d px > rail %d px", p.boards, p.w, p.h, tag, sz,
                      railStackH(c.scale, m.line, small), c.comp.h);
            } else {
                CHECK(c.chat.y >= c.comp.bottom(), "%s: chat over the band", p.boards);
            }
        }
    }
}

static void testPolicy() {
    CHECK(companionFor(320, 240, false) == Companion::None, "a board without a banner gets none");
    CHECK(chatLayout(320, 240, 2, 200, false).chat.w == 320, "none: the chat takes the width");
    const Chat rail = chatLayout(320, 240, 2, 200, true);
    CHECK(rail.scale == 3 && rail.comp.w == 3 * AV + 2 * PAD, "320x240: a 3x rail (%d)", rail.scale);
    CHECK(rail.chat.x == rail.comp.w && rail.chat.w == 320 - rail.comp.w, "the chat starts at the rail");
    CHECK(chatLayout(280, 240, 2, 200, true).scale == 2, "280x240: the rail steps down to 2x");
    const Chat top = chatLayout(240, 320, 2, 280, true);
    CHECK(top.scale == 3, "240x320: a 3x band (got %d)", top.scale);
    CHECK(largestScale(2, 4, [](int s) { return s == 3; }) == 3, "largestScale picks the fitting one");
    CHECK(largestScale(2, 4, [](int) { return false; }) == 0, "none fits: 0");
}

// The character view on every panel: avatar and bubble column on screen and
// apart, the column readable wherever either arrangement fits.
static void checkScene(const Panel& p) {
    const Scene sc = sceneLayout(p.w, p.h);
    const int av = AV * sc.scale;
    const Rect spr{sc.sprX, sc.sprY, av, av};
    CHECK(sc.scale >= 2 && sc.scale <= 4, "%s %dx%d: scene scale %d", p.boards, p.w, p.h, sc.scale);
    CHECK(inside(spr, p.w, p.h), "%s %dx%d: avatar off screen", p.boards, p.w, p.h);
    CHECK(inside(sc.col, p.w, p.h), "%s %dx%d: column off screen", p.boards, p.w, p.h);
    if (sc.above)
        CHECK(sc.col.y >= spr.bottom(), "%s %dx%d: column over the avatar", p.boards, p.w, p.h);
    else
        CHECK(sc.col.x >= spr.right(), "%s %dx%d: column over the avatar", p.boards, p.w, p.h);
}

static void testScenePolicy() {
    const Scene embed = sceneLayout(320, 170);       // the T-Embed keeps its layout
    CHECK(!embed.above && embed.scale == 4 && embed.sprX == 12 && embed.col.x == 146 &&
              embed.col.w == 172 && embed.col.y == 0 && embed.col.h == 170,
          "320x170: 4x beside, column 146+172");
    const Scene square = sceneLayout(240, 240);      // beside would leave a 92 px column at 4x
    CHECK(square.above && square.scale == 3, "240x240: 3x above (above=%d scale=%d)", square.above,
          square.scale);
    const Scene tall = sceneLayout(240, 320);
    CHECK(tall.above && tall.scale == 4, "240x320: 4x above (above=%d scale=%d)", tall.above, tall.scale);
    const Scene wide = sceneLayout(240, 135);        // too short to stack: 2x beside
    CHECK(!wide.above && wide.scale == 2, "240x135: 2x beside (above=%d scale=%d)", wide.above, wide.scale);
}

static void testPreview() {
    const Preview embed = previewLayout(320, 110);   // the T-Embed's card at size M
    CHECK(embed.scale == 3 && embed.avX == 18 && embed.textX == 132, "320 wide: 3x, text at 132");
    CHECK(previewLayout(320, 90).scale == 2, "a card shorter than 96 px: 2x, not over the header");
    CHECK(previewLayout(240, 160).scale == 2, "240 wide: 2x keeps the name its room");
    CHECK(previewLayout(320, 50).scale == 1, "a card shorter than 64 px: 1x");
    const Preview none = previewLayout(320, 20);
    CHECK(none.scale == 0 && none.textX == PREVIEW_X, "no room even at 1x: no avatar, text at the left");
    const UiMetrics m = uiMetricsFor(FONT_FAMILIES[0].size[FONT_SIZE_M].box);
    CHECK(previewLayout(320, previewCardH(170, CONTENT_Y, m.header, m.status)).scale == 3,
          "the T-Embed at Atkinson M keeps its 3x preview");
}

// The preview on every panel at every font cut, in its real card: the avatar
// never taller than the card (it would cover the tab header), the text
// never off the right edge.
static void checkPreview(const Panel& p) {
    for (size_t f = 0; f < FONT_FAMILY_COUNT; f++)
        for (int s = 0; s < FONT_SIZE_COUNT; s++) {
            const UiMetrics m = uiMetricsFor(FONT_FAMILIES[f].size[s].box);
            const int card = previewCardH(p.h, CONTENT_Y, m.header, m.status);
            const Preview pv = previewLayout(p.w, card);
            // Bound the rendered sprite using the art's dimensions and the
            // screen's header/status edges, independently of the layout fit rule.
            const int top = CONTENT_Y + m.header + 2;
            const int bottom = p.h - m.status;
            const int height = avatar::H * pv.scale;
            const int y = top + (bottom - top - height) / 2;
            if (pv.scale) {
                CHECK(y >= top && y + height <= bottom,
                      "%s: preview sprite crosses header or status", p.boards);
                CHECK(pv.avX >= 0 && pv.avX + avatar::W * pv.scale <= pv.textX,
                      "%s: preview sprite overlaps text", p.boards);
            }
            const FontCut& cut = FONT_FAMILIES[f].size[s];
            CHECK(pv.textX + vlwWidth(cut, "Chirper") <= p.w,
                  "%s: preview cannot display its title", p.boards);
        }
}

int main() {
    testPolicy();
    testScenePolicy();
    testPreview();
    for (const Panel& p : PANELS) {
        checkPanel(p);
        checkScene(p);
        checkPreview(p);
    }
    if (failures) {
        printf("ui_layout: %d check(s) failed\n", failures);
        return 1;
    }
    printf("ui_layout: all checks passed\n");
    return 0;
}
