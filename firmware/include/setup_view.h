// setup_view.h — the Setup tab as one scrolling list, as pure logic.
//
// Rows, top to bottom: the agents, the screen choice, the voice (venice mode),
// "Set up on phone", "Restart", the chirp! logo, a dotted rule, then the info rows (firmware,
// WiFi, ...). The agents, the screen choice, "Set up on phone" and "Restart"
// are actions, drawn as buttons (taller than the plain rows); the info rows
// only show what the device runs. The wheel selects every row except the logo
// and the rule, so the info rows scroll into view however few rows the font
// leaves room for. Owner: the loop task (menu.cpp); no state here.
#pragma once

namespace setupview {

static const int INFO_ROWS = 6;     // what the device runs and talks to (menu.cpp INFO_LABELS)

enum class Kind { Agent, Screen, Voice, Change, Restart, Logo, Rule, Info };

inline bool isAction(Kind k) {
    return k == Kind::Agent || k == Kind::Screen || k == Kind::Voice || k == Kind::Change ||
           k == Kind::Restart;
}
inline bool selectable(Kind k) { return k != Kind::Logo && k != Kind::Rule; }

// `voice`: the venice voice row (venice mode only), right after the screen choice.
struct Layout {
    int agents;                     // rows [0, agents) are the agents
    bool voice;
    Layout(int agents, bool voice = false) : agents(agents), voice(voice) {}
    int v() const { return voice ? 1 : 0; }
    int screenRow() const { return agents; }
    int voiceRow() const { return voice ? agents + 1 : -1; }
    int changeRow() const { return agents + 1 + v(); }
    int restartRow() const { return agents + 2 + v(); }
    int logoRow() const { return agents + 3 + v(); }
    int ruleRow() const { return agents + 4 + v(); }
    int firstInfo() const { return agents + 5 + v(); }
    int total() const { return agents + 5 + v() + INFO_ROWS; }

    Kind kind(int row) const {
        if (row < agents) return Kind::Agent;
        if (row == screenRow()) return Kind::Screen;
        if (row == voiceRow()) return Kind::Voice;
        if (row == changeRow()) return Kind::Change;
        if (row == restartRow()) return Kind::Restart;
        if (row == logoRow()) return Kind::Logo;
        if (row == ruleRow()) return Kind::Rule;
        return Kind::Info;
    }
    int infoIndex(int row) const { return row - firstInfo(); }
};

// Pixel pitches: a plain row, an action drawn as a button, the logo row.
struct Pitch {
    int row, button, logo;
    int of(const Layout& l, int r) const {
        const Kind k = l.kind(r);
        return isAction(k) ? button : k == Kind::Logo ? logo : row;
    }
    int span(const Layout& l, int from, int to) const {   // rows [from, to]
        int h = 0;
        for (int r = from; r <= to; r++) h += of(l, r);
        return h;
    }
};

// The selection after `steps` wheel steps (positive = down the list), one
// selectable row at a time, over the logo and the rule, stopping at the ends.
inline int move(const Layout& l, int sel, int steps) {
    const int dir = steps < 0 ? -1 : 1;
    int left = steps < 0 ? -steps : steps;
    if (sel < 0) sel = 0;
    if (sel > l.total() - 1) sel = l.total() - 1;
    while (!selectable(l.kind(sel))) sel += dir;   // the logo and rule sit between selectable rows
    while (left-- > 0) {
        int next = sel + dir;
        while (next >= 0 && next < l.total() && !selectable(l.kind(next))) next += dir;
        if (next < 0 || next >= l.total()) break;
        sel = next;
    }
    return sel;
}

// The first visible row for a viewport `view` px tall: the old top, moved
// just far enough that `sel` shows whole, and never further down than the
// top that shows the last row at the bottom.
inline int viewTop(const Layout& l, const Pitch& p, int sel, int top, int view) {
    if (sel < top) top = sel;
    while (top < sel && p.span(l, top, sel) > view) top++;
    int last = l.total() - 1;                      // the lowest top that still ends on the last row
    while (last > 0 && p.span(l, last - 1, l.total() - 1) <= view) last--;
    if (top > last) top = last;
    if (top < 0) top = 0;
    return top;
}

}  // namespace setupview
