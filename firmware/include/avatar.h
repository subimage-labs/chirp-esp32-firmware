// avatar.h — the character mode's pixel-art avatar: a seeded, layered 32x32
// character per agent and its animation. Pure (no Arduino), host-tested;
// character.cpp draws the frames (loop task).
//
// Everything is a deterministic function of its inputs:
//   traitsFromSeed(profile)  → Traits (stable per agent, no setup)
//   paletteFor(traits, bg)   → SlotCount RGB565 colors: the character's own
//                              palette (never theme-derived); only the
//                              background slot, the silhouette edge and the
//                              floating effects follow the theme background
//   Character::build(traits) → the static layers (feet, torso, ears, head)
//   Character::render(state, ctx, out) → a 32x32 palette-index frame
// There is no random state: blinks, glances and fidgets come from a stateless
// hash of (seed, interval number), so one call always gives one frame.
//
// Traits are a plain record of vocabulary indexes. mergeTraits() overlays a
// sparse record (0xFF = keep the seeded value), so persona-derived choices
// from the host can override single parts later without touching the
// renderer.
//
// Coordinates: the mirror axis lies between columns 15 and 16. The silhouette
// tables are integers (avatar_art.h); ellipses use exact integer tests in
// tenths of a pixel, so host and device agree pixel for pixel.
#pragma once

#include <stdint.h>
#include <string.h>

#include "avatar_art.h"
#include "meter.h"
#include "theme.h"

namespace avatar {

const int W = 32;
const int H = 32;
const int PIXELS = W * H;
const uint32_t FRAME_MS = 66;       // 15 fps animation tick

enum State : uint8_t { Idle, Listening, Thinking, Speaking, Error, Sleep, StateCount };

// One-shot expressions layered over the state's pose for a moment, started by
// events (the answer starts, nothing was heard, a turn begins). A reaction is
// pure animation: the same state frame with the face and body overridden for
// reactionMs() from its start.
enum Reaction : uint8_t { ReactNone, Pleased, Puzzled, Surprised, ReactionCount };
inline uint32_t reactionMs(uint8_t r) {
    return r == Pleased ? 1000 : r == Puzzled ? 1200 : r == Surprised ? 600 : 0;
}

// What the device knows at draw time.
struct Ctx {
    uint32_t t;        // ms since boot
    uint32_t stateT;   // ms since this state began
    uint8_t level;     // 0..255: playback level (Speaking) / mic level (Listening)
    bool stt;          // Thinking before the LLM stage = transcribing
    uint8_t react;     // Reaction in progress (ReactNone = none)
    uint32_t reactT;   // ms since it began
    Ctx(uint32_t t_ = 0, uint32_t stateT_ = 0, uint8_t level_ = 0, bool stt_ = false,
        uint8_t react_ = ReactNone, uint32_t reactT_ = 0)
        : t(t_), stateT(stateT_), level(level_), stt(stt_), react(react_), reactT(reactT_) {}
};

// ---------------------------------------------------------------- rng
inline uint32_t fnv1a(const char* s) {
    uint32_t h = 0x811C9DC5u;
    for (; *s; s++) h = (h ^ (uint8_t)*s) * 0x01000193u;
    return h;
}

// xorshift32, never zero-seeded.
struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 0x9E3779B9u) {}
    uint32_t next() {
        uint32_t x = s;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        s = x;
        return s;
    }
    uint32_t pick(uint32_t n) { return (next() >> 8) % n; }
};

// Stateless hash for the animation schedules.
inline uint32_t hash2(uint32_t a, uint32_t b) {
    uint32_t h = a * 0x9E3779B1u + b * 0x85EBCA6Bu + 0x1B873593u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    return h;
}

// ---------------------------------------------------------------- traits
const int TRAIT_FIELDS = 12;
const int TRAIT_PALETTE = 10;       // index of the palette field in the record
static const uint8_t TRAIT_RANGE[TRAIT_FIELDS] = {
    HEAD_COUNT, TORSO_COUNT, FEET_COUNT, ARMS_COUNT, EarsCount, EyesCount, MouthsCount, HatsCount,
    AccsCount, PatternsCount, PALETTE_COUNT, TempersCount};
const uint8_t TRAIT_KEEP = 0xFF;   // in an override record: take the seeded value

struct Traits {
    uint8_t head, torso, feet, arms, ears, eyes, mouth, hat, acc, pattern, palette, temper;
    uint32_t seed;

    uint8_t* at(int i) {
        uint8_t* p[TRAIT_FIELDS] = {&head, &torso, &feet,    &arms,    &ears,   &eyes,
                                    &mouth, &hat,  &acc,     &pattern, &palette, &temper};
        return p[i];
    }
    uint8_t get(int i) const { return *const_cast<Traits*>(this)->at(i); }
    // The 12-byte record (one index per field), the host's override format.
    void record(uint8_t* out) const {
        for (int i = 0; i < TRAIT_FIELDS; i++) out[i] = get(i);
    }
    bool operator==(const Traits& o) const {
        for (int i = 0; i < TRAIT_FIELDS; i++)
            if (get(i) != o.get(i)) return false;
        return seed == o.seed;
    }
};

// The seeded character for a 32-bit seed.
inline Traits traitsFromHash(uint32_t seed) {
    Rng r(seed);
    r.next();
    r.next();
    Traits t;
    t.seed = seed;
    for (int i = 0; i < TRAIT_FIELDS; i++)
        if (i != TRAIT_PALETTE) *t.at(i) = (uint8_t)r.pick(TRAIT_RANGE[i]);
    // The palette is its own draw from the seed, so it does not depend on (or
    // shift) the other traits: variety stays high.
    t.palette = (uint8_t)(hash2(seed, 0xC0101Eu) % PALETTE_COUNT);
    // Composition rules keeping the draw legible. The ears/hat rules are mirrored
    // by legible() in host/avatar_look.py.
    const bool tallEars = t.ears == Bunny || t.ears == Fox || t.ears == Antlers;
    const bool tallHat = t.hat == Winged || t.hat == Headphones || t.hat == Tophat || t.hat == Viking;
    const bool centredEars = t.ears == Antenna || t.ears == Unicorn || t.ears == Sprout || t.ears == Spikes;
    if (tallHat && tallEars) t.ears = EarsNone;        // tall ears do not fit under tall hats
    if (centredEars && t.hat != HatNone && t.hat != Bow && t.hat != Halo)
        t.ears = EarsNone;                             // a hat sits where a centred ear grows
    if (t.eyes == Led && r.pick(2)) t.mouth = Grill;   // LED eyes get a grill mouth
    if (t.hat != HatNone && r.pick(3) == 0) t.acc = AccNone;   // fewer hat+accessory pile-ups
    if (t.acc == Glasses && t.eyes == Led) t.acc = AccNone;
    if (t.acc == Eyepatch && t.eyes == Cyclops) t.acc = AccNone;   // the patch would hide the only eye
    return t;
}

// Seeded default: every agent gets a stable character. "" is "default".
inline Traits traitsFromSeed(const char* name) {
    return traitsFromHash(fnv1a(name && *name ? name : "default"));
}

// Variants: the Chirper tab lets the owner browse other characters for an
// agent. Variant 0 is the agent's own seeded character ("Original"); variant
// n > 0 is the character of hash(name seed, n). They are generated on the
// fly over the CURRENT generator, so a variant number names a different
// character whenever the vocabularies grow: browsing is exploration only. What
// the owner keeps is the character itself (the kept look, below), never the
// number.
const uint16_t VARIANT_MAX = 999;
inline uint32_t variantSeed(const char* name, uint16_t n) {
    const uint32_t base = fnv1a(name && *name ? name : "default");
    return n == 0 ? base : hash2(base, 0x56415200u + n);
}
inline Traits traitsForVariant(const char* name, uint16_t n) {
    return traitsFromHash(variantSeed(name, n));
}

// An agent's persona look: TRAIT_FIELDS bytes as hex (setup_form.h Agent::look,
// chosen on the host), 0xFF = keep the seeded value. Returns the byte count
// (0 for an empty or malformed string: no look).
inline int lookFromHex(const char* hex, uint8_t* rec) {
    if (!hex) return 0;
    const size_t n = strlen(hex);
    if (n != (size_t)TRAIT_FIELDS * 2) return 0;
    for (size_t i = 0; i < n; i++) {
        const char c = hex[i];
        const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (!ok) return 0;
    }
    for (int i = 0; i < TRAIT_FIELDS; i++) {
        int v = 0;
        for (int k = 0; k < 2; k++) {
            const char c = hex[i * 2 + k];
            v = v * 16 + (c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10);
        }
        rec[i] = (uint8_t)v;
    }
    return TRAIT_FIELDS;
}

inline Traits mergeTraits(const Traits& seeded, const uint8_t* rec, int n);

// An agent's Original (variant 0) or a variant over the current generator.
// Original is the persona look over the seeded character when the agent has
// one, else the seeded character alone; variants ignore the look.
inline Traits traitsForAgent(const char* name, uint16_t variant, const char* lookHex) {
    Traits t = traitsForVariant(name, variant);
    uint8_t rec[TRAIT_FIELDS];
    if (variant == 0 && lookFromHex(lookHex, rec)) t = mergeTraits(t, rec, TRAIT_FIELDS);
    return t;
}

// ---- The kept look
// "Keep" in the Chirper tab stores the character's own Traits record (the
// TRAIT_FIELDS indexes, then the 32-bit seed that drives blinks, glances and
// spot placement): 16 bytes under NVS key keptKey(agent). Every part is an
// index into an append-only vocabulary, so a kept look draws the same
// character after the generator grows. Nothing else is stored: a record
// that does not decode (wrong length, an index out of range) is no kept look
// and the agent shows its Original.
const int KEPT_BYTES = TRAIT_FIELDS + 4;

inline void keptEncode(const Traits& t, uint8_t* out /* KEPT_BYTES */) {
    t.record(out);
    for (int i = 0; i < 4; i++) out[TRAIT_FIELDS + i] = (uint8_t)(t.seed >> (8 * i));
}
inline bool keptDecode(const uint8_t* in, size_t n, Traits& out) {
    if (!in || n != (size_t)KEPT_BYTES) return false;
    for (int i = 0; i < TRAIT_FIELDS; i++)
        if (in[i] >= TRAIT_RANGE[i]) return false;
    Traits t;
    for (int i = 0; i < TRAIT_FIELDS; i++) *t.at(i) = in[i];
    t.seed = 0;
    for (int i = 0; i < 4; i++) t.seed |= (uint32_t)in[TRAIT_FIELDS + i] << (8 * i);
    out = t;
    return true;
}

