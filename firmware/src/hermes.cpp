#include "hermes.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <LittleFS.h>

#include "config.h"
#include "settings.h"
#include "venice.h"
#include "venice_policy.h"

static String sessionId;                          // server-issued, resumes history
static venice_policy::History vHistory;           // venice: the device holds the context

// ---------------------------------------------------------------- session
// One file per gateway + agent (sessionFileName, setup_form.h): a session id
// belongs to the profile on the gateway that issued it, and switching agents
// resumes that agent's own last conversation.
static String sessionPath() {
    return sessionFileName(settingsStored().hermes, settingsProfile()).c_str();
}

void hermesSessionRestore() {
    const String path = sessionPath();
    if (!LittleFS.exists(path)) return;
    File f = LittleFS.open(path, "r");
    if (!f) return;
    String s = f.readString();
    f.close();
    s.trim();
    if (s.length() >= 8 && s.length() <= 64) {
        sessionId = s;
        Serial.printf("[Session] resumed %.6s…\n", s.c_str());
    }
}

void hermesSessionSave() {
    if (sessionId.length() < 8) return;
    File f = LittleFS.open(sessionPath(), FILE_WRITE);
    if (f) { f.print(sessionId); f.close(); }
}

void hermesSessionSet(const String& id) {
    sessionId = id;
    hermesSessionSave();
}

void hermesSessionForget() {
    sessionId = "";
    LittleFS.remove(sessionPath());
    vHistory.clear();                             // a new chat starts empty everywhere
}

