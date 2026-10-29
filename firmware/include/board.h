// board.h — the build's board descriptor. platformio.ini defines exactly one
// CHIRP_BOARD_* macro per environment; everything board-specific (panel
// rotation, mic kind, input devices, layout features, pins) comes from the
// header picked here, and the TFT_eSPI setup header picked in
// lib/TFT_eSPI/User_Setup_Select.h. Adding a board: write board_<name>.h,
// a TFT_eSPI User_Setup, a boards/<name>.json when the platform lacks one,
// and an env in platformio.ini.
#pragma once

#if defined(CHIRP_BOARD_TDECK)
#include "board_tdeck.h"
#elif defined(CHIRP_BOARD_TEMBED)
#include "board_tembed.h"
#else
#error "no CHIRP_BOARD_* defined: pick a board env in platformio.ini"
#endif

// Features every board states so plain #if works in module code.
#ifndef CHIRP_HAS_KEYBOARD
#define CHIRP_HAS_KEYBOARD 0
#endif
#ifndef CHIRP_HAS_TRACKBALL
#define CHIRP_HAS_TRACKBALL 0
#endif
// The chat view's companion banner (character always visible) and the typed
// compose line need width; boards opt in.
#ifndef CHIRP_BANNER
#define CHIRP_BANNER 0
#endif
#ifndef CHIRP_COMPOSE
#define CHIRP_COMPOSE 0
#endif
// A side tap in the menu picks the row (the board steps tabs some other way)
// instead of stepping to the next tab.
#ifndef CHIRP_SIDE_SELECTS
#define CHIRP_SIDE_SELECTS 0
#endif
