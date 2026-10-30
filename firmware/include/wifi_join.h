// wifi_join.h — joins a saved WiFi network: at boot, after a light sleep and
// on every background retry while offline. Loop task only; this module owns
// all of its state (the scan/join state below).
//
// The saved networks are DeviceSettings::networks (setup_form.h, most
// recently joined first). A join is scan → wifi_pick::order → join one entry
// → wifi_pick::promote + save once the link is up (settings.h). The scan is
// the async one (WiFi.scanNetworks(true), polled with scanComplete()), so
// waiting for it never stalls the loop. Before a scan the station is
// disconnected and its own auto-reconnect is off, so nothing aborts the scan.
//
// A completed scan (0 or more results) joins order(known, seen)[k % n]: k
// counts consecutive joins that did not connect before the next retry tick
// (reset by a successful join), so a wrong password on the strongest network
// moves on to the next one, and a saved hidden network gets its turn. A scan
// that fails to start or reports WIFI_SCAN_FAILED (the core's own 6 s
// timeout included) joins nothing: the next retry tick scans again; after 3
// failed scans in a row the join goes ahead as if the scan found nothing.
//
// States: idle (offline, `mayRetry`: a scan starts WIFI_RETRY_MS after the
// last join or failed scan) and scanning (until scanComplete() reports).
#pragma once

#include <Arduino.h>

// STA mode on and a scan started now: the join follows from wifiJoinTick().
// setup() (before its wait for the link) and the light-sleep wake call it.
void wifiJoinBegin();

// Every loop pass (and in setup()'s wait for the link): finishes a scan and
// joins from its result, records a successful join, and while offline starts
// the background retry when `mayRetry` (the app is idle) and WIFI_RETRY_MS
// has passed.
void wifiJoinTick(bool mayRetry);
