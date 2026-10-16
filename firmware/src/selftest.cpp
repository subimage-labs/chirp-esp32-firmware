#include "selftest.h"

#include <HTTPClient.h>
#include <WiFi.h>

#include "hermes.h"
#include "selftest_policy.h"
#include "settings.h"

static const uint16_t PROBE_TIMEOUT_MS = 3000;

static int probe(const String& url) {
    HTTPClient http;
    http.begin(url);
    http.setConnectTimeout(PROBE_TIMEOUT_MS);
    http.setTimeout(PROBE_TIMEOUT_MS);
    const int code = http.GET();
    http.end();
    return code;
}

const char* selfTestRun() {
    const bool wifi = WiFi.status() == WL_CONNECTED;
    const int hermes = wifi ? hermesProbe() : 0;
    const int shim = wifi ? probe(settingsEndpoints().shimHealthUrl) : 0;
    const char* problem = selfTestProblem(wifi, hermes, shim);
    Serial.printf("[SelfTest] %s\n", problem ? problem : "ok");
    return problem;
}
