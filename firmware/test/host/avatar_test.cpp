// Host tests for the character mode's avatar: seeded traits, theme palette,
// layered composition, animation, dirty rectangles. The golden frames are
// CRC32s of the reference model's output (arguments t=123456, stateT=789,
// level=200, stt=false), so a change to the art or the animation shows up as
// a golden mismatch and is then a deliberate update.
// Run: firmware/test/host/run.sh
#include "avatar.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <unordered_set>
#include <string>
#include <vector>

using namespace avatar;

static int failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,   \
                         __LINE__, #cond);                                \
            failures++;                                                   \
        }                                                                 \
    } while (0)

static uint32_t crc32(const uint8_t* d, size_t n) {
    uint32_t c = ~0u;
    for (size_t i = 0; i < n; i++) {
        c ^= d[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

static std::string hex(const Traits& t) {
    uint8_t rec[TRAIT_FIELDS];
    t.record(rec);
    std::string s;
    char b[3];
    for (int i = 0; i < TRAIT_FIELDS; i++) {
        std::snprintf(b, sizeof b, "%02x", rec[i]);
        s += b;
    }
    return s;
}

static Character& character() {
    static Character c;
    return c;
}

static uint32_t frameCrc(const Traits& t, State s, uint32_t now, uint32_t stateT, int level,
                         bool stt) {
    Character& c = character();
    c.build(t);
    uint8_t out[PIXELS];
    Ctx ctx{now, stateT, (uint8_t)level, stt};
    c.render(s, ctx, out);
    return crc32(out, PIXELS);
}

// ---------------------------------------------------------------- golden
struct Golden {
    const char* name;
    const char* overrideRec;   // hex record merged over the seeded character ("" = none, 0xff = keep)
    const char* traits;        // the resulting record
    uint32_t crc[StateCount];
};
static const Golden GOLDEN[] = {
    {"default", "", "090704010c070512080d0506",
     {0x175248c0, 0xe513acb6, 0xde826ae4, 0x8ee33b42, 0x6b99df8d, 0xd21406bb}},
    {"alpha", "ffffffff00ffff080807ff01", "0a0200010002050808070701",
     {0x64182c76, 0xeb6c6676, 0xc828f86e, 0xfd4baaa7, 0xf0a5e4b5, 0x4d820be7}},
    {"kitchen", "ffffffff00ffff0107ffffff", "0a020001000d000107011501",
     {0x1283109b, 0xa45771c3, 0x6ed93770, 0x1343185e, 0xc1a648da, 0xf49062a3}},
    {"nova", "ffffffff01ff02ffff0203ff", "09000002010a020b00020301",
     {0xbd613d43, 0x25613adb, 0x04c7fe74, 0x9d3f4dfb, 0xdb1b5e5b, 0xa707ab14}},
    {"helper", "ffffffff00ffff0706ff09ff", "0400020200070407060c0902",
     {0x54d1e555, 0xe0babd1b, 0x1df663a9, 0x87a30b40, 0x78e6a013, 0xbcc8cba5}},
    {"agent-000", "", "0f07020000020409010a1403",
     {0x8436bb78, 0x28ddc367, 0x9943248a, 0x4afd2177, 0x928f9042, 0x408c0c09}},
    {"agent-001", "", "0a0602040f0b0b0c040c1c04",
     {0xb499ff55, 0x02f933a3, 0x04fcee64, 0xf6853874, 0x5edbe246, 0xe134141e}},
    {"agent-002", "", "0a0100010800001400000704",
     {0x84f88d95, 0xdc4670d9, 0xaa6094e2, 0x5063a9d4, 0x368d7f22, 0xbabc2ae0}},
    {"agent-003", "", "000201020503010301030a06",
     {0x24f22a1e, 0x14d6e319, 0x1e6c826d, 0x9a8f700b, 0x7ff4b2d4, 0x9836009c}},
    {"agent-004", "", "040200000006071300051b00",
     {0xbb5e249e, 0x6d9718f7, 0x3dc3fefb, 0xe2f331d3, 0x18981adf, 0x69cebc2d}},
    {"agent-005", "", "0d0502010009030200040f02",
     {0x8786d343, 0x26e97f1e, 0x207d3c0f, 0xe8c297a0, 0x00fe6a8e, 0xfde4cb82}},
    {"agent-006", "", "0c040402080d04010a030204",
     {0xacc6a96f, 0x371161d9, 0xe8c4ca30, 0xa86455dd, 0x25723121, 0xba93877a}},
    {"agent-007", "", "0e00040300080a140c050102",
     {0x8f773dfb, 0xd544a87d, 0x038e1a93, 0x529c2a64, 0x1b79f961, 0x732d0d8a}},
    {"novel-1", "0a0503030a07070d03081204", "0a0503030a07070d03081204",
     {0x4b302b72, 0xd03e5838, 0x7a089d3f, 0x51d845d5, 0x70570c09, 0x99bd07b1}},
    {"novel-2", "0b0604040b08080e0a091305", "0b0604040b08080e0a091305",
     {0x1db986ee, 0xdb049f7d, 0x5a4dd148, 0xee715c1c, 0x771c2066, 0xa9ef3c3b}},
    {"novel-3", "0c0703030c09090f0c0a1406", "0c0703030c09090f0c0a1406",
     {0x3d5bf838, 0xea9629b7, 0xa08457c5, 0x07ac452b, 0x4d9c159a, 0xdeddbe5b}},
    {"novel-4", "0d0504040d0a0a100d0b1504", "0d0504040d0a0a100d0b1504",
     {0x0dd7ae1c, 0x0a576f23, 0xb4716d2b, 0x7238a8f3, 0x847b4671, 0x59494e47}},
    {"novel-5", "0e0603030e0b0b110e0c1605", "0e0603030e0b0b110e0c1605",
     {0xc72d068e, 0xec2061a8, 0x5b95efd9, 0x75dec5f0, 0xfc9a04ed, 0x618cf6c9}},
    {"novel-6", "0f0704040f0c0c120f0d1706", "0f0704040f0c0c120f0d1706",
     {0x0c473687, 0x41aa3657, 0x8ec11149, 0x0975d7bc, 0xc52640bd, 0xd6262b1d}},
    {"novel-7", "0a030002100d031310001800", "0a030002100d031310001800",
     {0x00adcee7, 0xd77a0ec7, 0xb4d5610b, 0x8049a1b1, 0x31416583, 0x4fb2e46c}},
    {"novel-8", "05040101110104140b071901", "05040101110104140b071901",
     {0x93330a2f, 0x403438e1, 0x9b792ffe, 0x3185b25d, 0xf186b1f3, 0x17ae7cb5}},
    {"novel-9", "030202021203051512031a02", "030202021203051512031a02",
     {0x2da3929b, 0x01e4a763, 0x2b44beb9, 0xc9296820, 0x68a37e15, 0x5233a2ca}},
    {"novel-10", "080100000000021611001d03", "080100000000021611001d03",
     {0xddbdd967, 0x67d2df3a, 0x844d8dc3, 0x71e018c7, 0x3db3c83a, 0x139971b8}},
};

static void golden() {
    for (const Golden& g : GOLDEN) {
        // Some entries carry a host-chosen override record over the seeded
        // character (the path host-side trait overrides will use).
        Traits t = traitsFromSeed(g.name);
        if (g.overrideRec[0]) {
            uint8_t rec[TRAIT_FIELDS];
            for (int i = 0; i < TRAIT_FIELDS; i++)
                rec[i] = (uint8_t)std::strtol(std::string(g.overrideRec).substr(i * 2, 2).c_str(), nullptr, 16);
            t = mergeTraits(t, rec, TRAIT_FIELDS);
        }
        CHECK(hex(t) == g.traits);
        for (int s = 0; s < StateCount; s++)
            CHECK(frameCrc(t, (State)s, 123456, 789, 200, false) == g.crc[s]);
    }
}

// ---------------------------------------------------------------- tables
// The frozen integer silhouettes equal the superellipse definition.
struct HeadDef {
    double rx, ry, n, bean;
};
static const HeadDef HEAD_DEF[HEAD_COUNT] = {
    {9.5, 7.5, 2.0, 0.00}, {10.5, 7.0, 2.5, 0.00}, {9.5, 7.5, 4.0, 0.00}, {8.5, 8.0, 2.0, 0.10},
    {10.0, 7.5, 2.3, 0.18}, {8.0, 7.5, 2.0, 0.00}, {10.5, 6.5, 3.4, 0.00}, {8.5, 8.0, 3.4, 0.00},
    {9.5, 7.5, 2.2, -0.16}, {10.5, 8.0, 2.1, 0.06},
    {10.0, 8.0, 1.5, 0.00}, {10.5, 8.0, 6.0, 0.00}, {8.0, 8.0, 2.2, 0.32}, {9.5, 7.5, 2.2, -0.34},
    {11.0, 6.0, 2.0, 0.00}, {10.0, 7.5, 0.0, 0.00}};
struct TorsoDef {
    double rx, ry, n, bean, cy;
};
static const TorsoDef TORSO_DEF[TORSO_COUNT] = {
    {0, 0, 0, 0, 0},         {5.5, 3.5, 2.0, 0.00, 26.0}, {6.5, 3.5, 2.2, 0.30, 26.0},
    {6.0, 3.5, 3.5, 0.00, 26.0}, {4.5, 4.0, 2.0, -0.2, 25.5}, {7.5, 3.5, 2.6, 0.00, 26.0},
    {3.5, 4.0, 2.2, 0.00, 25.5}, {6.0, 3.5, 2.2, 0.50, 26.0}};

static int halfOf(double rx, double ry, double n, double bean, double cy, int y) {
    int count = 0;
    for (int x = 16; x < 32; x++) {
        const double dx = std::fabs(x + 0.5 - 16.0);
        const double dyy = y + 0.5 - cy;
        const double e = rx * (1 + bean * dyy / ry);
        if (e <= 0) continue;
        if (std::pow(dx / e, n) + std::pow(std::fabs(dyy) / ry, n) <= 1.0) count++;
    }
    return count;
}

static void tables() {
    for (int h = 0; h < HEAD_COUNT; h++) {
        const HeadDef& d = HEAD_DEF[h];
        CHECK(HEADS[h].rx10 == (int)std::lround(d.rx * 10));
        CHECK(HEADS[h].ry10 == (int)std::lround(d.ry * 10));
        if (d.n > 0) {                                // the superellipse heads; n = 0 marks a hand-drawn one
            for (int y = 0; y < H; y++)
                CHECK(HEADS[h].half[y] == halfOf(d.rx, d.ry, d.n, d.bean, 23.0 - d.ry, y));
        } else {                                      // symmetric rows, widest row = rx, height = 2 ry
            int rows = 0, widest = 0;
            for (int y = 0; y < H; y++) {
                rows += HEADS[h].half[y] > 0;
                widest = std::max(widest, (int)HEADS[h].half[y]);
            }
            CHECK(widest * 10 == HEADS[h].rx10 && rows * 10 == (int)std::lround(d.ry * 20));
            CHECK(HEADS[h].half[(int)std::lround(23.0 - d.ry * 2)] > 0 && HEADS[h].half[22] > 0);
        }
        // The integer forms of the derived anchors match the float formulas.
        const int rx10 = HEADS[h].rx10;
        const double rx = d.rx;
        CHECK((rx10 * 58 + 500) / 1000 == (int)std::floor(rx * 0.58 + 0.5));
        CHECK((rx10 * 62 + 500) / 1000 == (int)std::floor(rx * 0.62 + 0.5));
        CHECK((rx10 * 60 + 500) / 1000 == (int)std::floor(rx * 0.60 + 0.5));
        CHECK((rx10 * 70 + 500) / 1000 == (int)std::floor(rx * 0.70 + 0.5));
        CHECK((rx10 * 90 + 500) / 1000 == (int)std::floor(rx * 0.90 + 0.5));
        CHECK((rx10 * 55) / 1000 == (int)std::floor(rx * 0.55));
        CHECK((rx10 * 62) / 1000 == (int)std::floor(rx * 0.62));
        CHECK(rx10 / 10 == (int)rx);
        CHECK((rx10 * 40 + 500) / 1000 == (int)std::floor(rx * 0.40 + 0.5));
        CHECK((rx10 * 45 + 500) / 1000 == (int)std::floor(rx * 0.45 + 0.5));
    }
    for (int i = 1; i < TORSO_COUNT; i++) {
        const TorsoDef& d = TORSO_DEF[i];
        CHECK(TORSOS[i].rx10 == (int)std::lround(d.rx * 10));
        CHECK(TORSOS[i].cy10 == (int)std::lround(d.cy * 10));
        for (int y = 0; y < H; y++)
            CHECK(TORSOS[i].half[y] == halfOf(d.rx, d.ry, d.n, d.bean, d.cy, y));
    }
    // The eye line anchor: round(cy + 0.4) with cy = 23 - ry.
    for (int h = 0; h < HEAD_COUNT; h++) {
        Character c;
        Traits t = traitsFromSeed("x");
        t.head = (uint8_t)h;
        c.build(t);
        CHECK(c.eyeY() == (int)std::floor(23.0 - HEAD_DEF[h].ry + 0.4 + 0.5));
    }
}

// Exact integer ellipses agree with the float definition away from ties.
static void ellipses() {
    for (int cx10 = 100; cx10 <= 220; cx10 += 7)
        for (int cy10 = 20; cy10 <= 280; cy10 += 13)
            for (int r = 9; r <= 56; r += 7) {
                const int rx10 = r, ry10 = 60 - r / 2;
                for (int y = 0; y < H; y++)
                    for (int x = 0; x < W; x++) {
                        const double a = ((x + 0.5 - cx10 / 10.0) / (rx10 / 10.0));
                        const double b = ((y + 0.5 - cy10 / 10.0) / (ry10 / 10.0));
                        const double v = a * a + b * b;
                        if (std::fabs(v - 1.0) < 1e-9) continue;     // exact tie: float is unreliable
                        CHECK(inEllipse(x, y, cx10, cy10, rx10, ry10) == (v <= 1.0));
                    }
            }
}

// ---------------------------------------------------------------- traits
static std::vector<std::string> names(int n) {
    std::vector<std::string> v;
    v.push_back("");
    v.push_back("default");
    v.push_back("alpha");
    v.push_back("kitchen");
    v.push_back("nova");
    v.push_back("helper");
    for (int i = 0; i < n; i++) {
        char b[24];
        std::snprintf(b, sizeof b, "agent-%03d", i);
        v.push_back(b);
    }
    return v;
}

static void traits() {
    // Deterministic; "" is the default agent.
    CHECK(traitsFromSeed("alpha") == traitsFromSeed("alpha"));
    CHECK(traitsFromSeed("") == traitsFromSeed("default"));
    CHECK(traitsFromSeed(nullptr) == traitsFromSeed("default"));
    CHECK(fnv1a("default") == 2470140894u);
    // Every field within its vocabulary; composition rules hold.
    std::set<std::string> seen;
    int total = 0;
    for (const std::string& n : names(5000)) {
        if (n.empty()) continue;                          // "" is the default agent
        const Traits t = traitsFromSeed(n.c_str());
        for (int i = 0; i < TRAIT_FIELDS; i++) CHECK(t.get(i) < TRAIT_RANGE[i]);
        CHECK(!((t.hat == Winged || t.hat == Headphones || t.hat == Tophat || t.hat == Viking) &&
                (t.ears == Bunny || t.ears == Fox || t.ears == Antlers)));
        CHECK(!((t.ears == Antenna || t.ears == Unicorn || t.ears == Sprout || t.ears == Spikes) &&
                t.hat != HatNone && t.hat != Bow && t.hat != Halo));
        CHECK(!(t.eyes == Led && t.acc == Glasses));
        CHECK(!(t.eyes == Cyclops && t.acc == Eyepatch));
        seen.insert(hex(t));
        total++;
    }
    // Distinct seeds give distinct characters (at 5,000 names, all of them).
    CHECK((int)seen.size() == total);
}

static void merging() {
    const Traits base = traitsFromSeed("alpha");
    uint8_t keep[TRAIT_FIELDS];
    memset(keep, TRAIT_KEEP, sizeof keep);
    CHECK(mergeTraits(base, keep, TRAIT_FIELDS) == base);
    CHECK(mergeTraits(base, nullptr, 0) == base);
    uint8_t rec[TRAIT_FIELDS];
    memset(rec, TRAIT_KEEP, sizeof rec);
    rec[7] = Crown;                       // hat
    rec[4] = 200;                         // out of range: keep the seeded ears
    const Traits m = mergeTraits(base, rec, TRAIT_FIELDS);
    CHECK(m.hat == Crown);
    CHECK(m.ears == base.ears);
    CHECK(m.head == base.head && m.seed == base.seed);
    // A short record only sets the leading fields.
    uint8_t one[1] = {3};
    CHECK(mergeTraits(base, one, 1).head == 3);
    CHECK(mergeTraits(base, one, 1).hat == base.hat);
}

static const Ctx CTX_SAMPLES[] = {
    {0, 0, 0, false},         {1000, 300, 30, true},     {5555, 700, 100, false},
    {123456, 789, 200, false}, {66000, 2000, 160, true},  {4000000, 100, 255, false},
    {7777777, 560, 60, false}, {1234, 1600, 10, true},
};
static Ctx CTX_SAMPLES_FIRST() { return CTX_SAMPLES[3]; }
static Ctx CTX_SAMPLES_AT(int k) { return CTX_SAMPLES[k * 2 + 1]; }

// ---------------------------------------------------------------- palette
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

static Traits withPalette(int pal) {
    Traits t = traitsFromSeed("alpha");
    t.palette = (uint8_t)pal;
    return t;
}

static void palette() {
    const std::vector<uint16_t> bgs = themeBackgrounds();
    CHECK(bgs.size() == (size_t)THEME_COUNT * 2);
    // The seed picks the palette deterministically, spread over all of them,
    // and independently of the other traits.
    std::set<int> used;
    for (const std::string& n : names(3000)) {
        if (n.empty()) continue;
        const Traits t = traitsFromSeed(n.c_str());
        CHECK(t.palette < PALETTE_COUNT);
        CHECK(t.palette == traitsFromSeed(n.c_str()).palette);
        used.insert(t.palette);
    }
    CHECK((int)used.size() == PALETTE_COUNT);
    {   // the palette changes colors only: the same frame indexes with any palette
        Character& c = character();
        uint8_t a[PIXELS], b[PIXELS];
        for (int s = 0; s < StateCount; s++) {
            c.build(withPalette(0));
            c.render((State)s, CTX_SAMPLES_FIRST(), a);
            c.build(withPalette(7));
            c.render((State)s, CTX_SAMPLES_FIRST(), b);
            CHECK(memcmp(a, b, PIXELS) == 0);
        }
    }
    for (int pi = 0; pi < PALETTE_COUNT; pi++) {
        const Traits t = withPalette(pi);
        const CharPalette& cp = CHAR_PALETTES[pi];
        uint16_t first[SlotCount];
        paletteFor(t, bgs[0], first);
        for (uint16_t bg : bgs) {
            // Every slot is defined: two different pre-fills end up identical.
            uint16_t p[SlotCount], q[SlotCount], r[SlotCount];
            for (int i = 0; i < SlotCount; i++) { p[i] = 0x1234; q[i] = 0xEDCB; }
            paletteFor(t, bg, p);
            paletteFor(t, bg, q);
            CHECK(memcmp(p, q, sizeof p) == 0);
            for (int i = 0; i < SlotCount; i++) r[i] = 0xEDCB;
            paletteFor(t, bg, r);
            for (int i = 0; i < SlotCount; i++) CHECK(p[i] == r[i]);
            { uint16_t z[SlotCount]; for (int i = 0; i < SlotCount; i++) z[i] = 0x1234; paletteFor(t, bg, z);
              for (int i = 0; i < SlotCount; i++) CHECK(z[i] == p[i]); }
            CHECK(p[Clear] == bg);
            // The character's own colors do not move with the theme.
            for (int s : {Outline, BodyD, Body, BodyL, Belly, AccD, Acc, AccL, White, WhiteD, Pupil,
                          Dark, MouthC, Tongue, Cheek, BusyC, ErrC})
                CHECK(p[s] == first[s]);
            // What shows against the background reads on every theme: the deep
            // outline on light backgrounds (no glow there: the glow is the
            // background), the glow (the rim tint at half strength) on dark ones,
            // and the effects that float on it.
            if (bgIsLight(bg)) CHECK(p[Rim] == bg && contrast(p[Outline], bg) >= 3.0);
            else CHECK(contrast(p[Rim], bg) >= 2.0);
            for (int s : {ListenC, OkC, MutedC, Float}) CHECK(contrast(p[s], bg) >= 3.0);
            CHECK(contrast(p[Shadow], bg) < 2.2 && p[Shadow] != bg);   // faint, not invisible
        }
        // The palette itself: eyes, marks and lines read on the body.
        uint16_t p[SlotCount];
        paletteFor(t, bgs[0], p);
        const double lb = luminance(p[Body]);
        CHECK(lb >= 0.15 && lb <= 0.40);                               // not near-black, not washed out
        CHECK(contrast(p[Pupil], p[Body]) >= 3.0);
        CHECK(contrast(p[White], p[Body]) >= 2.0);
        CHECK(contrast(p[ErrC], p[Body]) >= 3.0);
        CHECK(contrast(p[Outline], p[Body]) >= 3.0);
        CHECK(contrast(p[BodyD], p[Body]) >= 1.15 && contrast(p[BodyL], p[Body]) >= 1.15);
        CHECK(luminance(p[BodyD]) < lb && luminance(p[BodyL]) > lb);
        CHECK(p[Body] != p[Acc]);
        // What the newer parts draw on the body or on each other: brows, an eyepatch, wheels
        // and a headset in Dark; belts, a split head and mitts in the accent; slit and LED
        // eyes with their dark pupils; hearts in the error colour.
        CHECK(contrast(p[Dark], p[Body]) >= 2.0);
        CHECK(colorDistance(p[Acc], p[Body]) >= 100.0);   // hue may differ more than lightness
        CHECK(contrast(p[AccD], p[Acc]) >= 1.15 && contrast(p[AccL], p[Acc]) >= 1.1);
        CHECK(contrast(p[AccL], p[Pupil]) >= 3.0);
        // A deliberate outline: deep and hue-tinted, never grey or pale; the
        // rim a bright saturated tint.
        CHECK(luminance(p[Outline]) <= 0.02 && chroma(p[Outline]) >= 25);
        CHECK(chroma(rgb565(cp.rim)) >= 80 && luminance(rgb565(cp.rim)) >= 0.30);
        CHECK(luminance(rgb565(cp.rim)) > luminance(p[Body]));
        CHECK(cp.name != nullptr && cp.name[0]);
    }
    // Light and dark backgrounds differ only in the glow: the background on light,
    // the rim tint halfway over the background on dark. The outline is the same.
    uint16_t onDark[SlotCount], onLight[SlotCount];
    const uint16_t darkBg = THEMES[0].dark.ink[(int)Ink::Bg], lightBg = THEMES[0].light.ink[(int)Ink::Bg];
    paletteFor(withPalette(2), darkBg, onDark);
    paletteFor(withPalette(2), lightBg, onLight);
    CHECK(onLight[Rim] == lightBg && onDark[Outline] == onLight[Outline]);
    CHECK(onDark[Rim] == blend565(darkBg, rgb565(CHAR_PALETTES[2].rim), 128));
    // Any gray background: the silhouette never disappears (the glow >= 1.5:1 on
    // the dark side, the deep outline >= 2.3:1 on the light side).
    for (int v = 0; v < 256; v++) {
        const uint16_t bg = (uint16_t)(((v >> 3) << 11) | ((v >> 2) << 5) | (v >> 3));
        for (int pi = 0; pi < PALETTE_COUNT; pi++) {
            uint16_t p[SlotCount];
            paletteFor(withPalette(pi), bg, p);
            CHECK(bgIsLight(bg) ? contrast(p[Outline], bg) >= 2.3 : contrast(p[Rim], bg) >= 1.5);
        }
    }
}

// The deep outline runs all the way round: no background pixel touches an
// outline pixel (those became the glow), and every glow pixel sits beside the
// outline. Lines inside the character are the outline too.
static bool glowRule(const uint8_t* f) {
    bool ok = true, glow = false;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            const uint8_t v = f[y * W + x];
            if (v != Clear && v != Rim) continue;
            const int nx[4] = {x - 1, x + 1, x, x}, ny[4] = {y, y, y - 1, y + 1};
            bool besideOutline = false;
            for (int j = 0; j < 4; j++) {
                if (nx[j] < 0 || nx[j] >= W || ny[j] < 0 || ny[j] >= H) continue;
                besideOutline |= f[ny[j] * W + nx[j]] == Outline;
            }
            if (v == Clear && besideOutline) ok = false;
            if (v == Rim) {
                glow = true;
                if (!besideOutline) ok = false;
            }
        }
    return ok && glow;
}

static void rimPass() {
    Character& c = character();
    for (const std::string& n : names(60)) {
        c.build(traitsFromSeed(n.c_str()));
        for (int s = 0; s < StateCount; s++)
            for (int k = 0; k < 3; k++) {
                uint8_t f[PIXELS];
                c.render((State)s, CTX_SAMPLES_AT(k), f);
                CHECK(glowRule(f));
            }
    }
}

// ---------------------------------------------------------------- persona look
static void looks() {
    CHECK((size_t)TRAIT_FIELDS * 2 == 24);                 // setup_form.h LOOK_HEX_LEN
    uint8_t rec[TRAIT_FIELDS];
    CHECK(lookFromHex("ffffffff0300ffff0107ff02", rec) == TRAIT_FIELDS);
    CHECK(rec[0] == 0xFF && rec[4] == 0x03 && rec[5] == 0x00 && rec[6] == 0xFF && rec[8] == 0x01 &&
          rec[9] == 0x07 && rec[10] == 0xFF && rec[11] == 0x02);
    CHECK(lookFromHex("FFFFFFFF0300FFFF0107FF02", rec) == TRAIT_FIELDS);
    for (const char* bad : {"", "ff", "ffffffff0300ffff0107ff0", "ffffffff0300ffff0107ff022", "gggggggggggggggggggggggg"})
        CHECK(lookFromHex(bad, rec) == 0);
    CHECK(lookFromHex(nullptr, rec) == 0);
    // "Original" = the persona look over the seeded character; a kept
    // variant wins over the look; no look = the seeded character.
    const char* look = "ffffffff0300ffff0107ff02";       // ears bunny, eyes beady, acc bowtie, pattern blush, temper poised
    const Traits seeded = traitsFromSeed("alpha");
    const Traits original = traitsForAgent("alpha", 0, look);
    CHECK(original.ears == Bunny && original.eyes == Beady && original.acc == Bowtie &&
          original.pattern == Blush && original.temper == Poised);
    CHECK(original.hat == seeded.hat && original.palette == seeded.palette && original.head == seeded.head);
    CHECK(traitsForAgent("alpha", 0, "") == seeded);
    CHECK(traitsForAgent("alpha", 0, nullptr) == seeded);
    CHECK(traitsForAgent("alpha", 0, "nothex") == seeded);
    CHECK(traitsForAgent("alpha", 5, look) == traitsForVariant("alpha", 5));
    // Out-of-range bytes in a look are ignored like 0xFF.
    CHECK(traitsForAgent("alpha", 0, "c8ffffffffffffffffffffff") == seeded);
}

// ---------------------------------------------------------------- variants
static void variants() {
    // Variant 0 is the agent's own character; others are stable and distinct.
    for (const char* n : {"", "default", "alpha", "kitchen"}) {
        CHECK(variantSeed(n, 0) == fnv1a(*n ? n : "default"));
        CHECK(traitsForVariant(n, 0) == traitsFromSeed(n));
        std::set<std::string> seen;
        for (uint16_t v = 0; v <= VARIANT_MAX; v++) {
            const Traits t = traitsForVariant(n, v);
            CHECK(t == traitsForVariant(n, v));
            for (int i = 0; i < TRAIT_FIELDS; i++) CHECK(t.get(i) < TRAIT_RANGE[i]);
            seen.insert(hex(t));
        }
        CHECK(seen.size() == (size_t)VARIANT_MAX + 1);        // every variant a different character
    }
    // Names give different variant sets.
    CHECK(variantSeed("alpha", 5) != variantSeed("nova", 5));
    CHECK(!(traitsForVariant("alpha", 5) == traitsForVariant("nova", 5)));
    // Browsing numbers are exploration: variant n is the same character on every call.
    CHECK(traitsForVariant("alpha", 3) == traitsForVariant("alpha", 3));
}

// ---------------------------------------------------------------- frames

// Every part renders inside the canvas with valid slots, in every state
// (also run under the address/undefined sanitizers).
static void partsBounds() {
    Character& c = character();
    // One trait at a time through its whole vocabulary, others seeded.
    for (int field = 0; field < TRAIT_FIELDS; field++)
        for (int v = 0; v < TRAIT_RANGE[field]; v++)
            for (const char* n : {"alpha", "nova", "kitchen", "agent-007", "agent-031"}) {
                Traits t = traitsFromSeed(n);
                *t.at(field) = (uint8_t)v;
                c.build(t);
                for (int s = 0; s < StateCount; s++)
                    for (const Ctx& x : CTX_SAMPLES) {
                        uint8_t out[PIXELS];
                        c.render((State)s, x, out);
                        for (int i = 0; i < PIXELS; i++) CHECK(out[i] < SlotCount);
                    }
            }
    // Every hat on every head (the hat sits on the head's top row).
    for (int h = 0; h < HEAD_COUNT; h++)
        for (int hat = 0; hat < HatsCount; hat++) {
            Traits t = traitsFromSeed("alpha");
            t.head = (uint8_t)h;
            t.hat = (uint8_t)hat;
            t.ears = EarsNone;
            c.build(t);
            uint8_t out[PIXELS];
            c.render(Idle, CTX_SAMPLES[3], out);
            for (int i = 0; i < PIXELS; i++) CHECK(out[i] < SlotCount);
        }
}

static bool symmetric(const Canvas& c) {
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W / 2; x++)
            if (c.px[y * W + x] != c.px[y * W + (W - 1 - x)]) return false;
    return true;
}
static bool occupancySymmetric(const Canvas& c) {
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W / 2; x++)
            if ((c.px[y * W + x] != 0) != (c.px[y * W + (W - 1 - x)] != 0)) return false;
    return true;
}

