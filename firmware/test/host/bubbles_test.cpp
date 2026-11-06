// Host tests for the character view's bubble column. Run: firmware/test/host/run.sh
#include "bubbles.h"
#include "anchor.h"

#include <cstdio>
#include <initializer_list>

using namespace bubbles;

static int failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,   \
                         __LINE__, #cond);                                \
            failures++;                                                   \
        }                                                                 \
    } while (0)

// colW, tail, inset, agentInset, padX, padY, lineH, gap, margin
static const Geometry G = {166, 8, 16, 8, 6, 5, 18, 6, 4};

// Agent bubbles start at the column's left edge (no tail), the owner's end
// right before the right tail; both fit their text and never reach across
// their side's inset.
static void sides() {
    for (int tw : {1, 30, 90, 200}) {
        const Box a = place(G, Agent, 2, tw, 0, 0, 0);
        const Box u = place(G, User, 2, tw, 0, 0, 0);
        CHECK(a.x == 0);
        CHECK(a.x + a.w <= G.colW - G.agentInset);
        CHECK(u.x + u.w == G.colW - G.tail);
        CHECK(u.x >= G.inset);
    }
    CHECK(place(G, Agent, 9, 1000, 0, 0, 0).w == G.colW - G.agentInset);   // a full answer line
}

// Width follows the widest line, capped at that side's text width (answers
// get the tail's width more); height is the lines plus the band plus padding.
static void sizes() {
    const Box narrow = place(G, Agent, 1, 40, 0, 0, 10);
    CHECK(narrow.w == 40 + 2 * G.padX && narrow.h == G.lineH + 2 * G.padY && narrow.y == 10);
    const Box wide = place(G, Agent, 5, 1000, 0, 0, 0);
    CHECK(wide.w == maxTextW(G, Agent) + 2 * G.padX);
    CHECK(wide.h == 5 * G.lineH + 2 * G.padY);
    CHECK(place(G, User, 1, 1000, 0, 0, 0).w == maxTextW(G, User) + 2 * G.padX);
    const Box tiny = place(G, User, 1, 3, 0, 0, 0);             // "4": still a bubble
    CHECK(tiny.w >= G.lineH);
    const Box band = place(G, Agent, 2, 20, 26, 118, 0);        // the speaking wave under short text
    CHECK(band.w == 118 + 2 * G.padX && band.h == 2 * G.lineH + 26 + 2 * G.padY);
    const Box meter = place(G, User, 0, 0, 30, 116, 0);         // listening: bars only
    CHECK(meter.h == 30 + 2 * G.padY && meter.w == 116 + 2 * G.padX);
    CHECK(maxTextW(G, User) >= 118);                             // the meters fit either side
}

// An empty transcript has no height.
static void empty() {
    CHECK(total(G, nullptr) == 0);
}

static void scrolling() {
    CHECK(maxTop(100, 170) == 0);
    CHECK(maxTop(500, 170) == 330);
    CHECK(clampTop(-5, 500, 170) == 0);
    CHECK(clampTop(400, 500, 170) == 330);
    CHECK(clampTop(120, 500, 170) == 120);
    CHECK(clampTop(50, 100, 170) == 0);                          // short column: never scrolls
}

// While an answer is spoken the spoken line stays one line below the top:
// the column starts at the answer and walks down it, within the column.
static void followsSpeech() {
    const Box b = {0, 300, 150, 20 * G.lineH + 2 * G.padY};      // a 20-line answer at y=300
    const int total = 300 + b.h + G.margin, view = 170;
    CHECK(speakTop(G, b, 0, total, view) == 300 - G.margin);     // first line: from its top
    CHECK(speakTop(G, b, 1, total, view) == 300 + G.padY);       // line 0 at the view's top
    CHECK(speakTop(G, b, 4, total, view) == 300 + G.padY + 3 * G.lineH);
    CHECK(speakTop(G, b, 19, total, view) == maxTop(total, view)); // the end: the column's bottom
    int last = -1;
    for (int at = 0; at < 20; at++) {                             // never scrolls back up
        const int t = speakTop(G, b, at, total, view);
        CHECK(t >= last);
        last = t;
    }
    CHECK(lineOf(0, 500, 10) == 0 && lineOf(-1, 500, 10) == 0);
    CHECK(lineOf(250, 500, 10) == 5);
    CHECK(lineOf(600, 500, 10) == 9);                             // spoken text longer: last line
    CHECK(lineOf(100, 0, 10) == 0 && lineOf(100, 500, 0) == 0);
}

static void visibility() {
    const Box b = {8, 100, 60, 40};
    CHECK(visible(b, 0, 170));
    CHECK(visible(b, 139, 170));                                 // its last row at the top
    CHECK(!visible(b, 140, 170));
    CHECK(!visible({8, 300, 60, 40}, 0, 170));
    CHECK(visible({8, 169, 60, 40}, 0, 170));                    // its first row at the bottom
    CHECK(!visible({8, 170, 60, 40}, 0, 170));
}

// The tail widens downward, reaches exactly the tail depth, and ends inside
// the bubble's height.
static void tail() {
    const Box b = place(G, Agent, 3, 80, 0, 0, 20);
    const int rows = 4, step = 2;
    for (int i = 1; i < rows; i++) CHECK(tailReach(G, i, rows) > tailReach(G, i - 1, rows));
    CHECK(tailReach(G, rows - 1, rows) == G.tail);
    const int top = tailTop(G, b, rows, step);
    CHECK(top > b.y && top + rows * step <= b.y + b.h);
}

// A view switch reads the place from the column offset and restores it the
// same way: offset -> line -> anchor -> line -> offset lands where it started,
// for every line, however many switches in a row.
static void placeRoundTrip() {
    const Box b = {0, 240, 150, 12 * G.lineH + 2 * G.padY};      // a 12-line answer
    for (int line = 0; line < 12; line++) {
        const int top = lineTop(G, b, line);
        CHECK(lineAtTop(G, b, top) == line);
        int t = top;
        for (int k = 0; k < 5; k++) {                             // switch back and forth
            const ChatAnchor a = {false, 3, 1, permilleOf(lineAtTop(G, b, t), 12)};
            t = lineTop(G, b, lineAt(a.permille, 12));
        }
        CHECK(t == top);
        CHECK(lineAtTop(G, b, top + G.lineH - 1) == line);       // anywhere inside the line
    }
    CHECK(lineAtTop(G, b, 0) == 0);                               // above the bubble: its first line
}

int main() {
    sides();
    sizes();
    empty();
    scrolling();
    followsSpeech();
    visibility();
    tail();
    placeRoundTrip();
    if (failures) {
        std::fprintf(stderr, "bubbles_test: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("bubbles_test: ok\n");
    return 0;
}
