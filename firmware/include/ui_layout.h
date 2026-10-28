// ui_layout.h — the chat screen's regions on any panel: where the companion
// (the agent's avatar, name, state and level meter) goes, how large its
// avatar is drawn, and the rectangle the transcript gets. Pure (no Arduino),
// host-tested on every common ESP32 panel (ui_layout_test.cpp); ui.cpp and
// character.cpp draw into what it returns.
//
// The companion goes where the panel has room to spare:
//   portrait (h > w)            a band on top: avatar left, name/state/meter
//                               right (T-HMI 240x320, T-Display S3 held tall)
//   landscape, w >= 280 and     a rail on the left: avatar, then name, state
//   h >= 200                    and meter under it; the chat keeps the full
//                               height (T-Deck, M5Stack Core, CYD 320x240)
//   anything smaller            none: the chat takes the whole screen and the
//                               character stays its own view (T-Embed
//                               320x170, Cardputer 240x135, round 240x240)
// Boards opt in (board.h CHIRP_BANNER); without it the answer is always none.
#pragma once

namespace ui_layout {

struct Rect {
    int x, y, w, h;
    int right() const { return x + w; }
    int bottom() const { return y + h; }
};

enum class Companion { None, Top, Rail };

const int AV = 32;            // the avatar's logical size (avatar.h W, H)
const int PAD = 6;            // inside the companion's area
const int META_MIN_W = 90;    // a top band's name/state/meter column
// The companion is as large as it can be while the chat keeps this much:
// a rail leaves the chat this wide, a band leaves it this tall.
const int RAIL_CHAT_MIN_W = 200;
const int TOP_CHAT_MIN_H = 200;
// A panel this tall can spare the chat's first line some air above it.
const int ROOMY_H = 200;
const int ROOMY_TOP = 4;

struct Chat {
    Companion companion;
    int scale;                // the avatar's scale (0 with no companion)
    Rect comp;                // the companion's area (zero-sized with none)
    Rect chat;                // transcript text, live row and scrollbar
};

// The largest scale in [lo, hi] for which fits(s) holds; 0 when none does.
template <class Fits>
inline int largestScale(int lo, int hi, Fits fits) {
    for (int s = hi; s >= lo; s--)
        if (fits(s)) return s;
    return 0;
}

inline Companion companionFor(int w, int h, bool wanted) {
    if (!wanted) return Companion::None;
    if (h > w) return Companion::Top;
    return w >= 280 && h >= 200 ? Companion::Rail : Companion::None;
}

// w x h panel; top = the first content row (ui.cpp CONTENT_Y); bottom = the
// top of what sits under the chat (typing line or status strip).
inline Chat chatLayout(int w, int h, int top, int bottom, bool wanted) {
    Chat c{};
    c.companion = companionFor(w, h, wanted);
    c.chat = {0, top, w, bottom - top};
    if (c.companion == Companion::Top) {
        c.scale = largestScale(2, 4, [&](int s) {
            return AV * s + 2 * PAD + META_MIN_W <= w && AV * s + 2 * PAD <= h - TOP_CHAT_MIN_H;
        });
        if (!c.scale) c.companion = Companion::None;
        else {
            c.comp = {0, top, w, AV * c.scale + 2 * PAD};
            c.chat = {0, c.comp.bottom(), w, bottom - c.comp.bottom()};
        }
    } else if (c.companion == Companion::Rail) {
        c.scale = largestScale(2, 3, [&](int s) { return AV * s + 2 * PAD <= w - RAIL_CHAT_MIN_W; });
        if (!c.scale) c.companion = Companion::None;
        else {
            c.comp = {0, top, AV * c.scale + 2 * PAD, bottom - top};
            c.chat = {c.comp.right(), top, w - c.comp.right(), bottom - top};
        }
    }
    if (c.companion == Companion::None) c.scale = 0;
    if (h >= ROOMY_H) {
        c.chat.y += ROOMY_TOP;
        c.chat.h -= ROOMY_TOP;
    }
    return c;
}

// The rail's stack under the avatar: name (one body line), state (one small
// line) and the meter. Its height, so the test can hold it inside the rail.
inline int railStackH(int scale, int bodyLine, int smallLine) {
    return PAD + AV * scale + PAD + bodyLine + 2 + smallLine + PAD + 8 + PAD;
}

// ---------------------------------------------------------------- the character view
// The full-screen character view: the avatar and the bubble column. Both
// arrangements are tried, the avatar beside the column or above it, and the
// one that allows the larger avatar while the column stays readable wins
// (beside on a tie): the T-Embed 320x170 keeps 4x beside, a square 240x240
// gets 3x above, a portrait panel 4x above.
const int SCENE_X = 12;          // the avatar's left edge, beside
const int SCENE_GAP = 6;         // avatar to column
const int SCENE_TOP = 8;         // the avatar's top edge, above
const int SCENE_COL_MIN_W = 150; // a bubble column narrower than this is not worth it
const int SCENE_COL_MIN_H = 100;

struct Scene {
    int scale;
    bool above;                  // the avatar above the column (else beside it)
    int sprX, sprY;
    Rect col;
};

inline Scene sceneLayout(int w, int h) {
    const int beside = largestScale(2, 4, [&](int s) {
        return AV * s <= h - 2 * SCENE_TOP && w - (SCENE_X + AV * s + SCENE_GAP) - 2 >= SCENE_COL_MIN_W;
    });
    const int above = largestScale(2, 4, [&](int s) {
        return AV * s <= w - 2 * PAD && h - (SCENE_TOP + AV * s + SCENE_GAP) >= SCENE_COL_MIN_H;
    });
    Scene sc{};
    if (above > beside) {
        sc.scale = above;
        sc.above = true;
        sc.sprX = (w - AV * above) / 2;
        sc.sprY = SCENE_TOP;
        const int top = SCENE_TOP + AV * above + SCENE_GAP;
        sc.col = {4, top, w - 6, h - top};
    } else {
        sc.scale = beside ? beside : 2;          // nothing fits well: the smallest avatar beside
        sc.sprX = SCENE_X;
        sc.sprY = (h - AV * sc.scale) / 2;
        const int x = SCENE_X + AV * sc.scale + SCENE_GAP;
        sc.col = {x, 0, w - x - 2, h};
    }
    return sc;
}

// ---------------------------------------------------------------- the Chirper tab's preview
// The avatar left in its card, the name and look to its right: the largest
// of 3x, 2x, 1x that fits the card's height and leaves the text its room.
// When none does, no avatar (scale 0) and the text starts at the left.
const int PREVIEW_X = 18, PREVIEW_GAP = 18, PREVIEW_TEXT_MIN_W = 110;

struct Preview {
    int scale, avX, textX;    // scale 0: no avatar
};

inline Preview previewLayout(int w, int cardH) {
    const int s = largestScale(1, 3, [&](int s) {
        return AV * s <= cardH && w - (PREVIEW_X + AV * s + PREVIEW_GAP) - 4 >= PREVIEW_TEXT_MIN_W;
    });
    return {s, PREVIEW_X, s ? PREVIEW_X + AV * s + PREVIEW_GAP : PREVIEW_X};
}

// The preview card's height (menu.cpp: under the tab header, above the
// status strip) on an h-tall panel; content starts at contentTop.
inline int previewCardH(int h, int contentTop, int header, int status) {
    return (h - status) - (contentTop + header + 2);
}

}  // namespace ui_layout
