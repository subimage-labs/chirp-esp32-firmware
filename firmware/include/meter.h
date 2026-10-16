// meter.h — the level meters' curve, shared by the character view's bars and
// wave and the chat view's live row. Pure (no Arduino), host-tested
// (avatar_test).
#pragma once

// The playback / mic level (0..255) as half a bar's height for the level
// meters: a square-root curve (speech sits low in the range), 1..maxHalf.
inline int waveHalf(int level, int maxHalf) {
    if (level < 0) level = 0;
    if (level > 255) level = 255;
    int r = 0;                                       // integer sqrt of level * 255
    const int v = level * 255;
    while ((r + 1) * (r + 1) <= v) r++;
    const int h = 1 + (r * (maxHalf - 1) + 127) / 255;
    return h > maxHalf ? maxHalf : h;
}
