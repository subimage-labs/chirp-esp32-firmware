// bench.h — serial test commands for unattended verification on the real
// device. Compiled only with -DBENCH_CMDS; release builds have no command
// surface. Loop task; lines arrive through console.h.
//
//   enc down|up, side down|up   drive the keys through the gesture decoder
//   wheel <clicks> [msPerClick] turn the wheel (+ = clockwise)
//   ask <text>                  a full turn from text (STT skipped)
//   speak <text>                a turn whose answer is <text>, streamed word by word
//                               and spoken; Hermes is not asked (local transcript only)
//   mic [ms]                   install the mic and read for ms (default 1500):
//                              `[Bench] mic <ms> peak mean zeros% samples`
//   keys on|off                echo every raw keyboard code as `[Kbd] raw XX`
//   vol <0-21>                  speaker volume
//   theme <n>                   theme family n/2, light if n is odd; redraws
//   view character|transcript   the screen choice (Setup tab row); redraws
//   panel invert on|off         send the panel INVON / INVOFF (colour-state checks)
//   panel status                read the panel's RDDST/RDDPM/RDDMADCTL/RDDCOLMOD/RDDIM
//                               registers over MISO: `[Bench] panel st=.. dpm=.. ...`
//   avatar variant <n>          keep variant n of the current generator (0 = the
//                               agent's Original: clears the kept look); saved
//                               per agent; the Chirper tab is driven with
//                               side/wheel/enc like any tab
//   avatar look [<32 hex>]      no argument: print `[Bench] look <32 hex>` (the
//                               kept look's record then seed, little-endian) or
//                               `[Bench] look orig` when there is none. With
//                               the hex: keep exactly that record (saved)
//   avatar react <r>            play a reaction now: pleased|puzzled|surprised
//   avatar sprite               the character sprite as last pushed: `[Sprite] bg=.. pal ..`
//                               (SlotCount RGB565 colors), 32 hex rows of slots, `[Sprite] end`
//   avatar <state>|auto|stats   pin the avatar to idle|listening|thinking|
//                               speaking|error|sleep (auto = follow the app);
//                               stats prints the sprite push cost
//   font [<name|index> [S|M|L]] no argument: list the families. Otherwise pick
//                               one (and a size; a lone S, M or L changes only
//                               the size), saved and redrawn, then print status
//   battery                     fresh charger sample: raw registers, voltage,
//                               curve percent, shown percent, charge state
//   status                      one [Bench] line: state, menu, scroll, power,
//                               bat (shown percent, + charging, ! low; ? before
//                               the first reading), view, avatar state,
//                               look=kept/shown (each `orig` or an 8-hex look
//                               tag: the agent's character and the one the
//                               Chirper tab shows), font, size, line height,
//                               theme name, mode (light|dark) and themecode (the
//                               number `theme <n>` takes); host/smoke.py parses it
//   heap [all|render|commit|pending|idle|msg|shimmer|font|keep] [n]
//                               no argument: one [Heap] line. Otherwise a
//                               fixed, repeatable ui workload (no Hermes) that
//                               prints one [HeapBench] line: step timing and
//                               internal-heap free/largest before, after and
//                               lowest seen (bench_heap.cpp). Needs idle.
//                               `keep` loads n fixed turns (4) and leaves them
//                               on screen (only on the device, not in Hermes)
//   avatar shot                 the character view's bubble column as composed
//                               (RGB565 big-endian rows, base64): `[Shot] WxH`,
//                               one `[Shot] <row> <b64>` per row, `[Shot] end`
#pragma once

#ifdef BENCH_CMDS
#include <Arduino.h>

void benchRun(String line);
void benchHeapRun(const String& args);        // bench_heap.cpp
#endif
