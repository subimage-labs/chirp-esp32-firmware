// Host tests for the generated font arrays (VLW layout as TFT_eSPI parses it),
// the font choice (fontpick.h), the layout derived from the line box, and text
// folding. Every family x size is checked. Run: firmware/test/host/run.sh
#include "fontpick.h"
#include "battery.h"
#include "setup_form.h"

#include <algorithm>
#include <cstdio>
#include <set>
#include <vector>
#include <string>

#include "../../src/fonts/font_data.cpp"

static int failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,   \
                         __LINE__, #cond);                                \
            failures++;                                                   \
        }                                                                 \
    } while (0)
// Same, naming the cut that failed.
#define CHECKF(cond, cut)                                                          \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s  [%s]\n", __FILE__,      \
                         __LINE__, #cond, (cut));                                  \
            failures++;                                                            \
        }                                                                          \
    } while (0)

static const int SCREEN_W = 320, SCREEN_H = 170;
static const int CONTENT_Y = 2;

// Terminus is a bitmap font: native hand-drawn strikes, 1-bit, a fixed cell per cut.
static bool isBitmap(const FontFamily& f) { return strcmp(f.name, "Terminus") == 0; }

// The cuts of every family, named for failure messages.
struct NamedCut {
    const FontFamily* fam;
    const FontCut* cut;
    int size;             // FONT_SIZE_*, or -1 for the title cut
    std::string name;
};
static std::vector<NamedCut> allCuts() {
    std::vector<NamedCut> v;
    for (size_t i = 0; i < FONT_FAMILY_COUNT; i++) {
        const FontFamily& f = FONT_FAMILIES[i];
        for (int s = 0; s < FONT_SIZE_COUNT; s++)
            v.push_back({&f, &f.size[s], s, std::string(f.name) + " " + fontSizeLetter((uint8_t)s)});
        v.push_back({&f, &f.title, -1, std::string(f.name) + " title"});
    }
    return v;
}

// Reads a cut the way TFT_eSPI::loadMetrics does and checks what the drawing
// code relies on.
static void checkVlw(const NamedCut& nc) {
    const FontCut& f = *nc.cut;
    const char* n = nc.name.c_str();
    const uint8_t* d = f.vlw;
    CHECKF(vlwBe32(d, 0) == f.glyphs, n);
    CHECKF(vlwBe32(d, 4) == 11, n);                               // vlw version
    const int ascent = (int)vlwBe32(d, 16), descent = (int)vlwBe32(d, 20);
    CHECKF(ascent + descent == f.box, n);
    CHECKF(f.box > 0 && f.box <= FONT_MAX_BOX, n);
    size_t bitmaps = 24 + 28 * (size_t)f.glyphs;
    uint32_t prev = 0;
    int maxDescent = descent;                                     // loadMetrics' maxDescent rule
    const bool bitmap = isBitmap(*nc.fam);
    int cell = -1;                                                // the one advance of a bitmap cut
    for (size_t i = 0; i < f.glyphs; i++) {
        const size_t at = 24 + 28 * i;
        const uint32_t code = vlwBe32(d, at);
        const int h = (int)vlwBe32(d, at + 4), w = (int)vlwBe32(d, at + 8);
        const int adv = (int)vlwBe32(d, at + 12);
        const int dy = (int)(int32_t)vlwBe32(d, at + 16), dx = (int)(int32_t)vlwBe32(d, at + 20);
        CHECKF(code > prev, n);                                   // ascending: binary-searchable
        prev = code;
        CHECKF(w >= 0 && w < 256 && h >= 0 && h < 256 && adv > 0 && adv < 256, n);
        CHECKF(dx >= -128 && dx < 128 && dy >= -32768 && dy < 32768, n);
        if (h) {
            CHECKF(dy <= ascent, n);                              // nothing pokes above the box
            CHECKF(h - dy <= descent, n);                         // nor hangs below it
            CHECKF(dy - h >= -descent, n);
            if (h - dy > maxDescent) maxDescent = h - dy;
            CHECKF(dx + w <= adv + 8 && dx >= -8, n);             // ink stays near its cell
        }
        if (bitmap) {
            for (int k = 0; k < w * h; k++) {                     // 1-bit: alpha 0 or 255
                const uint8_t a = d[bitmaps + k];
                CHECKF(a == 0 || a == 255, n);
            }
            if (cell < 0) cell = adv;
            CHECKF(adv == cell, n);                               // monospace: one advance
            if (h) CHECKF(dx >= 0 && dx + w <= cell, n);          // ink stays inside its cell
        }
        bitmaps += (size_t)w * h;
    }
    CHECKF(maxDescent == descent, n);
    if (bitmap) {                                                 // only the strikes drawn at whole cell sizes
        const int want = f.box == 14 ? 8 : f.box == 16 ? 8 : f.box == 20 ? 10 : f.box == 24 ? 12 : -1;
        CHECKF(want > 0 && cell == want, n);                      // 8x14, 8x16, 10x20, 12x24
    }
    CHECKF(bitmaps < f.bytes, n);                                 // bitmaps + name trailer fit
    CHECKF(d[f.bytes - 1] == 1, n);                               // smooth flag
}

