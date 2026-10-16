#include "console.h"

#include <Arduino.h>

#include "bench.h"
#include "settings.h"

static const size_t CONSOLE_LINE_CAP = 1024;      // fits a full settings line

static void provision(const char* json) {
    SettingsInput in;
    if (!settingsInputFromJson(json, in)) {
        Serial.println("[Settings] rejected: bad JSON");
        return;
    }
    DeviceSettings out;
    const char* err = mergeSettings(settingsStored(), in, out);
    if (err) {
        Serial.printf("[Settings] rejected: %s\n", err);
        return;
    }
    if (!settingsSave(out)) {
        Serial.println("[Settings] rejected: NVS write failed");
        return;
    }
    Serial.println("[Settings] saved, restarting");
    Serial.flush();
    delay(300);
    ESP.restart();
}

// Trimmed first: a stray leading space must not route a settings line (and
// its secrets) into the bench echo.
static void runLine(String& line, bool app) {
    line.trim();
    if (line.startsWith("settings")) {
        provision(line.c_str() + 8);
        return;
    }
#ifdef BENCH_CMDS
    if (app) benchRun(line);
#else
    (void)app;
#endif
}

void consolePoll(bool app) {
    static String line;
    static bool overflow = false;                 // drop the rest of an overlong line
    while (Serial.available()) {
        const char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            if (overflow) Serial.println("[Settings] rejected: line too long");
            else if (line.length()) runLine(line, app);
            line = "";
            overflow = false;
        } else if (line.length() < CONSOLE_LINE_CAP) {
            line += c;
        } else {
            overflow = true;
        }
    }
}
