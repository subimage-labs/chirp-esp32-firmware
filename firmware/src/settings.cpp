#include "settings.h"

#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFi.h>

// One JSON record under one key: an NVS write of a single key is atomic, so
// a power cut mid-save leaves the old settings or the new ones, never a mix.
static const char* NVS_NAMESPACE = "chirp";
static const char* NVS_KEY = "settings";

static DeviceSettings stored;
static Endpoints endpoints;
static bool usable = false;
static const std::string NO_PROFILE;

static bool recordFromJson(const char* json, DeviceSettings& out) {
    JsonDocument doc;
    if (deserializeJson(doc, json) || !doc.is<JsonObject>()) return false;
    out.ssid = doc["ssid"] | "";
    out.pass = doc["pass"] | "";
    out.hermes = doc["hermes"] | "";
    out.shim = doc["shim"] | "";
    out.backend = doc["backend"] | "local";
    out.venice = doc["venice"] | "";
    if (out.venice.empty()) out.venice = VENICE_HOST_DEFAULT;
    out.veniceKey = doc["veniceKey"] | "";
    out.agents.clear();
    for (JsonObject a : doc["agents"].as<JsonArray>())
        out.agents.push_back(Agent{a["profile"] | "", a["key"] | "", a["look"] | ""});
    out.active = doc["active"] | 0u;
    return true;
}

static String toJson(const DeviceSettings& s) {
    JsonDocument doc;
    doc["ssid"] = s.ssid.c_str();
    doc["pass"] = s.pass.c_str();
    doc["hermes"] = s.hermes.c_str();
    doc["shim"] = s.shim.c_str();
    doc["backend"] = s.backend.c_str();
    doc["venice"] = s.venice.c_str();
    doc["veniceKey"] = s.veniceKey.c_str();
    JsonArray agents = doc["agents"].to<JsonArray>();
    for (const Agent& a : s.agents) {
        JsonObject o = agents.add<JsonObject>();
        o["profile"] = a.profile.c_str();
        o["key"] = a.key.c_str();
        if (!a.look.empty()) o["look"] = a.look.c_str();
    }
    doc["active"] = (unsigned)s.active;
    String json;
    serializeJson(doc, json);
    return json;
}

// Present string members replace the stored value; absent ones keep it.
static std::string member(JsonDocument& doc, const char* name, const std::string& keep) {
    return doc[name].is<const char*>() ? std::string(doc[name].as<const char*>()) : keep;
}

bool settingsInputFromJson(const char* json, SettingsInput& out) {
    JsonDocument doc;
    if (deserializeJson(doc, json) || !doc.is<JsonObject>()) return false;
    SettingsInput in;
    in.ssid = member(doc, "ssid", stored.ssid);
    in.open = doc["pass"].is<const char*>() ? !doc["pass"].as<const char*>()[0]
                                            : stored.pass.empty();
    in.pass = member(doc, "pass", "");             // blank keeps it on the same network
    in.hermes = member(doc, "hermes", stored.hermes);
    in.shim = member(doc, "shim", stored.shim);
    in.backend = member(doc, "backend", stored.backend.empty() ? "local" : stored.backend);
    in.backendSet = doc["backend"].is<const char*>();
    in.venice = member(doc, "venice", stored.venice);
    in.veniceKey = member(doc, "veniceKey", "");
    in.addProfile = member(doc, "profile", "");
    in.addKey = member(doc, "key", "");
    in.lookSet = doc["look"].is<const char*>();
    in.look = member(doc, "look", "");
    in.useSet = doc["use"].is<const char*>();
    in.use = member(doc, "use", "");
    if (doc["forget"].is<const char*>()) in.forget.push_back(doc["forget"].as<const char*>());
    for (const char* f : doc["forget"].as<JsonArray>())
        if (f) in.forget.push_back(f);
    out = in;
    return true;
}

