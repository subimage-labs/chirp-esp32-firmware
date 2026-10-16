// Host tests for the device settings rules. Run: firmware/test/host/run.sh
#include "setup_form.h"

#include <cstdio>
#include <cstring>

static int failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,   \
                         __LINE__, #cond);                                \
            failures++;                                                   \
        }                                                                 \
    } while (0)

static bool hp(const char* in, uint16_t def, const char* host, uint16_t port) {
    HostPort r{"", 0};
    return parseHostPort(in, def, r) && r.host == host && r.port == port;
}

static bool hpRejects(const char* in) {
    HostPort r{"untouched", 1};
    return !parseHostPort(in, 8642, r) && r.host == "untouched";
}

static void hostPort() {
    CHECK(hp("gateway", 8642, "gateway", 8642));
    CHECK(hp("gateway.lan:9000", 8642, "gateway.lan", 9000));
    CHECK(hp("  http://Gateway-1:8642/ ", 8643, "Gateway-1", 8642));   // pasted URL
    CHECK(hp("HTTP://box", 8643, "box", 8643));
    CHECK(hp("box:65535", 1, "box", 65535));
    CHECK(hpRejects("https://box"));          // device speaks plain http only
    CHECK(hpRejects("ftp://box"));
    CHECK(hpRejects("box/p/alpha"));           // the profile is its own field
    CHECK(hpRejects("box:0"));
    CHECK(hpRejects("box:65536"));
    CHECK(hpRejects("box:80a"));
    CHECK(hpRejects("box:"));
    CHECK(hpRejects(":8642"));
    CHECK(hpRejects(""));
    CHECK(hpRejects("-box"));
    CHECK(hpRejects("bad host"));
    CHECK(hpRejects("box\r\nX-Injected: 1"));
}

static DeviceSettings stored() {
    DeviceSettings s;
    s.ssid = "Home";
    s.pass = "storedpass";
    s.hermes = "gw:8642";
    s.agents = {Agent{"alpha", "storedkey"}};
    return s;
}

static DeviceSettings twoAgents() {
    DeviceSettings s = stored();
    s.agents.push_back(Agent{"", "defaultkey"});      // the Hermes default profile
    s.active = 1;
    return s;
}

// A form submission: WiFi + hosts, optionally adding one agent.
static SettingsInput form(const char* ssid, const char* pass, const char* hermes,
                          const char* addProfile = "", const char* addKey = "",
                          const char* shim = "") {
    SettingsInput in;
    in.ssid = ssid;
    in.pass = pass;
    in.hermes = hermes;
    in.addProfile = addProfile;
    in.addKey = addKey;
    in.shim = shim;
    return in;
}

static SettingsInput opened(SettingsInput in) {
    in.open = true;
    return in;
}

static const std::string& activeProfile(const DeviceSettings& s) {
    return s.agents[s.active].profile;
}

static void blankSecretsKeepStored() {
    DeviceSettings out;
    CHECK(mergeSettings(stored(), form("Home", "", "gw"), out) == nullptr);
    CHECK(out.pass == "storedpass");
    CHECK(out.agents.size() == 1 && out.agents[0].key == "storedkey");
}

// The stored password never follows to another network.
static void newNetworkNeedsPassword() {
    DeviceSettings out;
    CHECK(mergeSettings(stored(), form("Cafe", "", "gw"), out) != nullptr);
    CHECK(mergeSettings(stored(), opened(form("Cafe", "", "gw")), out) == nullptr);
    CHECK(out.pass.empty());
}

static void openNetwork() {
    DeviceSettings out;
    CHECK(mergeSettings(stored(), opened(form("Home", "", "gw")), out) == nullptr);
    CHECK(out.pass.empty());                  // clears a stored password on the same SSID
    CHECK(mergeSettings(stored(), opened(form("Home", "typedpass", "gw")), out) != nullptr);
    DeviceSettings wasOpen = stored();
    wasOpen.ssid = "Open";
    wasOpen.pass = "";
    CHECK(mergeSettings(wasOpen, form("Open", "", "gw"), out) != nullptr);   // unticked: needs one
}

static void newValuesReplaceStored() {
    DeviceSettings out;
    CHECK(mergeSettings(stored(), form("Home", "newpassword", "http://gw2:1234/", "alpha", "k2",
                                       "shimbox"),
                        out) == nullptr);
    CHECK(out.pass == "newpassword");
    CHECK(out.hermes == "gw2:1234");
    CHECK(out.shim == "shimbox:8643");
}

