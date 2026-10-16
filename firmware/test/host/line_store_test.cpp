// Host tests for the transcript line arena (line_store.h) and the PSRAM
// allocator's host fallback (psram_alloc.h). Run: firmware/test/host/run.sh
#include "line_store.h"
#include "psram_alloc.h"

#include <cstdio>
#include <cstring>
#include <memory>
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

// Counts blocks and bytes taken through it, so a test can see what a
// container allocates (and that nothing else does).
static long liveBlocks = 0, totalBlocks = 0;
template <class T>
struct CountAlloc {
    typedef T value_type;
    CountAlloc() {}
    template <class U>
    CountAlloc(const CountAlloc<U>&) {}
    T* allocate(size_t n) {
        liveBlocks++;
        totalBlocks++;
        return static_cast<T*>(::operator new(n * sizeof(T)));
    }
    void deallocate(T* p, size_t) {
        liveBlocks--;
        ::operator delete(p);
    }
};
template <class T, class U>
bool operator==(const CountAlloc<T>&, const CountAlloc<U>&) { return true; }
template <class T, class U>
bool operator!=(const CountAlloc<T>&, const CountAlloc<U>&) { return false; }

static void basics() {
    BasicLineStore<std::allocator> s;
    CHECK(s.empty() && s.size() == 0);
    s.push("hello world", 11, 0);
    s.push("", 0, 2);
    s.push("caf\xC3\xA9", 5, 1);
    CHECK(s.size() == 3 && !s.empty());
    CHECK(std::strcmp(s.text(0), "hello world") == 0 && s.length(0) == 11 && s.kind(0) == 0);
    CHECK(std::strcmp(s.text(1), "") == 0 && s.length(1) == 0 && s.kind(1) == 2);
    CHECK(std::strcmp(s.text(2), "caf\xC3\xA9") == 0 && s.length(2) == 5 && s.kind(2) == 1);
    CHECK(s.lastKind() == 1);
    // Each line is NUL-terminated in place, so it can be drawn without a copy.
    CHECK(s.text(0)[11] == '\0');

    s.popBack();
    CHECK(s.size() == 2 && s.lastKind() == 2);
    s.popBack();
    CHECK(s.size() == 1 && std::strcmp(s.text(0), "hello world") == 0);
    s.push("again", 5, 1);                           // reuses the space popBack gave back
    CHECK(s.size() == 2 && std::strcmp(s.text(1), "again") == 0 && s.textBytes() == 12 + 6);
    s.clear();
    CHECK(s.empty() && s.textBytes() == 0);
    s.push("x", 1, 0);
    CHECK(s.size() == 1 && std::strcmp(s.text(0), "x") == 0);

    // A pushed range need not be NUL-terminated; only its n bytes are taken.
    const char raw[] = {'a', 'b', 'c', 'd'};
    s.clear();
    s.push(raw, 2, 0);
    CHECK(std::strcmp(s.text(0), "ab") == 0 && s.length(0) == 2);
}

static void manyLines() {
    BasicLineStore<std::allocator> s;
    std::string expect[600];
    for (int i = 0; i < 600; i++) {
        expect[i] = "line number " + std::to_string(i) + " with some words to wrap around";
        s.push(expect[i].data(), expect[i].size(), (uint8_t)(i % 3));
    }
    CHECK(s.size() == 600);
    for (int i = 0; i < 600; i++) {
        CHECK(expect[i] == s.text(i) && s.length(i) == expect[i].size() && s.kind(i) == i % 3);
    }
}

// The point of the arena: a whole chat is two live blocks, however many
// lines, and every block comes from the given allocator.
static void allocationShape() {
    liveBlocks = totalBlocks = 0;
    {
        BasicLineStore<CountAlloc> s;
        CHECK(liveBlocks == 0);
        for (int i = 0; i < 800; i++) {
            const std::string t = "a wrapped line of about forty characters " + std::to_string(i);
            s.push(t.data(), t.size(), 1);
        }
        CHECK(liveBlocks == 2);                      // text buffer + line table
        CHECK(totalBlocks < 60);                     // geometric growth, not one block per line
        const long before = totalBlocks;
        s.clear();
        for (int i = 0; i < 800; i++) {              // re-wrapping reuses the capacity
            const std::string t = "a wrapped line of about forty characters " + std::to_string(i);
            s.push(t.data(), t.size(), 1);
        }
        CHECK(totalBlocks == before);
        CHECK(liveBlocks == 2);
    }
    CHECK(liveBlocks == 0);                          // all returned
}

static void psramFallback() {
    PsString a("hello");
    a += " there, a string long enough to leave the small-string buffer";
    CHECK(a == "hello there, a string long enough to leave the small-string buffer");
    PsVector<PsString> v;
    for (int i = 0; i < 100; i++) v.push_back(PsString(40, (char)('a' + i % 26)));
    CHECK(v.size() == 100 && v[99].size() == 40 && v[99][0] == (char)('a' + 99 % 26));
    v.erase(v.begin());
    CHECK(v.size() == 99 && v[0][0] == 'b');
    BasicLineStore<PsramAlloc> s;
    s.push("psram line", 10, 0);
    CHECK(std::strcmp(s.text(0), "psram line") == 0);
    CHECK(PsramAlloc<char>() == PsramAlloc<int>());
}

int main() {
    basics();
    manyLines();
    allocationShape();
    psramFallback();
    if (failures) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::puts("line_store_test: ok");
    return 0;
}
