// wifi_pick.h — which saved WiFi network to join. Pure (no Arduino),
// host-tested. The device remembers several networks (setup_form.h,
// `DeviceSettings::networks`, most recently joined first); wifi_join.cpp
// scans, takes order() of the result as the sequence to try, joins one and
// calls promote() when the join worked. pick() is the first of that order
// that is actually in range.
#pragma once

#include <stddef.h>

#include <algorithm>
#include <string>
#include <vector>

#include "setup_form.h"

namespace wifi_pick {

struct Seen {
    std::string ssid;
    int rssi;                                   // dBm, closer to 0 = stronger
};

// Index into `known` of the saved network to join: the strongest one present
// in `scan` (exact name match; a network seen several times counts with its
// strongest rssi). Equal rssi: the more recently joined one (lower index).
// None in range, or `known` empty: -1.
inline int pick(const std::vector<setup_form::Network>& known, const std::vector<Seen>& scan) {
    int best = -1;
    int bestRssi = 0;
    for (size_t i = 0; i < known.size(); i++) {
        bool seen = false;
        int rssi = 0;
        for (const Seen& s : scan) {
            if (s.ssid != known[i].ssid) continue;
            if (!seen || s.rssi > rssi) rssi = s.rssi;
            seen = true;
        }
        if (seen && (best < 0 || rssi > bestRssi)) {     // strict >: ties keep the lower index
            best = (int)i;
            bestRssi = rssi;
        }
    }
    return best;
}

// The saved networks in the order to try them: those present in `scan` first,
// strongest first (a network seen several times counts with its strongest
// rssi; equal rssi: lower index first), then those not in `scan`, in saved
// order (they may be hidden or just out of range). Every index of `known`
// appears exactly once. Empty `known`: empty result.
inline std::vector<int> order(const std::vector<setup_form::Network>& known,
                              const std::vector<Seen>& scan) {
    std::vector<int> seenIdx, unseen;
    std::vector<int> rssiOf(known.size(), 0);
    for (size_t i = 0; i < known.size(); i++) {
        bool seen = false;
        for (const Seen& s : scan) {
            if (s.ssid != known[i].ssid) continue;
            if (!seen || s.rssi > rssiOf[i]) rssiOf[i] = s.rssi;
            seen = true;
        }
        (seen ? seenIdx : unseen).push_back((int)i);
    }
    std::stable_sort(seenIdx.begin(), seenIdx.end(),
                     [&](int a, int b) { return rssiOf[(size_t)a] > rssiOf[(size_t)b]; });
    seenIdx.insert(seenIdx.end(), unseen.begin(), unseen.end());
    return seenIdx;
}

// Moves known[idx] to the front, keeping the others' order. True when the
// order changed (the caller then saves the settings); false for idx 0 or out
// of range.
inline bool promote(std::vector<setup_form::Network>& known, int idx) {
    if (idx <= 0 || (size_t)idx >= known.size()) return false;
    const setup_form::Network n = known[(size_t)idx];
    known.erase(known.begin() + idx);
    known.insert(known.begin(), n);
    return true;
}

}  // namespace wifi_pick