static void fonts() {
    CHECK(FONT_FAMILY_COUNT == 4);
    for (const NamedCut& nc : allCuts()) {
        checkVlw(nc);
        const char* n = nc.name.c_str();
        if (nc.size >= 0) {
            for (uint32_t c = 0x21; c < 0x7F; c++) CHECKF(vlwHas(*nc.cut, c), n);
            CHECKF(vlwHas(*nc.cut, 0xE9) && vlwHas(*nc.cut, 0xB0), n);   // é °
            CHECKF(vlwHas(*nc.cut, 0xA0), n);                     // the UI draws spaces as U+00A0
            CHECKF(!vlwHas(*nc.cut, 0x20), n);                    // U+0020 is never looked up
            CHECKF(vlwAdvance(*nc.cut, ' ') > 0, n);
        } else {
            for (uint32_t c = 0x21; c < 0x7F; c++) CHECKF(vlwHas(*nc.cut, c), n);   // the agent's name
            CHECKF(vlwAdvance(*nc.cut, ' ') > 0, n);
            CHECKF(vlwWidth(*nc.cut, "READY") > 0 && vlwWidth(*nc.cut, "READY") <= SCREEN_W - 20, n);
        }
    }
    // Sizes grow: line box and advance never shrink from S to M to L, and L > S.
    for (size_t i = 0; i < FONT_FAMILY_COUNT; i++) {
        const FontFamily& f = FONT_FAMILIES[i];
        CHECKF(f.size[0].box <= f.size[1].box && f.size[1].box <= f.size[2].box, f.name);
        CHECKF(f.size[0].box < f.size[2].box, f.name);
        // Every step up grows the line box or the text width (a bitmap font's S and M
        // share a cell width and differ in height), and never shrinks either.
        for (int k = 0; k < 2; k++) {
            const int w0 = vlwWidth(f.size[k], "Hamburgefonstiv"), w1 = vlwWidth(f.size[k + 1], "Hamburgefonstiv");
            CHECKF(w0 <= w1 && (w0 < w1 || f.size[k].box < f.size[k + 1].box), f.name);
        }
        CHECKF(vlwWidth(f.size[0], "Hamburgefonstiv") < vlwWidth(f.size[2], "Hamburgefonstiv"), f.name);
    }
    // Names are unique and non-empty (the choice is stored by name).
    std::set<std::string> names;
    for (size_t i = 0; i < FONT_FAMILY_COUNT; i++) {
        CHECK(FONT_FAMILIES[i].name[0]);
        CHECK(names.insert(FONT_FAMILIES[i].name).second);
    }
    // Display order is alphabetical, like the themes.
    for (size_t i = 1; i < FONT_FAMILY_COUNT; i++)
        CHECK(strcmp(FONT_FAMILIES[i - 1].name, FONT_FAMILIES[i].name) < 0);
}