static void firstSetup() {
    DeviceSettings out;
    CHECK(mergeSettings(DeviceSettings(), form("Home", "password1", "gw", "", " k "), out) == nullptr);
    CHECK(out.hermes == "gw:8642");
    CHECK(out.agents.size() == 1 && out.agents[0].profile.empty());   // = Hermes default profile
    CHECK(out.agents[0].key == "k" && out.active == 0);
    CHECK(out.shim.empty());                  // = the Hermes host
    // No agent anywhere: the device can't talk to Hermes.
    CHECK(mergeSettings(DeviceSettings(), form("Home", "password1", "gw"), out) != nullptr);
}

static void passwordRules() {
    DeviceSettings out;
    CHECK(mergeSettings(stored(), form("Home", "short", "gw"), out) != nullptr);
    CHECK(mergeSettings(stored(), form("Home", std::string(64, 'a').c_str(), "gw"), out) == nullptr);
    CHECK(mergeSettings(stored(), form("Home", std::string(64, 'z').c_str(), "gw"), out) != nullptr);
    CHECK(mergeSettings(stored(), form("Home", std::string(63, 'z').c_str(), "gw"), out) == nullptr);
    CHECK(mergeSettings(stored(), form("Home", "pass\nword123", "gw"), out) != nullptr);
}

static void requiredFields() {
    DeviceSettings out;
    CHECK(mergeSettings(stored(), form("", "", "gw"), out) != nullptr);
    CHECK(mergeSettings(stored(), form(std::string(33, 's').c_str(), "", "gw"), out) != nullptr);
    CHECK(mergeSettings(stored(), form("Home", "", " "), out) != nullptr);
}

// The key, profile and hosts go into HTTP requests: nothing may split a
// header line or escape the /p/<profile> path.
static void headerInjectionRejected() {
    DeviceSettings out;
    CHECK(mergeSettings(stored(), form("Home", "", "gw", "x", "k\r\nX-Evil: 1"), out) != nullptr);
    CHECK(mergeSettings(stored(), form("Home", "", "gw", "x", "two words"), out) != nullptr);
    CHECK(mergeSettings(stored(), form("Home", "", "gw", "", "", "s\nX: 1"), out) != nullptr);
    CHECK(mergeSettings(stored(), form("Home", "", "gw", "alpha/../x", "k"), out) != nullptr);
}

static void failureLeavesOutUntouched() {
    DeviceSettings out = stored();
    CHECK(mergeSettings(twoAgents(), form("Home", "", "https://gw"), out) != nullptr);
    CHECK(out.ssid == "Home" && out.hermes == "gw:8642" && out.agents.size() == 1);
}

// Adding an agent keeps the others and switches to it; a stored name with a
// new key re-keys it in place.
static void addAndRekey() {
    DeviceSettings out;
    CHECK(mergeSettings(stored(), form("Home", "", "gw", "work", "workkey"), out) == nullptr);
    CHECK(out.agents.size() == 2 && activeProfile(out) == "work");
    CHECK(out.agents[0].profile == "alpha" && out.agents[0].key == "storedkey");
    CHECK(mergeSettings(twoAgents(), form("Home", "", "gw", "alpha", "newkey"), out) == nullptr);
    CHECK(out.agents.size() == 2 && out.agents[0].key == "newkey");
    CHECK(activeProfile(out) == "alpha");
}

// Typing a name that isn't stored, without its key, doesn't create anything:
// the profile and its key have to exist on Hermes first.
static void unknownNameNeedsKey() {
    DeviceSettings out = stored();
    CHECK(mergeSettings(stored(), form("Home", "", "gw", "newagent"), out) != nullptr);
    CHECK(out.agents.size() == 1);
    // A stored name alone just selects it.
    CHECK(mergeSettings(twoAgents(), form("Home", "", "gw", "alpha"), out) == nullptr);
    CHECK(activeProfile(out) == "alpha" && out.agents[0].key == "storedkey");
}

