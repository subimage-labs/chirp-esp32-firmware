#include "portal.h"

#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_system.h>
#include <qrcode.h>

#include <algorithm>
#include <vector>

#include "config.h"
#include "console.h"
#include "input.h"
#include "logo_web.h"
#include "settings.h"
#include "ui.h"

static const uint32_t SETUP_MAGIC = 0x5E7C0F16;
RTC_NOINIT_ATTR static uint32_t setupFlag;        // survives esp_restart()

static const IPAddress AP_IP(192, 168, 4, 1);
static const IPAddress AP_MASK(255, 255, 255, 0);
static const size_t SCAN_MAX = 20;

static DNSServer dns;
static WebServer server(80);
static std::vector<std::string> nearby;           // scanned SSIDs, strongest first
static String apSsid, apPass;
static std::string formToken;                     // per run: /save must echo it
static uint32_t startedMs = 0;
static uint32_t lastActivityMs = 0;
static uint32_t restartAtMs = 0;                  // 0 = none pending

bool portalRequested() {
    const bool r = setupFlag == SETUP_MAGIC;
    setupFlag = 0;
    return r;
}

void portalRestartInto() {
    setupFlag = SETUP_MAGIC;
    ESP.restart();
    while (true) {}
}

// ---------------------------------------------------------------- page
static void field(String& h, const char* label, const char* name, const char* type,
                  const std::string& value, const char* placeholder, const char* extra) {
    h += "<label>";
    h += label;
    h += "<input name=";
    h += name;
    h += " type=";
    h += type;
    h += " value=\"";
    h += htmlEscape(value).c_str();
    h += "\" placeholder=\"";
    h += placeholder;
    h += "\" ";
    h += extra;
    h += "></label>";
}

