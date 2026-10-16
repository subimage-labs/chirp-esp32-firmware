// speech.h — speaker playback and the streamed TTS chunk pipeline.
//
// The first chunk is one sentence and later chunks pack >= 2 sentences
// (sentence_packer.h): speech starts while Hermes is still generating, and
// the fetch task stages chunk N+1 in PSRAM while N plays; the decoder plays
// the staged bytes in place.
//
// The audio library's decode hook (audio_process_extern) taps the decoded
// level for the character's mouth; it runs inside audio.loop() on the loop
// task, so speechLevel() needs no lock.
//
// Ownership: the turn worker produces (speechStart/Feed/Finish); the fetch
// task downloads; the loop consumes with speechTick and owns the speaker,
// volume, playback flag and current chunk. The loop starts consuming only
// after the worker has finished speechStart (it announces the LLM stage after
// the reset, through the stage cell), so playback runs during the stream and
// continues in SPEAKING. The chunk queue is mutex-guarded; the end of
// production is an atomic flag set by speechFinish.
//
// Playback is from memory: the decoder reads each MP3 where it already is
// (a PSRAM slot for answer chunks, the flash-mapped arrays for the chirp and
// the error tone) through a read-only fs::FS (mem_source.h, speech.cpp), so
// there is no file system round trip before a chunk or between chunks.
// Slot lifetime: chunk i lives in PSRAM slot i % 2 from the moment its
// download is published until the decoder has CLOSED it (the eof callback,
// speechStop, or the chirp/error tone replacing it), not merely started.
// Only then does the loop free the slot and publish it as consumed; the fetch
// task refills a slot only after that (speechSlotFree), so chunk i+1
// downloads while i plays but i+2 waits for i to finish.
#pragma once

#include <stdint.h>

void speechBegin();              // speaker pins, volume

// Loop task.
uint8_t speechVolume();
void speechSetVolume(uint8_t v);
bool speechPlaying();
bool speechSpeaking();           // playing an answer chunk (not the chirp or the error tone)
int speechSpokenChars();         // while speaking: how far into the answer's spoken text
                                 // playback is (chars; interpolated within a chunk); else -1
uint8_t speechLevel();           // loudest decoded buffer since the last call, 0..255
void speechPump();               // decoder step while something plays
void speechChirp();              // instant ack at capture release
void speechErrorTone();          // a turn or the self-test failed (plays while idle)
void speechStop();               // stop playback; fetch stops between chunks

enum class SpeakStep : uint8_t { Busy, Done };
SpeakStep speechTick();          // play chunks in order; Done once production
                                 // ended and every queued chunk has played

// Turn worker.
bool speechStart();              // reap the previous fetch task, clear the queue;
                                 // false: that task is still downloading
void speechFeed(const char* delta);
void speechFinish();             // flush the unterminated tail
int speechChunks();
