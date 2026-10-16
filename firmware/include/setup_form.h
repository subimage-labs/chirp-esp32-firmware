// setup_form.h — device settings rules shared by the setup portal, serial
// provisioning and pairing: validation, merging a submission with the stored
// values, host[:port] parsing, the portal's access control (AP password,
// form token, Origin check, lifetime) and escaping for the portal page and
// the WiFi QR code. Pure (no Arduino), host-tested.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <utility>
#include <vector>

static const uint16_t HERMES_PORT_DEFAULT = 8642;
static const uint16_t SHIM_PORT_DEFAULT = 8643;
static const size_t MAX_AGENTS = 4;

// One Hermes agent the device can talk to: a profile on the gateway and its
// API key. An empty profile is the Hermes default profile. `look` is the
// agent's character traits chosen on the host (host/pair.py): LOOK_HEX_LEN
// lowercase hex digits, one byte per trait (avatar.h; 0xff = keep the seeded
// value), or empty for the seeded character. Only trait indexes ever get here.
struct Agent {
    std::string profile, key, look;
    Agent(std::string p = "", std::string k = "", std::string l = "")
        : profile(std::move(p)), key(std::move(k)), look(std::move(l)) {}
};
static const size_t LOOK_HEX_LEN = 24;

// Stored settings (NVS). hermes and shim are canonical "host:port"; an empty
// shim means the Hermes host on SHIM_PORT_DEFAULT; an empty pass means an
// open network. All agents live on the one Hermes host; `active` indexes the
// one the device answers as.
struct DeviceSettings {
    std::string ssid, pass, hermes, shim;
    std::vector<Agent> agents;
    size_t active = 0;
};

// One submission: the phone form, a serial `settings` line or a pairing code.
// The caller fills every field it doesn't change from the stored settings.
struct SettingsInput {
    std::string ssid, pass, hermes, shim;
    bool open = false;                    // network has no password
    bool useSet = false;                  // make `use` the active agent
    std::string use;
    std::vector<std::string> forget;      // profiles to remove
    std::string addProfile, addKey;       // add (or re-key) one agent
    bool lookSet = false;                 // set the added / selected agent's look
    std::string look;                     // "" clears it
};

struct HostPort {
    std::string host;
    uint16_t port;
};

inline std::string agentLabel(const std::string& profile) {
    return profile.empty() ? "default" : profile;
}

namespace setup_form {

inline bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

inline std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && isSpace(s[b])) b++;
    while (e > b && isSpace(s[e - 1])) e--;
    return s.substr(b, e - b);
}

// Control characters would let a value break out of an HTTP header line.
inline bool hasControl(const std::string& s) {
    for (unsigned char c : s)
        if (c < 0x20 || c == 0x7f) return true;
    return false;
}

inline bool isAlnum(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

inline bool isHex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

inline bool startsWithNoCase(const std::string& s, const char* prefix) {
    size_t i = 0;
    for (; prefix[i]; i++) {
        if (i >= s.size()) return false;
        char c = s[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c != prefix[i]) return false;
    }
    return true;
}

}  // namespace setup_form

// Accepts "host", "host:port" and pasted URLs "http://host[:port][/]".
// Hosts are names or IPv4 (letters, digits, '.', '-'); no other scheme, no
// path. Missing port = defPort.
inline bool parseHostPort(const std::string& in, uint16_t defPort, HostPort& out) {
    using namespace setup_form;
    std::string s = trim(in);
    if (startsWithNoCase(s, "http://")) s = s.substr(7);
    if (s.find("://") != std::string::npos) return false;   // https:// etc.
    if (!s.empty() && s[s.size() - 1] == '/') s.erase(s.size() - 1);
    std::string host = s;
    uint32_t port = defPort;
    const size_t colon = s.find(':');
    if (colon != std::string::npos) {
        host = s.substr(0, colon);
        const std::string p = s.substr(colon + 1);
        if (p.empty() || p.size() > 5) return false;
        port = 0;
        for (char c : p) {
            if (c < '0' || c > '9') return false;
            port = port * 10 + (uint32_t)(c - '0');
        }
        if (port == 0 || port > 65535) return false;
    }
    if (host.empty() || host.size() > 253 || !isAlnum(host[0])) return false;
    for (char c : host)
        if (!isAlnum(c) && c != '.' && c != '-') return false;
    out.host = host;
    out.port = (uint16_t)port;
    return true;
}

