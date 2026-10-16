// logo_draw.h — paints the chirp! logo (logo.h) on the panel at an integer
// scale. Loop task only (setup(), the transcript render and the menu run
// there); no state beyond a row buffer used inside one call.
#pragma once

#include "logo.h"

const int LOGO_MAX_SCALE = 4;                    // the characters' scale

inline int logoWidth(int scale) { return logo::W * scale; }
inline int logoHeight(int scale) { return logo::H * scale; }

// The largest scale <= LOGO_MAX_SCALE at which the logo fits maxW x maxH px
// (1 when none does).
inline int logoScaleFor(int maxW, int maxH) { return logo::fitScale(maxW, maxH, LOGO_MAX_SCALE); }

// Paints the logo's frame, background pixels included, with its top-left at
// (x, y), in the current theme's colors. The caller keeps it on the panel.
void logoDraw(int x, int y, int scale);

// The boot splash: the logo at the largest scale that fits, centred in the
// area above the status strip. The caller has cleared the screen.
void logoSplash();
