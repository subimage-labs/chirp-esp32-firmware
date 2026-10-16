// selftest_policy.h — what a failed reachability probe means to the user.
// Pure (no Arduino), host-tested. A code is the HTTP status, or <= 0 when no
// HTTP response arrived (HTTPClient's negative connection errors).
#pragma once

// The gateway answers 404 for an unknown profile before it checks the key,
// so 404 and 401 name different fixes.
inline const char* hermesProblem(int code) {
    if (code == 200) return nullptr;
    if (code == 401 || code == 403) return "Hermes key rejected";
    if (code == 404) return "Hermes profile not found";
    if (code <= 0) return "Hermes unreachable";
    return "Hermes error";
}

inline const char* shimProblem(int code) {
    if (code == 200) return nullptr;
    return code <= 0 ? "Speech server unreachable" : "Speech server error";
}

// One line for the boot check, in dependency order: the network, then Hermes
// (nothing works without it), then the speech server.
inline const char* selfTestProblem(bool wifi, int hermesCode, int shimCode) {
    if (!wifi) return "No WiFi";
    if (const char* h = hermesProblem(hermesCode)) return h;
    return shimProblem(shimCode);
}
