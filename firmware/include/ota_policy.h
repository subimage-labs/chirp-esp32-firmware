// ota_policy.h — pure OTA decisions (no Arduino): parsing the shim's signed
// firmware manifest, the string that is signed, and the verdicts around it.
// Host-tested (test/host/ota_policy_test.cpp); the device I/O and the
// signature check itself (mbedtls) live in ota.h/ota.cpp.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const size_t OTA_MANIFEST_MAX = 512;   // bytes the device reads; more is refused
static const size_t OTA_SIG_MAX = 72;         // DER ECDSA P-256 signature, upper bound
static const size_t OTA_SIG_B64_MAX = 96;     // base64 of OTA_SIG_MAX bytes
static const size_t OTA_CANON_MAX = 192;      // canonical string incl. NUL
static const uint32_t OTA_TOTAL_MS = 120000;  // whole download-and-flash attempt

// The shim's GET /firmware/manifest.json body, written by
// host/publish-firmware.py:
//   {"build":1790000000,"version":"v0.3-14-gdeadbee","size":1234567,
//    "sha256":"<64 lowercase hex>","sig":"<base64 DER ECDSA P-256>"}
// Exactly these five members, each once, in any order; anything else is
// refused. `sig` signs the canonical string (otaCanonical).
struct OtaManifest {
    uint32_t build;     // monotonic build number (unix seconds at build time)
    char version[48];   // git describe output the image was built with
    uint32_t size;      // chirp.bin bytes
    char sha256[65];    // lowercase hex of chirp.bin, NUL-terminated
    uint8_t sig[OTA_SIG_MAX];   // DER signature bytes
    size_t sigLen;
    bool valid;         // every member present and well-formed
};

inline void otaSkipWs(const char*& p) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
}

// A JSON string at p (no escapes: versions, hex and base64 never need them)
// into dst; advances p past the closing quote. Empty or longer than cap-1
// bytes fails.
inline bool otaParseString(const char*& p, char* dst, size_t cap) {
    if (*p != '"') return false;
    const char* q = p + 1;
    size_t n = 0;
    while (*q && *q != '"') {
        if (*q == '\\' || (unsigned char)*q < 0x20 || n + 1 >= cap) return false;
        dst[n++] = *q++;
    }
    if (*q != '"' || n == 0) return false;
    dst[n] = '\0';
    p = q + 1;
    return true;
}

// A JSON integer at p that fits a uint32_t: digits only, no sign, no leading
// zero. Advances p past the digits; what follows is the grammar's business.
inline bool otaParseU32(const char*& p, uint32_t* out) {
    if (*p < '0' || *p > '9') return false;
    if (*p == '0' && p[1] >= '0' && p[1] <= '9') return false;
    uint32_t acc = 0;
    const char* q = p;
    for (; *q >= '0' && *q <= '9'; q++) {
        const uint32_t d = (uint32_t)(*q - '0');
        if (acc > (0xFFFFFFFFu - d) / 10u) return false;
        acc = acc * 10u + d;
    }
    *out = acc;
    p = q;
    return true;
}

// git-describe versions are [0-9A-Za-z._+-]; anything else — including a
// crafted value that would swallow the next member — is rejected.
inline bool otaVersionWellFormed(const char* v) {
    if (!v[0]) return false;
    for (size_t i = 0; v[i]; i++) {
        const char c = v[i];
        const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
                        (c >= 'A' && c <= 'Z') || c == '.' || c == '_' ||
                        c == '+' || c == '-';
        if (!ok) return false;
    }
    return true;
}

// Exactly 64 lowercase hex digits.
inline bool otaSha256WellFormed(const char* v) {
    for (size_t i = 0; i < 64; i++)
        if (!((v[i] >= '0' && v[i] <= '9') || (v[i] >= 'a' && v[i] <= 'f')))
            return false;
    return v[64] == '\0';
}

inline int otaB64Val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

// Standard base64 with padding. The length must be a multiple of four, '='
// only closes the last group, and the output must fit cap.
inline bool otaBase64Decode(const char* in, uint8_t* out, size_t cap, size_t* n) {
    const size_t len = strlen(in);
    if (len == 0 || len % 4 != 0) return false;
    size_t o = 0;
    for (size_t i = 0; i < len; i += 4) {
        const bool last = i + 4 == len;
        int v[4];
        int pad = 0;
        for (int k = 0; k < 4; k++) {
            if (in[i + k] == '=') {
                if (!last || k < 2) return false;
                pad++;
                v[k] = 0;
            } else {
                if (pad) return false;          // data after padding
                v[k] = otaB64Val(in[i + k]);
                if (v[k] < 0) return false;
            }
        }
        const uint32_t w = ((uint32_t)v[0] << 18) | ((uint32_t)v[1] << 12) |
                           ((uint32_t)v[2] << 6) | (uint32_t)v[3];
        const size_t bytes = 3 - (size_t)pad;
        if (o + bytes > cap) return false;
        out[o++] = (uint8_t)(w >> 16);
        if (bytes > 1) out[o++] = (uint8_t)(w >> 8);
        if (bytes > 2) out[o++] = (uint8_t)w;
    }
    *n = o;
    return true;
}

