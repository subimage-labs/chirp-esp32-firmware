#include "setup_view.h"
#include <set>

#include <cstdio>
static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { \
    std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
    ++failures; } } while (0)

using namespace setupview;
int main() {
    for (int agents = 0; agents <= 4; ++agents) {
        Layout l(agents, true), plain(agents), off(agents, false);
        CHECK(l.total() == agents + 6 + INFO_ROWS);
        CHECK(l.screenRow() == agents && l.voiceRow() == agents + 1);
        CHECK(l.changeRow() == agents + 2 && l.restartRow() == agents + 3);
        CHECK(l.logoRow() == agents + 4 && l.ruleRow() == agents + 5 && l.firstInfo() == agents + 6);
        int voices = 0;
        const Kind tail[] = {Kind::Screen, Kind::Voice, Kind::Change, Kind::Restart, Kind::Logo, Kind::Rule};
        for (int r = 0; r < l.total(); ++r) {
            if (l.kind(r) == Kind::Voice) ++voices;
            if (r < agents) CHECK(l.kind(r) == Kind::Agent);
            else if (r < agents + 6) CHECK(l.kind(r) == tail[r - agents]);
            else { CHECK(l.kind(r) == Kind::Info); CHECK(l.infoIndex(r) == r - l.firstInfo()); }
        }
        CHECK(voices == 1);
        CHECK(l.infoIndex(l.firstInfo()) == 0 && l.infoIndex(l.total() - 1) == INFO_ROWS - 1);
        CHECK(plain.voiceRow() == -1 && off.voiceRow() == -1);
        CHECK(plain.total() == agents + 5 + INFO_ROWS && off.total() == plain.total());
        for (int r = 0; r < plain.total(); ++r) {
            CHECK(plain.kind(r) != Kind::Voice && off.kind(r) == plain.kind(r));
            CHECK(plain.kind(r) == l.kind(r <= plain.screenRow() ? r : r + 1));
        }
    }
    CHECK(isAction(Kind::Voice) && selectable(Kind::Voice));
    Layout l(2, true);
    CHECK(move(l, 2, 1) == 3 && move(l, 3, 1) == 4 && move(l, 3, -1) == 2);
    CHECK(move(l, 5, 1) == 8 && move(l, 8, -1) == 5);
    CHECK(move(l, 0, 2) == 2 && move(l, 0, 3) == 3);
    CHECK(move(l, 0, 100) == 13 && move(l, -3, 0) == 0 && move(l, 99, 0) == 13);
    CHECK(move(l, 6, 0) == 8 && move(l, 7, 0) == 8);
    std::set<int> seen; int sel = 0, voices = 0;
    for (int i = 0; i < l.total(); ++i) {
        CHECK(seen.insert(sel).second);
        if (l.kind(sel) == Kind::Voice) ++voices;
        int next = move(l, sel, 1);
        if (next == sel) break;
        sel = next;
    }
    CHECK(sel == l.total() - 1 && voices == 1);
    for (int r = 0; r < l.total(); ++r) CHECK((seen.count(r) != 0) == selectable(l.kind(r)));
    Layout zero(0, true);
    CHECK(zero.screenRow() == 0 && zero.voiceRow() == 1);
    CHECK(move(zero, 0, 1) == 1 && move(zero, 1, -1) == 0);
    Layout one(1, true); Pitch p{20, 30, 40};
    CHECK(p.of(one, one.voiceRow()) == 30);
    CHECK(p.span(one, 0, one.total() - 1) == 330);
    CHECK(viewTop(one, p, 3, 0, 100) == 1 && viewTop(one, p, 0, 0, 100) == 0);
    const int last = one.total() - 1, top = viewTop(one, p, last, 0, 100);
    CHECK(top > 0 && p.span(one, top, last) <= 100);
    if (top > 0) CHECK(p.span(one, top - 1, last) > 100);
    return failures ? 1 : 0;
}