// What the form shows: the stored settings on a fresh visit, the user's own
// submission after an error. Secrets are write-only: the WiFi password and
// agent keys always start empty, except a key from a pairing code
// (host/pair.py), which rides in hidden fields — it came from this same
// phone's scan. A fresh page carries the pairing-code script (below); a page
// re-shown after a failed save carries the hidden fields themselves (`paired`).
static void sendForm(const SettingsInput& v, bool paired, const char* error) {
    const DeviceSettings& cur = settingsStored();
    String h;
    h.reserve(6144 + sizeof(LOGO_WEB_PNG));
    h += "<!doctype html><html><head><meta charset=utf-8>"
         "<meta name=viewport content='width=device-width,initial-scale=1'>"
         "<title>Chirp setup</title><style>"
         "body{font-family:system-ui,sans-serif;max-width:30em;margin:1em auto;padding:0 1em}"
         "label{display:block;margin-top:1em;font-weight:600}"
         "input{display:block;width:100%;box-sizing:border-box;padding:.6em;font-size:1em;"
         "font-weight:400;margin-top:.3em}"
         ".cb{display:flex;gap:.5em;align-items:center;font-weight:400}.cb input{width:auto;margin:0}"
         ".ag{display:flex;justify-content:space-between;align-items:center;padding:.4em 0;"
         "border-bottom:1px solid #ddd}.ag label{margin:0}"
         "h3{margin:1.6em 0 0}fieldset{border:1px solid #ccc;border-radius:4px;margin-top:1em}"
         "button{margin-top:1.5em;width:100%;padding:.8em;font-size:1.1em}"
         ".err{background:#fdd;padding:.6em;border-radius:4px}small{color:#666}"
         ".brand{background:" LOGO_WEB_BG ";border-radius:8px;padding:.8em}"
         ".brand img{display:block;margin:0 auto;image-rendering:pixelated;max-width:100%;height:auto}"
         "</style></head><body><div class=brand><img alt=chirp! " LOGO_WEB_SIZE " src=\"";
    h += LOGO_WEB_PNG;
    h += "\"></div><h2>Chirp setup</h2>";
    if (error) {
        h += "<p class=err>";
        h += htmlEscape(error).c_str();
        h += "<br><small>Re-enter the password and key if you changed them.</small></p>";
    }
    h += "<form method=post action=/save autocomplete=off><input type=hidden name=csrf value=";
    h += formToken.c_str();
    h += "><p class=err id=pairerr hidden>That pairing code is damaged; run host/pair.py "
         "again</p><h3>WiFi</h3>";
    field(h, "Network", "ssid", "text", v.ssid, "",
          "list=nets required maxlength=32 autocapitalize=none autocorrect=off");
    h += "<datalist id=nets>";
    for (const std::string& s : nearby) {
        h += "<option value=\"";
        h += htmlEscape(s).c_str();
        h += "\">";
    }
    h += "</datalist>";
    field(h, "Password", "pass", "password", "",
          cur.pass.empty() ? "" : "saved; blank keeps it (same network)",
          "class=pw maxlength=64");
    h += "<label class=cb><input type=checkbox name=open";
    if (v.open) h += " checked";
    h += ">Open network (no password)</label>";

    h += "<h3>Backend</h3>"
         "<label class=cb><input type=radio name=backend value=local";
    if (v.backend != "venice") h += " checked";
    h += ">Hermes on my network</label>"
         "<label class=cb><input type=radio name=backend value=venice";
    if (v.backend == "venice") h += " checked";
    h += ">Venice <small>(cloud: one API key, no computer)</small></label>"
         "<div id=venicefields>";
    field(h, "Venice API key", "venicekey", "password", "",
          cur.veniceKey.empty() ? "the key from venice.ai" : "blank keeps the stored key",
          "class=pw maxlength=256 autocapitalize=none autocorrect=off");
    field(h, "Venice host <small>(advanced)</small>", "venice", "text", v.venice,
          "api.venice.ai:443", "autocapitalize=none autocorrect=off");
    h += "</div><div class=localonly><h3>Hermes</h3>";
    field(h, "Hermes host", "hermes", "text", v.hermes, "host or host:8642",
          "required autocapitalize=none autocorrect=off");
    if (!cur.agents.empty())
        h += "<p><small>A different host removes the agents below (their keys belong to "
             "this host); add one for the new host underneath.</small></p>";

    h += "<h3>Agents</h3><p><small>Each agent is a profile on your Hermes host with its "
         "own API key. Chirp answers as the one marked <b>use</b>; switch later from the "
         "device's Setup tab.</small></p>";
    for (size_t i = 0; i < cur.agents.size(); i++) {
        const std::string& p = cur.agents[i].profile;
        const std::string esc = htmlEscape(p);
        const bool removing =
            std::find(v.forget.begin(), v.forget.end(), p) != v.forget.end();
        h += "<div class=ag><label class=cb><input type=radio name=use value=\"";
        h += esc.c_str();
        h += "\"";
        if (v.useSet && v.use == p) h += " checked";
        h += "><span>use <b>";
        h += htmlEscape(agentLabel(p)).c_str();
        h += "</b></span></label><label class=cb><input type=checkbox name=forget";
        h += String((unsigned)i);
        h += " value=\"";
        h += esc.c_str();
        h += "\"";
        if (removing) h += " checked";
        h += ">remove</label></div>";
    }
    if (paired) {
        h += "<input type=hidden name=paired value=1><input type=hidden name=newprofile value=\"";
        h += htmlEscape(v.addProfile).c_str();
        h += "\"><input type=hidden name=newkey value=\"";
        h += htmlEscape(v.addKey).c_str();
        if (v.lookSet) {                           // the character look rides along
            h += "\"><input type=hidden name=newlook value=\"";
            h += htmlEscape(v.look).c_str();
        }
        h += "\"><p>Adds the agent <b>";
        h += htmlEscape(agentLabel(v.addProfile)).c_str();
        h += "</b> from the pairing code and uses it.</p>";
    } else {
        h += "<fieldset id=manual><legend>";
        h += cur.agents.empty() ? "Your agent" : "Add an agent (up to 4)";
        h += "</legend>";
        field(h, "Profile name on Hermes", "newprofile", "text", v.addProfile,
              "blank = the default profile", "autocapitalize=none autocorrect=off");
        field(h, "Its API key", "newkey", "password", "",
              cur.agents.empty() ? "required" : "required for a new agent", "class=pw");
        h += "<p><small>The profile must already exist on Hermes; typing a new name here "
             "does not create one. To make a new agent, run <code>host/pair.py --create "
             "NAME</code> on the Hermes machine: it creates the profile and its key and "
             "sends both here. A name that is already listed with a new key replaces "
             "its key. The added agent becomes the one in use.</small></p></fieldset>"
             "<div id=pairbox hidden><input type=hidden name=paired value=1 disabled>"
             "<input type=hidden name=newprofile disabled><input type=hidden name=newkey disabled>"
             "<input type=hidden name=newlook disabled><p>Adds the agent <b id=pairname></b> "
             "from the pairing code and uses it.</p></div>";
    }
    h += "</div><div class=localonly><h3>Speech</h3>";
    field(h, "Audio shim host <small>(optional)</small>", "shim", "text", v.shim,
          "same as Hermes host, port 8643", "autocapitalize=none autocorrect=off");
    h += "</div>";
    h += "<label class=cb><input type=checkbox onclick=\"document.querySelectorAll('.pw')"
         ".forEach(e=>e.type=this.checked?'text':'password')\">Show password and key</label>"
         "<button>Save and restart</button></form>"
         "<p><small>Stored on the device only. The password and keys are never shown "
         "again.</small></p>";
    // host/pair.py's QR code opens /#pair=<base64url JSON: hermes, profile, key,
    // shim, look>. The fragment never leaves the phone (no request line, no
    // Referer); the script moves it into the form and strips it from the URL.
    h += R"JS(<script>(function(){
var f=document.forms[0];
var upd=function(){var v=(f.backend&&f.backend.value)?f.backend.value:'local';
var hs=v!=='local',ls=document.querySelectorAll('.localonly');
for(var i=0;i<ls.length;i++){ls[i].hidden=hs;
var t=ls[i].querySelectorAll('input:not(#pairbox input)');
for(var k=0;k<t.length;k++)t[k].disabled=hs;}
var w=document.getElementById('venicefields');
if(w){w.hidden=v!=='venice';var vi=w.querySelectorAll('input');
for(var z=0;z<vi.length;z++)vi[z].disabled=v!=='venice';}};
var rs=f.querySelectorAll('[name=backend]');for(var i=0;i<rs.length;i++)
rs[i].addEventListener('change',upd);
upd();
var m=/^#pair=(.*)$/.exec(location.hash);
if(!m)return;
history.replaceState(null,'',location.pathname);
var p=document.getElementById('pairbox');
if(!p)return;
try{
var b=atob(m[1].replace(/-/g,'+').replace(/_/g,'/')),u=new Uint8Array(b.length);
for(var i=0;i<b.length;i++)u[i]=b.charCodeAt(i);
var j=JSON.parse(new TextDecoder().decode(u));
var s=function(v){return typeof v==='string'?v:''};
if(!s(j.hermes)||!s(j.key))throw 0;
f.backend.value='local';upd();
f.hermes.value=s(j.hermes);
f.shim.value=s(j.shim);
var q=function(n){var e=p.querySelector('[name='+n+']');e.disabled=false;return e};
q('paired').value=1;
q('newprofile').value=s(j.profile);
q('newkey').value=j.key;
if(typeof j.look==='string')q('newlook').value=j.look;
else p.querySelector('[name=newlook]').remove();
document.getElementById('pairname').textContent=s(j.profile)||'default';
p.hidden=false;
var d=document.getElementById('manual');d.disabled=true;d.hidden=true;
}catch(e){document.getElementById('pairerr').hidden=false}
})()</script></body></html>)JS";
    server.send(200, "text/html", h);
}

