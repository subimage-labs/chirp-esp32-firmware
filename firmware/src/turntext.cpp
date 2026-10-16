#include "turntext.h"

#include <cstring>

namespace {

// Copies at most cap-1 bytes plus NUL. Returns true when the source was
// longer than the cap (surfaced as the view's truncation flag).
bool copyBounded(char* dst, size_t cap, const char* src) {
    const size_t n = strnlen(src, cap - 1);
    memcpy(dst, src, n);
    dst[n] = '\0';
    return src[n] != '\0';
}

}  // namespace

void TurnSnap::publish(const char* qText, const char* aText) {
    std::lock_guard<std::mutex> g(m);
    qTrunc = copyBounded(q, sizeof(q), qText);
    aTrunc = copyBounded(a, sizeof(a), aText);
    rev++;
}

void TurnSnap::read(TurnSnapView& out, uint32_t& revOut) const {
    std::lock_guard<std::mutex> g(m);
    memcpy(out.q, q, sizeof(out.q));
    memcpy(out.a, a, sizeof(out.a));
    out.qTrunc = qTrunc;
    out.aTrunc = aTrunc;
    revOut = rev;
}

uint32_t TurnSnap::currentRev() const {
    std::lock_guard<std::mutex> g(m);
    return rev;
}