// The NVS key of an agent's kept look: "ak" + the name's hash in hex (10
// characters; NVS keys allow 15).
inline void keptKey(const char* name, char* out /* >= 11 bytes */) {
    const uint32_t h = fnv1a(name && *name ? name : "default");
    static const char hexd[] = "0123456789abcdef";
    out[0] = 'a';
    out[1] = 'k';
    for (int i = 0; i < 8; i++) out[2 + i] = hexd[(h >> (28 - 4 * i)) & 0xF];
    out[10] = '\0';
}

// A short stable name of a character for logs and the bench status: 8 hex
// digits of the hash of its kept bytes.
inline void lookTag(const Traits& t, char* out /* >= 9 bytes */) {
    uint8_t b[KEPT_BYTES];
    keptEncode(t, b);
    uint32_t h = 0x811C9DC5u;                     // FNV-1a over the bytes (indexes are often 0)
    for (int i = 0; i < KEPT_BYTES; i++) h = (h ^ b[i]) * 0x01000193u;
    static const char hexd[] = "0123456789abcdef";
    for (int i = 0; i < 8; i++) out[i] = hexd[(h >> (28 - 4 * i)) & 0xF];
    out[8] = '\0';
}

// Browse positions of the Chirper tab: KEPT_SLOT is the kept look (only while
// there is one), 0 the Original, n > 0 variant n.
const int KEPT_SLOT = -1;
inline Traits traitsForSlot(const char* name, int slot, const char* lookHex, const Traits* kept) {
    if (slot == KEPT_SLOT && kept) return *kept;
    return traitsForAgent(name, slot < 0 ? 0 : (uint16_t)slot, lookHex);
}

// What "keep" does with the shown character: nothing when it already is the
// agent's character, forget the kept look when it is the Original again, else
// store it.
enum KeepAction : uint8_t { KeepNothing, KeepStore, KeepClear };
inline KeepAction keepAction(const Traits& shown, const Traits& original, const Traits* kept) {
    const Traits& current = kept ? *kept : original;
    if (shown == current) return KeepNothing;
    return shown == original ? KeepClear : KeepStore;
}

// Overlay a sparse record: field i = rec[i] unless it is TRAIT_KEEP, missing
// or out of range.
inline Traits mergeTraits(const Traits& seeded, const uint8_t* rec, int n) {
    Traits out = seeded;
    for (int i = 0; i < TRAIT_FIELDS; i++) {
        const uint8_t v = i < n ? rec[i] : TRAIT_KEEP;
        if (v != TRAIT_KEEP && v < TRAIT_RANGE[i]) *out.at(i) = v;
    }
    return out;
}

// ---------------------------------------------------------------- palette
// The character's colors are its palette's (avatar_art.h) and the same on
// every theme. Three things follow the theme background, decided by its
// brightness alone:
//  - the glow just outside the silhouette (slot Rim, a ring of background
//    pixels around the deep outline): the palette's bright rim tint at half
//    strength over dark backgrounds, nothing (the background) on light ones;
//  - the effects that float on the background (listening ripples, speaking
//    sparks, sleep Zs, halo, propeller): a dark-background or a
//    light-background variant; the ripples and sparks also fade through
//    their color at 25 / 50 / 75 % over the background (ListenF*, OkF*);
//  - the floor shadow, a faint tint of the background toward that edge color.
struct FxSet {
    uint32_t listen, ok, muted, floating;
};
static const FxSet FX_ON_DARK = {0x55c8f2, 0x7ddc8a, 0x8e97b0, 0xffd45e};
static const FxSet FX_ON_LIGHT = {0x1c6fc4, 0x2a8a3e, 0x5f6678, 0xa86400};

// Eyes, hats and mouths share these across all palettes.
const uint32_t CHAR_WHITE = 0xf6f0e4, CHAR_WHITE_D = 0xcbbfae, CHAR_PUPIL = 0x221a30,
               CHAR_DARK = 0x332a45, CHAR_MOUTH = 0x43202f, CHAR_TONGUE = 0xec6a80,
               CHAR_BUSY = 0xde5a14;

// Brightness of an RGB565 color, 0..255 in sRGB space (integer).
inline int brightness565(uint16_t c) {
    const int r = (c >> 11) * 255 / 31, g = ((c >> 5) & 0x3F) * 255 / 63, b = (c & 0x1F) * 255 / 31;
    return (54 * r + 183 * g + 19 * b) >> 8;
}
inline bool bgIsLight(uint16_t bg) { return brightness565(bg) > 96; }

// The sprite's SlotCount RGB565 colors for a character on background bg.
inline void paletteFor(const Traits& t, uint16_t bg, uint16_t* p) {
    const CharPalette& c = CHAR_PALETTES[t.palette < PALETTE_COUNT ? t.palette : 0];
    const bool light = bgIsLight(bg);
    const FxSet& fx = light ? FX_ON_LIGHT : FX_ON_DARK;
    const uint16_t rim = rgb565(c.rim);
    p[Clear] = bg;
    p[Outline] = rgb565(c.outline);
    p[Rim] = light ? bg : blend565(bg, rim, 128);
    p[BodyD] = rgb565(c.bodyD);
    p[Body] = rgb565(c.body);
    p[BodyL] = rgb565(c.bodyL);
    p[Belly] = rgb565(c.belly);
    p[AccD] = rgb565(c.accD);
    p[Acc] = rgb565(c.acc);
    p[AccL] = rgb565(c.accL);
    p[White] = rgb565(CHAR_WHITE);
    p[WhiteD] = rgb565(CHAR_WHITE_D);
    p[Pupil] = rgb565(CHAR_PUPIL);
    p[Dark] = rgb565(CHAR_DARK);
    p[MouthC] = rgb565(CHAR_MOUTH);
    p[Tongue] = rgb565(CHAR_TONGUE);
    p[Cheek] = rgb565(c.cheek);
    p[ListenC] = rgb565(fx.listen);
    p[BusyC] = rgb565(CHAR_BUSY);
    p[OkC] = rgb565(fx.ok);
    p[ErrC] = rgb565(c.err);
    p[Shadow] = blend565(bg, light ? rgb565(c.outline) : rim, 46);
    p[MutedC] = rgb565(fx.muted);
    p[Float] = rgb565(fx.floating);
    for (int a = 1; a <= 3; a++) {
        p[ListenF1 + a - 1] = blend565(bg, p[ListenC], a * 64);
        p[OkF1 + a - 1] = blend565(bg, p[OkC], a * 64);
    }
}

// ---------------------------------------------------------------- canvases
struct Canvas {
    uint8_t px[PIXELS];
    void clear() { memset(px, 0, sizeof px); }
    void put(int x, int y, uint8_t c) {
        if ((unsigned)x < (unsigned)W && (unsigned)y < (unsigned)H) px[y * W + x] = c;
    }
};

// A shape before it is outlined and drawn: slots on a grid with a margin, so
// art hanging over the canvas edge still outlines its in-canvas neighbours.
// 0 = absent.
struct Pix {
    static const int M = 3;
    static const int PW = W + 2 * M;
    static const int PH = H + 2 * M;
    uint8_t c[PW * PH];
    void clear() { memset(c, 0, sizeof c); }
    static bool inside(int x, int y) { return x >= -M && x < W + M && y >= -M && y < H + M; }
    void set(int x, int y, uint8_t s) {
        if (inside(x, y)) c[(y + M) * PW + x + M] = s;
    }
    uint8_t get(int x, int y) const { return inside(x, y) ? c[(y + M) * PW + x + M] : 0; }
    // Union with the left mirror of the right half (columns 16..31).
    void mirrorRight() {
        for (int y = -M; y < H + M; y++)
            for (int x = W / 2; x < W; x++) {
                const uint8_t s = get(x, y);
                if (s) set(W - 1 - x, y, s);
            }
    }
};

// Row bitmasks of a silhouette; bit x of rows[y].
struct Silhouette {
    uint32_t rows[H];
    bool has(int x, int y) const {
        return (unsigned)x < (unsigned)W && (unsigned)y < (unsigned)H && ((rows[y] >> x) & 1u);
    }
    void fromHalf(const uint8_t* half) {
        for (int y = 0; y < H; y++) {
            const int k = half[y];
            rows[y] = k ? (((1u << (2 * k)) - 1u) << (W / 2 - k)) : 0u;
        }
    }
    bool empty() const {
        for (int y = 0; y < H; y++)
            if (rows[y]) return false;
        return true;
    }
};

// Exact ellipse test; all arguments in tenths of a pixel (pixel centres at
// +5): ((x + .5 - cx) / rx)^2 + ((y + .5 - cy) / ry)^2 <= 1.
inline bool inEllipse(int x, int y, int cx10, int cy10, int rx10, int ry10) {
    const int X = 10 * x + 5 - cx10, Y = 10 * y + 5 - cy10;
    if (X > rx10 || X < -rx10 || Y > ry10 || Y < -ry10) return false;
    return X * X * ry10 * ry10 + Y * Y * rx10 * rx10 <= rx10 * rx10 * ry10 * ry10;
}

inline void blit(Canvas& dst, const Canvas& src, int dx, int dy) {
    for (int y = 0; y < H; y++) {
        const int ty = y + dy;
        if (ty < 0 || ty >= H) continue;
        for (int x = 0; x < W; x++) {
            const uint8_t v = src.px[y * W + x];
            const int tx = x + dx;
            if (v && tx >= 0 && tx < W) dst.px[ty * W + tx] = v;
        }
    }
}

// Bounding box of the changed pixels, x1/y1 exclusive; false when equal.
struct Rect {
    int x0, y0, x1, y1;
};
inline bool dirtyRect(const uint8_t* prev, const uint8_t* cur, Rect& r) {
    int x0 = W, y0 = H, x1 = -1, y1 = -1;
    for (int y = 0; y < H; y++) {
        const uint8_t* a = prev + y * W;
        const uint8_t* b = cur + y * W;
        if (memcmp(a, b, W) == 0) continue;
        if (y < y0) y0 = y;
        y1 = y;
        for (int x = 0; x < W; x++)
            if (a[x] != b[x]) {
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
            }
    }
    if (x1 < 0) return false;
    r.x0 = x0;
    r.y0 = y0;
    r.x1 = x1 + 1;
    r.y1 = y1 + 1;
    return true;
}

