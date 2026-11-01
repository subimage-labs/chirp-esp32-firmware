// venice.h — the venice backend's transport: TLS against the configured
// backend host and the Google trust root its certificates chain to.
// Bench-local http:// backends skip it.
//
// Ownership: stateless helpers, callable from any context.
#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

bool veniceHttpBegin(HTTPClient& http, const String& url);  // TLS or plain
// The same, on the caller's TLS client with the handshake bounded to
// `handshakeS` seconds (the library's own client waits up to 120 s on a
// stalled network). `tls` must outlive the request.
bool veniceHttpBegin(HTTPClient& http, WiFiClientSecure& tls, const String& url,
                     unsigned long handshakeS);