static void sendDone(const std::string& ssid) {
    String h =
        "<!doctype html><html><head><meta charset=utf-8>"
        "<meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>Chirp setup</title></head>"
        "<body style='font-family:system-ui,sans-serif;max-width:30em;margin:1em auto;padding:0 1em'>"
        "<h2>Saved</h2><p>Chirp is restarting and joining <b>";
    h += htmlEscape(ssid).c_str();
    h += "</b>. You can reconnect this phone to your usual WiFi.</p></body></html>";
    server.send(200, "text/html", h);
}

// ---------------------------------------------------------------- handlers
static void statusLine(const char* msg, Ink ink);   // screen section

// A bare form: pairing codes are handled by the page's script (fragment).
static void onForm() {
    lastActivityMs = millis();
    const DeviceSettings& cur = settingsStored();
    SettingsInput v;
    v.ssid = cur.ssid;
    v.open = !cur.ssid.empty() && cur.pass.empty();
    v.hermes = cur.hermes;
    v.shim = cur.shim;
    v.backend = cur.backend.empty() ? "local" : cur.backend;
    v.venice = cur.venice;
    v.useSet = cur.active < cur.agents.size();
    if (v.useSet) v.use = cur.agents[cur.active].profile;
    sendForm(v, false, nullptr);
}