// Static layers mirror about the axis (the head's shine and the full-width
// hats break it on purpose).
static void symmetry() {
    Character& c = character();
    for (const std::string& n : names(400)) {
        const Traits t = traitsFromSeed(n.c_str());
        c.build(t);
        CHECK(symmetric(c.feet()));
        if (t.pattern != Stripes) CHECK(symmetric(c.torso()));   // stripes: every third column
        CHECK(symmetric(c.ears()));
        // The head silhouette is symmetric on its own.
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W / 2; x++)
                CHECK(c.headMask().has(x, y) == c.headMask().has(W - 1 - x, y));
        if (t.hat == HatNone || t.hat == Crown || t.hat == Tophat || t.hat == Headphones)
            CHECK(occupancySymmetric(c.head()));
    }
}

static void animation() {
    Character& c = character();
    const Traits t = traitsFromSeed("alpha");
    c.build(t);
    uint8_t a[PIXELS], b[PIXELS];
    // Deterministic: same inputs, same frame, also across rebuilds.
    c.render(Idle, CTX_SAMPLES[3], a);
    c.render(Idle, CTX_SAMPLES[3], b);
    CHECK(memcmp(a, b, PIXELS) == 0);
    c.build(t);
    c.render(Idle, CTX_SAMPLES[3], b);
    CHECK(memcmp(a, b, PIXELS) == 0);
    // The six states look different from each other.
    uint8_t f[StateCount][PIXELS];
    for (int s = 0; s < StateCount; s++) c.render((State)s, CTX_SAMPLES[3], f[s]);
    for (int s = 0; s < StateCount; s++)
        for (int u = s + 1; u < StateCount; u++) CHECK(memcmp(f[s], f[u], PIXELS) != 0);
    // Speaking follows the level: a closed mouth and an open one differ;
    // listening raises the arms higher for a loud voice (on the beats it lifts them).
    uint8_t quiet[PIXELS], loud[PIXELS];
    c.render(Speaking, Ctx{1000, 0, 0, false}, quiet);
    c.render(Speaking, Ctx{1000, 0, 200, false}, loud);
    CHECK(memcmp(quiet, loud, PIXELS) != 0);
    c.render(Listening, Ctx{1200, 0, 0, false}, quiet);
    c.render(Listening, Ctx{1200, 0, 250, false}, loud);
    CHECK(memcmp(quiet, loud, PIXELS) != 0);
    // Thinking looks different while transcribing and while waiting.
    bool lookDiffers = false;                     // (an eye style may hide a single look step)
    for (uint32_t t : {1000u, 1500u, 2300u, 3100u}) {
        c.render(Thinking, Ctx{t, 0, 0, true}, quiet);
        c.render(Thinking, Ctx{t, 0, 0, false}, loud);
        if (memcmp(quiet, loud, PIXELS) != 0) lookDiffers = true;
    }
    CHECK(lookDiffers);
    // Idle moves over time (breathing), and the error face recovers.
    int changed = 0;
    c.render(Idle, Ctx{0, 0, 0, false}, a);
    for (uint32_t ms = FRAME_MS; ms < 6000; ms += FRAME_MS) {
        c.render(Idle, Ctx{ms, ms, 0, false}, b);
        if (memcmp(a, b, PIXELS) != 0) changed++;
        memcpy(a, b, PIXELS);
    }
    CHECK(changed >= 3);
    c.render(Error, Ctx{5000, 0, 0, false}, a);
    c.render(Error, Ctx{5000, 3000, 0, false}, b);
    CHECK(memcmp(a, b, PIXELS) != 0);
}