static void atMostFourAgents() {
    DeviceSettings full = stored();
    full.agents = {Agent{"a", "k1"}, Agent{"b", "k2"}, Agent{"c", "k3"}, Agent{"d", "k4"}};
    DeviceSettings out;
    CHECK(mergeSettings(full, form("Home", "", "gw", "e", "k5"), out) != nullptr);
    CHECK(mergeSettings(full, form("Home", "", "gw", "d", "k9"), out) == nullptr);   // re-key fits
    SettingsInput swap = form("Home", "", "gw", "e", "k5");
    swap.forget = {"a"};                                       // remove one, add one
    CHECK(mergeSettings(full, swap, out) == nullptr);
    CHECK(out.agents.size() == 4 && activeProfile(out) == "e" && findAgent(out.agents, "a") < 0);
}

static void useAndForget() {
    DeviceSettings out;
    SettingsInput in = form("Home", "", "gw");
    in.useSet = true;
    in.use = "alpha";
    CHECK(mergeSettings(twoAgents(), in, out) == nullptr && activeProfile(out) == "alpha");
    in.use = "nobody";
    CHECK(mergeSettings(twoAgents(), in, out) != nullptr);

    // Removing the active agent (still marked use, as the form sends it)
    // hands over to the first remaining one.
    in = form("Home", "", "gw");
    in.useSet = true;
    in.use = "";
    in.forget = {""};
    CHECK(mergeSettings(twoAgents(), in, out) == nullptr);
    CHECK(out.agents.size() == 1 && activeProfile(out) == "alpha");

    // Without `use`, the stored active agent stays active by name.
    in = form("Home", "", "gw");
    in.forget = {"alpha"};
    CHECK(mergeSettings(twoAgents(), in, out) == nullptr);
    CHECK(out.agents.size() == 1 && out.agents[0].profile.empty() && out.active == 0);

    in.forget = {"alpha", ""};                                  // the last one can't go
    CHECK(mergeSettings(twoAgents(), in, out) != nullptr);
    in.forget = {"ghost"};
    CHECK(mergeSettings(twoAgents(), in, out) != nullptr);
}

// Keys were issued by one gateway: a new host never inherits them.
static void newGatewayReplacesAgents() {
    DeviceSettings out = stored();
    // Host changed, no agent for it: rejected, nothing stored changes.
    CHECK(mergeSettings(twoAgents(), form("Home", "", "other"), out) != nullptr);
    CHECK(out.hermes == "gw:8642" && out.agents.size() == 1);
    // Selecting an old agent doesn't carry it over either.
    SettingsInput in = form("Home", "", "other");
    in.useSet = true;
    in.use = "alpha";
    CHECK(mergeSettings(twoAgents(), in, out) != nullptr);
    in = form("Home", "", "other", "alpha");                 // stored name, no key
    CHECK(mergeSettings(twoAgents(), in, out) != nullptr);
    // With an agent for the new host: it is the only one, even under an old name.
    in = form("Home", "", "other:9000", "alpha", "otherkey");
    in.forget = {""};                                       // stale removals are moot
    CHECK(mergeSettings(twoAgents(), in, out) == nullptr);
    CHECK(out.hermes == "other:9000" && out.agents.size() == 1);
    CHECK(out.agents[0].key == "otherkey" && out.active == 0);
    // The same host spelled differently is not a new gateway.
    CHECK(mergeSettings(twoAgents(), form("Home", "", "http://gw:8642/"), out) == nullptr);
    CHECK(out.agents.size() == 2);
}

// An agent's character look (host/pair.py): 24 hex digits of trait indexes.
static const char* const LOOK = "ffffffff0300ffff0107ff02";

