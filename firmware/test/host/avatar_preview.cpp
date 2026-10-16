// Host harness around the firmware's own avatar.h: renders frames for
// firmware/tools/avatar_preview.py (contact sheets, part sheets, GIFs) and
// counts distinct characters. Not a test; nothing here models the avatar.
//
//   avatar_preview stats <n>   text: how many distinct characters n seeds give
//   avatar_preview serve       binary requests on stdin, replies on stdout
//
// serve requests (one text line each; every reply is binary):
//   I                          -> u8 W, u8 H, u8 SlotCount, u8 TRAIT_FIELDS,
//                                 TRAIT_FIELDS x u8 range, u8 States, u8 Reactions
//   V <name> <variant>         -> TRAIT_FIELDS x u8 record, u32le seed
//   B <family> <light 0|1>     -> u16le theme background (RGB565)
//   F <rec hex> <seed> <state> <t> <stateT> <level> <stt> <react> <reactT> <bg>
//                              -> SlotCount x u16le palette, PIXELS x u8 slots
#include "avatar.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <unordered_set>

using namespace avatar;

static uint32_t crc32(const uint8_t* d, size_t n, uint32_t c = ~0u) {
    for (size_t i = 0; i < n; i++) {
        c ^= d[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return c;
}

static int stats(int n) {
    static Character c;
    std::set<std::string> records;                 // identity: the 12 trait indexes
    std::unordered_set<uint64_t> looks;            // visual: every state's frame plus the palette
    std::unordered_set<uint64_t> shapes;           // visual without colour
    std::set<std::string> outlines, faces;         // the parts that shape the silhouette / the face
    double space = 1;
    for (int i = 0; i < TRAIT_FIELDS; i++) space *= TRAIT_RANGE[i];
    for (int i = 0; i < n; i++) {
        char name[32];
        std::snprintf(name, sizeof name, "agent-%05d", i);
        const Traits t = traitsFromSeed(name);
        uint8_t rec[TRAIT_FIELDS];
        t.record(rec);
        records.insert(std::string((const char*)rec, TRAIT_FIELDS));
        outlines.insert(std::string((const char*)rec, 2) + (char)rec[4] + (char)rec[7] + (char)rec[8]);   // head, torso, ears, hat, acc
        faces.insert(std::string() + (char)rec[5] + (char)rec[6] + (char)rec[9]);                        // eyes, mouth, pattern
        c.build(t);
        uint32_t crc = ~0u;
        for (int s = 0; s < StateCount; s++) {
            uint8_t out[PIXELS];
            c.render((State)s, Ctx{123456, 789, 200, false}, out);
            crc = crc32(out, PIXELS, crc);
        }
        shapes.insert(crc);
        looks.insert(((uint64_t)crc << 8) | t.palette);
    }
    std::printf("fields: ");
    for (int i = 0; i < TRAIT_FIELDS; i++) std::printf("%d%s", TRAIT_RANGE[i], i + 1 < TRAIT_FIELDS ? " " : "\n");
    std::printf("combination space: %.4g\n", space);
    std::printf("seeds: %d\n", n);
    std::printf("distinct trait records: %zu\n", records.size());
    std::printf("distinct rendered characters (all states, with palette): %zu\n", looks.size());
    std::printf("distinct rendered shapes (all states, palette ignored): %zu\n", shapes.size());
    std::printf("distinct silhouettes (head, torso, ears, hat, accessory): %zu\n", outlines.size());
    std::printf("distinct faces (eyes, mouth, pattern): %zu\n", faces.size());
    return 0;
}

static void put16(uint16_t v) {
    std::putchar(v & 0xFF);
    std::putchar(v >> 8);
}

static int serve() {
    static Character c;
    char line[512];
    while (std::fgets(line, sizeof line, stdin)) {
        char cmd = line[0];
        if (cmd == 'I') {
            std::putchar(W);
            std::putchar(H);
            std::putchar(SlotCount);
            std::putchar(TRAIT_FIELDS);
            for (int i = 0; i < TRAIT_FIELDS; i++) std::putchar(TRAIT_RANGE[i]);
            std::putchar(StateCount);
            std::putchar(ReactionCount);
        } else if (cmd == 'V') {
            char name[128];
            unsigned n = 0;
            if (std::sscanf(line + 1, " %127s %u", name, &n) != 2) return 2;
            const Traits t = traitsForVariant(std::strcmp(name, "-") == 0 ? "" : name, (uint16_t)n);
            for (int i = 0; i < TRAIT_FIELDS; i++) std::putchar(t.get(i));
            put16(t.seed & 0xFFFF);
            put16(t.seed >> 16);
        } else if (cmd == 'B') {
            unsigned fam = 0, light = 0;
            if (std::sscanf(line + 1, " %u %u", &fam, &light) != 2 || fam >= THEME_COUNT) return 2;
            put16(light ? THEMES[fam].light.ink[(int)Ink::Bg] : THEMES[fam].dark.ink[(int)Ink::Bg]);
        } else if (cmd == 'F') {
            char hexrec[64];
            unsigned long seed = 0;
            unsigned state = 0, t = 0, stateT = 0, level = 0, stt = 0, react = 0, reactT = 0, bg = 0;
            if (std::sscanf(line + 1, " %63s %lu %u %u %u %u %u %u %u %x", hexrec, &seed, &state, &t,
                            &stateT, &level, &stt, &react, &reactT, &bg) != 10 ||
                std::strlen(hexrec) != (size_t)TRAIT_FIELDS * 2 || state >= StateCount)
                return 2;
            Traits tr;
            tr.seed = (uint32_t)seed;
            for (int i = 0; i < TRAIT_FIELDS; i++) {
                char b[3] = {hexrec[2 * i], hexrec[2 * i + 1], 0};
                *tr.at(i) = (uint8_t)std::strtol(b, nullptr, 16);
            }
            c.build(tr);
            uint8_t out[PIXELS];
            c.render((State)state, Ctx{t, stateT, (uint8_t)level, stt != 0, (uint8_t)react, reactT}, out);
            uint16_t pal[SlotCount];
            paletteFor(tr, (uint16_t)bg, pal);
            for (int i = 0; i < SlotCount; i++) put16(pal[i]);
            std::fwrite(out, 1, PIXELS, stdout);
        } else {
            return 2;
        }
        std::fflush(stdout);
    }
    return 0;
}

int main(int argc, char** argv) {
    if (argc == 3 && std::strcmp(argv[1], "stats") == 0) return stats(std::atoi(argv[2]));
    if (argc == 2 && std::strcmp(argv[1], "serve") == 0) return serve();
    std::fprintf(stderr, "usage: avatar_preview stats <n> | serve\n");
    return 2;
}