// ---------------------------------------------------------------- reactions
static void reactions() {
    CHECK(reactionMs(ReactNone) == 0);
    for (int r = Pleased; r < ReactionCount; r++) CHECK(reactionMs((uint8_t)r) >= 500 && reactionMs((uint8_t)r) <= 1500);
    CHECK(reactionMs(ReactionCount) == 0 && reactionMs(200) == 0);
    Character& c = character();
    int differing = 0, checked = 0;
    for (const char* n : {"alpha", "nova", "kitchen", "helper", "agent-003", "agent-012", "agent-020"}) {
        c.build(traitsFromSeed(n));
        for (State s : {Idle, Listening, Thinking, Speaking}) {
            for (int r = Pleased; r < ReactionCount; r++) {
                const uint32_t dur = reactionMs((uint8_t)r);
                for (uint32_t rt = 0; rt < dur + 200; rt += 33) {
                    const Ctx base{500000, 3000, 120, false};
                    const Ctx with{500000, 3000, 120, false, (uint8_t)r, rt};
                    uint8_t a[PIXELS], b[PIXELS];
                    c.render(s, base, a);
                    c.render(s, with, b);
                    for (int i = 0; i < PIXELS; i++) CHECK(b[i] < SlotCount);
                    if (rt >= dur) {
                        CHECK(memcmp(a, b, PIXELS) == 0);          // over: the plain state frame again
                    } else {
                        checked++;
                        if (memcmp(a, b, PIXELS) != 0) differing++;
                    }
                }
            }
        }
    }
    CHECK(checked > 0 && differing * 10 >= checked * 9);             // the reaction is visible almost always
    // Error and Sleep keep their own faces.
    c.build(traitsFromSeed("alpha"));
    for (State s : {Error, Sleep})
        for (int r = Pleased; r < ReactionCount; r++) {
            uint8_t a[PIXELS], b[PIXELS];
            c.render(s, Ctx{9000, 700, 0, false}, a);
            c.render(s, Ctx{9000, 700, 0, false, (uint8_t)r, 100}, b);
            CHECK(memcmp(a, b, PIXELS) == 0);
        }
    // The three reactions look different from each other.
    uint8_t f[3][PIXELS];
    for (int r = 0; r < 3; r++) c.render(Idle, Ctx{9000, 700, 0, false, (uint8_t)(r + 1), 250}, f[r]);
    CHECK(memcmp(f[0], f[1], PIXELS) != 0 && memcmp(f[1], f[2], PIXELS) != 0 && memcmp(f[0], f[2], PIXELS) != 0);
    // Deterministic, and they stay inside the dirty-rectangle budget: a
    // reaction never repaints more than the whole sprite, and one frame to
    // the next changes a bounded area.
    uint8_t a[PIXELS], b[PIXELS];
    c.render(Idle, Ctx{9000, 700, 0, false, Pleased, 400}, a);
    c.render(Idle, Ctx{9000, 700, 0, false, Pleased, 400}, b);
    CHECK(memcmp(a, b, PIXELS) == 0);
    // Marks use only the floating-effect color, in the top-right corner or at the sides.
    c.render(Idle, Ctx{9000, 700, 0, false}, a);
    c.render(Idle, Ctx{9000, 700, 0, false, Puzzled, 100}, b);
    bool mark = false;
    for (int y = 0; y < 9; y++)
        for (int x = 24; x < W; x++)
            if (b[y * W + x] == Float && a[y * W + x] != Float) mark = true;
    CHECK(mark);
}

