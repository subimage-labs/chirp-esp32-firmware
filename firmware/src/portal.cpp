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
#if CHIRP_HAS_KEYBOARD
#include "keyboard.h"
#endif
#include "logo_draw.h"
#include "logo_web.h"
#include "portal_layout.h"
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

    h += "<h3>Hermes</h3>";
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

    h += "<h3>Speech</h3>";
    field(h, "Audio shim host <small>(optional)</small>", "shim", "text", v.shim,
          "same as Hermes host, port 8643", "autocapitalize=none autocorrect=off");
    h += "<label class=cb><input type=checkbox onclick=\"document.querySelectorAll('.pw')"
         ".forEach(e=>e.type=this.checked?'text':'password')\">Show password and key</label>"
         "<button>Save and restart</button></form>"
         "<p><small>Stored on the device only. The password and keys are never shown "
         "again.</small></p>";
    // host/pair.py's QR code opens /#pair=<base64url JSON: hermes, profile, key,
    // shim, look>. The fragment never leaves the phone (no request line, no
    // Referer); the script moves it into the form and strips it from the URL.
    h += R"JS(<script>(function(){
var m=/^#pair=(.*)$/.exec(location.hash);
if(!m)return;
history.replaceState(null,'',location.pathname);
var p=document.getElementById('pairbox');
if(!p)return;
try{
var b=atob(m[1].replace(/-/g,'+').replace(/_/g,'/')),u=new Uint8Array(b.length);
for(var i=0;i<b.length;i++)u[i]=b.charCodeAt(i);
var j=JSON.parse(new TextDecoder().decode(u)),f=document.forms[0];
var s=function(v){return typeof v==='string'?v:''};
if(!s(j.hermes)||!s(j.key))throw 0;
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
        sendForm(in, paired, err);
        return;
    }
    sendDone(out.ssid);
    statusLine(portal_layout::MSG_SAVED, Ink::Ok);
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
// The geometry and the words are portal_layout.h (pure, host-tested on
// every board's panel); this part only draws them. The QR is a white card
// with rounded corners (its quiet zone takes the rounding). One face
// throughout, the Fixed cut (ui.h): hierarchy comes from ink and space,
// never from the chosen font.
namespace pl = portal_layout;
static pl::Layout lay;
static bool layReady = false;                      // statusLine draws only after the screen
static const int CARD_R = 6;                       // the credentials card's corners
static const int QR_R = 8;                         // <= the 4-module quiet zone at 2x

// Version 3, ECC M holds the 35-byte join payload; a 4-module white quiet
// zone per side, as the QR spec asks. False only if no version fits.
static bool makeQr(const std::string& payload, QRCode& qr, std::vector<uint8_t>& buf) {
    for (uint8_t ver = 3; ver <= 6; ver++) {
        buf.assign(qrcode_getBufferSize(ver), 0);
        if (qrcode_initText(&qr, buf.data(), ver, ECC_MEDIUM, payload.c_str()) == 0) return true;
    }
    return false;                                  // cannot happen: payload is short
}

static void drawQr(QRCode& qr) {
    TFT_eSPI& tft = uiTft();
    const int x = lay.qrX, y = lay.qrY, s = lay.qrScale;
    tft.fillSmoothRoundRect(x, y, lay.qrSide, lay.qrSide, QR_R, TFT_WHITE, uiInk(Ink::Bg));
    for (uint8_t my = 0; my < qr.size; my++)
        for (uint8_t mx = 0; mx < qr.size; mx++)
            if (qrcode_getModule(&qr, mx, my))
                tft.fillRect(x + (mx + 4) * s, y + (my + 4) * s, s, s, TFT_BLACK);
}

// 12 px glyphs drawn with shapes (the Fixed face has none): rising bars for
// the network, a key for its password. (x, y) is the top-left.
static void iconBars(int x, int y, uint16_t c) {
    for (int i = 0; i < 3; i++) {
        const int h = 4 + 4 * i;
        uiTft().fillRect(x + i * 4, y + 12 - h, 3, h, c);
    }
}
static void iconKey(int x, int y, uint16_t c, uint16_t bg) {
    TFT_eSPI& tft = uiTft();
    tft.fillSmoothCircle(x + 4, y + 6, 4, c, bg);  // the bow
    tft.fillSmoothCircle(x + 4, y + 6, 1, bg, c);  // its hole
    tft.fillRect(x + 8, y + 5, 5, 2, c);           // the shaft
    tft.fillRect(x + 11, y + 7, 2, 3, c);          // a tooth
}