// The strict parse. A failed parse leaves *out zeroed with valid == false.
// Beyond the grammar: version and sha256 must be well-formed, and the
// signature must decode to a DER SEQUENCE of plausible length (mbedtls does
// the real check).
inline bool otaManifestParse(const char* json, OtaManifest* out) {
    *out = OtaManifest();
    if (!json) return false;
    size_t len = 0;
    while (json[len] && len <= OTA_MANIFEST_MAX) len++;
    if (len > OTA_MANIFEST_MAX) return false;

    enum { F_BUILD = 1, F_VERSION = 2, F_SIZE = 4, F_SHA = 8, F_SIG = 16, F_ALL = 31 };
    OtaManifest m = OtaManifest();
    char sigText[OTA_SIG_B64_MAX + 1];
    unsigned seen = 0;
    const char* p = json;
    otaSkipWs(p);
    if (*p++ != '{') return false;
    for (;;) {
        otaSkipWs(p);
        char key[10];
        if (!otaParseString(p, key, sizeof key)) return false;
        otaSkipWs(p);
        if (*p++ != ':') return false;
        otaSkipWs(p);
        unsigned bit = 0;
        bool ok = false;
        if (!strcmp(key, "build")) {
            bit = F_BUILD;
            ok = !(seen & bit) && otaParseU32(p, &m.build);
        } else if (!strcmp(key, "version")) {
            bit = F_VERSION;
            ok = !(seen & bit) && otaParseString(p, m.version, sizeof m.version);
        } else if (!strcmp(key, "size")) {
            bit = F_SIZE;
            ok = !(seen & bit) && otaParseU32(p, &m.size);
        } else if (!strcmp(key, "sha256")) {
            bit = F_SHA;
            ok = !(seen & bit) && otaParseString(p, m.sha256, sizeof m.sha256);
        } else if (!strcmp(key, "sig")) {
            bit = F_SIG;
            ok = !(seen & bit) && otaParseString(p, sigText, sizeof sigText);
        }
        if (!ok) return false;                      // unknown, duplicate or malformed
        seen |= bit;
        otaSkipWs(p);
        if (*p == ',') { p++; continue; }
        if (*p == '}') { p++; break; }
        return false;
    }
    otaSkipWs(p);
    if (*p != '\0' || seen != F_ALL) return false;

    if (!otaVersionWellFormed(m.version)) return false;
    if (!otaSha256WellFormed(m.sha256)) return false;
    if (!otaBase64Decode(sigText, m.sig, sizeof m.sig, &m.sigLen)) return false;
    if (m.sigLen < 8 || m.sig[0] != 0x30) return false;
    m.valid = true;
    *out = m;
    return true;
}

// The string the signature covers; host/publish-firmware.py builds the same.
// Returns its length, or 0 when it does not fit cap.
inline size_t otaCanonical(char* dst, size_t cap, const OtaManifest& m) {
    const int n = snprintf(dst, cap, "chirp-ota-v1|%lu|%s|%lu|%s",
                           (unsigned long)m.build, m.version,
                           (unsigned long)m.size, m.sha256);
    return (n > 0 && (size_t)n < cap) ? (size_t)n : 0;
}

// Installs only move forward: the manifest's build must beat both the image
// that is running and the highest build ever installed (NVS), so a replayed
// older signed manifest cannot downgrade a device, and a build that rolled
// back is not fetched again. Downgrades go over USB.
inline bool otaBuildIsNewer(uint32_t manifestBuild, uint32_t ownBuild,
                            uint32_t recordedBuild) {
    const uint32_t top = ownBuild > recordedBuild ? ownBuild : recordedBuild;
    return manifestBuild > top;
}

// The manifest whose install attempt failed last (kept in RTC no-init RAM:
// it survives a restart, not a power cycle, so garbage on power-up must read
// as empty). The device skips that image until the published manifest changes.
struct OtaFailMemory {
    uint32_t magic;
    char sha256[65];
};
static const uint32_t OTA_FAIL_MAGIC = 0x4F544146u;   // "OTAF"

inline bool otaFailMemoryValid(const OtaFailMemory* f) {
    return f->magic == OTA_FAIL_MAGIC && otaSha256WellFormed(f->sha256);
}

inline void otaFailRemember(OtaFailMemory* f, const char* sha256) {
    f->magic = OTA_FAIL_MAGIC;
    memcpy(f->sha256, sha256, sizeof f->sha256);
}

inline void otaFailClear(OtaFailMemory* f) {
    memset(f, 0, sizeof *f);
}

inline bool otaFailBlocks(const OtaFailMemory* f, const char* sha256) {
    return otaFailMemoryValid(f) && strcmp(f->sha256, sha256) == 0;
}

// Update.begin(0) means "unknown size" (unbounded write), so a zero size is
// as unusable as one that cannot fit the OTA partition.
inline bool otaSizeFits(uint32_t size, uint32_t otaPartitionSize) {
    return size > 0 && size <= otaPartitionSize;
}

enum OtaVerdict {
    OTA_INSTALL,
    OTA_NOT_NEWER,          // build does not beat the running/recorded build
    OTA_PREVIOUSLY_FAILED,  // this very image failed to install; wait for a new manifest
    OTA_TOO_LARGE           // cannot fit the OTA slot
};

// What to do with a manifest whose signature already verified.
inline OtaVerdict otaDecide(const OtaManifest& m, uint32_t ownBuild,
                            uint32_t recordedBuild, const OtaFailMemory* fail,
                            uint32_t otaPartitionSize) {
    if (!otaBuildIsNewer(m.build, ownBuild, recordedBuild)) return OTA_NOT_NEWER;
    if (otaFailBlocks(fail, m.sha256)) return OTA_PREVIOUSLY_FAILED;
    if (!otaSizeFits(m.size, otaPartitionSize)) return OTA_TOO_LARGE;
    return OTA_INSTALL;
}

// Elapsed-time check that survives millis() wrapping.
inline bool otaTimedOut(uint32_t sinceMs, uint32_t nowMs, uint32_t limitMs) {
    return (uint32_t)(nowMs - sinceMs) > limitMs;
}
