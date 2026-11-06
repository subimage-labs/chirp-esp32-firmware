// Host tests for SentencePacker. Run: firmware/test/host/run.sh
#include "sentence_packer.h"

#include <cstdio>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,   \
                         __LINE__, #cond);                                \
            failures++;                                                   \
        }                                                                 \
    } while (0)

using Chunks = std::vector<std::string>;

static Chunks stream(SentencePacker& p, std::initializer_list<const char*> deltas) {
    Chunks out;
    for (const char* d : deltas) p.feed(d, out);
    p.finish(out);
    return out;
}

// Short answers (below the pack threshold) come out as one chunk at finish.
static void shortAnswerIsOneChunk() {
    SentencePacker p(2, 14, 80);
    const Chunks c = stream(p, {"Hi ", "there. ", "How are", " you?"});
    CHECK(c.size() == 1 && c[0] == "Hi there. How are you?");
}

// A decimal split across deltas ("3" "." "5") must not end a sentence.
static void decimalAcrossDeltasStaysWhole() {
    SentencePacker p(1, 1, 80);
    Chunks out;
    p.feed("It costs 3", out);
    p.feed(".", out);
    CHECK(out.empty());              // "." at buffer end waits for more text
    p.feed("5 dollars today. Next", out);
    CHECK(out.size() == 1 && out[0] == "It costs 3.5 dollars today.");
    p.finish(out);
    CHECK(out.size() == 2 && out[1] == "Next");
}

// Chunks close once both minimums are met, carrying whole sentences.
static void packsToMinimums() {
    SentencePacker p(2, 6, 80);
    Chunks out;
    p.feed("One two three. ", out);
    CHECK(out.empty());              // 1 sentence: below minSents
    p.feed("Four five six seven. Eight", out);
    CHECK(out.size() == 1 && out[0] == "One two three. Four five six seven.");
    p.finish(out);
    CHECK(out.size() == 2 && out[1] == "Eight");
}

// maxWords forces a chunk even below minSents.
static void maxWordsForcesClose() {
    SentencePacker p(3, 1, 5);
    Chunks out;
    p.feed("a b c d e f. ", out);
    CHECK(out.size() == 1 && out[0] == "a b c d e f.");
}

static void speakableStripsMarkdownAndUrls() {
    CHECK(SentencePacker::speakable("  **Bold** see https://x.y/z now #1 `code`") ==
          "Bold see  now 1 code");
    SentencePacker p(1, 1, 80);
    const Chunks c = stream(p, {"Visit http://a.b today. ", "_ok_."});
    CHECK(c.size() == 2 && c[0] == "Visit  today." && c[1] == "ok.");
}

// One-character leftovers (a stray "!" tail) are dropped, never spoken.
static void strayPunctuationDropped() {
    SentencePacker p(1, 1, 80);
    const Chunks c = stream(p, {"Done. ", "!"});
    CHECK(c.size() == 1 && c[0] == "Done.");
}

static void resetClearsPartialState() {
    SentencePacker p(1, 1, 80);
    Chunks out;
    p.feed("stale half sentence", out);
    p.reset();
    p.finish(out);
    CHECK(out.empty());
}

// The first chunk goes out after one sentence so speech starts early; the
// rest of the answer packs normally.
static void firstChunkClosesAfterFirstSentence() {
    SentencePacker p(2, 14, 80, 3);
    Chunks out;
    p.feed("The sky looks blue. ", out);
    CHECK(out.size() == 1 && out[0] == "The sky looks blue.");
    p.feed("Air scatters short waves. Blue is short. ", out);
    CHECK(out.size() == 1);          // second chunk: below 14 words, waits
    p.finish(out);
    CHECK(out.size() == 2 && out[1] == "Air scatters short waves. Blue is short.");
}

// "Sure." alone would be a clipped chunk: a too-short first sentence waits
// for the next one.
static void tinyFirstSentenceWaits() {
    SentencePacker p(2, 14, 80, 3);
    Chunks out;
    p.feed("Sure. ", out);
    CHECK(out.empty());
    p.feed("Here it is now. ", out);
    CHECK(out.size() == 1 && out[0] == "Sure. Here it is now.");
}

static void resetRestoresFirstChunkRule() {
    SentencePacker p(2, 14, 80, 3);
    Chunks out;
    p.feed("First answer starts here. ", out);
    p.reset();
    out.clear();
    p.feed("Second answer starts here. ", out);
    CHECK(out.size() == 1);
}

// ---- the chunk ramp: 1 sentence, then a short second chunk, then growth.
// The device's numbers (config.h CHUNK_*): first 3 words, min 2 sentences /
// 14 words, second chunk capped at 30 words, later chunks >= 30, max 80.
static SentencePacker deviceRamp() { return SentencePacker(2, 14, 80, 3, 30, 30); }

// "n" words ending in a period, then a space: one sentence of exactly n words.
static std::string sentence(int n, char tag = 'a') {
    std::string s;
    for (int i = 0; i < n; i++) s += (i ? " " : "") + std::string(1, tag) + std::to_string(i);
    return s + ". ";
}
static std::string sentences(int count, int words) {
    std::string s;
    for (int i = 0; i < count; i++) s += sentence(words, (char)('a' + i % 26));
    return s;
}
static int words(const std::string& s) { return SentencePacker::wordCount(s); }

