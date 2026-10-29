// Host tests for the venice request policy. Run: firmware/test/host/run.sh
#include "venice_policy.h"

#include <cstdio>
#include <string>

static int failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,   \
                         __LINE__, #cond);                                \
            failures++;                                                   \
        }                                                                 \
    } while (0)

using venice_policy::History;

static History makeHistory(size_t n) {
    History h;
    for (size_t i = 0; i < n; i++)
        h.emplace_back("q" + std::to_string(i), "a" + std::to_string(i));
    return h;
}

static void messages() {
    // Persona first, question last, history in between, in order.
    const auto m = venice_policy::buildMessages(makeHistory(2), "why is lava hot");
    CHECK(m.size() == 6);
    CHECK(std::string(m[0].first) == "system");
    CHECK(m[0].second.find("Chirper") != std::string::npos);
    CHECK(std::string(m[1].first) == "user" && m[1].second == "q0");
    CHECK(std::string(m[2].first) == "assistant" && m[2].second == "a0");
    CHECK(std::string(m[3].first) == "user" && m[3].second == "q1");
    CHECK(std::string(m[4].first) == "assistant" && m[4].second == "a1");
    CHECK(std::string(m[5].first) == "user" && m[5].second == "why is lava hot");

    const auto empty = venice_policy::buildMessages(History(), "hi");
    CHECK(empty.size() == 2);
    CHECK(std::string(empty[0].first) == "system" &&
          std::string(empty[1].first) == "user" && empty[1].second == "hi");

    // The cap keeps whole exchanges, newest first-class: 20 exchanges in,
    // HISTORY_TURNS out, and the first sent exchange is the newest kept one.
    History big = makeHistory(20);
    venice_policy::trimHistory(big);
    CHECK(big.size() == venice_policy::HISTORY_TURNS);
    CHECK(big.front().first == "q8");            // 20 - 12 = 8
    const auto capped = venice_policy::buildMessages(big, "next");
    CHECK(capped.size() == 2 + 2 * venice_policy::HISTORY_TURNS);
    CHECK(capped[1].second == "q8" && capped[2].second == "a8");
    CHECK(capped[capped.size() - 1].second == "next");
}

static void errors() {
    CHECK(std::string(venice_policy::httpError(401)) == "Bad API key");
    CHECK(std::string(venice_policy::httpError(402)) == "Out of credit");
    CHECK(std::string(venice_policy::httpError(403)) == "Key not allowed");
    CHECK(std::string(venice_policy::httpError(429)) == "Rate limited");
    CHECK(venice_policy::httpError(500) == nullptr);
    CHECK(venice_policy::httpError(200) == nullptr);
    CHECK(venice_policy::httpError(0) == nullptr);
    for (int code : {401, 402, 403, 429}) {
        const char* lbl = venice_policy::httpError(code);
        CHECK(lbl && std::string(lbl).size() < 24);   // turn.cpp's stage label cap
    }
}

static void persona() {
    const std::string p = venice_policy::SYSTEM_PROMPT;
    CHECK(p.find("one button") != std::string::npos);          // the device shape
    CHECK(p.find("grown-up they trust") != std::string::npos); // the escalation rule
    CHECK(p.find("Never ask for personal details") != std::string::npos);
    CHECK(p.find("no adult topics") != std::string::npos);
    CHECK(p.size() > 800 && p.size() < 2500);                  // lean: sent every turn
}

int main() {
    messages();
    errors();
    persona();
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("venice_policy: all checks passed\n");
    return 0;
}
