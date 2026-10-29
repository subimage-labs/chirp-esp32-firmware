// hermes.h — Hermes chat API (the active agent's profile): streamed answers,
// the session that carries conversation history, and the session list/messages
// for the conversations menu. The persona is the profile's own (its SOUL.md
// on the Hermes host); the device sends no system prompt.
//
// Ownership: the session id is written by the turn worker during a turn
// (server-issued on the first answer) and by the loop (menu, new chat,
// boot restore) only while no turn runs; the turn handoff orders the two.
#pragma once

#include <Arduino.h>

#include <vector>

#include "app.h"

struct SessionItem {
    String id;
    String title;
};

void hermesSessionRestore();          // boot: this gateway + agent's resume file
void hermesSessionSave();             // persist the current id (if any)
void hermesSessionSet(const String& id);
void hermesSessionForget();           // new conversation, also across reboots

// Worker: one streamed answer. onDelta receives each content delta; stopped
// is polled while waiting and ends the stream early when it returns true.
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
    NoClock,       // venice: no valid time for the certificate checks
};
AskEnd hermesStreamAsk(const String& q, void (*onDelta)(const char*), bool (*stopped)());

// Loop: conversations menu and the boot self-test. Status codes as in
// selftest_policy.h (200 = ok, <= 0 = no HTTP response).
int hermesListSessions(std::vector<SessionItem>& out, int max);
bool hermesLoadMessages(const String& id, std::vector<Turn>& out, size_t maxTurns);
int hermesProbe();                    // authenticated GET of the session list
