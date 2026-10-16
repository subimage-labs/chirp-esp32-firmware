// bubbles.h — the character view's chat column: comic speech bubbles stacked
// top to bottom in chat order, each as tall as its text, scrolled as one
// column. Pure (no Arduino), host-tested; character.cpp wraps the text,
// paints and scrolls.
//
// Both sides' bubbles fit their text and keep an inset free on the far side,
// so the two read apart (the agent's is the smaller: answers get the width).
// Agent bubbles hug the column's left edge with no tail (the character
// beside them may be mid-jump or at its feet); the owner's hug the right edge
// with a tail pointing right.
#pragma once

#include <stdint.h>

namespace bubbles {

enum Side : uint8_t { Agent, User };

struct Geometry {
    int colW;          // column width, scrollbar lane excluded
    int tail;          // the owner's tail depth: their body ends this far in from the right
    int inset;         // kept free on the far side of the owner's bubbles
    int agentInset;    // kept free on the far side of the agent's
    int padX, padY;    // text inset inside the border
    int lineH;         // text line pitch
    int gap;           // between bubbles
    int margin;        // above the first and below the last bubble
};

// Widest text line a bubble on that side can hold.
inline int maxTextW(const Geometry& g, Side side) {
    return g.colW - (side == User ? g.inset + g.tail : g.agentInset) - 2 * g.padX;
}

struct Box {
    int x, y, w, h;    // the body (the owner's tail hangs outside it, to the right)
};

// A bubble of `lines` text lines whose widest line is textW px, plus extraH
// px below the text (a meter band, at least bandW px wide), with its top at y.
inline Box place(const Geometry& g, Side side, int lines, int textW, int extraH, int bandW, int y) {
    const int maxW = maxTextW(g, side);
    int inner = textW > bandW ? textW : bandW;
    if (inner > maxW) inner = maxW;
    if (inner < g.lineH) inner = g.lineH;                // "4" still looks like a bubble
    Box b;
    b.w = inner + 2 * g.padX;
    b.h = lines * g.lineH + extraH + 2 * g.padY;
    b.x = side == Agent ? 0 : g.colW - g.tail - b.w;
    b.y = y;
    return b;
}

// Top of the next bubble after b.
inline int below(const Geometry& g, const Box& b) { return b.y + b.h + g.gap; }

// Height of the whole column once the last bubble is b (none: empty column).
inline int total(const Geometry& g, const Box* last) {
    return last ? last->y + last->h + g.margin : 0;
}

// Scroll offset (column px at the view's top) bounds.
inline int maxTop(int total, int view) { return total > view ? total - view : 0; }
inline int clampTop(int top, int total, int view) {
    const int m = maxTop(total, view);
    return top < 0 ? 0 : top > m ? m : top;
}

// A text line of bubble b at the view's top (a margin of space above it), and
// back: which line of b is at the top for an offset. The same reference both
// ways, so a view switch reading the place and restoring it never drifts.
inline int lineTop(const Geometry& g, const Box& b, int line) {
    return b.y + g.padY + line * g.lineH - g.margin;
}
inline int lineAtTop(const Geometry& g, const Box& b, int top) {
    const int into = top - lineTop(g, b, 0);
    return into <= 0 ? 0 : into / g.lineH;
}

// A pinned column rests at the bottom (the newest). While an answer is spoken
// it follows the speech instead: the line being spoken (lineAt, 0-based, of
// bubble b) sits one line below the view's top, so a long answer scrolls down
// as it is read out; never above b's top margin, never past the bottom.
inline int speakTop(const Geometry& g, const Box& b, int lineAt, int total, int view) {
    int top = b.y + g.padY + (lineAt - 1) * g.lineH;
    if (top < b.y - g.margin) top = b.y - g.margin;
    return clampTop(top, total, view);
}

// Which of `lines` lines the spoken position is on: `spoken` of `length`
// chars of the answer (0 = its first line, clamped to the last).
inline int lineOf(int spoken, int length, int lines) {
    if (lines <= 0 || length <= 0 || spoken <= 0) return 0;
    const int at = (int)((long long)spoken * lines / length);
    return at < lines ? at : lines - 1;
}

// Whether any row of b shows in a view of `view` px at offset top.
inline bool visible(const Box& b, int top, int view) {
    return b.y + b.h > top && b.y < top + view;
}

// The owner's tail: `rows` steps of `step` px, the last one at the body's
// bottom padding. Step i (0 = top) reaches reach(i) px out from the body's
// right edge, so the tail widens toward the bottom, like a comic panel's.
inline int tailTop(const Geometry& g, const Box& b, int rows, int step) {
    return b.y + b.h - g.padY / 2 - rows * step;
}
inline int tailReach(const Geometry& g, int i, int rows) { return g.tail * (i + 1) / rows; }

}  // namespace bubbles
