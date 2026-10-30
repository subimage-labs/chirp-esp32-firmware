#include "wifi_join.h"

#include <WiFi.h>

#include <vector>

#include "config.h"
#include "settings.h"
#include "wifi_pick.h"

static const unsigned SCAN_FAILS_BEFORE_BLIND_JOIN = 3;

static bool scanning = false;       // an async scan is in flight
static bool joinPending = false;    // a join was issued and has not connected yet
static uint32_t stampMs = 0;        // when the last join was issued or the last scan ended
static unsigned attempt = 0;        // k: consecutive joins that did not connect
static unsigned scanFails = 0;      // consecutive scans that failed
static uint32_t scanT0 = 0;         // when the scan in flight started

// Join order(known, seen)[attempt % n]: the strongest saved network in range
// first, then the next one at each retry, saved networks not seen last (they
// may be hidden). `seen` empty = the scan found nothing (or kept failing).
static void join(const std::vector<wifi_pick::Seen>& seen) {
    const std::vector<setup_form::Network>& nets = settingsStored().networks;
    if (nets.empty()) return;
    const std::vector<int> order = wifi_pick::order(nets, seen);
    const int idx = order[attempt % order.size()];
    Serial.printf("[WiFi] joining %d/%u (try %u)\n", idx + 1, (unsigned)nets.size(),
                  attempt + 1);
    const setup_form::Network& n = nets[(size_t)idx];
    WiFi.setAutoReconnect(true);                    // the stack may retry this one until the next tick
    WiFi.begin(n.ssid.c_str(), n.pass.empty() ? nullptr : n.pass.c_str());
    joinPending = true;
    stampMs = millis();
}

static void scanFailed(const char* why) {
    scanFails++;
    stampMs = millis() - WIFI_RETRY_MS - 1;         // it took its time already: rescan next tick
    Serial.printf("[WiFi] scan %s (%u/%u)\n", why, scanFails, SCAN_FAILS_BEFORE_BLIND_JOIN);
    if (scanFails >= SCAN_FAILS_BEFORE_BLIND_JOIN) {
        scanFails = 0;
        join({});                                   // as if the scan had found nothing
    }                                               // else: the next tick scans again
}

// A connect in flight, or the stack's own reconnect, would abort the scan:
// the station is dropped and stays down until the scan is over.
static void scanStart() {
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false, false);
    scanT0 = millis();
    // The core fails an async scan after 20 x max_ms_per_chan: its default (300 ms,
    // 6 s) cut off every scan here, which takes ~7.3 s (T-Deck, measured), so 500.
    if (WiFi.scanNetworks(true, false, false, 500) == WIFI_SCAN_FAILED) {
        scanFailed("failed to start");
        return;
    }
    scanning = true;
}

void wifiJoinBegin() {
    scanning = false;
    joinPending = false;
    attempt = 0;
    scanFails = 0;
    WiFi.mode(WIFI_STA);
    // One saved network: nothing to choose, join at once (a scan takes ~7 s).
    if (settingsStored().networks.size() == 1) join({});
    else scanStart();
}

void wifiJoinTick(bool mayRetry) {
    const uint32_t now = millis();
    if (WiFi.status() == WL_CONNECTED) {
        if (scanning) {                             // linked anyway: the scan is moot
            WiFi.scanDelete();
            scanning = false;
        }
        attempt = 0;
        scanFails = 0;
        if (joinPending) {
            joinPending = false;
            const std::vector<setup_form::Network>& nets = settingsStored().networks;
            const int idx = findNetwork(nets, WiFi.SSID().c_str());   // the network we are really on
            if (idx >= 0) {
                Serial.printf("[WiFi] joined %d/%u (rssi %d)\n", idx + 1, (unsigned)nets.size(),
                              WiFi.RSSI());
                settingsNetworkJoined(idx);
            }
        }
        return;
    }
    if (scanning) {
        const int16_t n = WiFi.scanComplete();      // the core fails a scan itself after 10 s
        if (n == WIFI_SCAN_RUNNING) return;
        scanning = false;
        Serial.printf("[WiFi] scan n=%d after %u ms\n", (int)n, (unsigned)(millis() - scanT0));
        if (n < 0) {
            WiFi.scanDelete();
            scanFailed("failed");
            return;
        }
        scanFails = 0;
        std::vector<wifi_pick::Seen> seen;
        for (int i = 0; i < n; i++) seen.push_back({WiFi.SSID(i).c_str(), WiFi.RSSI(i)});
        WiFi.scanDelete();
        join(seen);
        return;
    }
    if (mayRetry && now - stampMs > WIFI_RETRY_MS) {
        if (joinPending) {                          // it did not connect: next in order
            joinPending = false;
            attempt++;
        }
        scanStart();
    }
}
