// Host tests for the view-switch scroll anchor. Run: firmware/test/host/run.sh
#include "anchor.h"

#include <cstdio>

static int failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,   \
                         __LINE__, #cond);                                \
            failures++;                                                   \
        }                                                                 \
    } while (0)

// A turn: question rows 10..12, rule 13, answer rows 14..23.
static const PartRows R = {10, 3, 14, 10};

static void rowsToAnchor() {
    ChatAnchor a;
    CHECK(anchorInTurn(R, 4, 10, a) && !a.newest && a.turn == 4 && a.part == 0 && a.permille == 0);
    CHECK(anchorInTurn(R, 4, 12, a) && a.part == 0 && a.permille == 666);
    CHECK(anchorInTurn(R, 4, 13, a) && a.part == 1 && a.permille == 0);   // the rule: the answer's start
    CHECK(anchorInTurn(R, 4, 19, a) && a.part == 1 && a.permille == 500);
    CHECK(anchorInTurn(R, 4, 23, a) && a.part == 1 && a.permille == 900);
    CHECK(!anchorInTurn(R, 4, 24, a));                                   // past the turn
    CHECK(anchorInTurn(R, 4, 5, a) && a.part == 0 && a.permille == 0);   // before it: its start
}

// The same place comes back from the same rows, and lands proportionally in
// a part wrapped to a different number of lines.
static void roundTrip() {
    for (int row = R.q; row < R.a + R.an; row++) {
        if (row == R.q + R.qn) continue;                                 // the rule maps to the answer
        ChatAnchor a;
        CHECK(anchorInTurn(R, 0, row, a));
        CHECK(anchorRow(R, a) == row);
    }
    const PartRows narrow = {0, 6, 7, 20};                               // the bubble wraps longer
    ChatAnchor a;
    anchorInTurn(R, 0, 19, a);                                           // halfway into the answer
    CHECK(anchorRow(narrow, a) == 7 + 10);
    anchorInTurn(R, 0, 11, a);                                           // a third into the question
    CHECK(anchorRow(narrow, a) == 2);
}

static void edges() {
    CHECK(anchorNewest().newest);
    CHECK(permilleOf(0, 0) == 0 && permilleOf(5, 0) == 0 && permilleOf(9, 9) == 999);
    CHECK(lineAt(999, 3) == 2 && lineAt(0, 3) == 0 && lineAt(500, 0) == 0);
    const PartRows empty = {30, 1, 32, 0};                               // no answer text yet
    ChatAnchor a;
    CHECK(anchorInTurn(empty, -1, 30, a) && a.turn == -1 && a.part == 0);
    CHECK(anchorRow(empty, {false, -1, 1, 500}) == 32);
}

int main() {
    rowsToAnchor();
    roundTrip();
    edges();
    if (failures) {
        std::fprintf(stderr, "anchor_test: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("anchor_test: ok\n");
    return 0;
}