inline std::string hostPortString(const HostPort& hp) {
    return hp.host + ":" + std::to_string(hp.port);
}

inline int findAgent(const std::vector<Agent>& agents, const std::string& profile) {
    for (size_t i = 0; i < agents.size(); i++)
        if (agents[i].profile == profile) return (int)i;
    return -1;
}

namespace setup_form {

inline const char* keyError(const std::string& key) {
    if (key.empty()) return "Enter the Hermes API key";
    if (key.size() > 256) return "Hermes API key is too long";
    for (char c : key)
        if (c <= ' ' || c == 0x7f) return "Hermes API key has spaces or invalid characters";
    return nullptr;
}

// A look is empty or LOOK_HEX_LEN hex digits; `norm` receives it lowercased.
inline const char* lookError(const std::string& look, std::string* norm = nullptr) {
    std::string n;
    if (!look.empty()) {
        if (look.size() != LOOK_HEX_LEN) return "The character look is malformed";
        for (char c : look) {
            if (!isHex(c)) return "The character look is malformed";
            n += (c >= 'A' && c <= 'F') ? (char)(c - 'A' + 'a') : c;
        }
    }
    if (norm) *norm = n;
    return nullptr;
}

inline const char* profileError(const std::string& profile) {
    if (profile.size() > 32) return "Agent (profile) name is too long";
    for (char c : profile)
        if (!isAlnum(c) && c != '-' && c != '_')
            return "Agent (profile) names use letters, digits, - and _";
    return nullptr;
}

}  // namespace setup_form