// ---------------------------------------------------------------- stream
AskEnd hermesStreamAsk(const String& q, void (*onDelta)(const char*), bool (*stopped)()) {
    const bool venice = settingsStored().backend == "venice";
    String body;
    if (venice) {
        // Venicle is stateless: the persona, the newest exchanges and the
        // question go out as one array, and Venice's own preamble and
        // hidden thinking are switched off for voice latency.
        const auto msgs =
            venice_policy::buildMessages(vHistory, std::string(q.c_str()));
        JsonDocument req;
        req["model"] = venice_policy::CHAT_MODEL;
        JsonArray ms = req["messages"].to<JsonArray>();
        for (const auto& m : msgs) {
            JsonObject o = ms.add<JsonObject>();
            o["role"] = m.first;
            o["content"] = m.second;
        }
        req["stream"] = true;
        JsonObject vp = req["venice_parameters"].to<JsonObject>();
        vp["include_venice_system_prompt"] = false;
        vp["disable_thinking"] = true;
        JsonObject rs = req["reasoning"].to<JsonObject>();
        rs["enabled"] = false;
        serializeJson(req, body);
    } else {
        JsonDocument req;
        JsonArray msgs = req["messages"].to<JsonArray>();
        JsonObject m = msgs.add<JsonObject>();
        m["role"] = "user";
        m["content"] = q;
        req["stream"] = true;
        serializeJson(req, body);
    }

    // One retry when the connection is refused (gateway restarting, WiFi just
    // back): nothing reached Hermes, so asking again can't double the answer.
    // Anything later (timeouts, errors) is not retried.
    HTTPClient http;
    int code = HTTPC_ERROR_CONNECTION_REFUSED;
    for (int attempt = 0; attempt < 2 && code == HTTPC_ERROR_CONNECTION_REFUSED; attempt++) {
        if (attempt) {
            http.end();
            Serial.println("[Hermes] connection refused, retrying once");
            delay(HERMES_RETRY_MS);
        }
        if (venice) {
            if (!veniceClock(8000)) return AskEnd::NoClock;
            veniceHttpBegin(http, settingsEndpoints().chatUrl);
        } else {
            http.begin(settingsEndpoints().chatUrl);
        }
        // HTTPClient::setTimeout is uint16 ms; the millis() deadline below is
        // not. Keep the wire timeout clamped, honor the full budget for the stream.
        http.setTimeout((uint16_t)(HERMES_TIMEOUT_MS > 60000UL ? 60000UL
                                                               : HERMES_TIMEOUT_MS));
        const char* hdrKeys[] = {"X-Hermes-Session-Id"};
        http.collectHeaders(hdrKeys, 1);
        http.addHeader("Content-Type", "application/json");
        http.addHeader("Accept", "text/event-stream");
        http.addHeader("Authorization", settingsEndpoints().auth);
        if (!venice && sessionId.length())
            http.addHeader("X-Hermes-Session-Id", sessionId);
        code = http.POST(body);
    }
    if (code != HTTP_CODE_OK) {
        Serial.printf("[Hermes] HTTP %d: %s\n", code,
                      code > 0 ? http.getString().c_str() : "(conn)");
        http.end();
        if (venice) {
            if (code == 401) return AskEnd::BadKey;
            if (code == 402) return AskEnd::NoCredit;
            if (code == 403) return AskEnd::NotAllowed;
            if (code == 429) return AskEnd::RateLimited;
        }
        return stopped() ? AskEnd::Stopped : AskEnd::Unreachable;
    }
    String sid = http.header("X-Hermes-Session-Id");
    if (!venice && sid.length()) sessionId = sid;  // continue this conversation
    WiFiClient* stream = http.getStreamPtr();
    if (!stream) { http.end(); return AskEnd::Unreachable; }
    stream->setTimeout((uint16_t)(SSE_IDLE_LIMIT_MS > 60000UL ? 60000UL
                                                              : SSE_IDLE_LIMIT_MS));

    char lineBuf[2048];
    size_t lineLen = 0;
    bool finished = false, sawFinish = false, gotDelta = false;
    std::string venAnswer;                         // venice: this turn's history entry
    const uint32_t deadline = millis() + HERMES_STREAM_BUDGET_MS;
    // Last data event: the gateway writes ": keepalive" comments while the
    // model hasn't answered, and the raw stream also carries the chunked
    // encoding's size lines, so only "data:" lines count as progress.
    uint32_t idleStart = millis();

    while (millis() < deadline) {
        if (stopped()) {
            Serial.println("[Hermes] stopped by the user");
            http.end();
            return AskEnd::Stopped;
        }
        if (!http.connected() && stream->available() == 0) break;
        size_t avail = stream->available();
        if (avail == 0) {
            if (millis() - idleStart > SSE_IDLE_LIMIT_MS) {
                Serial.printf("[Hermes] no data for %us\n", (unsigned)(SSE_IDLE_LIMIT_MS / 1000));
                http.end();
                return AskEnd::Silent;
            }
            delay(2);
            continue;
        }
        while (avail-- > 0) {
            const int c = stream->read();
            if (c < 0) break;
            if (c == '\n') {
                lineBuf[lineLen] = '\0';
                if (strncmp(lineBuf, "data:", 5) == 0) {
                    idleStart = millis();
                    char* data = lineBuf + 5;
                    if (*data == ' ') data++;
                    if (strcmp(data, "[DONE]") == 0) {
                        finished = true;
                    } else {
                        JsonDocument doc;
                        if (!deserializeJson(doc, data, lineLen - (data - lineBuf))) {
                            const char* delta = doc["choices"][0]["delta"]["content"];
            if (delta && delta[0] != '\0') {
                gotDelta = true;
                if (venice) venAnswer += delta;
                onDelta(delta);
            }
                            const char* fin = doc["choices"][0]["finish_reason"];
                            if (fin && fin[0] != '\0') sawFinish = true;
                        }
                    }
                }
                lineLen = 0;
                if (finished) break;
            } else if (c != '\r') {
                if (lineLen < sizeof(lineBuf) - 1) lineBuf[lineLen++] = (char)c;
            }
        }
        if (finished) break;
    }
    http.end();
    if (finished || sawFinish || gotDelta) {
        if (venice && !venAnswer.empty()) {
            vHistory.emplace_back(std::string(q.c_str()), venAnswer);
            venice_policy::trimHistory(vHistory);
        }
        return AskEnd::Answered;
    }
    return millis() < deadline ? AskEnd::Unreachable : AskEnd::Silent;
}