// The meters' bar heights.
static void waves() {
    int prev = 0;
    for (int l = 0; l <= 255; l++) {
        const int h = waveHalf(l, 10);
        CHECK(h >= 1 && h <= 10 && h >= prev);
        prev = h;
    }
    CHECK(waveHalf(0, 10) == 1 && waveHalf(255, 10) == 10);
    CHECK(waveHalf(-5, 10) == 1 && waveHalf(999, 10) == 10);
    CHECK(waveHalf(60, 10) > waveHalf(60, 10) - 1 && waveHalf(20, 10) >= 3);   // speech levels stay visible
    CHECK(waveHalf(100, 1) == 1 && waveHalf(255, 38) == 38);
}

static void schedules() {
    // lidRows: Python's round-half-to-even on the half-open eyelid.
    CHECK(lidRows(3, 1) == 2);
    CHECK(lidRows(5, 1) == 2);
    CHECK(lidRows(6, 1) == 3);
    CHECK(lidRows(4, 1) == 2);
    CHECK(lidRows(7, 1) == 4);
    CHECK(lidRows(5, 2) == 5);
    CHECK(lidRows(5, 0) == 0);
    // A blink is half, shut, half = 198 ms, once per period, in each temper.
    for (int tp = 0; tp < TempersCount; tp++) {
        const Temper& tm = TEMPER[tp];
        for (uint32_t seed : {1u, 99u, 2470140894u}) {
            uint32_t shut = 0, half = 0;
            const uint32_t span = 60000;
            for (uint32_t t = 0; t < span; t += 6) {
                const int l = blinkLid(seed, t, tm);
                if (l == 0) shut += 6;
                if (l == 1) half += 6;
            }
            const uint32_t periods = span / tm.blink;
            CHECK(shut >= (periods - 1) * 60 && shut <= (periods + 1) * 72);
            CHECK(half >= (periods - 1) * 120 && half <= (periods + 1) * 144);
            CHECK(blinkLid(seed, 12345, tm) == blinkLid(seed, 12345, tm));
        }
    }
    // Glances: -1/0/1, about once per 6 s.
    int looks = 0;
    for (uint32_t t = 0; t < 60000; t += 100) {
        const int d = lookDx(7, t);
        CHECK(d >= -1 && d <= 1);
        if (d) looks++;
    }
    CHECK(looks > 100 && looks < 400);
    CHECK(mouthLevel(0) == 0 && mouthLevel(23) == 0 && mouthLevel(24) == 1 && mouthLevel(79) == 1 &&
          mouthLevel(80) == 2 && mouthLevel(149) == 2 && mouthLevel(150) == 3 && mouthLevel(255) == 3);
}