static void choice() {
    CHECK(fontFind(FONT_DEFAULT_FAMILY) >= 0);
    CHECK(fontFind(FONT_FIXED_FAMILY) >= 0);
    CHECK(strcmp(FONT_FAMILIES[fontFind("Inter")].name, "Inter") == 0);
    CHECK(fontFind("inter") < 0 && fontFind("") < 0 && fontFind(nullptr) < 0 && fontFind("Nope") < 0);
    CHECK(fontFind("Terminus") >= 0 && fontFind("Paper Mono") >= 0 && fontFind("Atkinson") >= 0);

    CHECK(fontSizeFind("S") == FONT_SIZE_S && fontSizeFind("M") == FONT_SIZE_M &&
          fontSizeFind("L") == FONT_SIZE_L);
    CHECK(fontSizeFind("l") == FONT_SIZE_L);
    CHECK(fontSizeFind("") < 0 && fontSizeFind("X") < 0 && fontSizeFind("MM") < 0 &&
          fontSizeFind(nullptr) < 0 && fontSizeFind("1") < 0);
    for (uint8_t s = 0; s < FONT_SIZE_COUNT; s++)
        CHECK(fontSizeFind(std::string(1, fontSizeLetter(s)).c_str()) == s);   // letter round trip
    CHECK(fontSizeNext(FONT_SIZE_S) == FONT_SIZE_M && fontSizeNext(FONT_SIZE_M) == FONT_SIZE_L &&
          fontSizeNext(FONT_SIZE_L) == FONT_SIZE_S);

    // Stored choice -> shown choice: each half falls back on its own.
    FontChoice c = fontChoiceFrom("Paper Mono", "L");
    CHECK(strcmp(FONT_FAMILIES[c.family].name, "Paper Mono") == 0 && c.size == FONT_SIZE_L);
    c = fontChoiceFrom("Paper Mono", "bogus");
    CHECK(strcmp(FONT_FAMILIES[c.family].name, "Paper Mono") == 0 && c.size == FONT_SIZE_DEFAULT);
    c = fontChoiceFrom("Gone", "S");
    CHECK(strcmp(FONT_FAMILIES[c.family].name, FONT_DEFAULT_FAMILY) == 0 && c.size == FONT_SIZE_S);
    c = fontChoiceFrom(nullptr, nullptr);
    CHECK(strcmp(FONT_FAMILIES[c.family].name, FONT_DEFAULT_FAMILY) == 0 &&
          c.size == FONT_SIZE_DEFAULT && c.size == FONT_SIZE_M);
    CHECK(strcmp(FONT_DEFAULT_FAMILY, "Atkinson") == 0);
    // An index saved by an older table would not be a name: a digit string is unknown.
    c = fontChoiceFrom("2", "M");
    CHECK(strcmp(FONT_FAMILIES[c.family].name, FONT_DEFAULT_FAMILY) == 0);
}

static void metricsDerive() {
    const UiMetrics m = uiMetricsFor(18);                          // the layout the screens were drawn for
    CHECK(m.line == 18 && m.row == 20 && m.status == 22 && m.header == 24 && m.button == 28 &&
          m.pinned == 34);
    // The portal draws in Atkinson M, laid out on 18 px lines.
    CHECK(FONT_FAMILIES[fontFind(FONT_FIXED_FAMILY)].size[FONT_SIZE_M].box == 18);

    for (const NamedCut& nc : allCuts()) {
        if (nc.size < 0) continue;
        const char* n = nc.name.c_str();
        const UiMetrics u = uiMetricsFor(nc.cut->box);
        CHECKF(u.line == nc.cut->box && u.row > u.line && u.status > u.line, n);
        CHECKF(u.button >= u.line + 8 && u.pinned > u.button, n);   // outline + padding fit
        const int bottom = SCREEN_H - u.status;
        // Transcript: at least five lines.
        CHECKF((bottom - CONTENT_Y) / u.line >= 5, n);
        // Menu lists: header, then rows (Theme, Text and Setup start at header + 2; Chats pins one).
        CHECKF((bottom - (CONTENT_Y + u.header + 2)) / u.row >= 4, n);
        CHECKF((bottom - (CONTENT_Y + u.header + u.pinned) - 2) / u.row >= 3, n);
        // The status text (3 px below the strip's rule) stays inside the strip.
        CHECKF(3 + nc.cut->box <= u.status, n);
        // The character view's bubble column (the whole height, 6 px margins)
        // shows at least four lines of an answer at once.
        CHECKF((SCREEN_H - 2 * 6 - 2 * 5) / u.line >= 4, n);
    }
}