// ---------------------------------------------------------------- animation
struct Temper {
    uint16_t blink, jitter, breathe;   // ms
    uint8_t hop;                       // rows on speech beats
    uint16_t fidget;                   // ms
};
static const Temper TEMPER[TempersCount] = {
    {3600, 2400, 900, 1, 9000},   // calm
    {2400, 1800, 520, 2, 4200},   // bouncy
    {4200, 2600, 1100, 1, 7000},  // poised
    {3000, 2500, 700, 1, 5200},   // alert
    {3000, 2000, 800, 1, 4800},   // curious
    {5200, 2600, 1400, 1, 8000},  // drowsy
    {2200, 1500, 480, 2, 3800},   // jumpy
};

// Eyelid: 2 open, 1 half, 0 shut; deterministic from the seed and the clock.
inline int blinkLid(uint32_t seed, uint32_t t, const Temper& tm) {
    const int64_t k = t / tm.blink;
    const int64_t d = (int64_t)t - (k * tm.blink + (int64_t)(hash2(seed, (uint32_t)k) % tm.jitter));
    if ((d >= 0 && d < 66) || (d >= 132 && d < 198)) return 1;
    if (d >= 66 && d < 132) return 0;
    return 2;
}

// Sidelong glance: -1, 0 or 1 columns.
inline int lookDx(uint32_t seed, uint32_t t) {
    const int64_t k = t / 6000;
    const int64_t d = (int64_t)t - (k * 6000 + (int64_t)(hash2(seed, (uint32_t)(77 + k)) % 3000));
    if (d >= 0 && d < 1100) {
        static const int8_t dir[3] = {-1, 1, 1};
        return dir[hash2(seed, (uint32_t)(99 + k)) % 3];
    }
    return 0;
}

// Playback level (0..255) to mouth openness 0..3.
inline int mouthLevel(int level) { return level < 24 ? 0 : level < 80 ? 1 : level < 150 ? 2 : 3; }

// Eyelid rows: round(h * lid / 2) with Python's round-half-to-even.
inline int lidRows(int h, int lid) {
    if (lid >= 2) return h;
    if (lid <= 0) return 0;
    const int n = h / 2;
    if (h % 2 == 0) return n;
    return (n % 2 == 0) ? n : n + 1;
}

enum Expr : uint8_t { ExprNormal, ExprHappy, ExprWink, ExprClosed, ExprX };
enum MouthExpr : uint8_t { MxSmile, MxFlat, MxFrown };

// ---------------------------------------------------------------- character
class Character {
public:
    void build(const Traits& t);
    const Traits& traits() const { return t_; }
    int eyeY() const { return eyeY_; }
    int headTop() const { return top_; }
    // Read-only views for tests.
    const Canvas& feet() const { return feet_; }
    const Canvas& torso() const { return torso_; }
    const Canvas& ears() const { return ears_; }
    const Canvas& head() const { return head_; }
    const Silhouette& headMask() const { return headMask_; }

    // One frame into out (PIXELS bytes of slot indexes).
    void render(State state, const Ctx& ctx, uint8_t* out);

private:
    Traits t_;
    int rx10_, ry10_, cy10_;       // head radii and centre, tenths
    int top_, eyeY_;
    Silhouette headMask_, torsoMask_;
    int torsoRx10_;
    Canvas feet_, torso_, ears_, head_;
    Canvas tmp_;                   // arms / held item, per frame
    Canvas frame_;
    Pix pix_;                      // scratch shape

    struct EyeGeom {
        int w, h, gap;
    };

    int headTopAt(int x) const;
    int headEdgeAt(int y) const;
    int cyInt() const { return cy10_ / 10; }
    void layer(Canvas& c, bool outline);
    void asciiPix(const Art& art, int x0, int y0, bool mirror);
    void ellipsePix(int cx10, int cy10, int rx10, int ry10, uint8_t slot);
    void shadePix(const Silhouette& m, uint8_t base, uint8_t dark, uint8_t light);
    void buildFeetTorso();
    void buildEars();
    void buildHead();
    void buildHat(Canvas& c);
    void arms(int dy);
    bool held(State state, const Ctx& ctx, int mlevel);
    EyeGeom eyeGeom() const;
    void drawEye(Canvas& f, int w, int h, int x0, int y0, int lid, int pdx, int pdy, Expr expr);
    void drawMouth(Canvas& f, int x, int y, int level, MouthExpr expr, int phase);
    void worn(Canvas& f, int bob, int ox, const EyeGeom& g, int ey0, int lx0, int rx0);
    void effect(Canvas& f, int kind, int arg);
    void mark(Canvas& f, int kind, uint32_t t);
};

// -- shared helpers ----------------------------------------------------
inline int Character::headTopAt(int x) const {
    x = x < 0 ? 0 : x > W - 1 ? W - 1 : x;
    for (int y = 0; y < H; y++)
        if ((headMask_.rows[y] >> x) & 1u) return y;
    return top_;
}

// The rightmost column of the head on row y (the axis column when the row is empty).
inline int Character::headEdgeAt(int y) const {
    y = y < 0 ? 0 : y > H - 1 ? H - 1 : y;
    for (int x = W - 1; x >= W / 2; x--)
        if ((headMask_.rows[y] >> x) & 1u) return x;
    return W / 2;
}

// Outline (4-neighbourhood) around the shape first, so a later layer's
// outline cuts into earlier ones; then the shape itself. Clears the scratch.
inline void Character::layer(Canvas& c, bool outline) {
    if (outline) {
        for (int y = -Pix::M; y < H + Pix::M; y++)
            for (int x = -Pix::M; x < W + Pix::M; x++) {
                if (!pix_.get(x, y)) continue;
                if (!pix_.get(x - 1, y)) c.put(x - 1, y, Outline);
                if (!pix_.get(x + 1, y)) c.put(x + 1, y, Outline);
                if (!pix_.get(x, y - 1)) c.put(x, y - 1, Outline);
                if (!pix_.get(x, y + 1)) c.put(x, y + 1, Outline);
            }
    }
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            const uint8_t s = pix_.get(x, y);
            if (s) c.px[y * W + x] = s;
        }
    pix_.clear();
}

// ASCII art into the scratch. mirror: rows are left halves; the reversed row
// follows each row directly.
inline void Character::asciiPix(const Art& art, int x0, int y0, bool mirror) {
    for (int j = 0; j < art.n; j++) {
        const char* row = art.rows[j];
        const int n = (int)strlen(row);
        for (int i = 0; i < n; i++) {
            const uint8_t s = artSlot(row[i]);
            if (s) pix_.set(x0 + i, y0 + j, s);
            if (mirror) {
                const uint8_t m = artSlot(row[n - 1 - i]);
                if (m) pix_.set(x0 + n + i, y0 + j, m);
            }
        }
    }
}

inline void Character::ellipsePix(int cx10, int cy10, int rx10, int ry10, uint8_t slot) {
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            if (inEllipse(x, y, cx10, cy10, rx10, ry10)) pix_.set(x, y, slot);
}

// Bottom rim dark, top-centre rim highlight (both mirror-symmetric).
inline void Character::shadePix(const Silhouette& m, uint8_t base, uint8_t dark, uint8_t light) {
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            if (!m.has(x, y)) continue;
            uint8_t c = base;
            if (!m.has(x, y + 1)) c = dark;
            else if (!m.has(x, y - 1) && x >= 11 && x <= 20) c = light;
            pix_.set(x, y, c);
        }
}

// -- build -------------------------------------------------------------
inline void Character::build(const Traits& t) {
    t_ = t;
    const HeadShape& hs = HEADS[t.head];
    rx10_ = hs.rx10;
    ry10_ = hs.ry10;
    cy10_ = 230 - ry10_;
    headMask_.fromHalf(hs.half);
    top_ = 0;
    while (top_ < H && !headMask_.rows[top_]) top_++;
    eyeY_ = (cy10_ + 4 + 5) / 10;                 // round(cy + 0.4)
    torsoRx10_ = 40;
    memset(&torsoMask_, 0, sizeof torsoMask_);
    feet_.clear();
    torso_.clear();
    ears_.clear();
    head_.clear();
    pix_.clear();
    buildFeetTorso();
    buildEars();
    buildHead();
}

inline void Character::buildFeetTorso() {
    for (int x = 0; x < W; x++) {
        const int d2 = x * 2 + 1 - W;
        const int a = d2 < 0 ? -d2 : d2;          // twice |x + .5 - 16|
        if (a <= 17) feet_.put(x, 30, Shadow);
        if (a <= 12) feet_.put(x, 31, Shadow);
    }
    if (t_.feet < 2) {                            // 0 nubs, 1 wide paws
        const int fw = t_.feet == 0 ? 3 : 5;
        for (int sx = -1; sx <= 1; sx += 2) {
            const int x0 = 16 + (sx > 0 ? (t_.feet == 0 ? 2 : 1) : (t_.feet == 0 ? -5 : -6));
            for (int i = 0; i < fw; i++)
                for (int j = 0; j < 2; j++) pix_.set(x0 + i, 29 + j, BodyD);
            layer(feet_, true);
        }
    }
    if (t_.feet == 3) {                           // boots: a shaft and a sole with a toe
        for (int sx = -1; sx <= 1; sx += 2) {
            const int x0 = sx > 0 ? 17 : 11;
            for (int i = 0; i < 4; i++) {
                pix_.set(x0 + i, 28, AccL);
                pix_.set(x0 + i, 29, i == (sx > 0 ? 0 : 3) ? AccL : Acc);
                pix_.set(x0 + i, 30, AccD);
            }
            pix_.set(sx > 0 ? x0 + 4 : x0 - 1, 30, AccD);
            layer(feet_, true);
        }
    } else if (t_.feet == 4) {                    // wheels
        for (int sx = -1; sx <= 1; sx += 2) {
            ellipsePix(160 + sx * 45, 295, 22, 22, Dark);
            pix_.set(sx > 0 ? 20 : 11, 29, AccL);
            layer(feet_, true);
        }
    }
    if (t_.torso == 0) return;
    const TorsoShape& ts = TORSOS[t_.torso];
    torsoRx10_ = ts.rx10;
    torsoMask_.fromHalf(ts.half);
    shadePix(torsoMask_, Body, BodyD, BodyL);
    if (t_.pattern == Muzzle || t_.pattern == Blaze) {
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
                if (torsoMask_.has(x, y) && pix_.get(x, y) != BodyD &&
                    inEllipse(x, y, 160, ts.cy10 + 8, 30, 24))
                    pix_.set(x, y, Belly);
    }
    if (t_.pattern == Stripes) {
        for (int y = 24; y <= 26; y += 2)
            for (int x = 11; x <= 20; x++)
                if (x % 3 == 0 && torsoMask_.has(x, y) && pix_.get(x, y) != BodyD)
                    pix_.set(x, y, BodyD);
    }
    if (t_.pattern == Belt) {
        for (int x = 0; x < W; x++)
            if (torsoMask_.has(x, 26)) pix_.set(x, 26, Acc);
        pix_.set(15, 26, AccL);
        pix_.set(16, 26, AccL);
    }
    layer(torso_, true);
}

