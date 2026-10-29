// route_ball_test.cpp — routing on boards whose side key is a trackball
// click and whose tabs change by sideways flicks (board.h
// CHIRP_SIDE_SELECTS, route.h routeSideSelects): a side tap picks in the
// menu and opens it outside; flicks step tabs both ways in the menu and do
// nothing outside it. Run: test/host/run.sh
#include <cstdio>
#include <initializer_list>

#include "route.h"

static int fails = 0;
#define CHECK(cond)                                              \
    do {                                                         \
        if (!(cond)) {                                           \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            fails++;                                             \
        }                                                        \
    } while (0)

static RouteCtx ctx(UiState s, bool menu, bool holdActed = false) {
    return RouteCtx{s, menu, false, true, false, holdActed, 500, 300};
}
static InputEvent sideTap() { return InputEvent{InputType::SideRelease, 0, 0, 80, false, false}; }
static InputEvent flick(int32_t dir) { return InputEvent{InputType::Tab, dir, 0, 0, false, false}; }

int main() {
    // The side tap picks in the menu on ball boards (it steps tabs on others).
    CHECK(routeSideSelects(sideTap(), ctx(UI_IDLE, true)) == Act::MenuActivate);
    CHECK(route(sideTap(), ctx(UI_IDLE, true)) == Act::MenuNext);
    // Outside the menu it is the side tap it always was.
    CHECK(routeSideSelects(sideTap(), ctx(UI_IDLE, false)) == Act::OpenMenu);
    CHECK(routeSideSelects(sideTap(), ctx(UI_THINKING, false)) == Act::StopTurn);
    // The release ending a volume hold only clears the hold, in the menu too.
    CHECK(routeSideSelects(sideTap(), ctx(UI_IDLE, true, true)) == Act::ClearHold);
    // Flicks: tabs in the menu, both ways; nothing outside it, in any state.
    CHECK(routeSideSelects(flick(1), ctx(UI_IDLE, true)) == Act::MenuNext);
    CHECK(routeSideSelects(flick(-1), ctx(UI_IDLE, true)) == Act::MenuPrev);
    for (UiState s : {UI_IDLE, UI_RECORDING, UI_THINKING, UI_SPEAKING}) {
        CHECK(routeSideSelects(flick(1), ctx(s, false)) == Act::None);
        CHECK(routeSideSelects(flick(-1), ctx(s, false)) == Act::None);
    }
    if (fails) {
        printf("route_ball: %d check(s) failed\n", fails);
        return 1;
    }
    printf("route_ball: all checks passed\n");
    return 0;
}