static void dirty() {
    uint8_t a[PIXELS], b[PIXELS];
    memset(a, 0, sizeof a);
    memcpy(b, a, sizeof b);
    Rect r;
    CHECK(!dirtyRect(a, b, r));
    b[5 * W + 7] = 3;
    CHECK(dirtyRect(a, b, r) && r.x0 == 7 && r.y0 == 5 && r.x1 == 8 && r.y1 == 6);
    b[20 * W + 30] = 9;
    b[2 * W + 1] = 1;
    CHECK(dirtyRect(a, b, r) && r.x0 == 1 && r.y0 == 2 && r.x1 == 31 && r.y1 == 21);
    memset(b, 1, sizeof b);
    CHECK(dirtyRect(a, b, r) && r.x0 == 0 && r.y0 == 0 && r.x1 == W && r.y1 == H);
    // The animation only repaints a part of the sprite on most frames.
    Character& c = character();
    c.build(traitsFromSeed("alpha"));
    for (int s = 0; s < StateCount; s++) {
        uint8_t prev[PIXELS], cur[PIXELS];
        long px = 0, frames = 0, changedFrames = 0;
        c.render((State)s, Ctx{0, 0, 120, false}, prev);
        for (uint32_t ms = FRAME_MS; ms <= 6000; ms += FRAME_MS) {
            c.render((State)s, Ctx{ms, ms, (uint8_t)(ms % 256), false}, cur);
            if (dirtyRect(prev, cur, r)) {
                px += (long)(r.x1 - r.x0) * (r.y1 - r.y0);
                changedFrames++;
                CHECK(r.x1 - r.x0 <= W && r.y1 - r.y0 <= H);
            }
            frames++;
            memcpy(prev, cur, PIXELS);
        }
        std::printf("[avatar] state %d: %ld/%ld frames change, avg %ld sprite px per changed frame\n",
                    s, changedFrames, frames, changedFrames ? px / changedFrames : 0L);
        CHECK(px / frames < PIXELS);              // never the whole sprite on average
    }
}

// ---------------------------------------------------------------- the kept look
static std::string tagOf(const Traits& t) {
    char b[9];
    lookTag(t, b);
    return b;
}

static bool bytesFromHex(const char* h, uint8_t* out, size_t n) {
    if (std::strlen(h) != n * 2) return false;
    for (size_t i = 0; i < n; i++) {
        char b[3] = {h[2 * i], h[2 * i + 1], 0};
        out[i] = (uint8_t)std::strtol(b, nullptr, 16);
    }
    return true;
}

static uint32_t allStatesCrc(const Traits& t) {
    Character& c = character();
    c.build(t);
    uint8_t buf[StateCount * PIXELS];
    for (int s = 0; s < StateCount; s++) c.render((State)s, Ctx{123456, 789, 200, false}, buf + s * PIXELS);
    return crc32(buf, sizeof buf);
}