static void characterLook() {
    DeviceSettings out;
    // Adding an agent with a look stores it (normalized to lowercase).
    SettingsInput in = form("Home", "", "gw:8642", "work", "workkey");
    in.lookSet = true;
    in.look = "FFFFFFFF0300FFFF0107FF02";
    CHECK(mergeSettings(stored(), in, out) == nullptr);
    CHECK(out.agents.size() == 2 && out.agents[1].profile == "work" && out.agents[1].look == LOOK);
    CHECK(out.agents[0].look.empty());
    CHECK(settingsUsable(out));
    // Re-keying without a look keeps it; with look "" clears it; a new look replaces it.
    DeviceSettings withLook = out;
    CHECK(mergeSettings(withLook, form("Home", "", "gw:8642", "work", "newkey"), out) == nullptr);
    CHECK(out.agents[1].key == "newkey" && out.agents[1].look == LOOK);
    in = form("Home", "", "gw:8642", "work", "newkey");
    in.lookSet = true;
    CHECK(mergeSettings(withLook, in, out) == nullptr && out.agents[1].look.empty());
    in.look = "00000000000000000000000a";
    CHECK(mergeSettings(withLook, in, out) == nullptr &&
          out.agents[1].look == "00000000000000000000000a");
    // Selecting a stored agent can set its look too, without a key.
    in = form("Home", "", "gw:8642", stored().agents[0].profile.c_str());
    in.lookSet = true;
    in.look = LOOK;
    CHECK(mergeSettings(stored(), in, out) == nullptr && out.agents[0].look == LOOK);
    // Unrelated changes (WiFi, switching agent) keep every look.
    CHECK(mergeSettings(withLook, form("Cafe", "newpassword", "gw:8642"), out) == nullptr);
    CHECK(out.agents[1].look == LOOK);
    // The default profile can carry one (empty name, key present).
    in = form("Home", "", "gw:8642", "", "defkey");
    in.lookSet = true;
    in.look = LOOK;
    CHECK(mergeSettings(stored(), in, out) == nullptr);
    CHECK(findAgent(out.agents, "") >= 0 && out.agents[findAgent(out.agents, "")].look == LOOK);
    // Malformed looks and looks without an agent are errors.
    for (const char* bad : {"ffffffff0300ffff0107ff0", "ffffffff0300ffff0107ff022", "zzzzzzzzzzzzzzzzzzzzzzzz",
                            "ffffffff0300ffff0107ff0 ", "ffffffff0300ffff0107ff0\n"}) {
        in = form("Home", "", "gw:8642", "work", "workkey");
        in.lookSet = true;
        in.look = bad;
        CHECK(mergeSettings(stored(), in, out) != nullptr);
    }
    in = form("Home", "", "gw:8642");
    in.lookSet = true;
    in.look = LOOK;
    CHECK(mergeSettings(stored(), in, out) != nullptr);
    // A stored malformed look makes the settings unusable (they would not load).
    DeviceSettings s = stored();
    s.agents[0].look = "nothex";
    CHECK(!settingsUsable(s));
    s.agents[0].look = LOOK;
    CHECK(settingsUsable(s));
    // A new gateway drops stored agents and their looks; the added agent brings its own.
    in = form("Home", "", "other:9000", "work", "k2");
    in.lookSet = true;
    in.look = LOOK;
    CHECK(mergeSettings(withLook, in, out) == nullptr);
    CHECK(out.agents.size() == 1 && out.agents[0].look == LOOK);
    in = form("Home", "", "other:9000", "work", "k2");
    CHECK(mergeSettings(withLook, in, out) == nullptr && out.agents[0].look.empty());
    // The look is exactly one trait byte per avatar field.
    CHECK(LOOK_HEX_LEN == 24);
}

static void sessionFiles() {
    const std::string a = sessionFileName("gw:8642", "alpha");
    CHECK(a == sessionFileName("gw:8642", "alpha"));
    CHECK(a != sessionFileName("gw:9000", "alpha"));          // same profile, other gateway
    CHECK(a != sessionFileName("gw:8642", "work"));
    CHECK(sessionFileName("gw:8642", "") != sessionFileName("gw:8642", "x"));
    const std::string longest = sessionFileName(std::string(253, 'h') + ":65535",
                                                std::string(32, 'p'));
    CHECK(longest.size() <= 48 && longest[0] == '/');        // LittleFS names cap at 64
}

static void usable() {
    CHECK(!settingsUsable(DeviceSettings()));
    CHECK(settingsUsable(stored()));
    CHECK(settingsUsable(twoAgents()));
    DeviceSettings s = stored();
    s.ssid = "Open";
    s.pass = "";
    CHECK(settingsUsable(s));                                   // open network
    s = stored();
    s.pass = "pw";
    CHECK(!settingsUsable(s));                                  // too short
    s = twoAgents();
    s.active = 2;
    CHECK(!settingsUsable(s));                                  // active out of range
    s = twoAgents();
    s.agents[1].profile = "alpha";
    CHECK(!settingsUsable(s));                                  // duplicate profile
    s = stored();
    s.agents[0].key = "";
    CHECK(!settingsUsable(s));
    s = stored();
    s.agents = {Agent{"a", "k"}, Agent{"b", "k"}, Agent{"c", "k"}, Agent{"d", "k"},
                Agent{"e", "k"}};
    CHECK(!settingsUsable(s));
}