// Validates a submission against the stored settings. Returns nullptr and
// fills `out` on success, else a message for the user (`out` untouched).
//   - open: the network has no password; a typed password is an error
//     rather than silently dropped. Otherwise a blank password keeps the
//     stored one when the network is unchanged; a new network needs one.
//   - Blank shim = the Hermes host.
//   - A different Hermes host (canonical host:port) drops every stored
//     agent: their keys were issued by the old gateway and must never be
//     sent to another. The submission has to add an agent for the new host.
//   - Agents: `forget` removes, then `addKey` adds the `addProfile` agent
//     (or replaces the key of an agent already stored) and makes it active.
//     A name without a key selects a stored agent; an unknown name without a
//     key is an error (the profile must exist on Hermes with its own key).
//     Without an add, `use` picks the active agent (a `use` that is also
//     being removed falls through); otherwise the stored active agent stays
//     when it still exists, else the first one.
//   - A look (`lookSet`) goes with the agent being added, re-keyed or
//     selected; "" clears it. Without `lookSet` an agent keeps its look
//     (re-keying does not reset it). It needs that agent in the submission.
//   - At least one and at most MAX_AGENTS agents.
inline const char* mergeSettings(const DeviceSettings& cur, const SettingsInput& in,
                                 DeviceSettings& out) {
    using namespace setup_form;
    DeviceSettings r;

    r.ssid = in.ssid;                               // SSIDs may carry spaces: no trim
    if (r.ssid.empty()) return "Enter the WiFi network name";
    if (r.ssid.size() > 32 || hasControl(r.ssid)) return "WiFi network name is invalid";

    r.pass = in.pass;
    if (in.open) {
        if (!r.pass.empty()) return "Open network: leave the password blank";
    } else {
        if (r.pass.empty() && r.ssid == cur.ssid) r.pass = cur.pass;
        if (r.pass.empty()) return "Enter the WiFi password, or tick Open network";
        bool hex64 = r.pass.size() == 64;
        for (char c : r.pass) hex64 = hex64 && isHex(c);
        const bool ascii = r.pass.size() >= 8 && r.pass.size() <= 63 && !hasControl(r.pass);
        if (!hex64 && !ascii) return "WiFi password must be 8-63 characters";
    }

    HostPort hp;
    const std::string hermes = trim(in.hermes);
    if (hermes.empty()) return "Enter the Hermes host";
    if (!parseHostPort(hermes, HERMES_PORT_DEFAULT, hp))
        return "Hermes host must look like name or name:port (http only)";
    r.hermes = hostPortString(hp);

    const std::string shim = trim(in.shim);
    if (!shim.empty()) {
        if (!parseHostPort(shim, SHIM_PORT_DEFAULT, hp))
            return "Audio shim host must look like name or name:port (http only)";
        r.shim = hostPortString(hp);
    }

    // Agent keys belong to the gateway that issued them: a different host
    // starts an empty roster, so no stored key is ever sent to it. Removals
    // of the old agents are moot then.
    const bool newGateway = !cur.hermes.empty() && r.hermes != cur.hermes;
    if (!newGateway) {
        r.agents = cur.agents;
        for (const std::string& f : in.forget) {
            const int i = findAgent(r.agents, trim(f));
            if (i < 0) return "That agent isn't on this device";
            r.agents.erase(r.agents.begin() + i);
        }
    }

    std::string active;
    bool haveActive = false;
    const std::string addProfile = trim(in.addProfile), addKey = trim(in.addKey);
    if (const char* e = profileError(addProfile)) return e;
    std::string look;
    if (in.lookSet) {
        if (const char* e = lookError(trim(in.look), &look)) return e;
        if (addKey.empty() && addProfile.empty())
            return "A character look needs the agent's profile name";
    }
    if (!addKey.empty()) {
        if (const char* e = keyError(addKey)) return e;
        const int i = findAgent(r.agents, addProfile);
        if (i >= 0) {
            r.agents[i].key = addKey;
        } else {
            if (r.agents.size() >= MAX_AGENTS)
                return "This device holds up to 4 agents; remove one first";
            r.agents.push_back(Agent{addProfile, addKey, ""});
        }
        if (in.lookSet) r.agents[findAgent(r.agents, addProfile)].look = look;
        active = addProfile;
        haveActive = true;
    } else if (!addProfile.empty()) {
        if (findAgent(r.agents, addProfile) < 0)
            return "Enter the API key for the new agent. Its profile must already exist "
                   "on Hermes: host/pair.py --create NAME makes one and sends the key";
        if (in.lookSet) r.agents[findAgent(r.agents, addProfile)].look = look;
        active = addProfile;
        haveActive = true;
    } else if (in.useSet) {
        // Marked "use" and also removed (the form's default when removing the
        // active agent): the first remaining agent takes over.
        if (findAgent(cur.agents, trim(in.use)) < 0) return "That agent isn't on this device";
        active = trim(in.use);
        haveActive = findAgent(r.agents, active) >= 0;
    } else if (cur.active < cur.agents.size()) {
        active = cur.agents[cur.active].profile;
        haveActive = findAgent(r.agents, active) >= 0;
    }
    if (r.agents.empty())
        return newGateway ? "A new Hermes host needs an agent from it: enter its profile name "
                            "and API key (agents of the previous host are removed)"
                          : "Add a Hermes agent: its profile name and API key";
    r.active = haveActive ? (size_t)findAgent(r.agents, active) : 0;

    out = r;
    return nullptr;
}

// LittleFS name of an agent's resume file: per gateway and profile, so a
// session id is only ever sent back to the gateway and profile that issued
// it. FNV-1a of the canonical host:port keeps the name short.
inline std::string sessionFileName(const std::string& hermes, const std::string& profile) {
    uint32_t h = 2166136261u;
    for (unsigned char c : hermes) h = (h ^ c) * 16777619u;
    static const char DIGITS[] = "0123456789abcdef";   // not HEX: an Arduino macro
    std::string name = "/s-";
    for (int i = 7; i >= 0; i--) name += DIGITS[(h >> (i * 4)) & 0xF];
    return name + "-" + agentLabel(profile) + ".txt";   // ≤ 48 chars
}

