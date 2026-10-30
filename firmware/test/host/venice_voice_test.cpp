// venice_voice_test.cpp — the Setup tab's venice voice list (venice_policy.h):
// a stored id finds its voice, an unknown or empty one falls back to the first,
// and stepping visits every voice once before coming back. Run: test/host/run.sh
#include <cstdio>
#include <cstring>
#include <set>
#include <string>

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
    for (int i = 0; i < VOICE_COUNT; i++) CHECK(voiceIndex(VOICES[i].id) == i);
    CHECK(voiceIndex("af_nope") == 0);                // a voice dropped from the list
    CHECK(voiceIndex("") == 0);                       // nothing stored yet
    std::set<std::string> seen;
    int v = 0;
    for (int n = 0; n < VOICE_COUNT; n++) {
        seen.insert(VOICES[v].id);
        v = voiceNext(v);
    }
    CHECK((int)seen.size() == VOICE_COUNT && v == 0);  // every voice once, then back
    CHECK(strcmp(VOICES[0].id, "af_sky") == 0);       // the default is the voice devices had
    if (fails) {
        printf("venice_voice: %d check(s) failed\n", fails);
        return 1;
    }
    printf("venice_voice: all checks passed\n");
    return 0;
}
