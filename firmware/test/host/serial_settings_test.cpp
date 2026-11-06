#include "serial_settings.h"

#include <cstdio>
static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { \
    std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
    ++failures; } } while (0)

static DeviceSettings stored() {
    DeviceSettings s;
    s.networks = {{"Home", "pw12345678"}, {"Cafe", ""}};
    s.hermes = "gw:8642"; s.shim = "sh:8643"; s.backend = "local";
    s.venice = "https://v.example:443"; s.agents = {Agent{"alpha", "AK"}};
    return s;
}
int main() {
    auto s = stored();
    SerialMembers m;
    auto in = settingsInputFromMembers(m, s);
    CHECK(in.ssid.empty() && in.pass.empty() && !in.open);
    CHECK(in.hermes == "gw:8642" && in.shim == "sh:8643");
    CHECK(in.backend == "local" && !in.backendSet);
    CHECK(in.venice == "https://v.example:443" && in.veniceKey.empty());
    CHECK(in.addProfile.empty() && in.addKey.empty());
    CHECK(!in.lookSet && in.look.empty() && !in.useSet && in.use.empty());
    CHECK(in.forget.empty() && in.forgetNetworks.empty());
    for (const char* name : {"Home", "Cafe", "New", "cafe", " Café "}) {
        m = {}; m.ssid = someStr(name);
        in = settingsInputFromMembers(m, s);
        CHECK(in.ssid == name && in.pass.empty());
        CHECK(in.open == (std::string(name) == "Cafe"));
    }
    for (const char* name : {"Home", ""}) for (const char* pass : {"", "x"}) {
        m = {}; if (*name) m.ssid = someStr(name); m.pass = someStr(pass);
        in = settingsInputFromMembers(m, s);
        CHECK(in.ssid == name && in.pass == pass && in.open == !*pass);
    }
    for (const char* value : {"", "new-value"}) {
        m = {}; m.hermes = m.shim = m.venice = m.veniceKey = someStr(value);
        m.profile = m.key = m.look = m.use = someStr(value);
        in = settingsInputFromMembers(m, s);
        CHECK(in.hermes == value && in.shim == value && in.venice == value);
        CHECK(in.veniceKey == value && in.addProfile == value && in.addKey == value);
        CHECK(in.lookSet && in.look == value && in.useSet && in.use == value);
    }
    for (const char* backend : {"", "local", "venice"}) {
        s.backend = backend; m = {};
        in = settingsInputFromMembers(m, s);
        CHECK(in.backend == (*backend ? backend : "local") && !in.backendSet);
        for (const char* value : {"", "venice"}) {
            m.backend = someStr(value); in = settingsInputFromMembers(m, s);
            CHECK(in.backendSet && in.backend == value);
        }
    }
    s = stored(); s.veniceKey = "STORED-KEY";
    m = {}; m.key.value = m.veniceKey.value = m.look.value = "not-present";
    in = settingsInputFromMembers(m, s);
    CHECK(in.addKey.empty() && in.veniceKey.empty() && in.look.empty() && !in.lookSet);
    m.forget = {"a", "", "b"}; m.forgetWifi = {"z", "", "a"};
    in = settingsInputFromMembers(m, s);
    CHECK(in.forget == m.forget && in.forgetNetworks == m.forgetWifi);

    s = stored(); DeviceSettings out;
    // Put Home second so this checks promotion as well as password retention.
    s.networks = {{"Cafe", ""}, {"Home", "pw12345678"}};
    m = {}; m.ssid = someStr("Home");
    CHECK(mergeSettings(s, settingsInputFromMembers(m, s), out) == nullptr);
    CHECK(out.networks.size() == 2 && out.networks[0].ssid == "Home" && out.networks[0].pass == "pw12345678");
    m.pass = someStr("");
    CHECK(mergeSettings(s, settingsInputFromMembers(m, s), out) == nullptr);
    CHECK(out.networks[0].ssid == "Home" && out.networks[0].pass.empty());
    m = {}; m.ssid = someStr("Cafe");
    CHECK(mergeSettings(s, settingsInputFromMembers(m, s), out) == nullptr);
    CHECK(out.networks[0].ssid == "Cafe" && out.networks[0].pass.empty());
    m.ssid = someStr("New");
    CHECK(mergeSettings(s, settingsInputFromMembers(m, s), out) != nullptr);
    m = {}; m.hermes = someStr("");
    CHECK(mergeSettings(s, settingsInputFromMembers(m, s), out) != nullptr);
    m = {}; m.backend = someStr("venice"); m.veniceKey = someStr("VK");
    CHECK(mergeSettings(s, settingsInputFromMembers(m, s), out) == nullptr);
    CHECK(out.backend == "venice" && out.veniceKey == "VK");
    auto withKey = out;
    SerialMembers keepKey; keepKey.backend = someStr("venice");
    CHECK(mergeSettings(withKey, settingsInputFromMembers(keepKey, withKey), out) == nullptr);
    CHECK(out.veniceKey == "VK");
    keepKey.veniceKey = someStr("");
    CHECK(mergeSettings(withKey, settingsInputFromMembers(keepKey, withKey), out) == nullptr);
    CHECK(out.veniceKey == "VK");
    m.veniceKey = {};
    CHECK(mergeSettings(s, settingsInputFromMembers(m, s), out) != nullptr);
#ifndef BENCH_CMDS
    m.veniceKey = someStr("VK"); m.venice = someStr("http://x:1");
    CHECK(mergeSettings(s, settingsInputFromMembers(m, s), out) != nullptr);
#endif
    m = {}; m.forget = {"alpha"};
    CHECK(mergeSettings(s, settingsInputFromMembers(m, s), out) != nullptr);
    m = {}; m.forgetWifi = {"Home"};
    CHECK(mergeSettings(s, settingsInputFromMembers(m, s), out) == nullptr);
    CHECK(out.networks.size() == 1 && out.networks[0].ssid == "Cafe");
    return failures ? 1 : 0;
}
