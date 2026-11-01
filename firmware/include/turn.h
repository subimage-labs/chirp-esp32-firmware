// turn.h — one voice turn: capture → STT → Hermes stream → speech.
//
// The worker task (core 0) runs STT and the Hermes stream so the loop keeps
// the spinner and progressive text alive. Handoffs:
//  - turnBegin (loop) gives the capture to the worker and wakes it;
//  - turnStop (loop) raises the stop flag (an atomic, cleared at every
//    turnBegin); the worker polls it after STT and while the stream waits;
//  - the worker publishes progressive text through TurnSnap and ends every
//    turn with a stage (OK / FAIL / SILENCE / STOPPED) under the stage lock,
//    then parks until the next turnBegin — so its final strings are stable
//    for the loop.
#pragma once

#include <Arduino.h>

void turnTaskStart();
void turnBegin(bool keepMic);   // loop: capture finished (PTT release / voice close)
void turnTick();                // loop: THINKING and SPEAKING states
void turnStop();                // loop: end a THINKING turn early ("Stopped", no error tone)
bool turnTranscribing();        // loop: THINKING and the worker is still in STT (not yet asking Hermes)

// Answer latency milestones: `[Lat] <event> +<ms>` measured from the moment the
// Hermes request starts. The worker starts the clock; the worker, the fetch
// task and the loop log against it (an atomic, any context). host/latency.py
// reads these lines.
void turnLatMark(const char* event);
void turnAskText(const String& q);   // a typed turn: the question is q, STT skipped
// The Setup tab's voice preview: the worker downloads `text` in the current
// voice between turns (speech.h). Loop, idle only; false: not idle or one is
// still being fetched.
bool turnPreviewVoice(const char* text);
#ifdef BENCH_CMDS
void turnSpeakText(const String& a); // bench: a turn whose answer is a, Hermes skipped
#endif
