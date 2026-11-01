// venice.h — the venice backend's transport: TLS against the configured
// backend host and the Google trust root its certificates chain to.
// Bench-local http:// backends skip it.
//
// Ownership: stateless helpers, callable from any context.
#pragma once

#include <Arduino.h>
#include <HTTPClient.h>

bool veniceHttpBegin(HTTPClient& http, const String& url);  // TLS or plain