// A burst: the whole answer arrives in one delta (the stream was held up by the
// first chunk's synthesis). Chunks must still follow the ramp, not collapse into one.
static void burstFollowsTheRamp() {
    SentencePacker p = deviceRamp();
    Chunks out;
    p.feed((sentences(12, 8) + "tail").c_str(), out);      // 12 sentences of 8 words
    CHECK(out.size() >= 4 && words(out[0]) == 8);          // 1 sentence
    CHECK(out.size() >= 4 && words(out[1]) == 16);         // second chunk: 2 sentences
    CHECK(out.size() >= 4 && words(out[2]) == 32);         // then grown: >= 30 words -> 4 sentences
    CHECK(out.size() >= 4 && words(out[3]) == 32);
    // Nothing ever exceeds its minimum by more than one sentence.
    for (size_t i = 2; i < out.size(); i++) CHECK(words(out[i]) < 30 + 8);
    p.finish(out);
    int total = 0;
    for (const auto& c : out) total += words(c);
    CHECK(total == 12 * 8 + 1);                            // nothing lost, nothing repeated
}

// The measured case: a short first chunk then a long one. With long sentences the
// second chunk is bounded by the word cap, not by whatever arrived together.
static void secondChunkIsCapped() {
    SentencePacker p = deviceRamp();
    Chunks out;
    p.feed(sentence(4).c_str(), out);                      // chunk 0
    CHECK(out.size() == 1);
    p.feed(sentence(29, 'b').c_str(), out);                // 29 words, 1 sentence: below cap, waits
    CHECK(out.size() == 1);
    p.feed("x. ", out);                                    // 30 words: the cap closes it
    CHECK(out.size() == 2 && words(out[1]) == 30);

    SentencePacker q = deviceRamp();
    Chunks o2;
    q.feed(sentence(4).c_str(), o2);
    q.feed(sentence(30, 'c').c_str(), o2);                 // exactly the cap, a single sentence
    CHECK(o2.size() == 2 && words(o2[1]) == 30);

    // Two sentences meeting the minimums close it even well under the cap.
    SentencePacker r = deviceRamp();
    Chunks o3;
    r.feed(sentence(4).c_str(), o3);
    r.feed(sentence(7, 'd').c_str(), o3);
    CHECK(o3.size() == 1);                                 // 7 words < 14: waits for more
    r.feed(sentence(7, 'e').c_str(), o3);
    CHECK(o3.size() == 2 && words(o3[1]) == 14);           // boundary: 2 sentences, exactly 14 words
}

// A second chunk of one very long sentence is the one place it can overshoot.
static void oneLongSentenceOvershootsOnlyBySentence() {
    SentencePacker p = deviceRamp();
    Chunks out;
    p.feed(sentence(4).c_str(), out);
    p.feed(sentence(60, 'f').c_str(), out);
    CHECK(out.size() == 2 && words(out[1]) == 60);
}

// Later chunks grow: their word minimum is laterMinWords, below the 80-word max.
static void laterChunksGrow() {
    SentencePacker p = deviceRamp();
    Chunks out;
    p.feed(sentence(4).c_str(), out);                      // chunk 0
    p.feed(sentences(2, 8).c_str(), out);                  // chunk 1 (16 words)
    CHECK(out.size() == 2);
    p.feed(sentences(3, 8).c_str(), out);                  // 24 words: below the later minimum
    CHECK(out.size() == 2);
    p.feed(sentence(8, 'z').c_str(), out);                 // 32 words
    CHECK(out.size() == 3 && words(out[2]) == 32);
    p.feed(sentence(90, 'y').c_str(), out);                // one huge sentence: max closes it
    CHECK(out.size() == 4 && words(out[3]) == 90);
}

// maxWords below the later minimum wins; finish() flushes what is waiting.
static void rampEdges() {
    SentencePacker p(2, 14, 20, 3, 30, 40);                // later min (40) above max (20)
    Chunks out;
    p.feed(sentence(4).c_str(), out);
    p.feed(sentences(2, 8).c_str(), out);
    CHECK(out.size() == 2);
    p.feed(sentences(3, 8).c_str(), out);                  // max 20 caps the later minimum: 16 waits, 24 closes
    CHECK(out.size() == 3 && words(out[2]) == 24);
    p.feed(sentence(5, 'q').c_str(), out);
    p.finish(out);
    CHECK(out.size() == 4 && words(out[3]) == 5);          // the leftover goes out at finish
}

// Without the ramp parameters nothing changes: same chunks as the old policy.
static void noRampMeansPlainMinimums() {
    SentencePacker p(2, 14, 80, 3);
    Chunks out;
    p.feed((sentence(4) + sentences(2, 8) + sentences(2, 8) + sentences(2, 8)).c_str(), out);
    CHECK(out.size() == 4 && words(out[0]) == 4 && words(out[1]) == 16 && words(out[2]) == 16 &&
          words(out[3]) == 16);
}

static void resetRestartsTheRamp() {
    SentencePacker p = deviceRamp();
    Chunks out;
    p.feed((sentence(4) + sentences(2, 8) + sentences(4, 8)).c_str(), out);
    CHECK(out.size() == 3);
    p.reset();
    out.clear();
    p.feed((sentence(4) + sentences(2, 8)).c_str(), out);
    CHECK(out.size() == 2 && words(out[0]) == 4 && words(out[1]) == 16);
}

int main() {
    burstFollowsTheRamp();
    secondChunkIsCapped();
    oneLongSentenceOvershootsOnlyBySentence();
    laterChunksGrow();
    rampEdges();
    noRampMeansPlainMinimums();
    resetRestartsTheRamp();
    shortAnswerIsOneChunk();
    decimalAcrossDeltasStaysWhole();
    packsToMinimums();
    maxWordsForcesClose();
    speakableStripsMarkdownAndUrls();
    strayPunctuationDropped();
    resetClearsPartialState();
    firstChunkClosesAfterFirstSentence();
    tinyFirstSentenceWaits();
    resetRestoresFirstChunkRule();
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("sentence_packer: all checks passed\n");
    return 0;
}