// The tab header: Chats | Theme | Chirper | Text | Setup, drawn at the chosen
// size but at most M (UiFont::Header), must fit 320 px with a bar gap of at
// least 4 px either side at every family and size.
static void header() {
    static const char* const NAMES[] = {"Chats", "Theme", "Chirper", "Text", "Setup"};
    static const char* const UPPER[] = {"CHATS", "THEME", "CHIRPER", "TEXT", "SETUP"};
    const int n = 5;
    for (size_t i = 0; i < FONT_FAMILY_COUNT; i++) {
        const FontFamily& f = FONT_FAMILIES[i];
        for (int s = 0; s < FONT_SIZE_COUNT; s++) {
            const FontCut& cut = f.size[s < FONT_SIZE_M ? s : FONT_SIZE_M];
            const std::string label = std::string(f.name) + " " + fontSizeLetter((uint8_t)s);
            for (int sel = 0; sel < n; sel++) {             // the selected tab is in capitals
                int w[5];
                for (int k = 0; k < n; k++) w[k] = vlwWidth(cut, k == sel ? UPPER[k] : NAMES[k]);
                const int pad = headerPad(w, n, vlwWidth(cut, "|"), SCREEN_W - 12, 8, 4);
                CHECKF(pad >= 4, label.c_str());
            }
        }
    }
    int w[3] = {50, 50, 50};
    CHECK(headerPad(w, 3, 4, 320, 8, 4) == 8);                      // roomy: the widest pad
    CHECK(headerPad(w, 3, 4, 150 + 2 * (4 + 2 * 5), 8, 4) == 5);    // exactly fits at pad 5
    CHECK(headerPad(w, 3, 4, 150, 8, 4) == -1);                     // not even minPad
}

