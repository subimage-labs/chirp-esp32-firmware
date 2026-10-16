// Host tests for turntext (TurnSnap + TurnReader). Run: firmware/test/host/run.sh
// Built with -fsanitize=thread so a data race fails the run.
#include "turntext.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

static int failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,   \
                         __LINE__, #cond);                                \
            failures++;                                                   \
        }                                                                 \
    } while (0)

static bool terminated(const char* buf, size_t cap) {
    return std::memchr(buf, '\0', cap) != nullptr;
}

// Reader never observes a mix of two publishes: q and a carry the same
// sequence number in every view, and both stay NUL-terminated.
static void concurrentViewsAreConsistent() {
    static TurnSnap snap;
    static TurnReader reader(snap);
    constexpr int kPublishes = 20000;
    std::atomic<bool> done{false};

    std::thread writer([&] {
        char q[64], a[SNAP_A_CAP];
        for (int i = 1; i <= kPublishes; i++) {
            std::snprintf(q, sizeof(q), "q%d", i);
            // Variable-length answer so stale tail bytes would show.
            const int pad = (i * 37) % 3000;
            int n = std::snprintf(a, sizeof(a), "a%d:", i);
            std::memset(a + n, 'x', pad);
            a[n + pad] = '\0';
            snap.publish(q, a);
        }
        done = true;
    });

    int views = 0;
    while (!done.load()) {
        if (!reader.poll()) continue;
        const TurnSnapView& v = reader.view();
        CHECK(terminated(v.q, sizeof(v.q)));
        CHECK(terminated(v.a, sizeof(v.a)));
        int qi = -1, ai = -2;
        std::sscanf(v.q, "q%d", &qi);
        std::sscanf(v.a, "a%d:", &ai);
        CHECK(qi == ai);
        const char* body = std::strchr(v.a, ':');
        CHECK(body && std::strlen(body + 1) == (size_t)((ai * 37) % 3000));
        views++;
    }
    writer.join();
    reader.poll();
    CHECK(std::strcmp(reader.view().q, "q20000") == 0);
    std::printf("concurrent: %d consistent views\n", views);
}

static void truncationIsBoundedAndFlagged() {
    TurnSnap snap;
    TurnReader reader(snap);
    const std::string fits(SNAP_Q_CAP - 1, 'q');
    const std::string over(SNAP_A_CAP + 500, 'a');
    snap.publish(fits.c_str(), over.c_str());
    CHECK(reader.poll());
    const TurnSnapView& v = reader.view();
    CHECK(std::strlen(v.q) == SNAP_Q_CAP - 1);
    CHECK(!v.qTrunc);
    CHECK(std::strlen(v.a) == SNAP_A_CAP - 1);
    CHECK(v.aTrunc);

    snap.publish("", "short");                    // shrink: old bytes gone
    CHECK(reader.poll());
    CHECK(reader.view().q[0] == '\0');
    CHECK(std::strcmp(reader.view().a, "short") == 0);
    CHECK(!reader.view().aTrunc);
}

// A new turn must never replay text published during the previous turn,
// including text the UI polled but had not rendered yet.
static void pinDropsPriorTurnText() {
    TurnSnap snap;
    TurnReader reader(snap);
    snap.publish("old q", "old answer");
    CHECK(reader.poll());
    CHECK(reader.dirty());                        // polled, not yet rendered

    reader.pin();                                 // beginTurn
    CHECK(!reader.dirty());
    CHECK(!reader.poll());                        // nothing new since pin
    CHECK(!reader.dirty());

    snap.publish("new q", "");
    CHECK(reader.poll());
    CHECK(reader.dirty());
    CHECK(std::strcmp(reader.view().q, "new q") == 0);
    CHECK(reader.view().a[0] == '\0');
}

static void renderedRevisionIsNotRedrawn() {
    TurnSnap snap;
    TurnReader reader(snap);
    snap.publish("q", "a1");
    CHECK(reader.poll());
    reader.clearDirty();                          // rendered
    CHECK(!reader.poll());
    CHECK(!reader.dirty());
    snap.publish("q", "a1 a2");
    CHECK(reader.poll());
    CHECK(reader.dirty());
}

int main() {
    concurrentViewsAreConsistent();
    truncationIsBoundedAndFlagged();
    pinDropsPriorTurnText();
    renderedRevisionIsNotRedrawn();
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("turntext: all checks passed\n");
    return 0;
}