// Arm nubs between feet and torso. t.arms: 0 none, 1 nubs, 2 big hands.
inline void Character::arms(int dy) {
    tmp_.clear();
    if (t_.arms == 0 || torsoMask_.empty()) return;
    const int rx = t_.arms == 1 ? 18 : t_.arms == 2 ? 24 : t_.arms == 3 ? 16 : 28;
    const int ry = t_.arms == 1 ? 22 : t_.arms == 2 ? 26 : t_.arms == 3 ? 40 : 26;
    for (int sx = -1; sx <= 1; sx += 2)
        ellipsePix(160 + sx * (torsoRx10_ + 1), 250 + 10 * dy, rx, ry, t_.arms == 4 ? Acc : BodyD);
    layer(tmp_, true);
}

inline void Character::buildEars() {
    const int rx10 = rx10_;
    const int ex = (rx10 * 58 + 500) / 1000;      // round(rx * 0.58)
    int xr = 16 + ex;
    int ty = headTopAt(xr);
    switch (t_.ears) {
    case EarsNone:
        return;
    case Cat: {
        const int base = ty + 1;
        for (int r = 0; r < 6; r++) {
            const int w = r + 1;
            for (int i = 0; i < w; i++) pix_.set(xr - w / 2 + i, base - 5 + r, Body);
        }
        for (int r = 2; r < 6; r++) {
            const int w = r - 1;
            for (int i = 0; i < w; i++) pix_.set(xr - w / 2 + i, base - 5 + r, Cheek);
        }
        break;
    }
    case Bear:
        xr = 16 + (rx10 * 70 + 500) / 1000;
        ty = headTopAt(xr);
        ellipsePix(10 * xr + 5, 10 * ty + 18, 29, 29, Body);
        ellipsePix(10 * xr + 5, 10 * ty + 22, 14, 14, Cheek);
        break;
    case Bunny:
        ty = headTopAt(19);
        ellipsePix(196, 10 * ty - 25, 22, 56, Body);
        ellipsePix(196, 10 * ty - 22, 9, 38, Cheek);
        break;
    case Fox: {
        const int base = ty + 2;
        for (int r = 0; r < 7; r++) {
            const int w = r + 1 < 5 ? r + 1 : 5;
            const int off = (6 - r) / 3;
            for (int i = 0; i < w; i++)
                pix_.set(xr - w / 2 + i + off, base - 6 + r, r < 2 ? BodyD : Body);
        }
        break;
    }
    case Horns:
        xr = 16 + (rx10 * 62 + 500) / 1000;
        ty = headTopAt(xr);
        for (int r = 0; r < 5; r++) {
            const int x = xr + r / 2;
            pix_.set(x, ty + 1 - r, AccL);
            pix_.set(x - 1, ty + 1 - r, Acc);
        }
        break;
    case Antenna:
        for (int j = 0; j < 5; j++)
            for (int x = 15; x <= 16; x++) pix_.set(x, headTopAt(x) + 1 - j, AccD);
        ellipsePix(160, 10 * top_ - 44, 24, 24, AccL);
        layer(ears_, true);
        return;
    case Fins: {
        static const uint8_t wid[7] = {1, 2, 3, 4, 3, 2, 1};
        const int ix = rx10 / 10;
        for (int r = 0; r < 7; r++)
            for (int i = 0; i < wid[r]; i++)
                pix_.set(16 + ix - 1 + i, eyeY_ - 3 + r, i < wid[r] - 1 ? Acc : AccD);
        break;
    }
    case Droopy:
        ellipsePix(160 + rx10 + 4, cy10_ + 10, 23, 50, BodyD);
        break;
    case Tufts:
        xr = 16 + (rx10 * 60 + 500) / 1000;
        ty = headTopAt(xr);
        for (int j = 0; j < 4; j++) {
            pix_.set(xr + j / 2, ty + 1 - j, BodyD);
            pix_.set(xr + j / 2 - 1, ty + 1 - j, Body);
        }
        break;
    case Elf: {
        const int xe = headEdgeAt(eyeY_);
        static const int8_t lo[6] = {3, 2, 1, -1, -1, -1}, hi[6] = {3, 3, 3, 3, 2, 1};
        for (int k = 0; k < 6; k++)
            for (int x = xe + lo[k]; x <= xe + hi[k]; x++) pix_.set(x, eyeY_ - 3 + k, Body);
        pix_.set(xe + 1, eyeY_, Cheek);
        pix_.set(xe + 2, eyeY_ - 1, Cheek);
        break;
    }
    case Discs: {
        const int xe = headEdgeAt(eyeY_);
        ellipsePix(10 * (xe + 2), 10 * eyeY_ + 5, 25, 25, Acc);
        pix_.set(xe + 1, eyeY_ - 1, AccL);
        pix_.set(xe + 1, eyeY_, AccL);
        pix_.set(xe + 2, eyeY_ + 1, AccD);
        pix_.set(xe + 3, eyeY_ + 1, AccD);
        break;
    }
    case Bobbles:
        xr = 16 + (rx10 * 40 + 500) / 1000;
        ty = headTopAt(xr);
        for (int j = -2; j <= 1; j++) pix_.set(xr, ty - j, AccD);
        for (int j = 3; j <= 5; j++) pix_.set(xr + 1, ty - j, AccD);
        ellipsePix(10 * (xr + 1) + 5, 10 * (ty - 7) + 5, 22, 22, AccL);
        pix_.set(xr, ty - 8, White);
        break;
    case Spikes:
        for (int k = 0; k < 3; k++) {
            const int x = 16 + 3 * k, h = 8 - k;
            const int by = headTopAt(x) + 1;
            for (int j = 0; j < h; j++) {
                pix_.set(x, by - j, j == h - 1 ? AccL : Acc);
                if (j < h / 2 + 1) pix_.set(x + 1, by - j, Acc);
            }
        }
        break;
    case Sprout:
        ty = headTopAt(16);
        for (int j = -1; j <= 3; j++) pix_.set(16, ty - j, OkC);
        pix_.set(16, ty - 4, AccL);
        pix_.set(16, ty - 5, AccL);
        for (int k = 0; k < 4; k++) {             // a leaf climbing outward
            pix_.set(17 + k, ty - 3 - k / 2, OkC);
            pix_.set(17 + k, ty - 4 - k / 2, OkC);
        }
        break;
    case Mouse:
        xr = 16 + (rx10 * 70 + 500) / 1000;
        ty = headTopAt(xr);
        ellipsePix(10 * xr + 5, 10 * ty + 8, 38, 38, Body);
        ellipsePix(10 * xr + 5, 10 * ty + 12, 22, 22, Cheek);
        break;
    case Antlers:
        xr = 16 + (rx10 * 40 + 500) / 1000;
        ty = headTopAt(xr);
        asciiPix(E_ANTLER, xr - 2, ty - 5, false);
        break;
    case Buns:
        xr = 16 + (rx10 * 62 + 500) / 1000;
        ty = headTopAt(xr);
        ellipsePix(10 * xr + 5, 10 * ty - 5, 32, 32, AccD);
        pix_.set(xr - 1, ty - 3, AccL);
        pix_.set(xr, ty - 3, AccL);
        pix_.set(xr - 1, ty - 2, AccL);
        break;
    case Unicorn:
        ty = top_;
        for (int j = 0; j < 8; j++) {             // a banded horn on a wider base
            const int y = ty + 1 - j;
            if (j == 0) {
                for (int x = 14; x <= 17; x++) pix_.set(x, y, AccD);
            } else {
                for (int x = 15; x <= 16; x++) pix_.set(x, y, j == 7 || (j & 1) ? AccL : Acc);
            }
        }
        layer(ears_, true);
        return;
    }
    pix_.mirrorRight();
    layer(ears_, true);
}