static void onSave() {
    // A form POST names its Origin: it must be this portal. Either way the
    // body must carry the token this run served. Neither counts as activity.
    if (!originAllowed(server.hasHeader("Origin"), server.header("Origin").c_str(),
                       AP_IP.toString().c_str()) ||
        !tokenMatches(formToken, server.arg("csrf").c_str())) {
        server.send(403, "text/plain", "Refused: open the setup page again and resubmit");
        return;
    }
    lastActivityMs = millis();
    SettingsInput in;
    in.ssid = server.arg("ssid").c_str();
    in.pass = server.arg("pass").c_str();
    in.open = server.hasArg("open");
    in.hermes = server.arg("hermes").c_str();
    in.shim = server.arg("shim").c_str();
    in.backendSet = server.hasArg("backend");
    in.backend = server.arg("backend").c_str();
    // Disabled inputs don't submit: an absent backend host keeps the stored
    // one (and its key) instead of silently resetting to the default.
    in.venice = server.hasArg("venice") ? server.arg("venice").c_str()
                                        : settingsStored().venice;
    in.veniceKey = server.arg("venicekey").c_str();
    in.useSet = server.hasArg("use");
    in.use = server.arg("use").c_str();
    for (size_t i = 0; i < MAX_AGENTS; i++) {
        const String name = String("forget") + (unsigned)i;
        if (server.hasArg(name)) in.forget.push_back(server.arg(name).c_str());
    }
    in.addProfile = server.arg("newprofile").c_str();
    in.addKey = server.arg("newkey").c_str();
    in.lookSet = server.hasArg("newlook");
    in.look = server.arg("newlook").c_str();
    const bool paired = server.hasArg("paired");
    DeviceSettings out;
    const char* err = mergeSettings(settingsStored(), in, out);
    if (!err && !settingsSave(out)) err = "Couldn't store the settings on the device";
    if (err) {
        in.pass.clear();
        if (!paired) in.addKey.clear();
        in.veniceKey.clear();                    // keys are write-only
        sendForm(in, paired, err);
        return;
    }
    sendDone(out.ssid);
    statusLine("Saved - restarting", Ink::Ok);
    restartAtMs = millis() + 2000;                 // let the page reach the phone
}

// Every other URL — including the phones' connectivity probes — lands on the
// form, which is what makes the OS pop up its sign-in sheet.
static void onOther() {
    lastActivityMs = millis();
    server.sendHeader("Location", String("http://") + AP_IP.toString() + "/", true);
    server.send(302, "text/plain", "");
}

// ---------------------------------------------------------------- screen
// Full-height QR on the left; text and the status line in the right column
// (the app's full-width status strip would cut into the code).
static const int QR_X = 4;
static const int TEXT_X = 162;
static const int STATUS_Y = 152;                  // the last 18 px: one Body line

// Version 3, ECC M holds the 35-byte join payload; a 4-module white quiet
// zone per side, as the QR spec asks.
static void drawQr(const std::string& payload) {
    TFT_eSPI& tft = uiTft();
    QRCode qr;
    std::vector<uint8_t> buf;
    for (uint8_t ver = 3; ver <= 6; ver++) {
        buf.assign(qrcode_getBufferSize(ver), 0);
        if (qrcode_initText(&qr, buf.data(), ver, ECC_MEDIUM, payload.c_str()) == 0) break;
        if (ver == 6) return;                      // cannot happen: payload is short
    }
    const int box = tft.height() - 4;
    const int scale = box / (qr.size + 8);
    const int side = scale * (qr.size + 8);
    const int y = (tft.height() - side) / 2;
    tft.fillRect(QR_X, y, side, side, TFT_WHITE);
    for (uint8_t my = 0; my < qr.size; my++)
        for (uint8_t mx = 0; mx < qr.size; mx++)
            if (qrcode_getModule(&qr, mx, my))
                tft.fillRect(QR_X + (mx + 4) * scale, y + (my + 4) * scale, scale, scale,
                             TFT_BLACK);
}

// Eight Fixed lines (Atkinson M, 18 px each, whatever font is chosen) and the
// status line fill the 170 px column next to the QR code. The 12-character
// password gets a line of its own.
static void drawScreen() {
    uiClear();
    drawQr(wifiQrPayload(apSsid.c_str(), apPass.c_str()));
    uiFont(UiFont::Fixed);
    const int x = TEXT_X;
    uiText("Chirp setup", x, 2, Ink::Accent, Ink::Bg);
    uiText("Scan, or join WiFi:", x, 20, Ink::Text, Ink::Bg);
    uiText(apSsid, x + 6, 40, Ink::Strong, Ink::Bg);
    uiText("Password:", x, 58, Ink::Text, Ink::Bg);
    uiText(apPass, x + 6, 76, Ink::Strong, Ink::Bg);
    uiText("The form opens itself;", x, 98, Ink::Text, Ink::Bg);
    uiText("else browse to", x, 116, Ink::Text, Ink::Bg);
    uiText(String("http://") + AP_IP.toString(), x, 134, Ink::Strong, Ink::Bg);
}

