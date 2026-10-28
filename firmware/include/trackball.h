// trackball.h — the T-Deck's trackball. The four direction pins pulse as
// the ball rolls; ISRs count pulses (the loop blocks in i2s_read for up to
// 100 ms, far longer than a roll's pulse train, so polling would drop
// motion). The center click is GPIO0 and is read as the encoder key
// (ENCODER_BTN_PIN), not here.
//
// The vertical axis feeds the same detent counter the wheel does, so every
// gesture the router knows (scroll, menu move, volume) works unchanged;
// the horizontal axis is counted but unused so far.
#pragma once

#include <cstdint>

bool trackballBegin();
int32_t trackballUp();      // up pulses since the last call
int32_t trackballDown();
int32_t trackballLeft();
int32_t trackballRight();