inline void Character::buildHead() {
    const int ey = eyeY_;
    shadePix(headMask_, Body, BodyD, BodyL);
    // Patterns that mirror a right-hand shape: set the cells on both sides.
    auto setSym = [this](int x, int y, uint8_t col) {
        if (headMask_.has(x, y)) pix_.set(x, y, col);
        if (headMask_.has(W - 1 - x, y)) pix_.set(W - 1 - x, y, col);
    };
    switch (t_.pattern) {
    case Stripes: {
        static const uint8_t off[3] = {0, 3, 6}, len[3] = {3, 3, 2};
        for (int k = 0; k < 3; k++)
            for (int j = 0; j < len[k]; j++) setSym(16 + off[k], top_ + 1 + j, BodyD);
        break;
    }
    case Spots: {
        Rng r(t_.seed ^ 0xA5A5u);
        for (int n = 0; n < 2; n++) {
            const int ox = 16 + 4 + (int)r.pick(4);
            const int oy = ey - 3 + (int)r.pick(2) * 5;
            for (int dy = 0; dy < 2; dy++)
                for (int dx = 0; dx < 2; dx++) setSym(ox + dx, oy + dy, BodyD);
        }
        break;
    }
    case Mask:
        for (int y = ey - 2; y <= ey + 1; y++)
            for (int x = 0; x < W; x++)
                if (headMask_.has(x, y) && (x <= 14 || x >= 17)) pix_.set(x, y, BodyD);
        break;
    case Blaze:
        for (int y = 0; y < ey - 2; y++)
            for (int x = 15; x <= 16; x++)
                if (headMask_.has(x, y)) pix_.set(x, y, Belly);
        break;
    case Twotone:
        for (int y = 0; y < ey - 2; y++)
            for (int x = 0; x < W; x++)
                if (headMask_.has(x, y)) pix_.set(x, y, pix_.get(x, y) != BodyL ? Acc : AccL);
        break;
    case Muzzle:
        for (int y = ey + 2; y < H; y++)
            for (int x = 0; x < W; x++)
                if (headMask_.has(x, y) && inEllipse(x, y, 160, 10 * ey + 42, 46, 30) &&
                    pix_.get(x, y) != BodyD)
                    pix_.set(x, y, Belly);
        break;
    case Split:
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W / 2; x++) {
                const uint8_t s = pix_.get(x, y);
                if (s == Body) pix_.set(x, y, Acc);
                else if (s == BodyD) pix_.set(x, y, AccD);
                else if (s == BodyL) pix_.set(x, y, AccL);
            }
        break;
    case Frost:
        for (int y = top_ + 1; y < ey - 3; y++)
            for (int x = 0; x < W; x++)
                if (headMask_.has(x, y) && ((x < W / 2 ? x : W - 1 - x) + y) % 2 && pix_.get(x, y) == Body)
                    pix_.set(x, y, BodyL);
        break;
    default:
        break;
    }
    layer(head_, true);
    const int sx0 = 16 - (rx10_ * 55) / 1000;     // upper-left shine
    static const int8_t shine[3][2] = {{0, 0}, {1, 0}, {0, 1}};
    for (int k = 0; k < 3; k++) {
        const int x = sx0 + shine[k][0], y = top_ + 2 + shine[k][1];
        if (headMask_.has(x, y)) head_.put(x, y, BodyL);
    }
    if (t_.pattern == Blush) {
        for (int i = 0; i < 2; i++) {
            const int x = 16 + (rx10_ * 62) / 1000 + i;
            head_.put(x, ey + 3, Cheek);
            head_.put(W - 1 - x, ey + 3, Cheek);
        }
    }
    if (t_.pattern == Freckles) {
        for (int k = 0; k < 3; k++) {
            const int x = 16 + 4 + 2 * k, y = ey + 3 + (k & 1);
            if (headMask_.has(x, y)) head_.put(x, y, BodyD);
            if (headMask_.has(W - 1 - x, y)) head_.put(W - 1 - x, y, BodyD);
        }
    }
    if (t_.pattern == Brows) {
        const EyeGeom g = eyeGeom();
        const int by = ey - g.h / 2 - 2;
        const int n = t_.eyes == Cyclops ? 1 : 2;
        for (int side = 0; side < n; side++)
            for (int i = 0; i < g.w; i++) {
                const int x = n == 1 ? 16 - g.w / 2 + i
                                     : (side ? 16 + g.gap / 2 + i : 16 - g.gap / 2 - 1 - i);
                const int y = n == 1 ? by : by + (i < g.w / 2 ? 1 : 0);
                if (headMask_.has(x, y)) head_.put(x, y, Dark);
            }
    }
    if (t_.pattern == Star) {
        const int r = ey - 6 > top_ + 2 ? ey - 6 : top_ + 2;
        for (int x = 14; x <= 17; x++)
            if (headMask_.has(x, r)) head_.put(x, r, x == 15 || x == 16 ? White : AccL);
        for (int x = 15; x <= 16; x++) {
            if (headMask_.has(x, r - 1)) head_.put(x, r - 1, AccL);
            if (headMask_.has(x, r + 1)) head_.put(x, r + 1, AccL);
        }
    }
    buildHat(head_);
}

inline void Character::buildHat(Canvas& c) {
    const int hat = t_.hat;
    if (hat == HatNone) return;
    const HatArt& ha = HAT_MIRRORED[hat];
    if (ha.art.n) {
        const int hw = (int)strlen(ha.art.rows[0]);
        asciiPix(ha.art, 16 - hw, top_ + ha.sink - ha.art.n, true);
        layer(c, ha.outline);
    } else if (hat == Roof) {
        asciiPix(H_ROOF, 8, top_ + 1 - H_ROOF.n, false);
        layer(c, true);
    } else if (hat == Cap) {
        asciiPix(H_CAP, 8, top_ + 2 - H_CAP.n, false);
        layer(c, true);
    } else if (hat == Bow) {
        const int x0 = 16 - (rx10_ * 62) / 1000 - 5;
        const int y0 = headTopAt(x0 + 3) - 3;
        asciiPix(H_BOW, x0, y0, false);
        layer(c, true);
    } else if (hat == Headphones) {
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                if (headMask_.has(x, y) || y >= eyeY_ - 1) continue;
                bool near = false;
                for (int dy = -2; dy <= 2 && !near; dy++)
                    for (int dx = -2; dx <= 2; dx++)
                        if (headMask_.has(x + dx, y + dy)) {
                            near = true;
                            break;
                        }
                if (near) pix_.set(x, y, Acc);
            }
        const int ix = rx10_ / 10;
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 6; j++) {
                const int x = 16 + ix - 1 + i, y = eyeY_ - 2 + j;
                const uint8_t s = i == 2 ? AccD : Acc;
                pix_.set(x, y, s);
                pix_.set(W - 1 - x, y, s);
            }
        layer(c, true);
    } else if (hat == Headband) {
        const int by = eyeY_ - 6 > top_ + 2 ? eyeY_ - 6 : top_ + 2;
        for (int y = by; y <= by + 1; y++)
            for (int x = 0; x < W; x++)
                if (headMask_.has(x, y)) pix_.set(x, y, y == by ? AccL : Acc);
        const int xe = headEdgeAt(by);                // the knot and its tails, on the right
        for (int dx = 1; dx <= 2; dx++)
            for (int dy = 0; dy <= 1; dy++) pix_.set(xe + dx, by + dy, AccD);
        pix_.set(xe + 3, by + 1, Acc);
        pix_.set(xe + 3, by + 2, Acc);
        pix_.set(xe + 2, by + 2, Acc);
        layer(c, true);
    } else if (hat == Wreath) {
        static const uint8_t bloom[3] = {BusyC, White, ListenC};
        for (int x = 8; x <= 23; x++) {
            const int yt = headTopAt(x);
            if (!headMask_.has(x, yt)) continue;
            pix_.set(x, yt, x % 2 ? OkC : AccD);
            if (x % 3 == 0) pix_.set(x, yt - 1, bloom[(x / 3) % 3]);
        }
        layer(c, true);
    }
}

// -- face --------------------------------------------------------------
inline Character::EyeGeom Character::eyeGeom() const {
    static const uint8_t dim[EyesCount][2] = {{2, 3}, {5, 5}, {4, 6}, {6, 4}, {5, 6}, {5, 3}, {3, 3},
                                              {5, 4}, {8, 6}, {5, 5}, {5, 4}, {5, 4}, {5, 6}, {4, 4}};
    EyeGeom g;
    g.w = dim[t_.eyes][0];
    g.h = dim[t_.eyes][1];
    int gap = (rx10_ * 90 + 500) / 1000 - g.w;    // round(rx * 0.9) - w
    if (gap < 2) gap = 2;
    gap -= gap % 2;
    g.gap = gap;
    return g;
}

// x0,y0: top-left of the fully open eye. lid 2 open, 1 half, 0 shut.
// pdx/pdy: look offset (-1..1).
inline void Character::drawEye(Canvas& f, int w, int h, int x0, int y0, int lid, int pdx, int pdy,
                               Expr expr) {
    const int kind = t_.eyes;
    if (expr == ExprX) {
        const int m = w < h ? w : h;
        const int n = kind != Beady ? (m < 4 ? m : 4) : 3;
        for (int k = 0; k < n; k++) {
            f.put(x0 + k, y0 + k, ErrC);
            f.put(x0 + n - 1 - k, y0 + k, ErrC);
        }
        return;
    }
    if (kind == Joy && expr == ExprNormal) {      // always smiling: arcs that look around, shut when blinking
        expr = lid >= 2 ? ExprHappy : ExprClosed;
        x0 += pdx;
        y0 += pdy;
    }
    if (expr == ExprHappy) {
        const int yy = y0 + h / 2;
        for (int i = 0; i < w; i++) {
            const bool mid = w / 2 - 1 <= i && i <= w - w / 2;
            f.put(x0 + i, yy + (mid ? 0 : 1), Pupil);
        }
        return;
    }
    int rows = lidRows(h, lid);
    if (expr == ExprClosed || rows <= 0) {
        for (int i = 0; i < w; i++) f.put(x0 + i, y0 + h - 1, Pupil);
        return;
    }
    static const char* const HEART[4] = {"xx.xx", "xxxxx", ".xxx.", "..x.."};
    const bool fullShape = kind == Dot || kind == Square;
    if (kind == Sleepy && rows > h - 1) rows = h - 1;   // the upper lid never lifts
    const int top = h - rows;
    const bool solid = kind == Beady || kind == Dot || kind == Shiny || kind == Led ||
                       kind == Heart || kind == Gleam;
    if (solid) {                                  // no pupil: the whole eye looks around
        x0 += pdx;
        y0 += pdy;
    }
    const uint8_t col = kind == Beady || kind == Dot || kind == Shiny || kind == Gleam ? Pupil
                        : kind == Led || kind == Slit                                  ? AccL
                        : kind == Heart                                                ? ErrC
                                                                                       : White;
    auto inShape = [&](int i, int j) {
        if (kind == Heart) return HEART[j][i] == 'x';
        return fullShape || !((i == 0 || i == w - 1) && (j == 0 || j == h - 1)) || w <= 2;
    };
    for (int j = top; j < h; j++)
        for (int i = 0; i < w; i++)
            if (inShape(i, j)) f.put(x0 + i, y0 + j, col);
    if (kind == Sleepy)                           // the lid
        for (int i = 0; i < w; i++) f.put(x0 + i, y0 + top - 1, BodyD);
    if (kind == Shiny && rows >= h - 1) {
        f.put(x0 + w - 3, y0 + 1, White);
        f.put(x0 + w - 2, y0 + 1, White);
        f.put(x0 + w - 3, y0 + 2, White);
    } else if (kind == Gleam) {
        if (rows >= h - 1) {
            for (int i = 1; i <= 3; i++) f.put(x0 + i, y0 + 4, AccL);
            f.put(x0 + 1, y0 + 1, White);
            f.put(x0 + 2, y0 + 1, White);
            f.put(x0 + 1, y0 + 2, White);
            f.put(x0 + 2, y0 + 2, White);
            f.put(x0 + 3, y0 + 4, White);
        }
    } else if (!solid) {
        const int pw = kind == Round ? 3 : kind == Tall ? 2 : kind == Sleepy ? 3 : kind == Cyclops ? 4
                       : kind == Square ? 2 : kind == Slit ? 1 : 3;
        const int ph = kind == Round ? 3 : kind == Tall ? 3 : kind == Sleepy ? 2 : kind == Cyclops ? 4
                       : kind == Square ? 2 : kind == Slit ? h : 2;
        int px = (w - pw) / 2 + pdx;
        px = px < 0 ? 0 : px > w - pw ? w - pw : px;
        int py = (h - ph + 1) / 2 + pdy;
        py = py < 0 ? 0 : py > h - ph ? h - ph : py;
        for (int j = 0; j < ph; j++)
            for (int i = 0; i < pw; i++)
                if (py + j >= top && inShape(px + i, py + j)) f.put(x0 + px + i, y0 + py + j, Pupil);
        if (rows >= h - 1 && kind != Slit) f.put(x0 + px, y0 + py, White);   // glint
    }
}

