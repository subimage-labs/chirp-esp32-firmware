// venice_policy.h — pure venice-mode request policy: the Chirper persona,
// the pinned model ids, the device-held conversation history every chat
// request carries, and the HTTP error map the UI shows. Venicle is stateless
// and zero-retention, so context lives on the device alone. Arduino-free and
// host-tested (test/host/venice_policy_test.cpp). The persona's source of
// truth is tmp/chirper-persona.md.
#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace venice_policy {

// Venice rotates its catalog; these are pinned per firmware release. A model
// error on a turn (400 with model detail) means a release bump, not a runtime
// lookup (the docs' own advice notwithstanding — the device has no catalog
// fetch path in venice mode's phase 2).
static const char CHAT_MODEL[] = "zai-org-glm-5-2";
static const char STT_MODEL[] = "nvidia/parakeet-tdt-0.6b-v3";
static const char TTS_MODEL[] = "tts-kokoro";

// One spoken exchange the device remembers. Answers that never finished
// (aborted, failed) are not added by the caller.
using History = std::vector<std::pair<std::string, std::string>>;

// The newest HISTORY_TURNS exchanges ride along on each request; older
// context is dropped. Twelve exchanges is ~1-2 KB of JSON, well inside the
// request budget, and about as much conversation as the speaker's patience.
static const size_t HISTORY_TURNS = 12;

// The Chirper persona. Spoken answers stay short (TTS chunks per sentence),
// nothing is read as markdown, and the safety rails are the soft kind: this
// is a character card, not a filter.
static const char SYSTEM_PROMPT[] =
    "You are Chirper, a cheerful pocket companion on a small hand-held talking "
    "device with one button. You talk with kids.\n"
    "How you talk\n"
    "- Speak in short, plain sentences a kid understands. Most answers are one "
    "to three sentences.\n"
    "- Never use markdown, lists, asterisks, or emoji: your words are spoken "
    "aloud.\n"
    "- Be warm, playful, and encouraging. Gently correct, never scold.\n"
    "- Vary your openers and your activities so you never sound repetitive.\n"
    "What you do\n"
    "- Answer questions about anything kid-friendly: animals, dinosaurs, space, "
    "science, how things work, history, words, numbers.\n"
    "- Offer activities and keep them varied: jokes, riddles, trivia quizzes, "
    "would-you-rather, twenty questions, story building where you each say a "
    "sentence, rhyming games, amazing facts.\n"
    "- When a kid returns to a topic, add something new instead of repeating.\n"
    "- End some turns with a small invitation, like \"Want another riddle?\" "
    "— but not every turn.\n"
    "Safety\n"
    "- You talk with children. Keep everything age-appropriate: no violence, "
    "no horror, no adult topics, no bad words.\n"
    "- If a topic is not for kids, say so kindly and offer a fun alternative.\n"
    "- Never ask for personal details like a full name, address, school, or "
    "passwords.\n"
    "- You are a toy, not a parent or a doctor. If a kid sounds sad, scared, "
    "or unsafe, be kind and suggest they tell a grown-up they trust.";

// The chat request's messages: the persona, the newest HISTORY_TURNS
// exchanges in order, then the new question.
inline std::vector<std::pair<const char*, std::string>> buildMessages(
    const History& h, const std::string& q) {
    std::vector<std::pair<const char*, std::string>> m;
    m.reserve(2 + 2 * HISTORY_TURNS);
    m.emplace_back("system", std::string(SYSTEM_PROMPT));
    const size_t from = h.size() > HISTORY_TURNS ? h.size() - HISTORY_TURNS : 0;
    for (size_t i = from; i < h.size(); i++) {
        m.emplace_back("user", h[i].first);
        m.emplace_back("assistant", h[i].second);
    }
    m.emplace_back("user", q);
    return m;
}

// Trims the history to the newest HISTORY_TURNS exchanges.
inline void trimHistory(History& h) {
    if (h.size() > HISTORY_TURNS)
        h.erase(h.begin(), h.end() - (long)HISTORY_TURNS);
}

// The HTTP codes a kid or parent can act on, with the on-screen label
// turn.cpp shows (24 chars max). nullptr when the code needs no special
// case and the generic unreachable path applies.
inline const char* httpError(int code) {
    switch (code) {
    case 401: return "Bad API key";
    case 402: return "Out of credit";
    case 403: return "Key not allowed";
    case 429: return "Rate limited";
    default: return nullptr;
    }
}

// The boot check's one line in venice mode (selftest.cpp): the network, then
// the authed GET /models probe (`code` <= 0: no HTTP response). Never names
// Hermes: a venice device has none to look for.
inline const char* veniceSelfTestProblem(bool wifi, int code) {
    if (!wifi) return "No WiFi";
    if (code == 200) return nullptr;
    if (const char* e = httpError(code)) return e;
    return code <= 0 ? "Venice unreachable" : "Venice error";
}

// The voices the Setup tab offers (TTS_MODEL's), chosen by ear on the T-Deck's
// speaker. Stored by id (NVS chirp/ttsVoice), so reordering never changes what
// a device speaks with; an unknown id (a voice dropped here) is the first.
struct Voice {
    const char* label;      // the Setup row: "Voice: Heart"
    const char* id;         // Kokoro's voice id
};
static const Voice VOICES[] = {{"Sky", "af_sky"}, {"Heart", "af_heart"}, {"Bella", "af_bella"}};
static const int VOICE_COUNT = (int)(sizeof(VOICES) / sizeof(VOICES[0]));
static const char VOICE_PREVIEW[] = "Hi, I'm Chirp!";

inline int voiceIndex(const std::string& id) {
    for (int i = 0; i < VOICE_COUNT; i++)
        if (id == VOICES[i].id) return i;
    return 0;
}
inline int voiceNext(int i) { return (i + 1) % VOICE_COUNT; }

}  // namespace venice_policy
