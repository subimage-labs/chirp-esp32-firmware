// anchor.h — the place in the chat that a view switch keeps: which turn, its
// question or answer, and how far into that part (a fraction, since the chat
// view and the character view's bubbles wrap to different widths), or the
// newest when the view was following. Pure (no Arduino), host-tested; ui.cpp
// maps it to and from wrapped rows, character.cpp to and from bubbles.
#pragma once

#include <stdint.h>

struct ChatAnchor {
    bool newest;        // the view followed the newest: so does the next one
    int turn;           // completed-turn index; -1 the in-flight turn
    uint8_t part;       // 0 question, 1 answer
    uint16_t permille;  // how far into the part (0..999)
};

inline ChatAnchor anchorNewest() { return {true, 0, 0, 0}; }

// A turn's rows in a wrapped list: question rows [q, q + qn), then a rule,
// then answer rows [a, a + an).
struct PartRows {
    int q, qn, a, an;
};

inline uint16_t permilleOf(int line, int lines) {
    if (lines <= 0 || line <= 0) return 0;
    if (line >= lines) return 999;
    return (uint16_t)(line * 1000 / lines);
}
inline int lineAt(uint16_t permille, int lines) {
    if (lines <= 0) return 0;
    const int l = ((int)permille * lines + 500) / 1000;   // nearest: permilleOf floors
    return l < lines ? l : lines - 1;
}

// The anchor of `row` when it falls in turn `turn` (its question, the rule
// after it, or its answer); false when the row is past the turn.
inline bool anchorInTurn(const PartRows& r, int turn, int row, ChatAnchor& out) {
    if (row >= r.a + r.an) return false;
    if (row < r.q + r.qn) {
        out = {false, turn, 0, permilleOf(row < r.q ? 0 : row - r.q, r.qn)};
    } else {                                       // the rule or the answer
        out = {false, turn, 1, permilleOf(row < r.a ? 0 : row - r.a, r.an)};
    }
    return true;
}

// The row an anchor puts at the top of the view.
inline int anchorRow(const PartRows& r, const ChatAnchor& a) {
    return a.part == 0 ? r.q + lineAt(a.permille, r.qn) : r.a + lineAt(a.permille, r.an);
}
