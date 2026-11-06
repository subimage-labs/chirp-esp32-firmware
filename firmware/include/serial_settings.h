// serial_settings.h — maps the members of a serial `settings {json}` line to
// a SettingsInput. Pure (no Arduino or ArduinoJson types), host-tested by
// serial_settings_test.cpp. settings.cpp parses the JSON and fills
// SerialMembers; this header holds the rules (present members replace the
// stored value, absent ones keep it, "pass" decides the open flag).
//
// Ownership: a pure function over its arguments (loop context).
#pragma once

#include <string>
#include <vector>

#include "setup_form.h"

// One JSON member. present == the member exists AND is a JSON string. A
// member of any other type (number, bool, null, object, array) is not present.
struct OptStr {
    bool present = false;
    std::string value;
};
inline OptStr someStr(const std::string& v) {
    OptStr s;
    s.present = true;
    s.value = v;
    return s;
}

struct SerialMembers {
    OptStr ssid, pass, hermes, shim, backend, venice, veniceKey, profile, key, look, use;
    std::vector<std::string> forget;       // agents to remove, already flattened
    std::vector<std::string> forgetWifi;   // saved networks to remove, already flattened
};

// stored = the merge base (the stored settings). Never fails.
inline SettingsInput settingsInputFromMembers(const SerialMembers& m, const DeviceSettings& stored) {
    SettingsInput in;
    // ssid / pass add or edit ONE network; absent ssid adds none. "pass":""
    // makes the network open; an absent pass keeps the saved password of the
    // same-named network (a saved open network stays open).
    in.ssid = m.ssid.present ? m.ssid.value : std::string();
    const int saved = findNetwork(stored.networks, in.ssid);
    in.open = m.pass.present ? m.pass.value.empty()
                             : saved >= 0 && stored.networks[saved].pass.empty();
    in.pass = m.pass.present ? m.pass.value : std::string();
    in.hermes = m.hermes.present ? m.hermes.value : stored.hermes;
    in.shim = m.shim.present ? m.shim.value : stored.shim;
    in.backend = m.backend.present ? m.backend.value
                                   : (stored.backend.empty() ? std::string("local") : stored.backend);
    in.backendSet = m.backend.present;
    in.venice = m.venice.present ? m.venice.value : stored.venice;
    in.veniceKey = m.veniceKey.present ? m.veniceKey.value : std::string();
    in.addProfile = m.profile.present ? m.profile.value : std::string();
    in.addKey = m.key.present ? m.key.value : std::string();
    in.lookSet = m.look.present;
    in.look = m.look.present ? m.look.value : std::string();
    in.useSet = m.use.present;
    in.use = m.use.present ? m.use.value : std::string();
    in.forget = m.forget;
    in.forgetNetworks = m.forgetWifi;
    return in;
}
