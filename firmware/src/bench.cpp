#include "bench.h"

#ifdef BENCH_CMDS

#include <WiFi.h>

#include "app.h"
#include "avatar.h"
#include "character.h"
#include "capture.h"
#include "config.h"
#if CHIRP_HAS_KEYBOARD
#include "keyboard.h"
#endif
#include "input.h"
#include "menu.h"
#include "power.h"
#include "speech.h"
#include "turn.h"
#include "fontpick.h"
#include "ui.h"
#include "voice.h"

static const char* stateName() {
    switch (uiState) {
    case UI_BOOT: return "boot";
    case UI_IDLE: return "idle";
    case UI_RECORDING: return "recording";
    case UI_THINKING: return "thinking";
    case UI_SPEAKING: return "speaking";
    }
    return "?";
}

static void status() {
    int top, rows, sel, count;
    bool following;
    transcriptPosition(top, rows, following);
    menuPosition(sel, count);
    const BatteryView bat = powerBattery();
    char batteryText[16] = "?";
    if (bat.known) snprintf(batteryText, sizeof batteryText, "%d%%%s%s", bat.pct,
                            bat.charging ? "+" : "", bat.low ? "!" : "");
    char keptTag[9], shownTag[9];
    characterLookTags(keptTag, shownTag);
    Serial.printf("[Bench] state=%s menu=%s sel=%d/%d voice=%d hearing=%d "
                  "scroll=%d/%d follow=%d vol=%u playing=%d wifi=%d power=%s bat=%s view=%s avatar=%s "
                  "look=%s/%s font=%s size=%c line=%d theme=%s mode=%s themecode=%u\n",
                  stateName(), menuTab(), sel, count, (int)voiceOn(),
                  (int)voiceHearing(), top, rows, (int)following, speechVolume(),
                  (int)speechPlaying(), (int)(WiFi.status() == WL_CONNECTED),
                  powerSourceName(), batteryText, characterOn() ? "character" : "transcript",
                  characterStateName(), keptTag, shownTag, uiFontFamilyName(uiFontFamily()),
                  fontSizeLetter(uiFontSize()), uiLineHeight(), uiThemeName(),
                  uiThemeLight() ? "light" : "dark",
                  (unsigned)(uiThemeFamily() * 2 + (uiThemeLight() ? 1 : 0)));
}

// avatar look                 print the kept look: 32 hex digits, or orig
// avatar look <32 hex>        keep exactly that record
static void benchLook(String arg) {
    arg.trim();
    if (!arg.length()) {
        uint8_t rec[avatar::KEPT_BYTES];
        if (!characterKeptRecord(rec)) {
            Serial.println("[Bench] look orig");
            return;
        }
        char hex[2 * avatar::KEPT_BYTES + 1];
        for (int i = 0; i < avatar::KEPT_BYTES; i++) snprintf(hex + 2 * i, 3, "%02x", rec[i]);
        Serial.printf("[Bench] look %s\n", hex);
        return;
    }
    uint8_t rec[avatar::KEPT_BYTES];
    bool ok = arg.length() == 2 * avatar::KEPT_BYTES;
    for (int i = 0; ok && i < 2 * avatar::KEPT_BYTES; i++) {
        const char c = arg[i];
        const int v = c >= '0' && c <= '9' ? c - '0'
                      : c >= 'a' && c <= 'f' ? c - 'a' + 10
                      : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                             : -1;
        if (v < 0) ok = false;
        else if (i % 2 == 0) rec[i / 2] = (uint8_t)(v << 4);
        else rec[i / 2] |= (uint8_t)v;
    }
    if (!ok || !characterKeepRecord(rec, sizeof rec)) {
        Serial.printf("[Bench] avatar look: %u hex digits, every index within its range\n",
                      (unsigned)(2 * avatar::KEPT_BYTES));
        return;
    }
    if (!menuOpen()) appRedraw();
}