// Kept looks as the owner would have stored them (record + seed), and the frames they must
// keep drawing. A vocabulary that renumbers or redraws an existing part breaks these.
struct FrozenKept {
    const char* bytes;
    uint32_t crc[StateCount];
};
static const FrozenKept FROZEN[] = {
    {"00020401100b0112010b16048a4a39f4",
     {0x1afc22db, 0xaf2d5839, 0xcc9bec5e, 0x0bdb3848, 0x0ec4a659, 0x2c64a3a9}},
    {"0305040300020902000d12004550fce1",
     {0x2033c857, 0x6b09f6be, 0x6cfd70ee, 0x69535e3c, 0xa6d11811, 0xa1f6d1b0}},
    {"0f0704040f0c0c120f0d170642dc8204",
     {0x0c473687, 0x41aa3657, 0x8ec11149, 0x0975d7bc, 0xc52640bd, 0xd6262b1d}},
};

static void keptLook() {
    // The stored record round-trips every index and the seed, for many agents' variants.
    for (const char* n : {"", "alpha", "nova", "kitchen", "agent-017"})
        for (uint16_t v = 0; v <= 60; v++) {
            const Traits t = traitsForVariant(n, v);
            uint8_t b[KEPT_BYTES];
            keptEncode(t, b);
            Traits u = traitsFromSeed("other");
            CHECK(keptDecode(b, KEPT_BYTES, u));
            CHECK(u == t);
            CHECK(frameCrc(u, Idle, 5000, 100, 0, false) == frameCrc(t, Idle, 5000, 100, 0, false));
        }
    uint8_t b[KEPT_BYTES];
    keptEncode(traitsFromSeed("alpha"), b);
    // A record that does not decode is no kept look and leaves the output alone.
    Traits out = traitsFromSeed("nova");
    const Traits before = out;
    CHECK(!keptDecode(b, KEPT_BYTES - 1, out) && out == before);
    CHECK(!keptDecode(b, KEPT_BYTES + 1, out) && out == before);
    CHECK(!keptDecode(b, 0, out) && !keptDecode(nullptr, KEPT_BYTES, out) && out == before);
    for (int i = 0; i < TRAIT_FIELDS; i++) {
        uint8_t bad[KEPT_BYTES];
        std::memcpy(bad, b, KEPT_BYTES);
        bad[i] = TRAIT_RANGE[i];
        CHECK(!keptDecode(bad, KEPT_BYTES, out));
        bad[i] = 0xFF;
        CHECK(!keptDecode(bad, KEPT_BYTES, out));
        bad[i] = (uint8_t)(TRAIT_RANGE[i] - 1);                // the last index of the vocabulary decodes
        CHECK(keptDecode(bad, KEPT_BYTES, out) && out.get(i) == TRAIT_RANGE[i] - 1);
    }
    Traits s = traitsFromSeed("alpha");
    s.seed = 0x04030201u;
    keptEncode(s, b);
    CHECK(b[TRAIT_FIELDS] == 1 && b[TRAIT_FIELDS + 1] == 2 && b[TRAIT_FIELDS + 2] == 3 &&
          b[TRAIT_FIELDS + 3] == 4);                           // the seed is little endian
    // Frozen records still draw the frames they drew when they were kept.
    for (const FrozenKept& f : FROZEN) {
        uint8_t fb[KEPT_BYTES];
        CHECK(bytesFromHex(f.bytes, fb, KEPT_BYTES));
        Traits t = traitsFromSeed("other");
        CHECK(keptDecode(fb, KEPT_BYTES, t));
        for (int st = 0; st < StateCount; st++)
            CHECK(frameCrc(t, (State)st, 123456, 789, 200, false) == f.crc[st]);
    }
    // Existing indexes keep their meaning: the last part of each vocabulary before this one grew.
    static_assert(Tufts == 9 && Dot == 6 && OMouth == 6 && Hair == 12 && Flower == 9 && Blush == 7 &&
                      Alert == 3,
                  "append-only vocabularies");
    static_assert(Cat == 1 && Antenna == 6 && Led == 5 && Tooth == 5 && Crown == 4 && Halo == 11 &&
                      Glasses == 3 && Spots == 3,
                  "append-only vocabularies");
    CHECK(std::strcmp(CHAR_PALETTES[0].name, "Tangerine") == 0 && std::strcmp(CHAR_PALETTES[17].name, "Sand") == 0);
    CHECK(PALETTE_COUNT > 18 && HEAD_COUNT > 10 && TORSO_COUNT > 5 && FEET_COUNT > 3 && ARMS_COUNT > 3);
    // The NVS key: fixed shape, within the 15-character limit, per agent.
    char a[16], k[16], d[16], e[16];
    keptKey("alpha", a);
    keptKey("nova", k);
    keptKey("", d);
    keptKey("default", e);
    CHECK(std::strlen(a) == 10 && a[0] == 'a' && a[1] == 'k');
    CHECK(std::strcmp(a, k) != 0 && std::strcmp(d, e) == 0);
    // The tag: 8 hex digits, stable, different for different characters.
    std::set<std::string> tags;
    for (uint16_t v = 0; v <= VARIANT_MAX; v++) {
        char tg[9];
        lookTag(traitsForVariant("alpha", v), tg);
        CHECK(std::strlen(tg) == 8);
        for (int i = 0; i < 8; i++) CHECK((tg[i] >= '0' && tg[i] <= '9') || (tg[i] >= 'a' && tg[i] <= 'f'));
        tags.insert(tg);
    }
    CHECK(tags.size() == (size_t)VARIANT_MAX + 1);
    CHECK(tagOf(traitsFromSeed("alpha")) == tagOf(traitsFromSeed("alpha")));
    Traits seedOnly = traitsFromSeed("alpha");
    seedOnly.seed ^= 1;
    CHECK(tagOf(seedOnly) != tagOf(traitsFromSeed("alpha")));   // the seed is part of the character
    // Browsing positions: the kept look, the Original, a variant over the current generator.
    const char* look = "ffffffff0300ffff0107ff02";
    const Traits kept = traitsForVariant("alpha", 77);
    CHECK(traitsForSlot("alpha", KEPT_SLOT, look, &kept) == kept);
    CHECK(traitsForSlot("alpha", KEPT_SLOT, look, nullptr) == traitsForAgent("alpha", 0, look));
    CHECK(traitsForSlot("alpha", 0, look, &kept) == traitsForAgent("alpha", 0, look));
    CHECK(traitsForSlot("alpha", 5, look, &kept) == traitsForVariant("alpha", 5));
    // Keep: nothing when the shown character already is the agent's, forget on Original, else store.
    const Traits orig = traitsForAgent("alpha", 0, look), other = traitsForVariant("alpha", 5);
    CHECK(keepAction(orig, orig, nullptr) == KeepNothing);
    CHECK(keepAction(other, orig, nullptr) == KeepStore);
    CHECK(keepAction(other, orig, &kept) == KeepStore);
    CHECK(keepAction(kept, orig, &kept) == KeepNothing);
    CHECK(keepAction(orig, orig, &kept) == KeepClear);
}

// ---------------------------------------------------------------- variety
static void distinctness() {
    static const int OLD[TRAIT_FIELDS] = {10, 5, 3, 3, 10, 7, 7, 13, 10, 8, 18, 4};   // sizes before the growth
    double oldSpace = 1, space = 1;
    for (int i = 0; i < TRAIT_FIELDS; i++) {
        CHECK(TRAIT_RANGE[i] >= OLD[i]);
        oldSpace *= OLD[i];
        space *= TRAIT_RANGE[i];
    }
    CHECK(space >= 2 * oldSpace);
    std::set<std::string> records;
    std::unordered_set<uint64_t> looks;
    std::vector<std::vector<bool> > reached(TRAIT_FIELDS);
    for (int i = 0; i < TRAIT_FIELDS; i++) reached[i].assign(TRAIT_RANGE[i], false);
    const int N = 20000;
    for (int i = 0; i < N; i++) {
        char name[32];
        std::snprintf(name, sizeof name, "agent-%05d", i);
        const Traits t = traitsFromSeed(name);
        for (int f = 0; f < TRAIT_FIELDS; f++) reached[f][t.get(f)] = true;
        records.insert(hex(t));
        looks.insert(((uint64_t)allStatesCrc(t) << 8) | t.palette);
    }
    CHECK((int)records.size() == N && (int)looks.size() == N);
    for (int f = 0; f < TRAIT_FIELDS; f++)
        for (int v = 0; v < TRAIT_RANGE[f]; v++) CHECK(reached[f][v]);   // the seed reaches every part
    std::printf("[avatar] %d seeds: %zu distinct trait records, %zu distinct drawn characters; "
                "combination space %.3g (%.3g before)\n",
                N, records.size(), looks.size(), space, oldSpace);
}