// Fixed text and the longest menu labels at every size: what has a fixed
// string fits untruncated; what is cut is cut to width, with the mark.
static void labels() {
    const int rowW = SCREEN_W - 20;                                 // drawRow's text width
    const int buttonW = SCREEN_W - 10 - 16 - 20;                    // drawButton's, beside a chevron
    for (const NamedCut& nc : allCuts()) {
        if (nc.size < 0) continue;
        const char* n = nc.name.c_str();
        const FontCut& c = *nc.cut;
        static const char* const BUTTONS[] = {"+ New chat", "Set up on phone", "View: Character",
                                              "View: Chat", "helper-agent (only agent)"};
        for (const char* r : BUTTONS) CHECKF(vlwWidth(c, r) <= buttonW, n);
        // Text tab rows: each family's name and the size marker, in that family's own face.
        for (size_t i = 0; i < FONT_FAMILY_COUNT; i++) {
            const std::string label = std::string(FONT_FAMILIES[i].name) + "  M";
            CHECKF(vlwWidth(FONT_FAMILIES[i].size[nc.size], label.c_str()) <= rowW, n);
        }
        // The info column: widest label, then room for a value of at least 150 px.
        int lw = 0;
        for (const char* l : {"Firmware", "WiFi", "Address", "Hermes", "Speech", "Battery"})
            lw = std::max(lw, vlwWidth(c, l));
        CHECKF(SCREEN_W - (6 + lw + 12) - 6 >= 150, n);

        // Long text is cut to width and marked; short text is left alone.
        auto width = [&](const char* s) { return vlwWidth(c, s); };
        const std::string longest = "Kitchen lights automation ideas for the evening and more";
        const std::string cut = fitText(longest, rowW, width);
        CHECKF(width(cut.c_str()) <= rowW, n);
        CHECKF(cut.size() > 3 && cut.compare(cut.size() - 3, 3, "\xE2\x80\xA6") == 0, n);
        CHECKF(fitText("Fits", rowW, width) == "Fits", n);
        for (const char* hint : {"Wheel=select  Encoder=open  Side=Theme",
                                 "Encoder=change settings on phone", "Encoder=switch agent (restarts)",
                                 "Wheel=font  Encoder=size M",
                                 "Ball=select  Enter=open  Click=next",
                                 "Ball=theme  Enter=light  Click=next",
                                 "Ball=font  Enter=size M  Click=next",
                                 "Ball=browse  Enter=keep  Click=next",
                                 "WiFi failed: Click 5x = Setup",
                                 "Hold the mic key to talk", "Enter to send, Click for menu"}) {
            const std::string h = fitText(hint, SCREEN_W - 12, width);   // uiMsg's rule
            CHECKF(width(h.c_str()) <= SCREEN_W - 12, n);
        }
    }
    // Cutting never splits a UTF-8 sequence, even when only multi-byte text is left.
    const FontCut& a = FONT_FAMILIES[fontFind("Atkinson")].size[FONT_SIZE_M];
    auto width = [&](const char* s) { return vlwWidth(a, s); };
    const std::string em = fitText("\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9", 20, width);
    const char* p = em.data();
    const char* end = p + em.size();
    while (p < end) CHECK(utf8Next(p, end) != 0xFFFD);
    CHECK(width(em.c_str()) <= 20);
    CHECK(fitText("anything", 3, width) == "\xE2\x80\xA6");          // nothing fits: just the mark
    CHECK(fitText("", 0, width).empty());

    // The setup portal: Atkinson M beside the QR code (text column 158 px wide).
    const FontCut& fx = FONT_FAMILIES[fontFind(FONT_FIXED_FAMILY)].size[FONT_SIZE_M];
    const int col = SCREEN_W - 162;
    for (const char* s : {"Chirp setup", "Scan, or join WiFi:", "Password:",
                          "The form opens itself;", "else browse to", "http://255.255.255.255",
                          "Phone connected", "Side key = cancel", "Waiting for a phone"})
        CHECKF(vlwWidth(fx, s) <= col, s);
    int widest = 0;                                                  // the password alphabet's widest glyph
    for (const char* k = PORTAL_PASS_ALPHABET; *k; k++)
        widest = std::max(widest, vlwAdvance(fx, (uint8_t)*k));
    CHECK(PORTAL_PASS_LEN * widest + 6 <= (size_t)col);              // its own line, indented 6 px
    CHECK(vlwWidth(fx, "Chirp-DDDD") + 6 <= col);   // portal.cpp "Chirp-%02X%02X", wide digits
}

// The battery block at the status strip's right edge, at every family and
// size: the glyph sits inside the strip and the status text keeps room.
static void batteryBlock() {
    int minRoom = 1000;
    for (size_t i = 0; i < FONT_FAMILY_COUNT; i++) {
        const FontFamily& f = FONT_FAMILIES[i];
        for (int s = 0; s < FONT_SIZE_COUNT; s++) {
            const FontCut& body = f.size[s];
            const std::string label = std::string(f.name) + " " + fontSizeLetter((uint8_t)s);
            const char* n = label.c_str();
            const BatteryLayout l = batteryLayout(body.box);
            const int status = uiMetricsFor(body.box).status;
            const int gy = 3 + (body.box - l.glyphH) / 2;           // 3 px below the strip's top
            CHECKF(gy >= 1 && gy + l.glyphH <= status, n);
            // Status text: from the left margin to BATTERY_GAP (8) left of the block.
            const int room = SCREEN_W - 6 - l.width - 8 - 6;
            CHECKF(room >= 200, n);
            minRoom = std::min(minRoom, room);
        }
    }
    std::printf("battery block: status text keeps >= %d px at every family and size\n", minRoom);
}