// Stored settings are usable only if they would pass the form on their own
// (an empty stored password is an open network) and every agent is valid.
inline bool settingsUsable(const DeviceSettings& s) {
    using namespace setup_form;
    if (s.agents.empty() || s.agents.size() > MAX_AGENTS || s.active >= s.agents.size())
        return false;
    for (size_t i = 0; i < s.agents.size(); i++) {
        if (keyError(s.agents[i].key) || profileError(s.agents[i].profile)) return false;
        if (lookError(s.agents[i].look)) return false;
        if (findAgent(s.agents, s.agents[i].profile) != (int)i) return false;   // duplicate
    }
    SettingsInput in;
    in.ssid = s.ssid;
    in.pass = s.pass;
    in.open = s.pass.empty();
    in.hermes = s.hermes;
    in.shim = s.shim;
    DeviceSettings ignored;
    return mergeSettings(s, in, ignored) == nullptr;
}

inline std::string htmlEscape(const std::string& s) {
    std::string r;
    r.reserve(s.size());
    for (char c : s) {
        switch (c) {
        case '&': r += "&amp;"; break;
        case '<': r += "&lt;"; break;
        case '>': r += "&gt;"; break;
        case '"': r += "&quot;"; break;
        case '\'': r += "&#39;"; break;
        default: r += c;
        }
    }
    return r;
}

// Payload of a "join WiFi" QR code (phone cameras understand it).
inline std::string wifiQrPayload(const std::string& ssid, const std::string& pass) {
    std::string r = "WIFI:T:WPA;S:";
    const std::string* parts[] = {&ssid, &pass};
    for (int i = 0; i < 2; i++) {
        for (char c : *parts[i]) {
            if (c == '\\' || c == ';' || c == ',' || c == ':' || c == '"') r += '\\';
            r += c;
        }
        r += i == 0 ? ";P:" : ";;";
    }
    return r;
}

// ---- portal access control -------------------------------------------------

// The soft-AP password is read off the screen and typed: unambiguous
// characters only (no 0/1/l/o/i, no m: the widest glyph, so the password's
// own screen line always fits the text column).
static const char PORTAL_PASS_ALPHABET[] = "23456789abcdefghjknpqrstuvwxyz";
static const size_t PORTAL_PASS_LEN = 12;

// `rnd` yields uniformly random 32-bit words (the modulo bias over a 30-letter
// alphabet is 2^-27, not worth a rejection loop).
template <typename Rng>
inline std::string portalPassword(Rng rnd) {
    std::string p;
    for (size_t i = 0; i < PORTAL_PASS_LEN; i++)
        p += PORTAL_PASS_ALPHABET[rnd() % (sizeof(PORTAL_PASS_ALPHABET) - 1)];
    return p;
}

// Lowercase hex of random bytes: the portal's per-run form token.
inline std::string hexString(const uint8_t* bytes, size_t n) {
    static const char DIGITS[] = "0123456789abcdef";
    std::string r;
    for (size_t i = 0; i < n; i++) {
        r += DIGITS[bytes[i] >> 4];
        r += DIGITS[bytes[i] & 15];
    }
    return r;
}

// /save must carry the token the form was served with. Constant-time compare;
// an empty expected token (not generated yet) matches nothing.
inline bool tokenMatches(const std::string& expected, const std::string& given) {
    if (expected.empty() || expected.size() != given.size()) return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < expected.size(); i++)
        diff |= (unsigned char)(expected[i] ^ given[i]);
    return diff == 0;
}

// A request that names an Origin (every browser form POST does) must name the
// portal's own address, so a page on another site can't submit the form.
// `hasOrigin` false = the header is absent (curl, tools): allowed, the token
// is the real check.
inline bool originAllowed(bool hasOrigin, const std::string& origin, const std::string& host) {
    if (!hasOrigin) return true;
    return origin == "http://" + host || origin == "http://" + host + ":80";
}

// A portal run ends on its own: with usable settings stored (it was opened
// from the menu) after `idleMs` without a phone associated, and at most
// `maxMs` after it started whatever a phone does. Without usable settings it
// stays up: there is nothing to go back to. Wrap-safe in millis().
inline bool portalExpired(bool settingsReady, uint32_t now, uint32_t startedMs,
                          uint32_t lastActivityMs, uint32_t idleMs, uint32_t maxMs) {
    if (!settingsReady) return false;
    return (uint32_t)(now - lastActivityMs) > idleMs || (uint32_t)(now - startedMs) > maxMs;
}