static void escaping() {
    CHECK(htmlEscape("\"><script>&'") == "&quot;&gt;&lt;script&gt;&amp;&#39;");
    CHECK(wifiQrPayload("Chirp-AB12", "abcd2345") == "WIFI:T:WPA;S:Chirp-AB12;P:abcd2345;;");
    CHECK(wifiQrPayload("a;b:c", "p\\\"q,") == "WIFI:T:WPA;S:a\\;b\\:c;P:p\\\\\\\"q\\,;;");
}

// The portal's AP password, form token, Origin check and lifetime.
static void portalAccess() {
    // 12 characters, unambiguous alphabet only, drawn from the generator's words.
    uint32_t n = 0;
    const std::string pw = portalPassword([&n] { return n++ * 2654435761u; });
    CHECK(pw.size() == 12 && PORTAL_PASS_LEN == 12);
    for (char c : pw) CHECK(std::string(PORTAL_PASS_ALPHABET).find(c) != std::string::npos);
    for (const char* bad = "01lLoOiIm"; *bad; bad++)
        CHECK(std::string(PORTAL_PASS_ALPHABET).find(*bad) == std::string::npos);
    CHECK(portalPassword([] { return 0u; }) == std::string(12, '2'));
    CHECK(portalPassword([] { return 0xFFFFFFFFu; }).size() == 12);   // index stays in range

    const uint8_t raw[] = {0x00, 0x0f, 0xa5, 0xff};
    CHECK(hexString(raw, 4) == "000fa5ff" && hexString(raw, 0).empty());

    CHECK(tokenMatches("0123abcd", "0123abcd"));
    CHECK(!tokenMatches("0123abcd", "0123abce"));
    CHECK(!tokenMatches("0123abcd", "0123abc"));
    CHECK(!tokenMatches("0123abcd", ""));
    CHECK(!tokenMatches("", ""));                      // no token generated: nothing matches

    const std::string host = "198.51.100.1";
    CHECK(originAllowed(false, "", host));             // no Origin header (tools)
    CHECK(originAllowed(true, "http://198.51.100.1", host));
    CHECK(originAllowed(true, "http://198.51.100.1:80", host));
    CHECK(!originAllowed(true, "http://evil.example", host));
    CHECK(!originAllowed(true, "null", host));
    CHECK(!originAllowed(true, "", host));             // present but empty
    CHECK(!originAllowed(true, "https://198.51.100.1", host));
    CHECK(!originAllowed(true, "http://198.51.100.1.evil.example", host));
    CHECK(!originAllowed(true, "http://198.51.100.1:8080", host));

    const uint32_t idle = 600000, max = 900000;
    CHECK(!portalExpired(false, 5000000, 0, 0, idle, max));          // first setup never times out
    CHECK(!portalExpired(true, 1000, 0, 0, idle, max));
    CHECK(portalExpired(true, idle + 1, 0, 0, idle, max));           // idle
    CHECK(!portalExpired(true, idle, 0, 0, idle, max));
    CHECK(!portalExpired(true, 800000, 0, 799000, idle, max));       // a phone is around
    CHECK(portalExpired(true, max + 1, 0, max, idle, max));          // ...but the cap still ends it
    CHECK(!portalExpired(true, max, 0, max, idle, max));
    // millis() wrapping: started 5 s before the wrap, now 5 s after.
    CHECK(!portalExpired(true, 5000, 0xFFFFEC78u, 5000, idle, max));
    CHECK(portalExpired(true, max, 0xFFFFEC78u, max, idle, max));
}

int main() {
    hostPort();
    blankSecretsKeepStored();
    newNetworkNeedsPassword();
    openNetwork();
    newValuesReplaceStored();
    firstSetup();
    passwordRules();
    requiredFields();
    headerInjectionRejected();
    failureLeavesOutUntouched();
    addAndRekey();
    unknownNameNeedsKey();
    atMostFourAgents();
    useAndForget();
    newGatewayReplacesAgents();
    characterLook();
    sessionFiles();
    usable();
    escaping();
    portalAccess();
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("setup_form: all checks passed\n");
    return 0;
}
