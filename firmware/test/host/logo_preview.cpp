// Host harness around the firmware's own logo.h: renders frames for
// firmware/tools/logo_preview.py (the logo on every theme background). Not a
// test; nothing here models the logo.
//
//   logo_preview themes              one line per theme: name mode bg text muted accent (RGB565)
//   logo_preview webbg               the setup web page's logo background: RRGGBB and RGB565
//   logo_preview frame <bg> [art]    the logo on background bg (RGB565): "w h", the SlotCount
//                                    palette colors, then h rows of w hex digits (slots);
//                                    art is a text file of art rows (';' starts a comment line),
//                                    default the firmware's own
#include "logo.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace logo;

static int themes() {
    for (uint8_t f = 0; f < THEME_COUNT; f++)
        for (int light = 0; light < 2; light++) {
            const Palette& p = light ? THEMES[f].light : THEMES[f].dark;
            std::string name = THEMES[f].name;
            for (char& c : name) if (c == ' ') c = '_';
            std::printf("%s %s %u %u %u %u\n", name.c_str(), light ? "light" : "dark",
                        p.ink[(int)Ink::Bg], p.ink[(int)Ink::Text], p.ink[(int)Ink::Muted],
                        p.ink[(int)Ink::Accent]);
        }
    return 0;
}

static int frame(uint16_t bg, const char* artFile) {
    std::vector<std::string> art;
    if (artFile) {
        std::ifstream in(artFile);
        for (std::string line; std::getline(in, line);) {
            if (line.empty() || line[0] == ';') continue;
            art.push_back(line);
        }
    } else {
        for (int y = 0; y < ART_H; y++) art.push_back(ROWS[y]);
    }
    if (art.empty()) { std::fprintf(stderr, "no art\n"); return 1; }
    const int w = (int)art[0].size(), h = (int)art.size();
    std::vector<const char*> rows;
    for (const std::string& r : art) {
        if ((int)r.size() != w) { std::fprintf(stderr, "ragged art row: %s\n", r.c_str()); return 1; }
        for (char c : r)
            if (slotOf(c) == SlotCount) { std::fprintf(stderr, "bad art char '%c'\n", c); return 1; }
        rows.push_back(r.c_str());
    }
    std::vector<uint8_t> px((size_t)frameW(w) * frameH(h));
    render(rows.data(), w, h, px.data());
    uint16_t pal[SlotCount];
    paletteFor(bg, pal);
    std::printf("%d %d\n", frameW(w), frameH(h));
    for (int i = 0; i < SlotCount; i++) std::printf("%u%c", pal[i], i + 1 < SlotCount ? ' ' : '\n');
    for (int y = 0; y < frameH(h); y++) {
        for (int x = 0; x < frameW(w); x++) std::printf("%x", px[y * frameW(w) + x]);
        std::printf("\n");
    }
    return 0;
}

int main(int argc, char** argv) {
    if (argc >= 2 && !std::strcmp(argv[1], "themes")) return themes();
    if (argc >= 2 && !std::strcmp(argv[1], "webbg")) {
        std::printf("%06x %u\n", (unsigned)WEB_BG, (unsigned)rgb565(WEB_BG));
        return 0;
    }
    if (argc >= 3 && !std::strcmp(argv[1], "frame"))
        return frame((uint16_t)std::atoi(argv[2]), argc >= 4 ? argv[3] : nullptr);
    std::fprintf(stderr, "usage: logo_preview themes | webbg | frame <bg565> [art.txt]\n");
    return 2;
}
