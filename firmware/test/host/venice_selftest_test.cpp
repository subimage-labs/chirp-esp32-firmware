// venice_selftest_test.cpp — Venice boot probe status and WiFi precedence.
// Run: test/host/run.sh
#include <cstdio>
#include <cstring>

#include "venice_policy.h"

using namespace venice_policy;

static int fails = 0;
#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            fails++;                                               \
        }                                                          \
    } while (0)

int main() {
    const struct {
        int code;
        const char* expected;
    } cases[] = {
        {0, "Venice unreachable"},
        {-1, "Venice unreachable"},
        {-11, "Venice unreachable"},
        {1, "Venice error"},
        {199, "Venice error"},
        {200, nullptr},
        {201, "Venice error"},
        {401, "Bad API key"},
        {402, "Out of credit"},
        {403, "Key not allowed"},
        {429, "Rate limited"},
        {404, "Venice error"},
        {500, "Venice error"},
    };
    for (const auto& c : cases) {
        const char* problem = veniceSelfTestProblem(true, c.code);
        if (c.expected) {
            CHECK(problem != nullptr && strcmp(problem, c.expected) == 0);
        } else {
            CHECK(problem == nullptr);
        }
        const char* mapped = httpError(c.code);
        if (mapped) CHECK(problem != nullptr && strcmp(problem, mapped) == 0);
        CHECK(problem == nullptr || strstr(problem, "Hermes") == nullptr);

        // Missing WiFi wins over success, mapped errors, and transport errors.
        problem = veniceSelfTestProblem(false, c.code);
        CHECK(problem != nullptr && strcmp(problem, "No WiFi") == 0);
        CHECK(problem == nullptr || strstr(problem, "Hermes") == nullptr);
    }
    if (fails) {
        printf("venice_selftest: %d check(s) failed\n", fails);
        return 1;
    }
    printf("venice_selftest: all checks passed\n");
    return 0;
}
