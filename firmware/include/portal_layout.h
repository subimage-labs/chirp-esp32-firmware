// portal_layout.h — the setup screen's geometry and words (portal.cpp draws
// them). Pure (no Arduino), host-tested on every board's panel so no
// element and no message can fall off the screen.
//
//                  [ chirp! ]                   the logo: only when the
//            Scan QR code to set up             panel has room (T-Deck)
//   +--------+  Or join WiFi:
//   |        |  ( bars  Chirp-XXXX  )           the credentials on a raised
//   |   QR   |  ( key   password    )           surface card
//   |        |  * Phone connected               feedback: only when something
//   +--------+                                  happens; room for two lines
//
// Written for someone who is not technical: one instruction, one
// alternative, and feedback only when it means something (no addresses, no
// "waiting"). The QR takes what the body allows with the text column kept at
// its narrowest readable width; the column (label, card and a two-line
// status) is centred on the code and kept inside the screen.
#pragma once

namespace portal_layout {

const int M = 8;                 // outer margin
const int GUTTER = 12;           // QR card to text column
const int LINE = 18;             // the Fixed cut's line pitch (ui.h UiFont::Fixed)
const int HEADLINE_H = 24;       // the instruction row
const int BODY_MIN = 156;        // the QR's room at 4x, with margins
const int LOGO_ROOM = 52;        // the logo's band, on a panel with room
const int CARD_TEXT_X = 18;      // card text inset from the column (icons left of it)
const int CARD_PAD_R = 4;        // card text's room to the card's right edge
// The text column's narrowest width: the widest password the portal can
// draw (12 x the alphabet's widest glyph, 120 px in the Fixed cut; the test
// measures it) plus the card's insets.
const int COL_MIN = 120 + CARD_TEXT_X + CARD_PAD_R + 2;
const int CARD_H = 56;           // the credentials card: two 20 px rows + pad
const int STATUS_GAP = 12;       // card to status
const int STATUS_TEXT_X = 14;    // status text inset (the dot left of it)
const int STATUS_LINES = 2;
const int GROUP_H = LINE + 8 + CARD_H + STATUS_GAP + STATUS_LINES * LINE;
const int BOTTOM_PAD = 2;        // the group never ends closer to the bottom edge

const char* const HEADLINE = "Scan QR code to set up";
const char* const LABEL = "Or join WiFi:";
const char* const MSG_CONNECTED = "Phone connected";
const char* const MSG_SAVED = "All set! Restarting";
const char* const MSG_FINISH = "Finish setup first";
const char* const MSG_CANCEL_SIDE_KEY = "Side key = cancel";     // boards with a side key
const char* const MSG_CANCEL_BACKSPACE = "Backspace = cancel";   // keyboard boards

struct Layout {
    int logoH;                   // 0: no logo band
    int headlineY;
    int qrX, qrY, qrScale, qrSide;
    int colX, colW;
    int labelY, cardY, statusY;
    int statusRoom;              // status text width per line
    int cardRoom;                // credentials text width
};

inline int imin(int a, int b) { return a < b ? a : b; }
inline int imax(int a, int b) { return a > b ? a : b; }

// w x h panel, a QR of `modules` modules per side (plus its 4-module quiet
// zone on every side).
inline Layout compute(int w, int h, int modules) {
    Layout l{};
    l.logoH = h - HEADLINE_H - BODY_MIN >= LOGO_ROOM ? LOGO_ROOM : 0;
    l.headlineY = l.logoH + (HEADLINE_H - LINE) / 2;
    const int bodyTop = l.logoH + HEADLINE_H, bodyH = h - bodyTop;
    const int room = imin(bodyH - 8, w - COL_MIN - 2 * M - GUTTER);
    l.qrScale = imax(2, room / (modules + 8));
    l.qrSide = l.qrScale * (modules + 8);
    l.qrX = M;
    l.qrY = bodyTop + (bodyH - l.qrSide) / 2;
    l.colX = l.qrX + l.qrSide + GUTTER;
    l.colW = w - l.colX - M;
    const int centred = l.qrY + (l.qrSide - GROUP_H) / 2;
    l.labelY = imax(bodyTop, imin(centred, h - BOTTOM_PAD - GROUP_H));
    l.cardY = l.labelY + LINE + 8;
    l.statusY = l.cardY + CARD_H + STATUS_GAP;
    l.statusRoom = l.colW - STATUS_TEXT_X;
    l.cardRoom = l.colW - CARD_TEXT_X - CARD_PAD_R;
    return l;
}

// Where a status message wraps: the byte index of the space after which the
// rest goes on the second line, or -1 when it fits on one. `width` measures
// a prefix (text, length). -2 when even two lines cannot hold it.
template <class Width>
inline int statusSplit(const char* s, int len, int room, Width width) {
    if (width(s, len) <= room) return -1;
    for (int i = len - 1; i > 0; i--)
        if (s[i] == ' ' && width(s, i) <= room)
            return width(s + i + 1, len - i - 1) <= room ? i : -2;
    return -2;
}

}  // namespace portal_layout
