// sentence_packer.h — turns streamed LLM text deltas into speech-length TTS
// chunks. Pure (std::string, no Arduino), host-tested.
//
// Chunk sizes ramp up so synthesis of chunk i+1 (which starts when chunk i
// starts playing) finishes before chunk i ends, and the speaker never waits:
//   chunk 0   closes at the first sentence of >= firstMinWords words (0 = no
//             special first chunk): the listener's wait is the time to the
//             first chunk, and synthesis takes ~0.2 s.
//   chunk 1   short: >= minSents sentences and >= minWords words (so about two
//             sentences), and closed at once when it reaches secondMaxWords
//             (0 = no cap). Without this the second chunk had no upper bound
//             but maxWords and a long one left a ~2 s synthesis gap after a
//             one-sentence first chunk.
//   chunk 2+  grow: the word minimum becomes laterMinWords (0 = minWords), the
//             cap maxWords.
// A chunk closes at the sentence boundary where it meets its rule, even in the
// middle of one delta (a stream that arrives in a burst is not one huge chunk);
// sentences are never split, so a chunk can overshoot by at most one sentence.
// A sentence ends at . ! ? followed by space or newline; a terminator at the
// very end of the buffered text waits for the next delta (streams split "3.5"
// as "3" "." "5"), and finish() flushes whatever is left. Emitted chunks are
// speakable: markdown emphasis/headers and URLs removed.
#pragma once

#include <cctype>
#include <string>
#include <vector>

class SentencePacker {
public:
    SentencePacker(int minSents, int minWords, int maxWords, int firstMinWords = 0,
                   int secondMaxWords = 0, int laterMinWords = 0)
        : minSents_(minSents), minWords_(minWords), maxWords_(maxWords),
          firstMinWords_(firstMinWords), secondMaxWords_(secondMaxWords),
          laterMinWords_(laterMinWords) {}

    void reset() {
        tail_.clear();
        acc_.clear();
        sents_ = words_ = 0;
        emitted_ = 0;
    }

    // Appends completed chunks (if any) to `out`.
    void feed(const char* delta, std::vector<std::string>& out) {
        tail_ += delta;
        size_t start = 0;
        for (size_t i = 0; i + 1 < tail_.size(); i++) {
            const char ch = tail_[i];
            const char nx = tail_[i + 1];
            if ((ch == '.' || ch == '!' || ch == '?') && (nx == ' ' || nx == '\n')) {
                addSentence(tail_.substr(start, i + 1 - start));
                close(false, out);                 // as soon as the chunk is complete
                start = i + 1;
            }
        }
        tail_.erase(0, start);
        close(false, out);
    }

    // End of stream: the unterminated tail is the last sentence.
    void finish(std::vector<std::string>& out) {
        if (tail_.size() > 1) addSentence(tail_);
        tail_.clear();
        close(true, out);
    }

    static std::string speakable(const std::string& in) {
        std::string out;
        out.reserve(in.size());
        size_t i = 0;
        while (i < in.size()) {
            if (in.compare(i, 7, "http://") == 0 || in.compare(i, 8, "https://") == 0) {
                while (i < in.size() && !isspace((unsigned char)in[i])) i++;
                continue;
            }
            const char c = in[i++];
            if (c == '*' || c == '`' || c == '_' || c == '#') continue;
            out += c;
        }
        size_t lead = 0;
        while (lead < out.size() && isspace((unsigned char)out[lead])) lead++;
        return out.substr(lead);
    }

    static int wordCount(const std::string& s) {
        int w = 0;
        bool in = false;
        for (const char c : s) {
            const bool sp = isspace((unsigned char)c);
            if (!sp && !in) { w++; in = true; }
            else if (sp) in = false;
        }
        return w;
    }

private:
    static std::string trim(const std::string& s) {
        size_t b = 0, e = s.size();
        while (b < e && isspace((unsigned char)s[b])) b++;
        while (e > b && isspace((unsigned char)s[e - 1])) e--;
        return s.substr(b, e - b);
    }

    void addSentence(const std::string& raw) {
        const std::string s = trim(raw);
        if (s.size() <= 1) return;
        if (!acc_.empty()) acc_ += ' ';
        acc_ += s;
        sents_++;
        words_ += wordCount(s);
    }

    void close(bool force, std::vector<std::string>& out) {
        if (acc_.size() < 2) return;
        int minSents = minSents_, minWords = minWords_, maxWords = maxWords_;
        if (emitted_ == 1 && secondMaxWords_ > 0 && secondMaxWords_ < maxWords)
            maxWords = secondMaxWords_;
        else if (emitted_ >= 2 && laterMinWords_ > minWords)
            minWords = laterMinWords_ < maxWords ? laterMinWords_ : maxWords;
        const bool big = words_ >= maxWords;
        const bool early = emitted_ == 0 && firstMinWords_ > 0 && sents_ >= 1 &&
                           words_ >= firstMinWords_;
        if (!force && !big && !early && (sents_ < minSents || words_ < minWords)) return;
        std::string chunk = speakable(acc_);
        if (chunk.size() >= 2) {
            out.push_back(std::move(chunk));
            emitted_++;
        }
        acc_.clear();
        sents_ = words_ = 0;
    }

    int minSents_, minWords_, maxWords_, firstMinWords_, secondMaxWords_, laterMinWords_;
    std::string tail_;  // text after the last sentence boundary
    std::string acc_;   // sentences waiting to reach chunk size
    int sents_ = 0, words_ = 0;
    int emitted_ = 0;   // chunks emitted this answer
};
