// capture.h — PDM mic, the recording buffer, and speech-to-text.
//
// Ownership: the loop task owns the mic, the buffer and the live upload
// stream while recording. turnBegin() hands them to the turn worker
// (captureTranscribe); they return to the loop when the turn ends (the
// worker parks until the next turnBegin).
#pragma once

#include <Arduino.h>

bool captureBegin();                 // PSRAM recording buffer
bool micInstall();
void micRemove();
bool micInstalled();

// Mic read with software gain. Returns bytes read (0 on timeout).
size_t micRead(int16_t* buf, size_t bytes, uint32_t timeoutMs);

// Recording buffer + live chunked upload to the shim's streaming STT
// endpoint. Any upload failure falls back to a whole-buffer POST.
void captureReset(int16_t peak);     // empty buffer, clock starts now
void captureStreamOpen();
void captureStreamClose();           // terminating chunk + disconnect (no-op if idle)
bool captureAppend(const int16_t* pcm, size_t bytes, int16_t peak);  // false: full
size_t captureBytes();
int16_t capturePeak();
uint8_t captureLevel();              // peak of the latest appended chunk, 0..255
uint32_t captureStartMs();
bool captureWorthSending();          // long and loud enough for a turn

// Worker side, after the handoff: finalizes the WAV and transcribes
// (streamed result if the upload stayed live, else a buffered POST).
bool captureTranscribe(String& text);