static void deriveEndpoints() {
    if (stored.backend == "venice") {
        HostPort vp{"", 0, Scheme::Https};
        parseHostPort(stored.venice, VENICE_PORT_DEFAULT, vp);
        const String base = String(hostSchemeString(vp).c_str()) + "/api/v1";
        endpoints.chatUrl = base + "/chat/completions";
        endpoints.sttUrl = base + "/audio/transcriptions";
        endpoints.ttsUrl = base + "/audio/speech";
        endpoints.shimHealthUrl = base + "/models";       // the self-test probe
        endpoints.sessionsUrl = "";                       // no server sessions
        endpoints.otaManifestUrl = "";                    // no shim: USB-only updates
        endpoints.otaBinUrl = "";
        endpoints.shimHost = vp.host.c_str();
        endpoints.shimPort = vp.port;
        endpoints.auth = String("Bearer ") + stored.veniceKey.c_str();
        return;
    }
    const Agent& agent = stored.agents[stored.active];
    HostPort hermes{"", 0, Scheme::Bare}, shim{"", 0, Scheme::Bare};
    parseHostPort(stored.hermes, HERMES_PORT_DEFAULT, hermes);
    if (stored.shim.empty() || !parseHostPort(stored.shim, SHIM_PORT_DEFAULT, shim))
        shim = HostPort{hermes.host, SHIM_PORT_DEFAULT};
    String hermesBase = String("http://") + hermes.host.c_str() + ":" + hermes.port;
    if (!agent.profile.empty()) hermesBase += String("/p/") + agent.profile.c_str();
    const String shimBase = String("http://") + shim.host.c_str() + ":" + shim.port;
    endpoints.chatUrl = hermesBase + "/v1/chat/completions";
    endpoints.sessionsUrl = hermesBase + "/api/sessions";
    endpoints.sttUrl = shimBase + "/v1/audio/transcriptions";
    endpoints.ttsUrl = shimBase + "/v1/audio/speech";
    endpoints.shimHealthUrl = shimBase + "/health";
    endpoints.otaManifestUrl = shimBase + "/firmware/manifest.json";
    endpoints.otaBinUrl = shimBase + "/firmware/chirp.bin";
    endpoints.shimHost = shim.host.c_str();
    endpoints.shimPort = shim.port;
    endpoints.auth = String("Bearer ") + agent.key.c_str();
}

void settingsLoad() {
    stored = DeviceSettings();
    Preferences prefs;
    if (prefs.begin(NVS_NAMESPACE, true)) {         // read-only open fails before first save
        if (prefs.isKey(NVS_KEY)) recordFromJson(prefs.getString(NVS_KEY).c_str(), stored);
        prefs.end();
    }
    usable = settingsUsable(stored);
    if (usable) deriveEndpoints();
    Serial.printf("[Settings] %s\n", usable ? "loaded" : "missing or incomplete");
    if (usable) Serial.printf("[Settings] backend %s\n", stored.backend.c_str());
    if (usable && stored.backend != "venice")
        Serial.printf("[Settings] agent %s (%u of %u)\n",
                      agentLabel(settingsProfile()).c_str(), (unsigned)stored.active + 1,
                      (unsigned)stored.agents.size());
}

bool settingsReady() { return usable; }
const DeviceSettings& settingsStored() { return stored; }
const Endpoints& settingsEndpoints() { return endpoints; }
const std::string& settingsProfile() {
    return usable && stored.backend != "venice" ? stored.agents[stored.active].profile
                                                : NO_PROFILE;
}

bool settingsSave(const DeviceSettings& s) {
    const String json = toJson(s);
    Preferences prefs;
    bool ok = prefs.begin(NVS_NAMESPACE, false);
    if (ok) {
        ok = prefs.putString(NVS_KEY, json) == json.length() && prefs.getString(NVS_KEY) == json;
        prefs.end();
    }
    Serial.printf("[Settings] %s\n", ok ? "saved" : "save failed");
    return ok;
}

bool settingsUseAgent(size_t index) {
    if (index >= stored.agents.size()) return false;
    DeviceSettings s = stored;
    s.active = index;
    return settingsSave(s);
}

void settingsWifiBegin() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(stored.ssid.c_str(), stored.pass.empty() ? nullptr : stored.pass.c_str());
}
