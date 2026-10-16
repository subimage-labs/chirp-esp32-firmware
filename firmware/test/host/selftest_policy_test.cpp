// Host tests for the boot self-test messages. Run: firmware/test/host/run.sh
#include "selftest_policy.h"

#include <cstdio>
#include <cstring>

static int failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,   \
                         __LINE__, #cond);                                \
            failures++;                                                   \
        }                                                                 \
    } while (0)

static bool is(const char* got, const char* want) {
    return got && std::strcmp(got, want) == 0;
}

static void hermesCodes() {
    CHECK(hermesProblem(200) == nullptr);
    CHECK(is(hermesProblem(401), "Hermes key rejected"));
    CHECK(is(hermesProblem(403), "Hermes key rejected"));
    CHECK(is(hermesProblem(404), "Hermes profile not found"));   // not "key rejected"
    CHECK(is(hermesProblem(-1), "Hermes unreachable"));          // connection refused
    CHECK(is(hermesProblem(-11), "Hermes unreachable"));         // read timeout
    CHECK(is(hermesProblem(500), "Hermes error"));
}

static void shimCodes() {
    CHECK(shimProblem(200) == nullptr);
    CHECK(is(shimProblem(-1), "Speech server unreachable"));
    CHECK(is(shimProblem(503), "Speech server error"));
}

// The first broken dependency is the one to fix: no WiFi hides everything,
// a Hermes problem outranks the speech server.
static void precedence() {
    CHECK(selfTestProblem(true, 200, 200) == nullptr);
    CHECK(is(selfTestProblem(false, 200, 200), "No WiFi"));
    CHECK(is(selfTestProblem(true, 401, -1), "Hermes key rejected"));
    CHECK(is(selfTestProblem(true, 200, -1), "Speech server unreachable"));
}

int main() {
    hermesCodes();
    shimCodes();
    precedence();
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("selftest_policy: all checks passed\n");
    return 0;
}
