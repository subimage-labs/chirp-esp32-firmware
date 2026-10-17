// venice.cpp — TLS transport for the venice backend. api.venice.ai serves a
// Google Trust Services chain (leaf <- WE1 <- GTS Root R4), so the firmware
// pins the self-signed R4 root (valid to 2036-06-22; pki.goog/repo/certs/
// gtsr4.pem). mbedTLS also refuses certificates whose validity window it
// cannot place in time, so an https backend needs SNTP before its first
// request; a bench-local http:// backend needs neither root nor clock.
#include "venice.h"

#include <time.h>

#include <WiFi.h>

#include "settings.h"

static const char GTS_ROOT_R4_PEM[] PROGMEM = R"PEM(-----BEGIN CERTIFICATE-----
MIICCTCCAY6gAwIBAgINAgPlwGjvYxqccpBQUjAKBggqhkjOPQQDAzBHMQswCQYD
VQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEUMBIG
A1UEAxMLR1RTIFJvb3QgUjQwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAwMDAw
WjBHMQswCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2Vz
IExMQzEUMBIGA1UEAxMLR1RTIFJvb3QgUjQwdjAQBgcqhkjOPQIBBgUrgQQAIgNi
AATzdHOnaItgrkO4NcWBMHtLSZ37wWHO5t5GvWvVYRg1rkDdc/eJkTBa6zzuhXyi
QHY7qca4R9gq55KRanPpsXI5nymfopjTX15YhmUPoYRlBtHci8nHc8iMai/lxKvR
HYqjQjBAMA4GA1UdDwEB/wQEAwIBhjAPBgNVHRMBAf8EBTADAQH/MB0GA1UdDgQW
BBSATNbrdP9JNqPV2Py1PsVq8JQdjDAKBggqhkjOPQQDAwNpADBmAjEA6ED/g94D
9J+uHXqnLrmvT/aDHQ4thQEd0dlq7A/Cr8deVl5c1RxYIigL9zC2L7F8AjEA8GE8
p/SgguMh1YQdc4acLa/KNJvxn7kjNuK8YAOdgLOaVsjh4rsUecrNIdSUtUlD
-----END CERTIFICATE-----
)PEM";

static bool clockStarted = false;

void veniceClockStart() {
    if (clockStarted) return;
    clockStarted = true;
    configTime(0, 0, "time.google.com", "pool.ntp.org");
    Serial.println("[Venice] clock sync started");
}

bool veniceClock(int waitMs) {
    if (!settingsEndpoints().chatUrl.startsWith("https://")) return true;  // bench
    if (time(nullptr) > 1700000000) return true;
    veniceClockStart();
    const uint32_t deadline = millis() + waitMs;
    while (time(nullptr) <= 1700000000) {
        if (millis() >= deadline) {
            Serial.println("[Venice] no clock: certificate checks would fail");
            return false;
        }
        delay(100);
    }
    return true;
}

bool veniceHttpBegin(HTTPClient& http, const String& url) {
    if (url.startsWith("https://")) return http.begin(url, GTS_ROOT_R4_PEM);
    return http.begin(url);
}