// font                      list the families
// font <name|index> [S|M|L] pick a family (and size); a lone S, M or L only changes the size
static void benchFont(String args) {
    args.trim();
    if (!args.length()) {
        for (size_t i = 0; i < uiFontFamilyCount(); i++)
            Serial.printf("[Bench] font %u %s\n", (unsigned)i, uiFontFamilyName(i));
        return;
    }
    int size = (int)uiFontSize();
    const int sp = args.lastIndexOf(' ');
    const String last = sp < 0 ? args : args.substring(sp + 1);
    if (fontSizeFind(last.c_str()) >= 0) {
        size = fontSizeFind(last.c_str());
        args = sp < 0 ? String() : args.substring(0, sp);
        args.trim();
    }
    int family = (int)uiFontFamily();
    if (args.length()) {
        family = fontFind(args.c_str());
        if (family < 0 && isDigit(args[0])) family = args.toInt();
        if (family < 0 || family >= (int)uiFontFamilyCount()) {
            Serial.println("[Bench] font: unknown family (font = list)");
            return;
        }
    }
    uiSetFont((size_t)family, (uint8_t)size);
    appRedraw();
    status();
}
// `mic [ms]`: install the mic, read for ms (default 1500), report peak and
// mean — gain tuning and a capture-health probe without a turn.
static void benchMic(const String& args) {
    const uint32_t ms = args.length() ? (uint32_t)constrain(args.toInt(), 100, 10000) : 1500;
    if (!micInstall()) {
        Serial.println("[Bench] mic init failed");
        return;
    }
    static int16_t buf[READ_CHUNK / 2];
    uint32_t sum = 0, n = 0, zero = 0;
    int16_t peak = 0;
    const uint32_t t0 = millis();
    while (millis() - t0 < ms) {
        const size_t br = micRead(buf, READ_CHUNK, 100);
        for (size_t i = 0; i < br / 2; i++) {
            const int16_t a = buf[i] < 0 ? -buf[i] : buf[i];
            sum += a;
            n++;
            if (a == 0) zero++;
            if (a > peak) peak = a;
        }
    }
    micRemove();
    Serial.printf("[Bench] mic %ums peak=%d mean=%lu zeros=%lu%% samples=%lu\n",
                  (unsigned)ms, (int)peak, n ? sum / n : 0, n ? zero * 100 / n : 0, n);
}