// Closed mouth shapes as ASCII ('x' mouth, 'w' white, 'd' dark), centred on
// an even width.
struct MouthRows {
    const char* r[3];
    uint8_t n;
};
inline MouthRows mouthRows(int kind, MouthExpr e) {
    static const MouthRows smile[3] = {{{"x....x", ".xxxx.", ""}, 2}, {{"......", "xxxxxx", ""}, 2},
                                       {{".xxxx.", "x....x", ""}, 2}};
    static const MouthRows tooth[3] = {{{"x....x", ".xxxx.", "..ww.."}, 3},
                                       {{"......", "xxxxxx", ""}, 2},
                                       {{".xxxx.", "x....x", ""}, 2}};
    static const MouthRows cat[3] = {{{"x.xx.x", ".x..x.", ""}, 2}, {{"x.xx.x", ".x..x.", ""}, 2},
                                     {{".x..x.", "x.xx.x", ""}, 2}};
    static const MouthRows grill[3] = {{{"x....x", ".xxxx.", ""}, 2}, {{"xdxdxd", "dxdxdx", ""}, 2},
                                       {{".xxxx.", "x....x", ""}, 2}};
    static const MouthRows fang[3] = {{{"x....x", ".xxxx.", ".w..w."}, 3},
                                      {{"......", "xxxxxx", ".w..w."}, 3},
                                      {{".xxxx.", "xw..wx", ""}, 2}};
    static const MouthRows tongue[3] = {{{"x....x", ".xxxx.", "..tt.."}, 3},
                                        {{"......", "xxxxxx", "..tt.."}, 3},
                                        {{".xxxx.", "x....x", ""}, 2}};
    static const MouthRows zigzag[3] = {{{"x.x.x.", ".x.x.x", ""}, 2}, {{"x.x.x.", ".x.x.x", ""}, 2},
                                        {{".x.x.x", "x.x.x.", ""}, 2}};
    static const MouthRows pout[3] = {{{"..xx..", "", ""}, 1}, {{"..xx..", "", ""}, 1},
                                      {{"..xx..", "..xx..", ""}, 2}};
    static const MouthRows grin[3] = {{{"x......x", ".xxxxxx.", ""}, 2},
                                      {{"........", "xxxxxxxx", ""}, 2},
                                      {{".xxxxxx.", "x......x", ""}, 2}};
    static const MouthRows smirk[3] = {{{".....x", ".xxxx.", ""}, 2}, {{"......", ".xxxxx", ""}, 2},
                                       {{".xxxx.", "x.....", ""}, 2}};
    switch (kind) {
    case Tooth: return tooth[e];
    case CatMouth: return cat[e];
    case Grill: return grill[e];
    case Fang: return fang[e];
    case TongueOut: return tongue[e];
    case Zigzag: return zigzag[e];
    case Pout: return pout[e];
    case Grin: return grin[e];
    case Smirk: return smirk[e];
    default: return smile[e];                      // smile, flat
    }
}

// The mouth centred on x (columns -3..2 relative), top row at y.
inline void Character::drawMouth(Canvas& f, int x, int y, int level, MouthExpr expr, int phase) {
    const int kind = t_.mouth;
    uint8_t g[6][8];                               // [dy][dx + 4]; 0 = nothing
    memset(g, 0, sizeof g);
    auto set = [&](int dx, int dy, uint8_t s) { g[dy][dx + 4] = s; };
    if (kind == OMouth) {
        if (level == 0) {
            static const char* const rows[3][2] = {{"x..x", ".xx."}, {"....", ".xx."}, {".xx.", "x..x"}};
            for (int j = 0; j < 2; j++)
                for (int i = 0; i < 4; i++)
                    if (rows[expr][j][i] == 'x') set(i - 2, j, MouthC);
        } else {
            static const uint8_t sz[4][2] = {{2, 1}, {2, 2}, {4, 3}, {4, 4}};
            const int sw = sz[level][0], sh = sz[level][1];
            for (int j = 0; j < sh; j++)
                for (int i = 0; i < sw; i++)
                    if (!(sw > 2 && (i == 0 || i == sw - 1) && (j == 0 || j == sh - 1)))
                        set(i - sw / 2, j, MouthC);
        }
    } else if (kind == Beak) {
        for (int i = -2; i < 2; i++) set(i, 0, Acc);
        if (level == 0) {
            set(-1, 1, AccD);
            set(0, 1, AccD);
        } else {
            for (int j = 0; j < level; j++) {
                set(-1, 1 + j, MouthC);
                set(0, 1 + j, MouthC);
            }
            set(-1, 1 + level, AccD);
            set(0, 1 + level, AccD);
        }
    } else if (kind == Grill && level > 0) {
        static const uint8_t hs[3][4] = {{1, 2, 1, 2}, {2, 3, 2, 3}, {3, 4, 3, 4}};
        for (int k = 0, i = -4; i < 4; k++, i++) {
            if (k % 2) continue;
            const int hgt = hs[level - 1][((k / 2) + phase) % 4];
            for (int j = 0; j < hgt; j++) set(i, 3 - j, j < hgt - 1 ? MouthC : AccL);
        }
    } else if (level == 0) {
        const MouthRows m = mouthRows(kind, expr);
        for (int j = 0; j < m.n; j++) {
            const int n = (int)strlen(m.r[j]);
            for (int i = 0; i < n; i++) {
                const char ch = m.r[j][i];
                uint8_t s = 0;
                if (ch == 'x') s = MouthC;
                else if (ch == 'w') s = White;
                else if (ch == 'd') s = Dark;
                else if (ch == 't') s = Tongue;
                if (s) set(i - n / 2, j, s);
            }
        }
        if (kind == Grill && expr == MxFlat) {     // the flat grill reads inverted
            for (int j = 0; j < 6; j++)
                for (int i = 0; i < 8; i++)
                    if (g[j][i] == MouthC) g[j][i] = Dark;
                    else if (g[j][i] == Dark) g[j][i] = MouthC;
        }
    } else {
        static const uint8_t SW[4] = {2, 4, 4, 6}, SH[4] = {1, 2, 3, 3};
        const int sw = SW[level] + (kind == Grin ? 2 : 0), sh = SH[level];
        for (int j = 0; j < sh; j++)
            for (int i = 0; i < sw; i++) {
                if (sw > 2 && sh > 1 && j == sh - 1 && (i == 0 || i == sw - 1)) continue;
                set(i - sw / 2, j, MouthC);
            }
        if (level == 3 || kind == TongueOut) {
            set(-1, sh - 1, Tongue);
            set(0, sh - 1, Tongue);
        }
        if (kind == Tooth) {
            set(-1, 0, White);
            set(0, 0, White);
        }
        if (kind == CatMouth) {
            set(-3, 0, MouthC);
            set(2, 0, MouthC);
        }
        if (kind == Fang) {
            set(-2, 0, White);
            set(1, 0, White);
        }
        if (kind == Grin)
            for (int i = 1; i < sw - 1; i++) set(i - sw / 2, 0, White);
    }
    for (int j = 0; j < 6; j++)
        for (int i = 0; i < 8; i++)
            if (g[j][i]) f.put(x + i - 4, y + j, g[j][i]);
}

// Worn accessories, over the face.
inline void Character::worn(Canvas& f, int bob, int ox, const EyeGeom& g, int ey0, int lx0,
                            int rx0) {
    switch (t_.acc) {
    case Bowtie:
        asciiPix(A_BOWTIE, 16 - (int)strlen(A_BOWTIE.rows[0]) + ox, 22 + bob, true);
        layer(f, true);
        break;
    case Scarf:
        asciiPix(A_SCARF, 9 + ox, 22 + bob, false);
        layer(f, true);
        break;
    case Glasses: {
        const int ew = g.w, eh = g.h;
        const int xs[2] = {lx0, rx0};
        const int lenses = t_.eyes == Cyclops ? 1 : 2;
        for (int k = 0; k < lenses; k++)
            for (int i = -1; i <= ew; i++)
                for (int j = -1; j <= eh; j++) {
                    const bool edgeX = i == -1 || i == ew, edgeY = j == -1 || j == eh;
                    if ((edgeX || edgeY) && !(edgeX && edgeY)) pix_.set(xs[k] + i, ey0 + j, Outline);
                }
        if (lenses == 2)
            for (int i = ew + 1; i < ew + g.gap; i++) pix_.set(lx0 + i, ey0 + eh / 2, Outline);
        layer(f, false);
        break;
    }
    case Mustache:
        asciiPix(A_MUSTACHE, 16 - (int)strlen(A_MUSTACHE.rows[0]) + ox, eyeY_ + 3 + bob, true);
        layer(f, false);
        break;
    case Monocle: {
        const int ew = g.w, eh = g.h;
        for (int i = -1; i <= ew; i++)
            for (int j = -1; j <= eh; j++) {
                const bool edgeX = i == -1 || i == ew, edgeY = j == -1 || j == eh;
                if ((edgeX || edgeY) && !(edgeX && edgeY)) pix_.set(rx0 + i, ey0 + j, AccL);
            }
        pix_.set(rx0 + ew, ey0 + eh, AccD);
        pix_.set(rx0 + ew, ey0 + eh + 1, AccD);
        pix_.set(rx0 + ew + 1, ey0 + eh + 2, AccD);
        pix_.set(rx0 + ew + 1, ey0 + eh + 3, AccD);
        layer(f, false);
        break;
    }
    case Eyepatch: {
        const int ew = g.w, eh = g.h;
        for (int x = rx0 - 1; x <= rx0 + ew; x++)
            for (int y = ey0 - 1; y <= ey0 + eh; y++)
                if (!((x == rx0 - 1 || x == rx0 + ew) && (y == ey0 - 1 || y == ey0 + eh)))
                    pix_.set(x, y, Dark);
        for (int k = 1; k <= 12; k++) {           // the strap climbs over the brow to both sides
            const int ya = ey0 - 1 - k / 3;
            if (headMask_.has(rx0 + ew + k - 1 - ox, ya - bob)) pix_.set(rx0 + ew + k - 1, ya, Dark);
            if (headMask_.has(rx0 - k - ox, ya - bob)) pix_.set(rx0 - k, ya, Dark);
        }
        layer(f, false);
        break;
    }
    case Necktie:
        asciiPix(A_NECKTIE, 13 + ox, 23 + bob, false);
        layer(f, true);
        break;
    case Headset: {
        const int xl = W - 1 - headEdgeAt(eyeY_) + ox, yc = eyeY_ + bob;
        for (int i = -2; i <= 0; i++)
            for (int j = -2; j <= 2; j++) pix_.set(xl + i, yc + j, i == 0 ? AccD : Dark);
        pix_.set(xl - 1, yc, AccL);
        pix_.set(xl, yc + 3, Dark);
        pix_.set(xl + 1, yc + 4, Dark);
        pix_.set(xl + 2, yc + 5, Dark);
        pix_.set(xl + 3, yc + 5, AccL);
        pix_.set(xl + 3, yc + 6, AccL);
        pix_.set(xl + 4, yc + 5, AccL);
        pix_.set(xl + 4, yc + 6, AccL);
        layer(f, true);
        break;
    }
    case Plaster:
        asciiPix(A_PLASTER, t_.eyes == Cyclops ? rx0 + 8 : rx0 + g.w - 2, ey0 + g.h, false);
        layer(f, true);
        break;
    default:
        break;
    }
}

