// avatar_art.h — the character's part library: palette slots, trait names,
// silhouette tables (integer row spans) and the pixel art of hats and
// accessories. Pure data, no Arduino; avatar.h composes it.
//
// Art is authored against palette SLOTS, never colors: a character palette
// (below) only swaps the color table (avatar.h paletteFor), the art stays.
#pragma once

#include <stdint.h>

namespace avatar {

// Palette-index slots of the sprite (index 0 is transparent in layers and
// the background color in a finished frame).
enum Slot : uint8_t {
    Clear,      // transparent / Bg
    Outline,
    BodyD, Body, BodyL,       // body ramp
    Belly,
    AccD, Acc, AccL,          // accent ramp
    White, WhiteD,            // eye white, hats
    Pupil, Dark, MouthC, Tongue, Cheek,
    ListenC, BusyC, OkC, ErrC,   // status colors straight from the Ink roles
    Shadow, MutedC, Float,
    Rim,        // the glow: background pixels beside the outline (theme-colored)
    ListenF1, ListenF2, ListenF3,   // ListenC at 25 / 50 / 75 % over the background (ripples)
    OkF1, OkF2, OkF3,               // OkC likewise (sparks)
    SlotCount
};

// Trait vocabularies (the record stores an index into each).
enum Ears : uint8_t {
    EarsNone, Cat, Bear, Bunny, Fox, Horns, Antenna, Fins, Droopy, Tufts,
    Elf, Discs, Bobbles, Spikes, Sprout, Mouse, Antlers, Buns, Unicorn, EarsCount
};
enum Eyes : uint8_t {
    Beady, Round, Tall, Wide, Shiny, Led, Dot,
    Sleepy, Cyclops, Slit, Heart, Joy, Gleam, Square, EyesCount
};
enum Mouths : uint8_t {
    Smile, Flat, CatMouth, Beak, Grill, Tooth, OMouth,
    Fang, TongueOut, Zigzag, Pout, Grin, Smirk, MouthsCount
};
enum Hats : uint8_t {
    HatNone, Toque, Tophat, Wizard, Crown, Propeller, Headphones, Winged, Roof, Beanie, Bow, Halo,
    Hair, Cap, Cowboy, Party, Beret, Fez, Helmet, Mortarboard, Viking, Headband, Wreath, HatsCount
};
enum Accs : uint8_t {
    AccNone, Bowtie, Scarf, Glasses, Mustache, Scissors, Staff, Whisk, Mug, Flower,
    Monocle, Eyepatch, Necktie, Headset, Plaster, Book, Magnifier, Lollipop, Balloon, AccsCount
};
enum Patterns : uint8_t {
    PatNone, Muzzle, Stripes, Spots, Mask, Blaze, Twotone, Blush,
    Freckles, Brows, Star, Frost, Belt, Split, PatternsCount
};
enum Tempers : uint8_t { Calm, Bouncy, Poised, Alert, Curious, Drowsy, Jumpy, TempersCount };

const int HEAD_COUNT = 16;
const int TORSO_COUNT = 8;       // index 0 = none
const int FEET_COUNT = 5;        // 0 nubs, 1 paws, 2 none, 3 boots, 4 wheels
const int ARMS_COUNT = 5;        // 0 none, 1 nubs, 2 big hands, 3 long, 4 mitts
const int PALETTE_COUNT = 30;

// A silhouette as per-row half widths about the mirror axis (columns 15|16):
// row y covers columns [16 - half, 16 + half - 1]; 0 = empty row. These are
// the superellipses of the design, evaluated once on the host and frozen as
// integers (the device never does floating point here); the host test
// recomputes them from the float definition and checks equality.
// rx10 / ry10 are the radii (head, torso: rx10 only) and cy10 the torso's
// centre row, in tenths of a pixel.
struct HeadShape {
    uint8_t rx10, ry10;
    uint8_t half[32];
};
struct TorsoShape {
    uint8_t rx10;
    uint16_t cy10;
    uint8_t half[32];
};

static const HeadShape HEADS[HEAD_COUNT] = {
    {95, 75, {0,0,0,0,0,0,0,0,3,6,7,8,9,9,9,10,9,9,9,8,7,6,3,0,0,0,0,0,0,0,0,0}},  // 0 round
    {105, 70, {0,0,0,0,0,0,0,0,0,5,8,9,10,10,10,10,10,10,10,10,9,8,5,0,0,0,0,0,0,0,0,0}},  // 1 wide
    {95, 75, {0,0,0,0,0,0,0,0,7,8,9,9,9,9,9,10,9,9,9,9,9,8,7,0,0,0,0,0,0,0,0,0}},  // 2 box
    {85, 80, {0,0,0,0,0,0,0,3,5,6,7,7,8,8,8,9,9,8,8,7,7,5,3,0,0,0,0,0,0,0,0,0}},  // 3 egg
    {100, 75, {0,0,0,0,0,0,0,0,4,6,7,8,9,9,10,10,10,10,10,10,9,8,5,0,0,0,0,0,0,0,0,0}},  // 4 bean
    {80, 75, {0,0,0,0,0,0,0,0,3,5,6,7,7,8,8,8,8,8,7,7,6,5,3,0,0,0,0,0,0,0,0,0}},  // 5 small round
    {105, 65, {0,0,0,0,0,0,0,0,0,0,7,9,10,10,10,10,11,10,10,10,10,9,7,0,0,0,0,0,0,0,0,0}},  // 6 flat wide
    {85, 80, {0,0,0,0,0,0,0,5,7,8,8,8,8,8,8,8,8,8,8,8,8,7,5,0,0,0,0,0,0,0,0,0}},  // 7 tall box
    {95, 75, {0,0,0,0,0,0,0,0,4,7,8,9,9,10,10,10,9,9,8,8,7,5,3,0,0,0,0,0,0,0,0,0}},  // 8 top-heavy
    {105, 80, {0,0,0,0,0,0,0,4,6,8,9,9,10,10,10,11,10,10,10,9,8,7,4,0,0,0,0,0,0,0,0,0}},  // 9 big round
    {100, 80, {0,0,0,0,0,0,0,2,4,6,7,8,9,9,10,10,9,9,8,7,6,4,2,0,0,0,0,0,0,0,0,0}},  // 10 diamond
    {105, 80, {0,0,0,0,0,0,0,9,10,10,10,10,10,10,10,10,10,10,10,10,10,10,9,0,0,0,0,0,0,0,0,0}},  // 11 squircle
    {80, 80, {0,0,0,0,0,0,0,2,4,5,6,6,7,7,8,8,8,8,8,8,8,6,4,0,0,0,0,0,0,0,0,0}},  // 12 pear
    {95, 75, {0,0,0,0,0,0,0,0,5,8,9,10,10,10,10,10,9,8,8,7,6,4,3,0,0,0,0,0,0,0,0,0}},  // 13 acorn
    {110, 60, {0,0,0,0,0,0,0,0,0,0,0,4,7,9,10,11,11,11,11,10,9,7,4,0,0,0,0,0,0,0,0,0}},  // 14 saucer
    {100, 75, {0,0,0,0,0,0,0,0,5,8,9,10,10,10,10,10,10,10,10,10,9,8,5,0,0,0,0,0,0,0,0,0}},  // 15 octagon (hand-drawn)
};
static const TorsoShape TORSOS[TORSO_COUNT] = {
    {0, 0, {0}},  // none
    {55, 260, {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,4,5,5,5,5,4,0,0,0}},  // 1 small
    {65, 260, {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,4,5,6,7,7,6,0,0,0}},  // 2 pear
    {60, 260, {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,5,6,6,6,6,5,0,0,0}},  // 3 box
    {45, 255, {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3,4,5,5,4,4,3,0,0,0}},  // 4 tapered
    {75, 260, {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,6,7,7,7,7,6,0,0,0}},  // 5 wide
    {35, 255, {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,3,3,4,3,3,2,0,0,0}},  // 6 slim
    {60, 260, {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3,4,6,6,7,6,0,0,0}},  // 7 bell
};

// ---------------------------------------------------------------- palettes
// A character's own colors (0xRRGGBB), never derived from the theme; the seed
// picks one (avatar.h). Chosen so that on every palette the body keeps its
// dark pupils (>= 3:1) and white eyes (>= 2:1) readable, the X-eye mark reads
// on the body, the deep outline separates from light backgrounds and the rim
// (a bright, saturated tint of the body hue) from dark ones (avatar_test.cpp
// checks all of it against every theme background).
struct CharPalette {
    const char* name;
    uint32_t body, bodyD, bodyL, belly;    // body ramp and the belly / muzzle tone
    uint32_t acc, accD, accL;              // accent ramp (hats, ears' details, held items)
    uint32_t cheek;
    uint32_t err;                          // the X eyes of a failed turn
    uint32_t outline;                      // deep, hue-tinted: lines between parts, the edge on light backgrounds
    uint32_t rim;                          // the edge on dark backgrounds
};

static const CharPalette CHAR_PALETTES[PALETTE_COUNT] = {
    {"Tangerine", 0xf08a24, 0xb8501c, 0xffbe4f, 0xffe0a3,
     0x3c8dad, 0x26566e, 0x7cc4d9, 0xff7a6b, 0x6a1028, 0x3a1226, 0xffb347},
    {"Pine", 0x3e9a5c, 0x266b42, 0x7fd08e, 0xd4f2d6,
     0xf2a541, 0xb0701e, 0xffd27a, 0xf08a7a, 0x3f0e18, 0x0c2a1f, 0x6fe0a0},
    {"Berry", 0xc0489a, 0x8a2d77, 0xe783c4, 0xf8cfe6,
     0xf2c94c, 0xb5892a, 0xfff09a, 0xff8fa3, 0x3d0a33, 0x2a0f2c, 0xff8fd0},
    {"Sky", 0x4aa3df, 0x2e6fb0, 0x8fd3f4, 0xdff3fb,
     0xef6f6c, 0xb0413f, 0xffa59f, 0xff9aa8, 0x6a1230, 0x10244a, 0x6fd0ff},
    {"Honey", 0xc99a1a, 0x8f6410, 0xf0cc4a, 0xfbe9a0,
     0x7b5fd1, 0x4d3a99, 0xb4a0f0, 0xf08a5a, 0x3a1408, 0x321e06, 0xffd13d},
    {"Plum", 0x7b57c9, 0x4f3a96, 0xad8ff0, 0xe1d3fb,
     0x5fd1a5, 0x2e8c6c, 0xa7f0d2, 0xff8fb0, 0xffb8c4, 0x1e1244, 0xb79cff},
    {"Cherry", 0xe0463f, 0xa82a35, 0xff8470, 0xffd6c4,
     0x3fb5a0, 0x247868, 0x8fe3d0, 0xff9a8a, 0x3e0814, 0x3a0c1c, 0xff7a66},
    {"Slate", 0x7d8fa8, 0x52627f, 0xb4c4da, 0xe6edf6,
     0xf0a030, 0xb06c1a, 0xffd27a, 0xf5a0a0, 0x5a1030, 0x141c38, 0x9fc2ff},
    {"Jade", 0x36b08a, 0x1f7a64, 0x7bdcb6, 0xdcf7ea,
     0xee6c8b, 0xb03f64, 0xffa6bd, 0xff8f9a, 0x5a0f2c, 0x0a2a2c, 0x5df0c0},
    {"Cocoa", 0xa56a43, 0x6f4129, 0xd29968, 0xf1d2aa,
     0x4fb3c8, 0x2d7388, 0x96e0ee, 0xe8785f, 0x2e0a0c, 0x2a140e, 0xffb070},
    {"Indigo", 0x5b6ee1, 0x3b47b0, 0x93a2ff, 0xdde2ff,
     0xffb84d, 0xc47a1c, 0xffe08a, 0xff8fb0, 0xffd0dc, 0x14164a, 0x8fa0ff},
    {"Salmon", 0xe88a76, 0xc05a52, 0xffbba8, 0xffe6d8,
     0x5aa86a, 0x34703f, 0x9ddc9f, 0xff7f8a, 0x6a1020, 0x3c1428, 0xffa088},
    {"Teal", 0x2f9fa8, 0x1c6b7a, 0x6fd0d2, 0xd3f2ee,
     0xf0805a, 0xb04a2e, 0xffb090, 0xff8f7a, 0x5a1020, 0x08282f, 0x4fe0e0},
    {"Lilac", 0xb08ad8, 0x7d5aa8, 0xd9bcf5, 0xf2e6fb,
     0x7cc36b, 0x478a3c, 0xb9eaa0, 0xff95b5, 0x4a1040, 0x241040, 0xd2a8ff},
    {"Lime", 0x7fae22, 0x55801a, 0xb7dd5a, 0xe8f3a8,
     0xd95f8e, 0x9c3763, 0xffa0c2, 0xf5936a, 0x4a0c1c, 0x182a0a, 0xc4ef4a},
    {"Denim", 0x4f7fc4, 0x31528f, 0x8fb4ee, 0xdbe8fa,
     0xf4c542, 0xb8901e, 0xfff09a, 0xff9a8a, 0xffe0e0, 0x0e1c48, 0x80b0ff},
    {"Rose", 0xe0709a, 0xa8447a, 0xff9fc0, 0xffd9e6,
     0x5bb6a0, 0x2f7a6a, 0xa0e5d2, 0xff7f9a, 0x4a0a26, 0x34102c, 0xff90b8},
    {"Sand", 0xb89358, 0x85663a, 0xe0c28a, 0xf6e6c4,
     0x5a7bd6, 0x34509a, 0x9fb8f5, 0xe8806a, 0x3a1608, 0x32200e, 0xf0c070},
    {"Sage", 0x79b288, 0x4d8153, 0xc7f9d9, 0xebfff0,
     0xe6456d, 0x8e1c2e, 0xffa6c5, 0xff737e, 0x470e3e, 0x042b15, 0x6bff81},
    {"Coral", 0xed674c, 0xab2d29, 0xffb193, 0xffdbd4,
     0x45d8e6, 0x1c798e, 0xa6fffe, 0xff737e, 0x0e4347, 0x2b1104, 0xff756b},
    {"Mustard", 0x947716, 0x6a4607, 0xd2c464, 0xfff2c9,
     0x8845e6, 0x571c8e, 0xc2a6ff, 0xff737e, 0x0e2047, 0x292602, 0xffce6b},
    {"Orchid", 0xbb66cc, 0x793e93, 0xfab0ff, 0xfadfff,
     0x88e645, 0x418e1c, 0xd4ffa6, 0xff73b9, 0x1d470e, 0x2b042b, 0xd86bff},
    {"Moss", 0x769945, 0x5a6e29, 0xb2d98f, 0xf0ffdc,
     0xe67a45, 0x8e4e1c, 0xffbba6, 0xff737e, 0x210e47, 0x152b04, 0xd0ff6b},
    {"Graphite", 0x7e828f, 0x525a67, 0xbdbfcc, 0xf7f9ff,
     0xe69045, 0x8e5d1c, 0xffc6a6, 0xff73b9, 0x47340e, 0x04082b, 0x6b9fff},
    {"Peach", 0xd19979, 0x975d4b, 0xffdcbd, 0xffeee4,
     0x45b0e6, 0x1c5d8e, 0xa6eaff, 0xff737e, 0x0e3747, 0x2b1904, 0xff936b},
    {"Cobalt", 0x4574f5, 0x235cb0, 0x8d9fff, 0xd1ddff,
     0xe6bd45, 0x8e7d1c, 0xffe0a6, 0xff73b9, 0x47330e, 0x04082b, 0x6ba1ff},
    {"Copper", 0xc2703a, 0x8c3c1f, 0xffc890, 0xffe4d2,
     0x45e6d0, 0x1c8e8a, 0xa6ffea, 0xff737e, 0x0e3547, 0x2b1a04, 0xff976b},
    {"Fern", 0x4fa839, 0x3d7920, 0x94ec8b, 0xddffd5,
     0xe645d8, 0x8e1c79, 0xfea6ff, 0xff737e, 0x370e47, 0x062b04, 0x97ff6b},
    {"Wine", 0xc25d7f, 0x8c385f, 0xffadbf, 0xffdee9,
     0x45e695, 0x1c8e61, 0xa6ffc9, 0xff737e, 0x0c3d29, 0x2b040b, 0xff6bab},
    {"Taupe", 0xa89987, 0x796557, 0xece1cf, 0xfff9f2,
     0x45a3e6, 0x1c538e, 0xa6e3ff, 0xff737e, 0x0e2e47, 0x2b2004, 0xffab6b},
};

// ---------------------------------------------------------------- pixel art
// ASCII sprites. Legend: o outline, w white, e white-dark, a accent, d
// accent-dark, l accent-light, k dark, p pupil, b body, c body-dark, h
// body-light, y listen, u busy, g ok, r err, m mouth, s spark, x muted, f
// belly; '.' = nothing.
inline uint8_t artSlot(char ch) {
    switch (ch) {
    case 'o': return Outline;
    case 'w': return White;
    case 'e': return WhiteD;
    case 'a': return Acc;
    case 'd': return AccD;
    case 'l': return AccL;
    case 'k': return Dark;
    case 'p': return Pupil;
    case 'b': return Body;
    case 'c': return BodyD;
    case 'h': return BodyL;
    case 'y': return ListenC;
    case 'u': return BusyC;
    case 'g': return OkC;
    case 'r': return ErrC;
    case 'm': return MouthC;
    case 'j': return Float;
    case 'x': return MutedC;
    case 'f': return Belly;
    default: return Clear;
    }
}

struct Art {
    const char* const* rows;
    uint8_t n;
};
#define AVATAR_ART(name, ...)                                                    \
    static const char* const name##_ROWS[] = {__VA_ARGS__};                      \
    static const Art name = {name##_ROWS, (uint8_t)(sizeof(name##_ROWS) / sizeof(name##_ROWS[0]))}

// Hats whose rows are the LEFT half (each row is mirrored; rows may differ in
// length, the mirror copy follows each row directly). sink = rows overlapping
// the head top; outline false for the halo.
struct HatArt {
    Art art;
    int8_t sink;
    bool outline;
};

AVATAR_ART(H_TOQUE, "...wwww", "..wwwww", ".wwwwww", ".wwwwww", "..ewwww", "..wwwww", "..wwwww",
           "..eeeee");
AVATAR_ART(H_TOPHAT, "...kkkkk", "...kkkkk", "...kkkkk", "...kkkkk", "...aaaaa", "...kkkkk",
           "kkkkkkkk");
AVATAR_ART(H_WIZARD, "......a", "......a", ".....aa", ".....aa", "....aal", "....aya", "...aaaa",
           "..aaaaa", "aaaaaaa");
AVATAR_ART(H_CROWN, "a....a", "aa..aa", "aaaaaa", "aaylya", "aaaaaa", "dddddd");
AVATAR_ART(H_PROPELLER, "......", ".....d", "..aaaa", ".aallll", "aaaaaaa", "dddddd");
AVATAR_ART(H_WINGED, "w....aaa", "ww..aaaa", "eww.aaal", ".eewaaaa", "..e.dddd");
AVATAR_ART(H_BEANIE, "....ww", "....ww", "..aaaa", ".aaaaa", "aaaaaa", "dddddd", "llllll");
AVATAR_ART(H_HALO, "..jjjjjj", ".j......", "..jjjjjj");
AVATAR_ART(H_HAIR, "..aaaaaa", ".aaalaaa", "aaalaaaa", "aaaaaaaa", "aaa.....");
AVATAR_ART(H_COWBOY, "........aaaa", "........aaaa", "aa......dddd", ".aaaaaaaaaaa");
AVATAR_ART(H_PARTY, ".....l", ".....a", "....aa", "...wwa", "...aaa", "..wwaa", "..aaaa", ".aaaaa", "dddddd");
AVATAR_ART(H_BERET, ".......d", "...aaaaa", ".aaaaaaa", "aaaalaaa", "..dddddd");
AVATAR_ART(H_FEZ, "....u", "..aaa", ".aaaa", ".aaaa", ".llll", ".aaaa");
AVATAR_ART(H_HELMET, "....aaal", "..aaaaal", ".aaaaaal", ".aaaaaal", "dddddddd");
AVATAR_ART(H_MORTAR, "......kkk", "...kkkkkl", "kkkkkkkkk", "..kkkkkkk");
AVATAR_ART(H_VIKING, "w.........", "w.....eeee", "ww..eeeeee", ".wweeeeeee", "..dddddddd");

// An antler (the right one; avatar.h mirrors it), its base row on the head.
AVATAR_ART(E_ANTLER, "l.l.l", "l.a.l", ".laal", "..a..", "..a..", "..a..", "..a..");

static const HatArt HAT_MIRRORED[HatsCount] = {
    {{nullptr, 0}, 0, false},       // none
    {H_TOQUE, 1, true},
    {H_TOPHAT, 1, true},
    {H_WIZARD, 1, true},
    {H_CROWN, 1, true},
    {H_PROPELLER, 1, true},
    {{nullptr, 0}, 0, false},       // headphones: drawn from the head outline
    {H_WINGED, 2, true},
    {{nullptr, 0}, 0, false},       // roof: full width
    {H_BEANIE, 1, true},
    {{nullptr, 0}, 0, false},       // bow: full width
    {H_HALO, -1, false},
    {H_HAIR, 3, true},
    {{nullptr, 0}, 0, false},       // cap: full width, the peak to the right
    {H_COWBOY, 1, true},
    {H_PARTY, 1, true},
    {H_BERET, 1, true},
    {H_FEZ, 1, true},
    {H_HELMET, 1, true},
    {H_MORTAR, 1, true},
    {H_VIKING, 1, true},
    {{nullptr, 0}, 0, false},       // headband: drawn on the forehead
    {{nullptr, 0}, 0, false},       // wreath: drawn along the head top
};

// Full-width hats (rows are the whole sprite, placed at a fixed column).
AVATAR_ART(H_ROOF, "...........dd...", ".......aa..dd...", "......aaaa.dd...",
           ".....aaaaaa.....", "....aaaaaaaa....", "...aaaaaaaaaa...", "..aadaadaadaada.",
           ".llllllllllllll.");
AVATAR_ART(H_CAP, "....dd..........", "...aaaaaa.......", "..aaaaaaaa......", "..aaaaaaaa......",
           "..ddddddddddddd.");
AVATAR_ART(H_BOW, "aaa.aaa", "aaaaaaa", "aaadaaa", "aaaaaaa", "aaa.aaa");

// Worn accessories, left halves.
AVATAR_ART(A_BOWTIE, "a...d", "aaadd", "a...d");
AVATAR_ART(A_MUSTACHE, ".ddddd", "dd....");

// Worn, full width.
AVATAR_ART(A_SCARF, "aaaaaaaaaaaaaa", "dadadadadadada", "........aa....", "........aa....",
           "........ad....");

// Held items, full width; the scissors have two poses.
AVATAR_ART(A_SCISSORS_OPEN, "e....e", ".e..e.", "..ee..", "..ee..", ".e..e.", "aa..aa", "a.aa.a",
           "aa..aa");
AVATAR_ART(A_SCISSORS_SHUT, "..ee..", "..ee..", "..ee..", "..ee..", ".e..e.", "aa..aa", "a.aa.a",
           "aa..aa");
AVATAR_ART(A_STAFF, "w.aa.w", "ww.a.ww", "..lad.", "...a..", "..da..", "...a..", "..da..", "...a..",
           "...a..", "...d..", "...d..", "...d..");
AVATAR_ART(A_WHISK, ".eeee.", "e.ee.e", "e.ee.e", "e.ee.e", ".eeee.", "..aa..", "..aa..", "..ad..");
AVATAR_ART(A_MUG, "wwwww..", "wwwwwww", "waaaw.w", "wwwwwww", ".wwww..");
AVATAR_ART(A_FLOWER, ".u.", "uyu", ".u.", ".g.", ".g.", ".g.");

// Worn, full width: a necktie under the chin; a plaster on a cheek.
AVATAR_ART(A_NECKTIE, "..aa..", "..ad..", "..aa..", ".aaaa.", ".aaaa.", "..aa..");
AVATAR_ART(A_PLASTER, "eeee", "ewwe", "eeee");

// Held items, full width. The book has two poses (a page turns on speech beats); the
// balloon is drawn by avatar.h (it sways).
AVATAR_ART(A_BOOK_FLAT, "aaaaaaa", "awwdwwa", "awwdwwa", "awwdwwa", "aaaaaaa");
AVATAR_ART(A_BOOK_TURN, "aaaaaaa", "aweedwa", "awwedwa", "awwdwwa", "aaaaaaa");
AVATAR_ART(A_MAGNIFIER, ".dddd.", "dwwwwd", "dwlwwd", "dwwwwd", ".dddd.", "...ad.", "...ad.", "...ad.");
AVATAR_ART(A_LOLLIPOP, ".aaa.", "aalaa", "alwla", "aalaa", ".aaa.", "..w..", "..w..", "..w..", "..w..");

#undef AVATAR_ART

}  // namespace avatar