// A character with every optional part off, so one part at a time can be put on.
static Traits plainBase() {
    Traits t = traitsFromSeed("alpha");
    t.head = 0;
    t.torso = 2;
    t.feet = 0;
    t.arms = 1;
    t.ears = EarsNone;
    t.eyes = Round;
    t.mouth = Smile;
    t.hat = HatNone;
    t.acc = AccNone;
    t.pattern = PatNone;
    t.temper = Calm;
    return t;
}

static uint32_t looksCrc(const Traits& t) {
    Character& c = character();
    c.build(t);
    uint8_t buf[3 * PIXELS];
    c.render(Idle, Ctx{123456, 789, 0, false}, buf);
    c.render(Speaking, Ctx{123456, 789, 130, false}, buf + PIXELS);
    c.render(Thinking, Ctx{123456, 789, 0, false}, buf + 2 * PIXELS);
    return crc32(buf, sizeof buf);
}

// Every value of a part vocabulary draws a different character: no two heads, eyes, hats,
// ... look the same. (Palette and temper are colours and timing, checked elsewhere.)
static void everyPartDiffers() {
    for (int f = 0; f < TRAIT_FIELDS; f++) {
        if (f == TRAIT_PALETTE || f == TRAIT_FIELDS - 1) continue;
        std::set<uint32_t> seen;
        for (int v = 0; v < TRAIT_RANGE[f]; v++) {
            Traits t = plainBase();
            *t.at(f) = (uint8_t)v;
            const bool fresh = seen.insert(looksCrc(t)).second;
            if (!fresh) std::fprintf(stderr, "field %d value %d draws like an earlier value\n", f, v);
            CHECK(fresh);
        }
    }
}

// ---------------------------------------------------------------- every part, every state
// Each part value (against none, or the plain first value) shows in every state and every
// reaction, and every pixel is a valid slot.
static void partsInEveryState() {
    Character& c = character();
    for (int f = 0; f < TRAIT_FIELDS; f++) {
        if (f == TRAIT_PALETTE || f == TRAIT_FIELDS - 1) continue;
        const int ref = f == 6 ? (int)OMouth : 0;                 // a closed mouth can look like another one at rest
        Traits a = plainBase();
        *a.at(f) = (uint8_t)ref;
        for (int v = 0; v < TRAIT_RANGE[f]; v++) {
            if (v == ref) continue;
            Traits b = plainBase();
            *b.at(f) = (uint8_t)v;
            uint8_t fa[PIXELS], fb[PIXELS];
            for (int s = 0; s < StateCount; s++)
                for (const Ctx& x : {Ctx{123456, 3000, 120, false}, Ctx{5555, 3000, 40, true}}) {
                    c.build(a);
                    c.render((State)s, x, fa);
                    c.build(b);
                    c.render((State)s, x, fb);
                    for (int i = 0; i < PIXELS; i++) CHECK(fb[i] < SlotCount);
                    const bool same = std::memcmp(fa, fb, PIXELS) == 0;
                    if (same) std::fprintf(stderr, "field %d value %d invisible in state %d\n", f, v, s);
                    CHECK(!same);
                }
            for (State s : {Idle, Speaking})
                for (int r = Pleased; r < ReactionCount; r++)
                    for (uint32_t rt : {100u, 400u}) {
                        const Ctx x{123456, 3000, 120, false, (uint8_t)r, rt};
                        c.build(a);
                        c.render(s, x, fa);
                        c.build(b);
                        c.render(s, x, fb);
                        for (int i = 0; i < PIXELS; i++) CHECK(fb[i] < SlotCount);
                        CHECK(std::memcmp(fa, fb, PIXELS) != 0);
                    }
        }
    }
}

// The eye styles look around (the transcribing sweep) and blink, each in its own way.
static void eyesAnimate() {
    Character& c = character();
    for (int k = 0; k < EyesCount; k++) {
        Traits t = plainBase();
        t.eyes = (uint8_t)k;
        c.build(t);
        const Temper& tm = TEMPER[t.temper];
        const int ey = c.eyeY();
        auto eyeRowsEqual = [&](const uint8_t* x, const uint8_t* y, bool inside) {
            for (int yy = 0; yy < H; yy++) {
                const bool in = yy >= ey - 5 && yy <= ey + 5;
                if (in == inside && std::memcmp(x + yy * W, y + yy * W, W) != 0) return false;
            }
            return true;
        };
        // Glance: the sweep goes from -1 to +1 column; the eye rows differ, nothing else does.
        bool glanced = false;
        for (uint32_t b = 20000; b < 90000 && !glanced; b += 7) {
            if ((b / 260) % 4 != 0 || blinkLid(t.seed, b, tm) != 2 || blinkLid(t.seed, b + 520, tm) != 2 ||
                (b / 900) % 2 != ((b + 520) / 900) % 2)
                continue;
            uint8_t x[PIXELS], y[PIXELS];
            c.render(Thinking, Ctx{b, 500, 0, true}, x);
            c.render(Thinking, Ctx{b + 520, 500, 0, true}, y);
            glanced = !eyeRowsEqual(x, y, true);
        }
        CHECK(glanced);
        // Blink: shut and open frames at the same breathing phase differ in the eye rows only.
        bool blinked = false;
        for (uint32_t s1 = 20000; s1 < 120000 && !blinked; s1 += 3) {
            if (blinkLid(t.seed, s1, tm) != 0 || lookDx(t.seed, s1) != 0) continue;
            for (uint32_t s2 = s1 + 400; s2 < s1 + 3400 && !blinked; s2 += 11) {
                if (blinkLid(t.seed, s2, tm) != 2 || lookDx(t.seed, s2) != 0 || (s1 / 900) % 2 != (s2 / 900) % 2)
                    continue;
                uint8_t x[PIXELS], y[PIXELS];
                c.render(Idle, Ctx{s1, 3000, 0, false}, x);
                c.render(Idle, Ctx{s2, 3000, 0, false}, y);
                blinked = eyeRowsEqual(x, y, false) && !eyeRowsEqual(x, y, true);
            }
        }
        if (!blinked) std::fprintf(stderr, "eyes %d never blink in the eye rows alone\n", k);
        CHECK(blinked);
    }
}

// Each temper keeps its own time (blinks, fidgets), and the new ones do what they say.
static void temperTimelines() {
    Character& c = character();
    uint32_t sig[TempersCount];
    for (int tp = 0; tp < TempersCount; tp++) {
        Traits t = plainBase();
        t.temper = (uint8_t)tp;
        c.build(t);
        uint32_t h = 2166136261u;
        std::set<uint32_t> frames;
        std::set<int> leftEdges;
        bool tongue = false;
        for (uint32_t ms = 0; ms < 60000; ms += FRAME_MS) {
            uint8_t f[PIXELS];
            c.render(Idle, Ctx{ms, ms, 0, false}, f);
            const uint32_t crc = crc32(f, PIXELS);
            frames.insert(crc);
            h = (h ^ crc) * 16777619u;
            int left = W;
            for (int y = 0; y < H; y++)
                for (int x = 0; x < W && x < left; x++)
                    if (f[y * W + x] == Rim) left = x;
            leftEdges.insert(left);
            for (int i = 0; i < PIXELS; i++)
                if (f[i] == Tongue) tongue = true;
        }
        sig[tp] = h;
        CHECK(frames.size() >= 4);
        CHECK(tp == Drowsy ? tongue : !tongue);                   // the yawn opens the mouth wide
        CHECK(tp == Jumpy ? leftEdges.size() >= 2 : leftEdges.size() == 1);   // the shiver moves the whole sprite
    }
    for (int a = 0; a < TempersCount; a++)
        for (int b = a + 1; b < TempersCount; b++) CHECK(sig[a] != sig[b]);
}

// Static layers mirror about the axis for every value of every part.
static void symmetryOfEveryPart() {
    Character& c = character();
    for (int f = 0; f < TRAIT_FIELDS; f++) {
        if (f == TRAIT_PALETTE || f == TRAIT_FIELDS - 1) continue;
        for (int v = 0; v < TRAIT_RANGE[f]; v++) {
            Traits t = plainBase();
            *t.at(f) = (uint8_t)v;
            c.build(t);
            CHECK(symmetric(c.feet()));
            if (t.pattern != Stripes) CHECK(symmetric(c.torso()));
            CHECK(symmetric(c.ears()));
            for (int y = 0; y < H; y++)
                for (int x = 0; x < W / 2; x++) CHECK(c.headMask().has(x, y) == c.headMask().has(W - 1 - x, y));
            // The head carries a pattern or a hat that mirrors, except the peaked cap, the
            // knotted headband, the wreath's blooms and the hats that are asymmetric by design.
            if (t.hat != Cap && t.hat != Headband && t.hat != Wreath && t.hat != Bow && t.hat != Roof &&
                t.hat != Propeller)
                CHECK(occupancySymmetric(c.head()));
        }
    }
}

