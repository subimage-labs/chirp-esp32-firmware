// venice.h — the venice backend's transport: TLS against the configured
// backend host, the Google trust root its certificates chain to, and the
// wall-clock sync that certificate date checks need before the first
// connection. Bench-local http:// backends skip all of it.
//
// Ownership: stateless helpers; the SNTP start flag lives here, set from
// whichever context first asks for the clock (turn worker in practice).
#pragma once

#include <Arduino.h>
#include <HTTPClient.h>

void veniceClockStart();               // idempotent: ask SNTP for the time
bool veniceClock(int waitMs);          // true once time is valid (http: always)
bool veniceHttpBegin(HTTPClient& http, const String& url);  // TLS or plain