static void drawScreen() {
    TFT_eSPI& tft = uiTft();
    QRCode qr;
    std::vector<uint8_t> buf;
    if (!makeQr(wifiQrPayload(apSsid.c_str(), apPass.c_str()), qr, buf)) return;
    lay = pl::compute(tft.width(), tft.height(), qr.size);
    layReady = true;

    uiClear();
    if (lay.logoH) {
        const int scale = logoScaleFor(tft.width() / 3, lay.logoH - 8);
        logoDraw((tft.width() - logoWidth(scale)) / 2, (lay.logoH - logoHeight(scale)) / 2, scale);
    }
    uiFont(UiFont::Fixed);
    uiText(pl::HEADLINE, (tft.width() - uiTextWidth(pl::HEADLINE)) / 2, lay.headlineY,
           Ink::Strong, Ink::Bg);
    drawQr(qr);

    uiText(pl::LABEL, lay.colX, lay.labelY, Ink::Text, Ink::Bg);
    // The credentials, never cut: a password with an ellipsis cannot be typed
    // (the column is sized for the widest one: portal_layout.h COL_MIN).
    const uint16_t surface = uiInk(Ink::Surface), muted = uiInk(Ink::Muted);
    const int y = lay.cardY, tx = lay.colX + pl::CARD_TEXT_X;
    tft.fillSmoothRoundRect(lay.colX - 4, y, lay.colW + 4, pl::CARD_H, CARD_R, surface,
                            uiInk(Ink::Bg));
    iconBars(lay.colX + 2, y + 10, muted);
    uiText(apSsid, tx, y + 6, Ink::Strong, Ink::Surface);
    iconKey(lay.colX + 2, y + 34, muted, surface);
    uiText(apPass, tx, y + 30, Ink::Strong, Ink::Surface);
}

// Feedback under the card, only when something happened: a dot in the
// state's ink and the message, wrapped onto the second line the layout
// reserves (portal_layout.h statusSplit). A null message clears it.
static void statusLine(const char* msg, Ink ink) {
    if (!layReady) return;                         // the screen is not drawn yet
    TFT_eSPI& tft = uiTft();
    const uint16_t bg = uiInk(Ink::Bg);
    tft.fillRect(lay.colX - 4, lay.statusY, lay.colW + 4, pl::STATUS_LINES * pl::LINE, bg);
    if (!msg || !*msg) return;
    uiFont(UiFont::Fixed);
    tft.fillSmoothCircle(lay.colX + 4, lay.statusY + pl::LINE / 2, 4, uiInk(ink), bg);
    const String text = msg;
    const int split = pl::statusSplit(msg, (int)text.length(), lay.statusRoom,
                                      [&](const char*, int n) { return uiTextWidth(text.substring(0, n)); });
    const int tx = lay.colX + pl::STATUS_TEXT_X;
    if (split < 0) {
        uiText(text, tx, lay.statusY, Ink::Text, Ink::Bg);   // -2 cannot happen: the test checks every message
        return;
    }
    uiText(text.substring(0, split), tx, lay.statusY, Ink::Text, Ink::Bg);
    uiText(text.substring(split + 1), tx, lay.statusY + pl::LINE, Ink::Text, Ink::Bg);
}

static void stationStatus(int stations) {
    if (stations > 0) statusLine(pl::MSG_CONNECTED, Ink::Ok);
    else if (settingsReady()) statusLine(CHIRP_CANCEL_HINT, Ink::Muted);
    else statusLine(nullptr, Ink::Muted);          // nothing to say until a phone joins
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

// The way back out: the side key (the T-Embed) or Backspace (keyboard
// boards, which have no side key) leaves when there are settings to go
// back to.
static void cancelRequested() {
    if (restartAtMs) return;
    if (settingsReady()) ESP.restart();
    statusLine(portal_layout::MSG_FINISH, Ink::Busy);
}

void portalRun() {
    Serial.println("[Setup] setup mode");
    inputBegin();
#if CHIRP_HAS_KEYBOARD
    keyboardBegin();
#endif
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
        while (inputPoll(ev))
            if (ev.type == InputType::SideRelease) cancelRequested();
#if CHIRP_HAS_KEYBOARD
        KbdEvent kev;
        while (keyboardPoll(kev))
            if (kev.kind == KbdEv::Backspace) cancelRequested();
#endif
        if (portalExpired(settingsReady(), now, startedMs, lastActivityMs, PORTAL_IDLE_MS,
                          PORTAL_MAX_MS)) {
            Serial.println("[Setup] timed out, back to the app");
            ESP.restart();
        }
        uiPanelHold();                             // as loop() does (ui.cpp)
        delay(2);
    }
}
