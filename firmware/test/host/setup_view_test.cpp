// Host tests for the Setup tab's rows and viewport (setup_view.h): every row
// but the rule is selectable and scrolls into view at every font family x size
// and 1..4 agents. Run: firmware/test/host/run.sh
#include "setup_view.h"

#include <cstdio>
#include <set>
#include <string>

#include "fontpick.h"
#include "logo.h"
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
#define CHECKF(cond, what)                                                         \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s  [%s]\n", __FILE__,      \
                         __LINE__, #cond, (what));                                 \
            failures++;                                                            \
        }                                                                          \
    } while (0)

using namespace setupview;

static const int SCREEN_H = 170;
static const int CONTENT_Y = 2;

static void layoutKinds() {
    for (int agents = 1; agents <= 4; agents++) {
        const Layout l{agents};
        for (int r = 0; r < agents; r++) CHECK(l.kind(r) == Kind::Agent);
        CHECK(l.kind(agents) == Kind::Screen);
        CHECK(l.kind(agents + 1) == Kind::Change);
        CHECK(l.kind(agents + 2) == Kind::Restart && isAction(Kind::Restart));
        CHECK(l.kind(agents + 3) == Kind::Logo);
        CHECK(l.kind(agents + 4) == Kind::Rule);
        for (int r = l.firstInfo(); r < l.total(); r++) {
            CHECK(l.kind(r) == Kind::Info);
            CHECK(l.infoIndex(r) == r - l.firstInfo());
        }
        CHECK(l.infoIndex(l.firstInfo()) == 0 && l.infoIndex(l.total() - 1) == INFO_ROWS - 1);
    }
}

static void moves() {
    const Layout l{2};          // rows: 0,1 agents; 2 screen; 3 change; 4 restart; 5 logo; 6 rule; 7..12 info
    CHECK(move(l, 0, 0) == 0);
    CHECK(move(l, 0, 1) == 1 && move(l, 0, 4) == 4);
    CHECK(move(l, 4, 1) == 7);                          // hops the logo and the rule
    CHECK(move(l, 7, -1) == 4);
    CHECK(move(l, 0, 100) == l.total() - 1);            // stops at the ends
    CHECK(move(l, l.total() - 1, 5) == l.total() - 1);
    CHECK(move(l, 1, -5) == 0);
    CHECK(move(l, 0, -1) == 0);
    CHECK(move(l, 0, 6) == 8);                          // a multi-step move skips them too
    CHECK(move(l, 9, -5) == 2);
    CHECK(move(l, -3, 0) == 0 && move(l, 99, 0) == l.total() - 1);   // out-of-range selection is clamped
    CHECK(move(l, 5, 0) == 7 && move(l, 6, 0) == 7);    // never rests on the logo or the rule
}

static void viewport() {
    // Layout{1}: 0 agent, 1 screen, 2 change, 3 restart (buttons, 30 px); 4 logo (40 px);
    // 5 rule, 6..11 info (20 px).
    const Layout l{1};
    const Pitch p{20, 30, 40};
    CHECK(p.of(l, 0) == 30 && p.of(l, 3) == 30 && p.of(l, 4) == 40 && p.of(l, 5) == 20);
    CHECK(viewTop(l, p, 0, 0, 100) == 0);
    CHECK(viewTop(l, p, 2, 0, 100) == 0);               // three buttons fit
    CHECK(viewTop(l, p, 3, 0, 100) == 1);               // the fourth pushes the first out
    CHECK(viewTop(l, p, 6, 0, 100) == 4);               // just far enough (the logo may lead)
    CHECK(viewTop(l, p, 2, 6, 100) == 2);               // back up
    CHECK(viewTop(l, p, 11, 0, 100) == 7);              // last page: rows 7..11 fill it
    CHECK(viewTop(l, p, 7, 11, 100) == 7);              // a stale top is pulled in
    CHECK(viewTop(l, p, 11, 3, 1000) == 0);             // everything fits
    CHECK(viewTop(l, p, 10, 0, 10) == 10 && viewTop(l, p, 0, 10, 10) == 0);   // one row at a time
}

// The Setup tab's pitches and viewport for a body line box `box`, as menu.cpp
// computes them (the logo row: the logo at 2x plus 8 px).
static Pitch pitchFor(int box) {
    const UiMetrics u = uiMetricsFor(box);
    return {u.row, u.button, 2 * logo::H + 8};
}
static int viewFor(int box) {
    const UiMetrics u = uiMetricsFor(box);
    return SCREEN_H - u.status - (CONTENT_Y + u.header + 2);
}

// Walks the wheel one click at a time down and back up: every row but the
// logo and the rule is selected and always shown whole inside the viewport.
static void reachable(int box, int agents, const char* name) {
    const Layout l{agents};
    const Pitch p = pitchFor(box);
    const int view = viewFor(box);
    CHECKF(view >= p.button && view >= p.logo, name);   // a button and the logo always fit
    std::set<int> seen;
    int sel = 0, top = 0;
    top = viewTop(l, p, sel, top, view);
    for (int pass = 0; pass < 2; pass++) {
        const int dir = pass == 0 ? 1 : -1;
        for (int i = 0; i < l.total() + 2; i++) {
            sel = move(l, sel, dir);
            top = viewTop(l, p, sel, top, view);
            CHECKF(selectable(l.kind(sel)), name);
            CHECKF(sel >= top && p.span(l, top, sel) <= view, name);
            seen.insert(sel);
        }
    }
    for (int r = 0; r < l.total(); r++)
        if (selectable(l.kind(r))) CHECKF(seen.count(r) == 1, name);
    // The last info row (Battery) is reachable, and the list ends on it.
    sel = move(l, 0, 1000);
    top = viewTop(l, p, sel, 0, view);
    CHECKF(l.kind(sel) == Kind::Info && l.infoIndex(sel) == INFO_ROWS - 1, name);
    CHECKF(p.span(l, top, l.total() - 1) <= view, name);
    CHECKF(top == 0 || p.span(l, top - 1, l.total() - 1) > view, name);   // no blank tail
    // Speech (info row 4), the one that once went missing, is selectable and shown.
    const int speech = l.firstInfo() + 4;
    top = viewTop(l, p, speech, 0, view);
    CHECKF(speech >= top && p.span(l, top, speech) <= view, name);
}

static void everyFont() {
    int checked = 0;
    for (size_t i = 0; i < FONT_FAMILY_COUNT; i++) {
        for (int s = 0; s < FONT_SIZE_COUNT; s++) {
            const std::string name =
                std::string(FONT_FAMILIES[i].name) + " " + fontSizeLetter((uint8_t)s);
            for (int agents = 1; agents <= 4; agents++) {
                reachable(FONT_FAMILIES[i].size[s].box, agents, name.c_str());
                checked++;
            }
        }
    }
    CHECK(checked == (int)FONT_FAMILY_COUNT * FONT_SIZE_COUNT * 4);
    // Also every plausible line box, not only the shipped ones.
    for (int box = 8; box <= 40; box++)
        for (int agents = 1; agents <= 4; agents++) reachable(box, agents, "box sweep");
}

int main() {
    layoutKinds();
    moves();
    viewport();
    everyFont();
    if (failures) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::puts("setup_view_test: ok");
    return 0;
}