static void statusLine(const char* msg, Ink ink) {
    TFT_eSPI& tft = uiTft();
    tft.fillRect(TEXT_X, STATUS_Y, tft.width() - TEXT_X, tft.height() - STATUS_Y,
                 uiInk(Ink::Bg));
    uiFont(UiFont::Fixed);
    uiText(msg, TEXT_X, STATUS_Y, ink, Ink::Bg);
}

static void stationStatus(int stations) {
    if (stations > 0) statusLine("Phone connected", Ink::Ok);
    else if (settingsReady()) statusLine("Side key = cancel", Ink::Muted);
    else statusLine("Waiting for a phone", Ink::Accent);
}

// ---------------------------------------------------------------- run
static void scanNearby() {
    WiFi.mode(WIFI_STA);
    const int n = WiFi.scanNetworks();
    std::vector<std::pair<int, std::string>> found;
    for (int i = 0; i < n; i++) {
        const String s = WiFi.SSID(i);
        if (s.length()) found.push_back({WiFi.RSSI(i), s.c_str()});
    }
    WiFi.scanDelete();
    std::sort(found.begin(), found.end(),
              [](const std::pair<int, std::string>& a, const std::pair<int, std::string>& b) {
                  return a.first > b.first;
              });
    for (const auto& f : found) {
        if (nearby.size() >= SCAN_MAX) break;
        if (std::find(nearby.begin(), nearby.end(), f.second) == nearby.end())
            nearby.push_back(f.second);
    }
    Serial.printf("[Setup] %u networks nearby\n", (unsigned)nearby.size());
}

// Unambiguous characters only: it is read off the screen and typed.
static String randomPassword() {
    return portalPassword([] { return (uint32_t)esp_random(); }).c_str();
}

void portalRun() {
    Serial.println("[Setup] setup mode");
    inputBegin();
    uiClear();
    uiMsg("Scanning WiFi...", Ink::Accent);
    scanNearby();                                  // also brings the RF (entropy) up
    uint8_t raw[16];
    esp_fill_random(raw, sizeof raw);
    formToken = hexString(raw, sizeof raw);
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char ssid[16];
    snprintf(ssid, sizeof(ssid), "Chirp-%02X%02X", mac[4], mac[5]);
    apSsid = ssid;
    apPass = randomPassword();
    WiFi.mode(WIFI_AP);                            // AP only: the channel stays put
    WiFi.softAPConfig(AP_IP, AP_IP, AP_MASK);
    WiFi.softAP(apSsid.c_str(), apPass.c_str());
    dns.setErrorReplyCode(DNSReplyCode::NoError);
    dns.start(53, "*", AP_IP);
    server.on("/", HTTP_GET, onForm);
    server.on("/save", HTTP_POST, onSave);
    server.onNotFound(onOther);
    static const char* HEADERS[] = {"Origin"};     // collectHeaders wants a mutable array
    server.collectHeaders(HEADERS, 1);
    server.begin();
    Serial.printf("[Setup] AP %s up\n", apSsid.c_str());

    drawScreen();
    int stations = -1;
    startedMs = lastActivityMs = millis();
    while (true) {
        dns.processNextRequest();
        server.handleClient();
        consolePoll(false);
        const uint32_t now = millis();
        if (restartAtMs && (int32_t)(now - restartAtMs) >= 0) ESP.restart();

        const int st = WiFi.softAPgetStationNum();
        if (st > 0) lastActivityMs = now;
        if (st != stations && !restartAtMs) stationStatus(st);
        stations = st;

        InputEvent ev;
        while (inputPoll(ev)) {
            if (ev.type != InputType::SideRelease || restartAtMs) continue;
            if (settingsReady()) ESP.restart();
            statusLine("Finish setup first", Ink::Busy);
        }
        if (portalExpired(settingsReady(), now, startedMs, lastActivityMs, PORTAL_IDLE_MS,
                          PORTAL_MAX_MS)) {
            Serial.println("[Setup] timed out, back to the app");
            ESP.restart();
        }
        uiPanelHold();                             // as loop() does (ui.cpp)
        delay(2);
    }
}