static std::string fold(const FontCut& c, const std::string& s) {
    return foldText(s.data(), s.size(), [&c](uint32_t cp) { return vlwHas(c, cp); });
}

static void folding() {
    for (const NamedCut& nc : allCuts()) {
        if (nc.size < 0) continue;
        const FontCut& c = *nc.cut;
        const char* n = nc.name.c_str();
        // Everything folded is something the font can draw.
        std::string all;
        for (uint32_t cp = 1; cp < 0x3000; cp++) utf8Append(all, cp);
        for (uint32_t cp = 0x1F300; cp < 0x1F340; cp++) utf8Append(all, cp);
        const std::string f = fold(c, all);
        const char* p = f.data();
        const char* end = p + f.size();
        bool boxFree = true;
        while (p < end) {
            const uint32_t cp = utf8Next(p, end);
            if (cp == '\n' || cp == ' ') continue;
            if (!vlwHas(c, cp)) boxFree = false;
        }
        CHECKF(boxFree, n);
        CHECKF(fold(c, "plain ASCII, 1+1=2.\nnext") == "plain ASCII, 1+1=2.\nnext", n);
        CHECKF(fold(c, "a\xC2\xA0" "b") == "a b", n);                // nbsp -> space
        CHECKF(fold(c, "hi \xF0\x9F\x98\x80!") == "hi !", n);        // emoji dropped
        CHECKF(fold(c, "bad\xFF" "byte") == "badbyte", n);           // malformed UTF-8
        // Typographic punctuation either survives (the font has it) or becomes ASCII.
        const std::string typo = fold(c, "it\xE2\x80\x99s \xE2\x80\x9Cq\xE2\x80\x9D \xE2\x80\x94 x\xE2\x80\xA6");
        const char* q = typo.data();
        const char* qend = q + typo.size();
        while (q < qend) {
            const uint32_t cp = utf8Next(q, qend);
            CHECKF(cp == ' ' || vlwHas(c, cp), n);
        }
        CHECKF(typo.find('s') != std::string::npos && typo.find('q') != std::string::npos, n);
    }
    // A font lacking a character falls back to ASCII look-alikes.
    const std::string s = "\xE2\x80\x9Cq\xE2\x80\x9D \xE2\x80\xA6 \xE2\x86\x92 \xE2\x82\xAC";
    CHECK(foldText(s.data(), s.size(), [](uint32_t cp) { return cp < 0x7F; }) == "\"q\" ... -> EUR");
    CHECK(fold(FONT_FAMILIES[0].size[1], "\xC5\x81\xC3\xB3" "d\xC5\xBA") == "L\xC3\xB3" "dz");   // Lódź
    CHECK(fold(FONT_FAMILIES[0].size[1], "e\xCC\x81") == "e");           // combining acute
}

static void utf8() {
    const std::string s = "a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80";
    const char* p = s.data();
    const char* end = p + s.size();
    CHECK(utf8Next(p, end) == 'a');
    CHECK(utf8Next(p, end) == 0xE9);
    CHECK(utf8Next(p, end) == 0x20AC);
    CHECK(utf8Next(p, end) == 0x1F600);
    CHECK(p == end);
    std::string out;
    for (uint32_t cp : {0x41u, 0xE9u, 0x20ACu, 0x1F600u}) utf8Append(out, cp);
    CHECK(out == "A\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80");
    const std::string trunc = "\xE2\x82";
    p = trunc.data();
    CHECK(utf8Next(p, p + trunc.size()) == 0xFFFD);
}

int main() {
    fonts();
    choice();
    metricsDerive();
    header();
    batteryBlock();
    labels();
    folding();
    utf8();
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("font: all checks passed\n");
    return 0;
}