void benchRun(String line) {
    line.trim();
    if (!line.length()) return;
    Serial.printf("[Bench] > %s\n", line.c_str());
    if (line == "status") {
        status();
    } else if (line == "enc down" || line == "enc up") {
        inputSimKey(true, line.endsWith("down"));
    } else if (line == "side down" || line == "side up") {
        inputSimKey(false, line.endsWith("down"));
    } else if (line.startsWith("wheel ")) {
        const String args = line.substring(6);
        const int sp = args.indexOf(' ');
        const int32_t clicks = (sp < 0 ? args : args.substring(0, sp)).toInt();
        const uint32_t ms = sp < 0 ? 200 : args.substring(sp + 1).toInt();
        inputSimWheel(clicks, ms ? ms : 1);
    } else if (line.startsWith("ask ") || line.startsWith("speak ")) {
        if (uiState != UI_IDLE || menuOpen())
            Serial.printf("[Bench] %s refused: not idle\n", line.startsWith("ask ") ? "ask" : "speak");
        else if (line.startsWith("ask "))
            turnAskText(line.substring(4));
        else
            turnSpeakText(line.substring(6));
    } else if (line == "battery") {
        powerBatteryReport();
    } else if (line == "heap" || line.startsWith("heap ")) {
        benchHeapRun(line.length() > 5 ? line.substring(5) : String());
    } else if (line == "mic" || line.startsWith("mic ")) {
        benchMic(line.length() > 4 ? line.substring(4) : String());
#if CHIRP_HAS_KEYBOARD
    } else if (line == "keys on" || line == "keys off") {
        keyboardEcho(line.endsWith("on"));
        Serial.printf("[Bench] key echo %s\n", line.endsWith("on") ? "on" : "off");
#endif
    } else if (line.startsWith("vol ")) {
        speechSetVolume((uint8_t)constrain(line.substring(4).toInt(), 0, MAX_VOLUME));
    } else if (line.startsWith("theme ")) {
        const int code = line.substring(6).toInt();
        if (code < 0 || !themeCodeValid((uint8_t)code)) {
            Serial.println("[Bench] theme: 0.." + String(THEME_COUNT * 2 - 1));
        } else {
            uiSetTheme((uint8_t)code / 2, code % 2);
            appRedraw();
        }
    } else if (line == "panel invert on" || line == "panel invert off") {
        uiPanelInvert(line.endsWith("on"));
    } else if (line == "panel status") {
        uiPanelStatus();
    } else if (line.startsWith("ttsvoice ")) {             // venice: audition a TTS model + voice
        const String a = line.substring(9);
        const int sp = a.indexOf(' ');
        if (uiState != UI_IDLE || speechPlaying()) Serial.println("[Bench] ttsvoice refused: not idle");
        else if (a == "off") speechBenchVoice("", "");
        else if (sp > 0) speechBenchVoice(a.substring(0, sp).c_str(), a.substring(sp + 1).c_str());
        if (uiState == UI_IDLE && !speechPlaying()) Serial.printf("[Bench] ttsvoice %s\n", a.c_str());
    } else if (line.startsWith("panel reg ")) {
        uint8_t b[16];
        int n = 0;
        char* p = (char*)line.c_str() + 10;
        const uint8_t cmd = (uint8_t)strtol(p, &p, 16);
        while (n < 16 && *p) {
            char* q;
            const long v = strtol(p, &q, 16);
            if (q == p) break;
            b[n++] = (uint8_t)v;
            p = q;
        }
        uiPanelReg(cmd, b, n);
    } else if (line == "panel test gray" || line.startsWith("panel test checker")) {
        uiPanelTest(line.indexOf("checker") > 0, line.endsWith(" 1"));
    } else if (line.startsWith("panel fill ")) {
        uiPanelFill((uint16_t)strtoul(line.c_str() + 11, nullptr, 16));
    } else if (line == "view character" || line == "view transcript") {
        characterSetOn(line.endsWith("character"));
        if (!menuOpen()) appRedraw();
    } else if (line == "font" || line.startsWith("font ")) {
        benchFont(line.substring(4));
    } else if (line.startsWith("avatar ")) {
        const String arg = line.substring(7);
        static const char* const NAMES[] = {"idle", "listening", "thinking", "speaking", "error", "sleep"};
        if (arg == "shot") {
            characterColumnShot();
        } else if (arg == "sprite") {
            characterSpriteShot();
        } else if (arg == "stats") {
            uint32_t n, avg, max;
            characterStats(n, avg, max);
            Serial.printf("[Timing] avatar push n=%u avg=%uus max=%uus\n", (unsigned)n,
                          (unsigned)avg, (unsigned)max);
        } else if (arg == "look" || arg.startsWith("look ")) {
            benchLook(arg.substring(4));
        } else if (arg.startsWith("variant ")) {
            const long n = arg.substring(8).toInt();
            if (n < 0 || n > avatar::VARIANT_MAX) {
                Serial.printf("[Bench] avatar variant: 0..%u\n", (unsigned)avatar::VARIANT_MAX);
            } else {
                characterKeepVariant((uint16_t)n);
                if (!menuOpen()) appRedraw();
            }
        } else if (arg.startsWith("react ")) {
            const String name = arg.substring(6);
            if (name == "pleased") characterReact(avatar::Pleased);
            else if (name == "puzzled") characterReact(avatar::Puzzled);
            else if (name == "surprised") characterReact(avatar::Surprised);
            else Serial.println("[Bench] avatar react: pleased|puzzled|surprised");
        } else if (arg == "auto") {
            characterForce(-1);
        } else {
            int found = -1;
            for (int i = 0; i < avatar::StateCount; i++)
                if (arg == NAMES[i]) found = i;
            if (found < 0) Serial.println("[Bench] avatar: idle|listening|thinking|speaking|error|sleep|auto|stats|variant <n>|look [<hex>]|react <r>");
            else characterForce(found);
        }
    } else {
        Serial.println("[Bench] unknown command");
    }
}

#endif
