// character.h — character mode: an animated pixel-art avatar of the active
// agent (avatar.h) beside a column of comic speech bubbles holding the whole
// stored chat (bubbles.h), instead of the scrolling transcript. The view takes
// the whole screen; the status strip only comes up over it for errors and
// notices (ui.h). Loop task only: this module owns the view choice (NVS
// "chirp"/"view"), the avatar, its animation tick, the frames on the panel,
// and the bubble layout and scroll position. The transcript data stays with
// ui.cpp; turn logic is untouched (it only reports a failed turn).
//
// Opt-in from the Setup tab ("Screen"); the transcript is the default. The
// Chirper tab (menu.cpp) browses and keeps the agent's character.
//
// Drawing: the 32x32 sprite is pushed 4x with only the changed rectangle per
// 15 fps tick (4 fps once idle on battery, power.h powerAnimSlow); the bubble
// column is composed in a PSRAM sprite and pushed whole, the live meter and
// countdown as their rows alone. The character's colors are its own palette
// (avatar.h); only the background, and with it the silhouette edge, follow
// the theme (recolored on the next full paint, appRedraw).
#pragma once

#include <Arduino.h>

#include "avatar.h"

void characterBegin();                  // after settingsLoad: the saved view, this agent's avatar
bool characterOn();                     // the character view replaces the transcript
void characterSetOn(bool on);           // Setup row: saved; the screen follows on menu close

void characterPaint();                  // whole screen: sprite and bubble column
void characterTextChanged();            // the chat changed: relayout, repaint the column
void characterFollow();                 // pin the column to the newest bubble again
void characterScroll(int rows);         // wheel, in transcript rows (+ = older)
void characterTick();                   // loop: animation, meters, card mode
void characterError(const char* heard, const char* label);   // a turn failed
void characterMeterLabel(const char* text);   // the listening bubble's countdown line (ui.cpp)
// A one-shot expression over the current pose (avatar.h Reaction). The module
// starts Surprised (a turn begins: idle → listening) and Pleased (the answer
// starts: thinking → speaking) itself; turn.cpp reports nothing heard (Puzzled).
void characterReact(avatar::Reaction r);

// The kept look (the Chirper tab). An agent's character is its kept look when
// the owner kept one, else its Original (seeded from the profile name, with
// the persona look over it). "Keep" stores the character's own trait record
// per agent (NVS "chirp", key from the profile name; avatar.h), so a kept look
// never changes when the generator grows. The tab browses with a preview that
// is thrown away unless kept: the kept look first (while there is one), then
// the Original, then the numbered variants.
void characterPreviewBegin();           // tab opened: the preview starts at the first slot
void characterPreviewPosition(int& sel, int& count);   // the browse position and slot count
void characterPreviewPaint(int top, int bottom);   // sprite and labels in [top, bottom)
void characterPreviewMove(int steps);   // browse (+ = next); repaints sprite and labels
bool characterPreviewKeep();            // keep the shown one; false when nothing changed
void characterPreviewTick();            // animation while the tab is open
void characterPreviewEnd();             // leaving: back to the agent's character

// Bench status.
const char* characterStateName();
void characterStats(uint32_t& pushes, uint32_t& avgUs, uint32_t& maxUs);
#ifdef BENCH_CMDS
void characterForce(int state);         // -1 = follow the app; else avatar::State
void characterKeepVariant(uint16_t n);  // keep variant n of the current generator (0 = Original)
bool characterKeepRecord(const uint8_t* rec, size_t n);   // keep this record; false if it does not decode
bool characterKeptRecord(uint8_t* out); // the kept look as avatar::KEPT_BYTES; false = none
void characterLookTags(char kept[9], char shown[9]);   // "orig" or the look tag of the agent's
                                                       // character and of the one the tab shows
void characterColumnShot();             // the bubble column sprite over serial (bench.h)
void characterSpriteShot();             // the character sprite frame + palette as pushed (bench.h)
#endif
