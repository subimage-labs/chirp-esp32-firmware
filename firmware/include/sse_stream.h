// sse_stream.h — the pure rules of one streamed chat answer: how a refused
// connection is retried, how an HTTP status maps to an AskEnd, how the SSE
// byte stream splits into "data:" events, when the read loop ends and how
// its end maps to an AskEnd. Host-tested by sse_stream_test.cpp.
//
// The JSON of a data event stays in hermes.cpp (ArduinoJson) behind the
// injected `Extract` function; hermes.cpp also owns the HTTP client, the
// millis() clock and the callbacks.
//
// Ownership: all of it runs in the turn worker's context during one answer;
// a Parser belongs to the one hermesStreamAsk call that declares it.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <string>

// Worker: how one streamed answer ended.
enum class AskEnd : uint8_t {
    Answered,      // [DONE], a finish reason, or at least one delta
    Unreachable,   // no HTTP 200, or the connection closed with no answer
    Silent,        // connected, but no data event for SSE_IDLE_LIMIT_MS (the
                   // gateway's keepalive comments don't count)
    Stopped,       // stopped() returned true
    BadKey,        // venice: 401 — the stored key is wrong or revoked
    NoCredit,      // venice: 402 — the prepaid balance is spent
    NotAllowed,    // venice: 403 — the key's IP allowlist rejected us
    RateLimited,   // venice: 429 — the rate limit or the failure budget
};

namespace sse {

static const size_t LINE_STORE_MAX = 2047;   // a stored line: 2048-byte buffer minus the NUL
static const int    HTTP_OK = 200;
static const int    CONN_REFUSED = -1;       // HTTPC_ERROR_CONNECTION_REFUSED
static const int    ATTEMPTS = 2;

// ---- connection attempts and status mapping ----

// One retry when the connection is refused (gateway restarting, WiFi just
// back): nothing reached Hermes, so asking again can't double the answer.
// Anything else (200, timeouts, errors) ends the loop. Start with
// lastCode = CONN_REFUSED so the first pass runs.
inline bool tryAgain(int attempt, int lastCode) {
    return attempt < ATTEMPTS && lastCode == CONN_REFUSED;
}

// code != HTTP_OK. Venice maps its account errors to their own ends; every
// other failure is Stopped (the user cancelled meanwhile) or Unreachable.
inline AskEnd endForStatus(bool venice, int code, bool stopped) {
    if (venice) {
        if (code == 401) return AskEnd::BadKey;
        if (code == 402) return AskEnd::NoCredit;
        if (code == 403) return AskEnd::NotAllowed;
        if (code == 429) return AskEnd::RateLimited;
    }
    return stopped ? AskEnd::Stopped : AskEnd::Unreachable;
}

// ---- the line reader ----

// JSON seam. Fills content/finish from one data payload: "" when the member
// is absent or not a string. Returns false when the payload is not parseable
// JSON. hermes.cpp supplies the ArduinoJson one.
using Extract = bool (*)(const std::string& payload, std::string& content, std::string& finish);

// Byte-wise "data:" line reader. The gateway writes ": keepalive" comments
// while the model hasn't answered, and the raw stream also carries the
// chunked encoding's size lines, so only "data:" lines count as progress.
class Parser {
public:
    struct Fed {
        bool dataLine = false;   // a complete "data:" line ended with this byte (progress)
        bool deliver  = false;   // a non-empty content delta is in `delta`
    };

    Fed feed(uint8_t c, Extract extract, std::string& delta) {
        Fed f;
        if (c == '\n') {
            if (line_.size() >= 5 && line_.compare(0, 5, "data:") == 0) {
                f.dataLine = true;
                size_t at = 5;
                if (at < line_.size() && line_[at] == ' ') at++;
                const std::string payload = line_.substr(at);
                if (strcmp(payload.c_str(), "[DONE]") == 0) {
                    finished_ = true;
                } else {
                    std::string content, finish;
                    if (extract(payload, content, finish)) {
                        if (!content.empty()) {
                            gotDelta_ = true;
                            f.deliver = true;
                            delta = content;
                        }
                        if (!finish.empty()) sawFinish_ = true;
                    }
                }
            }
            line_.clear();
        } else if (c != '\r') {
            if (line_.size() < LINE_STORE_MAX) line_.push_back((char)c);
        }
        return f;
    }

    bool finished()  const { return finished_; }    // "[DONE]" seen
    bool sawFinish() const { return sawFinish_; }   // a non-empty finish_reason seen
    bool gotDelta()  const { return gotDelta_; }    // a non-empty content delta seen

private:
    std::string line_;
    bool finished_ = false, sawFinish_ = false, gotDelta_ = false;
};

// ---- loop timing and the final decision ----

enum class Step : uint8_t { Read, Wait, Stopped, Closed, Idle, Budget, Done };

// One pass of the read loop; first match wins. Both times are elapsed
// milliseconds (unsigned subtraction of millis() values: wrap-safe).
inline Step step(uint32_t elapsedMs, uint32_t sinceDataMs, bool stopped, bool connected,
                 size_t avail, uint32_t budgetMs, uint32_t idleLimitMs) {
    if (elapsedMs >= budgetMs) return Step::Budget;
    if (stopped) return Step::Stopped;
    if (!connected && avail == 0) return Step::Closed;
    if (avail == 0) return sinceDataMs > idleLimitMs ? Step::Idle : Step::Wait;
    return Step::Read;                       // idle is not tested while bytes arrive
}

// endedBy: Stopped, Closed, Idle, Budget or Done. Stopped and Idle stay so
// even after partial text; Closed and Budget are answered once anything
// useful arrived.
inline AskEnd askEnd(Step endedBy, bool sawFinish, bool gotDelta) {
    switch (endedBy) {
        case Step::Done:    return AskEnd::Answered;
        case Step::Stopped: return AskEnd::Stopped;
        case Step::Idle:    return AskEnd::Silent;
        case Step::Closed:
        case Step::Budget:
            if (sawFinish || gotDelta) return AskEnd::Answered;
            return endedBy == Step::Closed ? AskEnd::Unreachable : AskEnd::Silent;
        case Step::Read:
        case Step::Wait:
            break;
    }
    return AskEnd::Unreachable;
}

}  // namespace sse
