#include "sse_stream.h"
#include <vector>

#include <cstdio>
static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { \
    std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
    ++failures; } } while (0)

static int calls;
static std::string payloadSeen;
static bool echo(const std::string& p, std::string& content, std::string& finish) {
    ++calls; payloadSeen = p; content.clear(); finish.clear();
    if (p == "BAD" || p.size() > 3000) return false;
    if (p == "FINISH") finish = "stop";
    else if (p == "BOTH") { content = "hi"; finish = "stop"; }
    else content = p;
    return true;
}
static bool invalid(const std::string& p, std::string&, std::string&) {
    ++calls; payloadSeen = p; return false;
}
static sse::Parser::Fed line(sse::Parser& p, const std::string& bytes, std::string& delta,
                            sse::Extract extract = echo) {
    sse::Parser::Fed f;
    for (unsigned char c : bytes) {
        f = p.feed(c, extract, delta);
        if (c != '\n') CHECK(!f.dataLine && !f.deliver);
    }
    return f;
}
static void parser() {
    struct Case { const char* wire; const char* content; };
    for (const Case c : {Case{"data: hello\n", "hello"}, Case{"data:hello\n", "hello"},
         Case{"data:  hello\n", " hello"}, Case{"data: hello\r\n", "hello"}, Case{"da\rta: x\n", "x"}}) {
        sse::Parser p; std::string d; calls = 0;
        const auto f = line(p, c.wire, d);
        CHECK(f.dataLine && f.deliver && d == c.content && calls == 1);
        CHECK(p.gotDelta() && !p.sawFinish() && !p.finished());
    }
    for (const char* wire : {": keepalive\n", "\n", "event: ping\n", "id: 1\n", "1f4\n", "DATA: x\n", " data: x\n", "data\n"}) {
        sse::Parser p; std::string d; calls = 0;
        const auto f = line(p, wire, d);
        CHECK(!f.dataLine && !f.deliver && calls == 0);
        CHECK(!p.gotDelta() && !p.sawFinish() && !p.finished());
    }
    for (const std::string& wire : {std::string("data: [DONE]\n"), std::string("data:[DONE]\n"),
         std::string("data: [DONE]\0xyz\n", 17)}) {
        sse::Parser p; std::string d; calls = 0;
        auto f = line(p, wire, d);
        CHECK(f.dataLine && !f.deliver && p.finished() && calls == 0);
        CHECK(!p.gotDelta() && !p.sawFinish());
    }
    for (const char* wire : {"data: [DONE] \n", "data: [done]\n"}) {
        sse::Parser p; std::string d; calls = 0;
        CHECK(line(p, wire, d).dataLine && !p.finished() && calls == 1);
    }
    sse::Parser p; std::string d;
    auto f = line(p, "data: BAD\n", d);
    CHECK(f.dataLine && !f.deliver && !p.gotDelta() && !p.sawFinish());
    calls = 0; f = line(p, "data:\n", d);
    CHECK(f.dataLine && !f.deliver && calls == 1 && payloadSeen.empty());
    CHECK(!p.gotDelta() && !p.sawFinish());
    f = line(p, "data: FINISH\n", d);
    CHECK(f.dataLine && !f.deliver && p.sawFinish() && !p.gotDelta());
    f = line(p, "data: hi\n", d);
    CHECK(f.deliver && d == "hi" && p.sawFinish() && p.gotDelta());
    line(p, "data:\ndata: BAD\n\n", d);
    CHECK(p.sawFinish() && p.gotDelta());
    line(p, "data:[DONE]\n", d); CHECK(p.finished());
    sse::Parser both;
    f = line(both, "data: BOTH\n", d);
    CHECK(f.deliver && d == "hi" && both.sawFinish() && both.gotDelta());
    for (size_t n : {2041u, 2042u}) {
        sse::Parser cap; calls = 0;
        f = line(cap, "data: " + std::string(n, 'x') + "\n", d);
        CHECK(f.dataLine && f.deliver && calls == 1);
        CHECK(payloadSeen == std::string(2041, 'x'));
    }
    sse::Parser longLine;
    f = line(longLine, "data:" + std::string(4995, 'x') + "\n", d, invalid);
    CHECK(f.dataLine && !f.deliver && !longLine.gotDelta());
    CHECK(payloadSeen.size() == 2042);
    line(longLine, std::string(5000, 'x') + "\n", d);
    CHECK(line(longLine, "data: ok\n", d).deliver && d == "ok");
    sse::Parser binary;
    const std::string bytes("a\0\xc3\xa9", 4);
    CHECK(line(binary, "data:" + bytes + "\n", d).deliver && d == bytes);

    // Chunk boundaries include immediately after a newline and within a prefix.
    const std::string stream = "data: hi\n\ndata: FINISH\ndata: BAD\ndata:[DONE]\n";
    for (size_t width : {1u, 3u, 9u, 100u}) {
        sse::Parser split; std::vector<std::string> delivered; int progress = 0;
        for (size_t off = 0; off < stream.size(); off += width) {
            for (unsigned char c : stream.substr(off, width)) {
                f = split.feed(c, echo, d);
                if (f.dataLine) ++progress;
                if (f.deliver) delivered.push_back(d);
            }
        }
        CHECK(delivered == std::vector<std::string>{"hi"});
        CHECK(progress == 4 && split.finished() && split.sawFinish() && split.gotDelta());
    }
}
int main() {
    CHECK(sse::tryAgain(0, -1) && sse::tryAgain(1, -1));
    CHECK(!sse::tryAgain(2, -1) && !sse::tryAgain(1, 200));
    CHECK(!sse::tryAgain(1, -11) && !sse::tryAgain(0, 200));
    struct Status { int code; AskEnd end; };
    for (const Status s : {Status{401, AskEnd::BadKey}, Status{402, AskEnd::NoCredit},
         Status{403, AskEnd::NotAllowed}, Status{429, AskEnd::RateLimited},
         Status{400, AskEnd::Unreachable}, Status{404, AskEnd::Unreachable},
         Status{500, AskEnd::Unreachable}, Status{-1, AskEnd::Unreachable}, Status{-11, AskEnd::Unreachable}})
    for (bool stopped : {false, true}) {
        CHECK(sse::endForStatus(false, s.code, stopped) == (stopped ? AskEnd::Stopped : AskEnd::Unreachable));
        CHECK(sse::endForStatus(true, s.code, stopped) ==
              (s.end == AskEnd::Unreachable && stopped ? AskEnd::Stopped : s.end));
    }
    using sse::Step;
    struct Timing { uint32_t elapsed, idle; bool stop, connected; size_t avail; Step end; };
    for (const Timing t : {
        Timing{239999, 0, false, true, 0, Step::Wait},
        Timing{240000, 0, false, true, 1, Step::Budget},
        Timing{240000, 100000, true, false, 0, Step::Budget},
        Timing{10, 100000, true, false, 0, Step::Stopped},
        Timing{10, 100000, true, true, 1, Step::Stopped},
        Timing{10, 0, false, false, 0, Step::Closed},
        Timing{10, 100000, false, false, 0, Step::Closed},
        Timing{10, 0, false, false, 5, Step::Read},
        Timing{10, 90000, false, true, 0, Step::Wait},
        Timing{10, 90001, false, true, 0, Step::Idle},
        Timing{10, 1000000, false, true, 1, Step::Read},
        Timing{10, 0, false, true, 0, Step::Wait},
        Timing{static_cast<uint32_t>(5u - 0xFFFFFF00u), 0, false, true, 0, Step::Wait}})
        CHECK(sse::step(t.elapsed, t.idle, t.stop, t.connected, t.avail, 240000, 90000) == t.end);
    // Columns: no flags, delta, finish, both. Explicit outcomes, not the decision formula.
    struct Ending { Step step; AskEnd expected[4]; };
    for (const Ending e : {
        Ending{Step::Done, {AskEnd::Answered, AskEnd::Answered, AskEnd::Answered, AskEnd::Answered}},
        Ending{Step::Stopped, {AskEnd::Stopped, AskEnd::Stopped, AskEnd::Stopped, AskEnd::Stopped}},
        Ending{Step::Idle, {AskEnd::Silent, AskEnd::Silent, AskEnd::Silent, AskEnd::Silent}},
        Ending{Step::Closed, {AskEnd::Unreachable, AskEnd::Answered, AskEnd::Answered, AskEnd::Answered}},
        Ending{Step::Budget, {AskEnd::Silent, AskEnd::Answered, AskEnd::Answered, AskEnd::Answered}}})
        for (int i = 0; i < 4; ++i) CHECK(sse::askEnd(e.step, i >= 2, i % 2) == e.expected[i]);
    parser();
    return failures ? 1 : 0;
}
