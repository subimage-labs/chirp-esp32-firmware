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
    // Venicle has one endpoint to check: the authed /models probe above
    // stands in for the shim's /health.
    const int shim = !wifi ? 0 : (settingsStored().backend == "venice"
                                      ? hermes
                                      : probe(settingsEndpoints().shimHealthUrl));
    const char* problem = selfTestProblem(wifi, hermes, shim);
    Serial.printf("[SelfTest] %s\n", problem ? problem : "ok");
    return problem;
}