// In-hand accessories into tmp_: the staff on the viewer's left, the rest on
// the right. False when there is none (or no torso to hold it).
inline bool Character::held(State state, const Ctx& ctx, int mlevel) {
    const int acc = t_.acc;
    if (acc != Scissors && acc != Staff && acc != Whisk && acc != Mug && acc != Flower &&
        acc != Book && acc != Magnifier && acc != Lollipop && acc != Balloon)
        return false;
    if (torsoMask_.empty()) return false;
    tmp_.clear();
    const int handX = (160 + torsoRx10_) / 10;
    if (acc == Balloon) {                         // sways on its string, which ends in the hand
        const int sway = (int)((ctx.t / 900) % 2);
        const int cx = handX + 3 > 27 ? 27 : handX + 3, cy = 7;
        ellipsePix(10 * (cx + sway) + 5, 10 * cy + 5, 33, 38, Acc);
        pix_.set(cx + sway - 1, cy - 2, AccL);
        pix_.set(cx + sway - 2, cy - 1, AccL);
        pix_.set(cx + sway - 1, cy - 1, AccL);
        pix_.set(cx + sway, cy + 4, AccD);
        pix_.set(cx + sway + 1, cy + 4, AccD);
        for (int k = 0; k <= 16; k++)
            pix_.set(cx + sway + (handX - cx - sway) * k / 16, cy + 5 + (28 - cy - 5) * k / 16, WhiteD);
        layer(tmp_, true);
        return true;
    }
    const bool open = (state == Speaking && mlevel >= 2) || (state == Idle && (ctx.t / 700) % 5 == 0);
    const Art* art = acc == Scissors   ? (open ? &A_SCISSORS_OPEN : &A_SCISSORS_SHUT)
                     : acc == Staff    ? &A_STAFF
                     : acc == Whisk    ? &A_WHISK
                     : acc == Mug      ? &A_MUG
                     : acc == Book     ? (open ? &A_BOOK_TURN : &A_BOOK_FLAT)
                     : acc == Magnifier ? &A_MAGNIFIER
                     : acc == Lollipop ? &A_LOLLIPOP
                                       : &A_FLOWER;
    int w = 0;
    for (int j = 0; j < art->n; j++) {
        const int n = (int)strlen(art->rows[j]);
        if (n > w) w = n;
    }
    if (acc == Staff) {
        int x0 = (160 - torsoRx10_) / 10 - w;
        if (x0 < 0) x0 = 0;
        asciiPix(*art, x0, 30 - art->n, false);
    } else {
        int x0 = (160 + torsoRx10_) / 10 - 1;
        if (x0 > W - w) x0 = W - w;
        const int lift = acc == Magnifier && state == Thinking ? 3 : 0;   // held up to look closer
        asciiPix(*art, x0, 29 - art->n - lift, false);
    }
    layer(tmp_, true);
    return true;
}

// -- effects -----------------------------------------------------------
enum Fx : int8_t { FxNone = -1, FxHear, FxSound, FxSweat, FxZzz };
// Marks of a reaction, top right: a "?", an "!" and two twinkling sparkles.
enum Mark : int8_t { MarkNone = -1, MarkQuestion, MarkExclaim, MarkSparkle };

// The listening ripples and the speaking sparks paint only the background and
// the floor shadow, after the glow pass: they pass behind the character and
// never cut its outline or glow. Both are pure functions of the state's time,
// so a frame depends only on its context.
inline bool onBackground(uint8_t s) { return s == Clear || s == Shadow; }

// base (ListenC or OkC) at strength a: 1..3 = 25 / 50 / 75 % (its fade slots), 4 = full.
inline uint8_t fadeOf(uint8_t base, int a) {
    if (a >= 4) return base;
    return (uint8_t)((base == ListenC ? ListenF1 : OkF1) + (a < 1 ? 0 : a - 1));
}

const uint32_t RIPPLE_STEP_MS = 300;                 // one ring lights per step
const int RIPPLE_RINGS = 3;
const int RIPPLE_REST_STEPS = 2;                     // blank steps once the last ring is gone
const int RIPPLE_CYCLE = RIPPLE_RINGS + 2 + RIPPLE_REST_STEPS;   // 7 steps, 2.1 s
static const int RIPPLE_R2[RIPPLE_RINGS] = {30, 25, 20};   // radii in half px: outer, middle, inner

// The strength (fadeOf, 0 = off) of ring i (0 = outer) at time t: the rings
// light one after another from the outside in, each at 75 % when it lights,
// then 50 % and 25 % as the next ones light, so the newest is always the
// strongest; once the inner ring has faded too, nothing shows for
// RIPPLE_REST_STEPS before the next cycle.
inline int rippleStrength(int i, uint32_t t) {
    const int age = (int)((t / RIPPLE_STEP_MS) % RIPPLE_CYCLE) - i;
    return age < 0 ? 0 : 3 - age < 0 ? 0 : 3 - age;
}

// Listening: rings around the head at r = 15, 12.5 and 10 px around (15.5, cy),
// drawn a little flattened (y * 1.15), soft (never full color).
inline void ripples(Canvas& f, uint32_t t, int cy) {
    for (int i = 0; i < RIPPLE_RINGS; i++) {
        const int a = rippleStrength(i, t);
        if (!a) continue;
        const int r2 = RIPPLE_R2[i];
        const int lo = (r2 - 1) * (r2 - 1), hi = (r2 + 1) * (r2 + 1);
        for (int y = 0; y < H; y++) {
            const int dy = (2 * (y - cy) * 147) / 128;
            for (int x = 0; x < W; x++) {
                const int dx = 2 * x - 31, d2 = dx * dx + dy * dy;
                if (d2 >= lo && d2 < hi && onBackground(f.px[y * W + x]))
                    f.px[y * W + x] = fadeOf(ListenC, a);
            }
        }
    }
}

const uint32_t SPARK_EVERY_MS = 170, SPARK_LIFE_MS = 1700;

// Spark k (one starts every 170 ms) at time t: its pixels (up to 8, possibly
// off the canvas) and its strength for fadeOf (1..4), or 0 when it is not
// alive. Side, column and kind (a little note or a spark) come from the
// character's seed; it rises about 7 px a second with a small sway and fades
// out over 1.7 s.
inline int spark(uint32_t k, uint32_t t, uint32_t seed, int cy, int8_t px[8][2], int& n) {
    static const int8_t SWAY[4] = {0, 1, 0, -1};
    static const int8_t NOTE[8][2] = {{1, 0}, {2, 0}, {2, 1}, {2, 2}, {0, 3}, {1, 3}, {2, 3}, {0, 2}};
    n = 0;
    if (k * SPARK_EVERY_MS > t) return 0;
    const uint32_t age = t - k * SPARK_EVERY_MS;
    if (age >= SPARK_LIFE_MS) return 0;
    const uint32_t h = hash2(seed ^ 0x5A17u, k);
    const int off = (int)((h >> 1) & 3);
    const int x = ((h & 1) ? 25 + off : 6 - off) + SWAY[((h >> 8) + age / 264) & 3];
    const int y = cy + 2 - (int)(age * 45 / 6600);
    if ((h >> 3) % 100 < 45) {
        for (const auto& d : NOTE) px[n][0] = (int8_t)(x - 1 + d[0]), px[n][1] = (int8_t)(y - 3 + d[1]), n++;
    } else {
        px[n][0] = (int8_t)x, px[n][1] = (int8_t)y, n++;
        if (age < 792) px[n][0] = (int8_t)(x + 1), px[n][1] = (int8_t)y, n++;
    }
    return age < 660 ? 4 : age < 990 ? 3 : age < 1320 ? 2 : 1;
}

// Speaking: the live sparks, newest first: a pixel two sparks share shows the
// younger (never dimmer) one, and the older one's trail fills in around it.
inline void sparks(Canvas& f, uint32_t t, uint32_t seed, int cy) {
    const uint32_t first = t >= SPARK_LIFE_MS ? (t - SPARK_LIFE_MS) / SPARK_EVERY_MS + 1 : 0;
    for (uint32_t k = t / SPARK_EVERY_MS + 1; k-- > first;) {
        int8_t px[8][2];
        int n = 0;
        const int a = spark(k, t, seed, cy, px, n);
        if (!a) continue;
        const uint8_t col = fadeOf(OkC, a);
        for (int i = 0; i < n; i++) {
            const int x = px[i][0], y = px[i][1];
            if ((unsigned)x < (unsigned)W && (unsigned)y < (unsigned)H && onBackground(f.px[y * W + x]))
                f.px[y * W + x] = col;
        }
    }
}

// Sweat drops (error) and sleep Zs, drawn before the glow pass.
inline void Character::effect(Canvas& f, int kind, int arg) {
    if (kind == FxSweat) {
        const int x = 24 + arg, y = 9 + arg * 2;
        static const int8_t d[5][2] = {{0, 0}, {0, 1}, {-1, 1}, {1, 1}, {0, 2}};
        for (int k = 0; k < 5; k++) f.put(x + d[k][0], y + d[k][1], ListenC);
        f.put(x, y + 1, White);
    } else if (kind == FxZzz) {
        static const int8_t z[3][3] = {{22, 8, 3}, {25, 4, 4}, {27, 0, 5}};
        for (int i = 0; i < 3 && i < arg; i++) {
            const int zx = z[i][0], zy = z[i][1], zs = z[i][2];
            for (int k = 0; k < zs; k++) {
                f.put(zx + k, zy, MutedC);
                f.put(zx + k, zy + zs - 1, MutedC);
                f.put(zx + zs - 1 - k, zy + k, MutedC);
            }
        }
    }
}