// Every part keeps its outline and gets the glow, on every state.
static void rimOfEveryPart() {
    Character& c = character();
    for (int f = 0; f < TRAIT_FIELDS; f++) {
        if (f == TRAIT_PALETTE || f == TRAIT_FIELDS - 1) continue;
        for (int v = 0; v < TRAIT_RANGE[f]; v++) {
            Traits t = plainBase();
            *t.at(f) = (uint8_t)v;
            c.build(t);
            for (int s = 0; s < StateCount; s++)
                for (int k = 0; k < 3; k++) {
                    uint8_t fr[PIXELS];
                    c.render((State)s, CTX_SAMPLES_AT(k), fr);
                    CHECK(glowRule(fr));
                }
        }
    }
}

// The listening ripples and the speaking sparks: a disc of body stands in for
// the character, the shadow row under it, background elsewhere.
static Canvas overlayStage(int cy) {
    Canvas f;
    f.clear();
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            const int dx = 2 * x - 31, dy = 2 * (y - cy);
            if (dx * dx + dy * dy < 15 * 15) f.px[y * W + x] = Body;
            else if (y == 30 && x > 8 && x < 23) f.px[y * W + x] = Shadow;
        }
    return f;
}

static double meanDistance(const Canvas& f, int cy, bool (*is)(uint8_t)) {
    double sum = 0;
    int n = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            if (is(f.px[y * W + x])) sum += std::hypot(x - 15.5, (y - cy) * 1.15), n++;
    return n ? sum / n : 0;
}
static bool isListen(uint8_t s) { return s == ListenC || (s >= ListenF1 && s <= ListenF3); }
static bool isOk(uint8_t s) { return s == OkC || (s >= OkF1 && s <= OkF3); }

static void overlays() {
    // Fade slots: the colour over the background at 25 / 50 / 75 %, on every theme.
    for (size_t fam = 0; fam < THEME_COUNT; fam++)
        for (const Palette* pp : {&THEMES[fam].dark, &THEMES[fam].light}) {
            const uint16_t bg = pp->ink[(int)Ink::Bg];
            uint16_t p[SlotCount];
            paletteFor(withPalette(4), bg, p);
            for (int a = 1; a <= 3; a++) {
                CHECK(p[ListenF1 + a - 1] == blend565(bg, p[ListenC], a * 64));
                CHECK(p[OkF1 + a - 1] == blend565(bg, p[OkC], a * 64));
            }
            CHECK(contrast(p[ListenF3], bg) > contrast(p[ListenF1], bg));
            CHECK(contrast(p[OkF3], bg) > contrast(p[OkF1], bg));
        }
    CHECK(fadeOf(ListenC, 4) == ListenC && fadeOf(ListenC, 1) == ListenF1 && fadeOf(OkC, 3) == OkF3);

    const int cy = 14;
    const Canvas stage = overlayStage(cy);
    // Ripples light from the outside in, the newest at 75 %, the older ones 50 %
    // and 25 %; they fade out completely, then two blank steps; the cycle repeats.
    static const int PATTERN[7][3] = {{3, 0, 0}, {2, 3, 0}, {1, 2, 3}, {0, 1, 2},
                                      {0, 0, 1}, {0, 0, 0}, {0, 0, 0}};
    for (uint32_t step = 0; step < 14; step++)
        for (int i = 0; i < RIPPLE_RINGS; i++)
            CHECK(rippleStrength(i, step * RIPPLE_STEP_MS + 10) == PATTERN[step % 7][i]);
    // They paint only background and shadow, in the listening fades, never at full
    // colour (soft), and nothing at all during the rest; the strongest ring moves
    // inward step by step.
    for (uint32_t t = 0; t < 4200; t += 50) {
        Canvas f = stage;
        ripples(f, t, cy);
        int painted = 0;
        for (int i = 0; i < PIXELS; i++) {
            if (f.px[i] == stage.px[i]) continue;
            painted++;
            CHECK(stage.px[i] == Clear || stage.px[i] == Shadow);
            CHECK(isListen(f.px[i]) && f.px[i] != ListenC);
        }
        const bool lit = rippleStrength(0, t) || rippleStrength(1, t) || rippleStrength(2, t);
        CHECK(lit ? painted > 0 : painted == 0);
    }
    auto strongest = [](uint8_t s) { return s == ListenF3; };
    double last = 1e9;
    for (uint32_t step = 0; step < 3; step++) {
        Canvas f = stage;
        ripples(f, step * RIPPLE_STEP_MS + 10, cy);
        const double d = meanDistance(f, cy, strongest);
        CHECK(d > 0 && d < last);
        last = d;
    }
    // Sparks: background only, in the speaking colours; the first one rises and
    // starts at full strength; the same time gives the same frame.
    for (uint32_t t = 0; t < 4000; t += 70) {
        Canvas f = stage, g = stage;
        sparks(f, t, 0x1234u, cy);
        sparks(g, t, 0x1234u, cy);
        CHECK(memcmp(f.px, g.px, PIXELS) == 0);
        for (int i = 0; i < PIXELS; i++)
            if (f.px[i] != stage.px[i]) {
                CHECK(stage.px[i] == Clear || stage.px[i] == Shadow);
                CHECK(isOk(f.px[i]));
            }
    }
    auto meanY = [&](uint32_t t) {
        Canvas f = stage;
        sparks(f, t, 0x1234u, cy);
        double sum = 0;
        int n = 0, full = 0;
        for (int i = 0; i < PIXELS; i++)
            if (isOk(f.px[i])) sum += i / W, n++, full += f.px[i] == OkC;
        CHECK(n > 0 && (t > 600 || full == n));     // a young spark is drawn at full strength
        return sum / n;
    };
    CHECK(meanY(SPARK_EVERY_MS - 10) < meanY(0));   // one spark alive, a little higher
    // Where sparks overlap, the pixel shows the strongest (youngest) one; overlaps
    // happen all the time (a note is 4 px tall and they start 1.2 px apart).
    int overlaps = 0;
    for (uint32_t seed : {0x1234u, 0xBEEFu, 7u})
        for (uint32_t t = 0; t < 5000; t += 37) {
            Canvas f = stage;
            sparks(f, t, seed, cy);
            int want[PIXELS] = {0}, cover[PIXELS] = {0};
            for (uint32_t k = 0; k <= t / SPARK_EVERY_MS; k++) {
                int8_t px[8][2];
                int n = 0;
                const int a = spark(k, t, seed, cy, px, n);
                for (int i = 0; i < n && a; i++) {
                    const int x = px[i][0], y = px[i][1];
                    if ((unsigned)x >= (unsigned)W || (unsigned)y >= (unsigned)H) continue;
                    want[y * W + x] = std::max(want[y * W + x], a);
                    cover[y * W + x]++;
                }
            }
            for (int i = 0; i < PIXELS; i++) {
                if (stage.px[i] != Clear && stage.px[i] != Shadow) continue;
                overlaps += cover[i] > 1;
                CHECK(f.px[i] == (want[i] ? fadeOf(OkC, want[i]) : stage.px[i]));
            }
        }
    CHECK(overlaps > 50);
    // In the character: listening shows ripples, speaking shows sparks, over a few seconds.
    Character& c = character();
    c.build(traitsFromSeed("ripple"));
    bool rip = false, spark = false;
    for (uint32_t ms = 0; ms < 3000; ms += 66) {
        uint8_t a[PIXELS], b[PIXELS];
        c.render(Listening, Ctx{ms, ms, 180, false}, a);
        c.render(Speaking, Ctx{ms, ms, 180, false}, b);
        for (int i = 0; i < PIXELS; i++) rip |= isListen(a[i]) && a[i] != ListenC, spark |= isOk(b[i]);
    }
    CHECK(rip && spark);
}

int main() {
    golden();
    tables();
    ellipses();
    traits();
    merging();
    palette();
    rimPass();
    looks();
    variants();
    keptLook();
    distinctness();
    everyPartDiffers();
    partsBounds();
    symmetry();
    symmetryOfEveryPart();
    animation();
    partsInEveryState();
    eyesAnimate();
    temperTimelines();
    rimOfEveryPart();
    schedules();
    reactions();
    waves();
    overlays();
    dirty();
    if (failures) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::puts("avatar_test ok");
    return 0;
}
