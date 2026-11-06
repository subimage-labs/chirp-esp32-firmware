#include "settings.h"

#include <ArduinoJson.h>
#include <Preferences.h>
#include "endpoint_urls.h"
#include "serial_settings.h"
#include "wifi_pick.h"

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
    out.networks.clear();
    for (JsonObject n : doc["networks"].as<JsonArray>())
        out.networks.push_back(setup_form::Network{n["ssid"] | "", n["pass"] | ""});
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
    JsonArray networks = doc["networks"].to<JsonArray>();
    for (const setup_form::Network& n : s.networks) {
        JsonObject o = networks.add<JsonObject>();
        o["ssid"] = n.ssid.c_str();
        o["pass"] = n.pass.c_str();
    }
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

// A string member becomes present; any other type (or absence) is not.
static OptStr optStr(JsonDocument& doc, const char* name) {
    return doc[name].is<const char*>() ? someStr(doc[name].as<const char*>()) : OptStr();
}

// A string member is one entry; an array member contributes its string
// elements in order; any other type is empty.
static std::vector<std::string> strList(JsonDocument& doc, const char* name) {
    std::vector<std::string> out;
    if (doc[name].is<const char*>()) out.push_back(doc[name].as<const char*>());
    for (const char* f : doc[name].as<JsonArray>())
        if (f) out.push_back(f);
    return out;
}

bool settingsInputFromJson(const char* json, SettingsInput& out) {
    JsonDocument doc;
    if (deserializeJson(doc, json) || !doc.is<JsonObject>()) return false;
    SerialMembers m;
    m.ssid = optStr(doc, "ssid");
    m.pass = optStr(doc, "pass");
    m.hermes = optStr(doc, "hermes");
    m.shim = optStr(doc, "shim");
    m.backend = optStr(doc, "backend");
    m.venice = optStr(doc, "venice");
    m.veniceKey = optStr(doc, "veniceKey");
    m.profile = optStr(doc, "profile");
    m.key = optStr(doc, "key");
    m.look = optStr(doc, "look");
    m.use = optStr(doc, "use");
    m.forget = strList(doc, "forget");
    m.forgetWifi = strList(doc, "forgetWifi");
    out = settingsInputFromMembers(m, stored);
    return true;
}

static void deriveEndpoints() {
    const EndpointUrls u = deriveEndpointUrls(stored);
    endpoints.chatUrl = u.chatUrl.c_str();
    endpoints.sessionsUrl = u.sessionsUrl.c_str();
    endpoints.sttUrl = u.sttUrl.c_str();
    endpoints.ttsUrl = u.ttsUrl.c_str();
    endpoints.shimHealthUrl = u.shimHealthUrl.c_str();
    endpoints.otaManifestUrl = u.otaManifestUrl.c_str();
    endpoints.otaBinUrl = u.otaBinUrl.c_str();
    endpoints.shimHost = u.shimHost.c_str();
    endpoints.shimPort = u.shimPort;
    endpoints.auth = u.auth.c_str();
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

void settingsNetworkJoined(int index) {
    std::vector<setup_form::Network> order = stored.networks;
    if (!wifi_pick::promote(order, index)) return;
    DeviceSettings s = stored;
    s.networks = order;
    if (settingsSave(s)) stored.networks = order;    // only the loop task touches the order
}
