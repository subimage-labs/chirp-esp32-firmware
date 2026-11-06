// voice.h — hands-free voice mode and the idle ambient noise floor. Loop
// task only. Owns the one VoiceDetector (voice_detect.h: DC blocker, room
// noise floor, speech detector, pre-roll ring); voice.cpp owns the mute
// window, the drain count, the idle exit, the UI text and every capture
// and turn call. Half-duplex: the mic is read only while idle, never
// during playback. Turns open on sustained energy and close on trailing
// silence; the pre-roll ring keeps the first word.
#pragma once

bool voiceOn();
bool voiceHearing();      // a hands-free turn is being captured
void voiceSet(bool on);
void voiceTick();         // idle + voice mode: listen, open/close turns
void ambientTick();       // idle, voice off: warm the noise floor silently
void voiceResume();       // after a turn: settle, drain stale audio, listen
