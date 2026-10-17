// settings.h — device settings (WiFi, Hermes gateway, its agents, audio shim)
// in NVS. They sit in their own flash partition, so firmware uploads keep
// them; only a full flash erase clears them. Builds carry no secrets.
//
// settingsLoad() runs in setup() before any task starts; the loaded values
// and the endpoints derived from them (for the active agent) are immutable
// for the rest of the boot, so every context (loop, turn, fetch) reads them
// without locking. Saving (setup portal, serial provisioning, switching
// agents) writes NVS only — the caller restarts the device and the new
// values apply on the next boot.
#pragma once

#include <Arduino.h>

#include "setup_form.h"

// Local mode derives every URL from the Hermes and shim hosts. Venice mode
// derives chat/STT/TTS from the backend base (<venice>/api/v1/...), sends
// the backend key, probes /api/v1/models for health, and has no server
// sessions and no shim OTA (those fields stay empty; updates are USB-only).
struct Endpoints {
    String chatUrl;       // http://hermes[/p/<profile>]/v1/chat/completions
    String sessionsUrl;   // http://hermes[/p/<profile>]/api/sessions
    String sttUrl;        // http://shim/v1/audio/transcriptions
    String ttsUrl;        // http://shim/v1/audio/speech
    String shimHealthUrl; // http://shim/health
    String otaManifestUrl; // http://shim/firmware/manifest.json
    String otaBinUrl;      // http://shim/firmware/chirp.bin
    String shimHost;      // raw socket for the streaming STT upload
    uint16_t shimPort;
    String auth;          // "Bearer <active agent's key>"
};

void settingsLoad();
bool settingsReady();                        // loaded values pass the setup rules
const DeviceSettings& settingsStored();      // portal prefill, merge base
const Endpoints& settingsEndpoints();
const std::string& settingsProfile();        // the active agent's profile ("" = default)
bool settingsSave(const DeviceSettings& s);  // NVS write, read back to verify
bool settingsUseAgent(size_t index);         // save with that agent active

// A serial `settings {json}` line as a submission. Members: ssid, pass,
// hermes, shim (absent = keep the stored value; "pass":"" = open network),
// profile + key (add or re-key that agent and use it; profile alone = use
// it), look (that agent's character traits as hex, "" clears; absent keeps),
// use, forget (a name or a list). False on malformed JSON.
bool settingsInputFromJson(const char* json, SettingsInput& out);

void settingsWifiBegin();                    // STA join with the stored network