// ---------------------------------------------------------------- menu data
// A 200 whose body isn't the expected JSON counts as a bad gateway reply.
static const int BAD_REPLY = 502;

static int getJson(const String& url, JsonDocument& doc, String& resp) {
    HTTPClient http;
    http.begin(url);
    http.setTimeout(8000);
    http.addHeader("Authorization", settingsEndpoints().auth);
    const int code = http.GET();
    if (code != HTTP_CODE_OK) { http.end(); return code; }
    resp = http.getString();
    http.end();
    return deserializeJson(doc, resp) ? BAD_REPLY : HTTP_CODE_OK;
}

int hermesListSessions(std::vector<SessionItem>& out, int max) {
    if (settingsStored().backend == "venice")
        return HTTP_CODE_OK;      // no server sessions; the card roster is phase 3
    JsonDocument doc;
    String resp;
    const int code = getJson(settingsEndpoints().sessionsUrl, doc, resp);
    if (code != HTTP_CODE_OK) return code;
    JsonArray arr = doc["data"].as<JsonArray>();
    if (arr.isNull()) {
        Serial.printf("[Menu] no session array: %.60s\n", resp.c_str());
        return BAD_REPLY;
    }
    int added = 0;
    for (JsonObject s : arr) {
        if (added >= max) break;
        const char* id = s["id"] | "";
        if (!id[0]) continue;
        const char* ti = s["title"] | "";
        out.push_back({String(id), ti[0] ? String(ti) : String("(untitled)")});
        added++;
    }
    Serial.printf("[Menu] %d sessions loaded\n", added);
    return HTTP_CODE_OK;
}

int hermesProbe() {
    const bool venice = settingsStored().backend == "venice";
    const String url = venice ? settingsEndpoints().shimHealthUrl   // authed /models
                              : settingsEndpoints().sessionsUrl;
    HTTPClient http;
    if (venice ? !veniceHttpBegin(http, url) : !http.begin(url)) {
        http.end();
        return HTTPC_ERROR_CONNECTION_REFUSED;
    }
    http.setConnectTimeout(3000);
    http.setTimeout(3000);
    http.addHeader("Authorization", settingsEndpoints().auth);
    const int code = http.GET();
    http.end();
    return code;
}

// Pairs user/assistant messages into turns; drops a trailing unanswered
// question; keeps the newest maxTurns. Leaves `out` untouched on bad data.
bool hermesLoadMessages(const String& id, std::vector<Turn>& out, size_t maxTurns) {
    JsonDocument doc;
    String resp;
    if (getJson(settingsEndpoints().sessionsUrl + "/" + id + "/messages", doc, resp) !=
        HTTP_CODE_OK)
        return false;
    JsonArray arr = doc["data"].as<JsonArray>();
    if (arr.isNull() || arr.size() == 0) {
        Serial.printf("[Menu] messages empty/invalid for %.20s\n", id.c_str());
        return false;
    }
    std::vector<Turn> turns;
    for (JsonObject m : arr) {
        const char* role = m["role"] | "";
        const char* c = m["content"] | "";
        if (role[0] == 'u' && c[0]) {
            if (!turns.empty() && turns.back().a.length() == 0)
                turns.back().a = String(c);
            else turns.push_back({String(c), ""});
        } else if (role[0] == 'a' && c[0]) {
            if (!turns.empty()) turns.back().a = String(c);
            else turns.push_back({"", String(c)});
        }
    }
    while (!turns.empty() && turns.back().a.length() == 0) turns.pop_back();
    if (turns.size() > maxTurns)
        turns.erase(turns.begin(), turns.begin() + (turns.size() - maxTurns));
    out = std::move(turns);
    return true;
}