inline void Character::mark(Canvas& f, int kind, uint32_t t) {
    if (kind == MarkQuestion) {
        static const char* const q[7] = {".fff.", "f...f", "....f", "...f.", "..f..", ".....", "..f.."};
        const int y0 = 1 + ((t / 150) % 2);               // a small wobble
        for (int j = 0; j < 7; j++)
            for (int i = 0; i < 5; i++)
                if (q[j][i] == 'f') f.put(25 + i, y0 + j, Float);
    } else if (kind == MarkExclaim) {
        const int y0 = t < 200 ? 0 : 1;
        for (int j = 0; j < 5; j++) {
            f.put(27, y0 + j, Float);
            f.put(28, y0 + j, Float);
        }
        f.put(27, y0 + 7, Float);
        f.put(28, y0 + 7, Float);
    } else if (kind == MarkSparkle) {
        const int phase = (int)((t / 120) % 3);
        static const int8_t at[3][2] = {{26, 3}, {5, 4}, {28, 10}};
        for (int k = 0; k < 3; k++) {
            if (k == phase) continue;                     // one of the three is dark: twinkling
            const int x = at[k][0], y = at[k][1];
            f.put(x, y, Float);
            f.put(x - 1, y, Float);
            f.put(x + 1, y, Float);
            f.put(x, y - 1, Float);
            f.put(x, y + 1, Float);
        }
    }
}

// -- render ------------------------------------------------------------
// Background pixels beside an outline pixel become the glow (Rim): the deep
// outline runs all the way round the silhouette, and the ring outside it
// takes its color from the theme (paletteFor). The floor shadow and the
// effects (no outline of their own) get no glow.
inline void silhouetteGlow(Canvas& f) {
    uint8_t src[PIXELS];
    memcpy(src, f.px, PIXELS);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            if (src[y * W + x] != Clear) continue;
            const int nx[4] = {x - 1, x + 1, x, x}, ny[4] = {y, y, y - 1, y + 1};
            for (int k = 0; k < 4; k++) {
                if ((unsigned)nx[k] >= (unsigned)W || (unsigned)ny[k] >= (unsigned)H) continue;
                if (src[ny[k] * W + nx[k]] == Outline) {
                    f.px[y * W + x] = Rim;
                    break;
                }
            }
        }
}

inline void Character::render(State state, const Ctx& ctx, uint8_t* out) {
    const Temper& tm = TEMPER[t_.temper];
    const uint32_t seed = t_.seed, now = ctx.t, st = ctx.stateT;
    const EyeGeom g = eyeGeom();
    const int mkind = t_.mouth;

    int bob = 0, lid = 2, pdx = 0, pdy = 0, mlevel = 0, armDy = 0, earDy = 0, shake = 0;
    Expr expr = ExprNormal;
    MouthExpr mexpr = mkind == Flat ? MxFlat : MxSmile;
    int fx = FxNone, fxArg = 0;
    const int breathe = (int)((now / tm.breathe) % 2);

    switch (state) {
    case Idle: {
        bob = breathe;
        lid = blinkLid(seed, now, tm);
        pdx = lookDx(seed, now);
        if (t_.temper == Drowsy && (now / 2400) % 5 == 4 && lid > 1) lid = 1;   // heavy lids now and then
        const int64_t fk = now / tm.fidget;
        const int64_t fd = (int64_t)now - (fk * tm.fidget +
                                           (int64_t)(hash2(seed, (uint32_t)(5 + fk)) % (tm.fidget / 2)));
        if (fd >= 0 && fd < 700) {
            if (t_.temper == Bouncy) {
                const bool hop = (fd / 175) % 2 == 0;
                bob = hop ? 2 : 0;
                armDy = hop ? -3 : 0;
            } else if (t_.temper == Poised) {
                expr = ExprWink;
            } else if (t_.temper == Alert) {
                earDy = (fd / 120) % 2 == 0 ? -1 : 0;
            } else if (t_.temper == Curious) {        // looks about, ears up
                static const int8_t sweep[4] = {-1, 0, 1, 0};
                pdx = sweep[(fd / 175) % 4];
                pdy = fd < 350 ? -1 : 0;
                earDy = (fd / 120) % 2 == 0 ? -1 : 0;
            } else if (t_.temper == Drowsy) {         // a yawn
                lid = 1;
                mlevel = fd < 150 ? 1 : fd < 550 ? 3 : 1;
            } else if (t_.temper == Jumpy) {          // a quick shiver
                shake = (fd / 90) % 2 == 0 ? -1 : 1;
            } else {
                pdx = fd < 350 ? -1 : 1;
            }
        }
        break;
    }
    case Listening:
        lid = blinkLid(seed, now, tm) == 2 ? 2 : 1;
        earDy = -1;
        armDy = (ctx.level < 60 || (now / 200) % 2) ? -3 : -4;
        mexpr = MxFlat;
        fx = FxHear;                               // ripples, after the glow pass
        break;
    case Thinking:
        bob = breathe;
        lid = blinkLid(seed, now, tm);
        if (ctx.stt) {
            static const int8_t sweep[4] = {-1, 0, 1, 0};
            pdx = sweep[(now / 260) % 4];
        } else {
            pdx = (now / 1300) % 3 != 2 ? 1 : -1;
            pdy = -1;
        }
        mexpr = MxFlat;                            // the chat shows it thinking (a dots bubble)
        break;
    case Speaking: {
        lid = blinkLid(seed, now, tm);
        const int lvl = ctx.level;
        mlevel = mouthLevel(lvl);
        bob = (lvl >= 150 && (now / 66) % 2 == 0) ? tm.hop : (lvl < 24 ? breathe : 0);
        if (lid == 2 && (now / 400) % 9 == 8) expr = ExprHappy;
        fx = FxSound;                              // sparks, after the glow pass
        earDy = (lvl >= 150 && (now / 132) % 2 == 0) ? -1 : 0;
        break;
    }
    case Error: {
        static const int8_t sh[5] = {-1, 1, -1, 1, 0};
        shake = st < 550 ? sh[st / 110 < 4 ? st / 110 : 4] : 0;
        expr = st < 1500 ? ExprX : ExprNormal;
        pdy = 1;
        mexpr = MxFrown;
        bob = st >= 550 ? 1 : 0;
        fx = FxSweat;
        fxArg = (int)((st / 450) % 3);
        break;
    }
    case Sleep:
        bob = (int)((now / 1500) % 2);
        expr = ExprClosed;
        mexpr = MxFlat;
        fx = FxZzz;
        fxArg = 1 + (int)((now / 700) % 3);
        break;
    default:
        break;
    }

    // A reaction overrides the pose while it lasts (not in Error / Sleep,
    // which have their own faces).
    int markKind = MarkNone;
    uint32_t rt = ctx.reactT;
    if (ctx.react != ReactNone && ctx.react < ReactionCount && rt < reactionMs(ctx.react) &&
        state != Error && state != Sleep) {
        if (ctx.react == Pleased) {
            const bool hop = rt < 800 && (rt / 200) % 2 == 0;
            bob = hop ? 2 : 0;
            armDy = hop ? -3 : 0;
            earDy = hop ? -1 : 0;
            lid = 2;
            expr = rt < 850 ? ExprHappy : ExprNormal;
            if (mlevel == 0) mexpr = MxSmile;
            markKind = MarkSparkle;
        } else if (ctx.react == Puzzled) {
            lid = 2;
            pdx = rt < 300 ? -1 : rt < 700 ? 1 : 0;
            pdy = -1;
            bob = (rt >= 200 && rt < 900) ? 1 : 0;
            earDy = (rt / 300) % 2 == 0 ? 1 : 0;
            mexpr = MxFlat;
            mlevel = 0;
            markKind = MarkQuestion;
        } else {
            bob = rt < 200 ? -1 : 0;
            lid = 2;
            pdx = 0;
            pdy = 0;
            earDy = rt < 400 ? -1 : 0;
            armDy = -4;
            if (rt < 350) mlevel = 2;
            markKind = MarkExclaim;
        }
    }

    const int ox = shake;
    Canvas& f = frame_;
    f.clear();
    blit(f, feet_, ox, 0);
    arms(armDy);
    blit(f, tmp_, ox, 0);
    blit(f, torso_, ox, 0);
    if (held(state, ctx, mlevel)) blit(f, tmp_, ox, 0);
    blit(f, ears_, ox, bob + earDy);
    blit(f, head_, ox, bob);

    // Face.
    const int ey0 = eyeY_ - g.h / 2 + bob;
    const int gx = g.gap / 2;
    int lx0 = 16 - gx - g.w + ox, rx0 = 16 + gx + ox;
    const bool oneEye = t_.eyes == Cyclops;       // a single eye, centred
    if (oneEye) lx0 = rx0 = 16 - g.w / 2 + ox;
    for (int side = -1; side <= 1; side += 2) {
        if (oneEye && side > 0) continue;
        Expr e = expr;
        if (e == ExprWink) e = ((oneEye || side == 1) && (now / 175) % 2 == 0) ? ExprHappy : ExprNormal;
        drawEye(f, g.w, g.h, side < 0 ? lx0 : rx0, ey0, lid, pdx, pdy, e);
    }
    drawMouth(f, 16 + ox, eyeY_ + 3 + bob + (mkind == Beak ? 0 : 1), mlevel, mexpr,
              (int)((now / 66) % 4));
    worn(f, bob, ox, g, ey0, lx0, rx0);
    if (t_.hat == Propeller) {
        static const int8_t half[4] = {5, 3, 1, 3};
        const bool spin = state == Thinking || state == Speaking;
        const int h = spin ? half[(now / 66) % 4] : 5;
        for (int i = -h; i < h; i++) pix_.set(16 + i + ox, top_ + bob - 5, Float);
        layer(f, false);
    }
    if (fx == FxSweat || fx == FxZzz) effect(f, fx, fxArg);
    if (markKind != MarkNone) mark(f, markKind, rt);
    silhouetteGlow(f);
    if (fx == FxHear) ripples(f, st, cyInt() + 1 + bob);
    else if (fx == FxSound) sparks(f, st, seed, cyInt() + 1 + bob);
    memcpy(out, f.px, PIXELS);
}

}  // namespace avatar
