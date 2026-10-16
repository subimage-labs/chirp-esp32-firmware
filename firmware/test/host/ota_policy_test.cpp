// Host tests for the OTA manifest policy. Run: firmware/test/host/run.sh
//
// The golden vector (canonical string, base64 signature) is shared with
// host/test_publish_firmware.py, which verifies the signature against its
// public key; the signature check on the device is mbedtls and is covered by
// the on-device test plan, not here.
#include "ota_policy.h"

#include <cstdio>
#include <cstring>
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

static const char* const SHA =
    "0123456789abcdef0123456789abcdef" "0123456789abcdef0123456789abcdef";
static const char* const SIG =
    "MEYCIQCLQTVXigzgyKv3XQTYdo5M4ignlngBCwSlMDZGc8FBiAIhAMsDA2kR0x6TxO01"
    "2StueohV4wra/W4uNX/yOMXwYPBB";
static const char* const GOLDEN_CANON =
    "chirp-ota-v1|1700000000|v0.9-3-gabc1234|1234567|"
    "0123456789abcdef0123456789abcdef" "0123456789abcdef0123456789abcdef";

// A manifest body; pass nullptr to drop a member.
static std::string body(const char* build, const char* version, const char* size,
                        const char* sha, const char* sig) {
    std::string s = "{";
    auto add = [&](const char* k, const char* v) {
        if (!v) return;
        if (s.size() > 1) s += ",";
        s += std::string("\"") + k + "\":" + v;
    };
    add("build", build);
    add("version", version ? (std::string("\"") + version + "\"").c_str() : nullptr);
    add("size", size);
    add("sha256", sha ? (std::string("\"") + sha + "\"").c_str() : nullptr);
    add("sig", sig ? (std::string("\"") + sig + "\"").c_str() : nullptr);
    return s + "}";
}

static bool parses(const std::string& json) {
    OtaManifest m;
    return otaManifestParse(json.c_str(), &m) && m.valid;
}

static void goodManifests() {
    OtaManifest m;
    // Exactly what host/publish-firmware.py writes (compact separators).
    const std::string j = body("1700000000", "v0.9-3-gabc1234", "1234567", SHA, SIG);
    CHECK(otaManifestParse(j.c_str(), &m));
    CHECK(m.valid);
    CHECK(m.build == 1700000000u);
    CHECK(std::strcmp(m.version, "v0.9-3-gabc1234") == 0);
    CHECK(m.size == 1234567u);
    CHECK(std::strcmp(m.sha256, SHA) == 0);
    CHECK(m.sigLen == 72);
    CHECK(m.sig[0] == 0x30 && m.sig[1] == 0x46 && m.sig[2] == 0x02 &&
          m.sig[3] == 0x21 && m.sig[71] == 0x41);

    // Whitespace anywhere between tokens, members in any order.
    const std::string spaced =
        std::string("\n{ \"sig\" : \"") + SIG + "\" ,\n \"sha256\":\"" + SHA +
        "\", \"size\": 7 ,\"version\" :\"scroll-stable\",\"build\":1 }\n";
    CHECK(parses(spaced));
    // A bare sha (git describe --always, outside tags); u32 extremes.
    CHECK(parses(body("4294967295", "c6d9110", "4294967295", SHA, SIG)));
    CHECK(parses(body("1", "v1", "1", SHA, SIG)));
    // The compact manifest fits the device's read buffer with room to spare.
    CHECK(j.size() + 1 < OTA_MANIFEST_MAX / 2);
}

static void badManifests() {
    OtaManifest m;
    CHECK(otaManifestParse(nullptr, &m) == false);
    CHECK(otaManifestParse("", &m) == false);
    CHECK(otaManifestParse("{}", &m) == false);
    CHECK(otaManifestParse("[]", &m) == false);

    // Each member is required.
    CHECK(!parses(body(nullptr, "v1", "1", SHA, SIG)));
    CHECK(!parses(body("1", nullptr, "1", SHA, SIG)));
    CHECK(!parses(body("1", "v1", nullptr, SHA, SIG)));
    CHECK(!parses(body("1", "v1", "1", nullptr, SIG)));
    CHECK(!parses(body("1", "v1", "1", SHA, nullptr)));

    // Unknown members, duplicates, trailing junk.
    const std::string ok = body("1", "v1", "1", SHA, SIG);
    CHECK(parses(ok));
    CHECK(!parses(ok.substr(0, ok.size() - 1) + ",\"extra\":1}"));
    CHECK(!parses(ok.substr(0, ok.size() - 1) + ",\"build\":2}"));
    CHECK(!parses(ok.substr(0, ok.size() - 1) + ",\"sig\":\"" + SIG + "\"}"));
    CHECK(!parses(ok + "x"));
    CHECK(!parses(ok + "{}"));
    CHECK(!parses(ok.substr(0, ok.size() - 1)));                  // unclosed
    CHECK(!parses(ok.substr(0, ok.size() - 1) + ",}"));           // trailing comma
    CHECK(!parses("{\"build\":1 \"version\":\"v1\"}"));           // missing comma
    // build / size: text, negative, trailing junk, leading zero, overflow.
    CHECK(!parses(body("\"1\"", "v1", "1", SHA, SIG)));
    CHECK(!parses(body("-2", "v1", "1", SHA, SIG)));
    CHECK(!parses(body("12x", "v1", "1", SHA, SIG)));
    CHECK(!parses(body("01", "v1", "1", SHA, SIG)));
    CHECK(!parses(body("1.5", "v1", "1", SHA, SIG)));
    CHECK(!parses(body("1e3", "v1", "1", SHA, SIG)));
    CHECK(!parses(body("4294967296", "v1", "1", SHA, SIG)));
    CHECK(!parses(body("1", "v1", "\"1\"", SHA, SIG)));
    CHECK(!parses(body("1", "v1", "-2", SHA, SIG)));
    CHECK(!parses(body("1", "v1", "4294967296", SHA, SIG)));

    // sha256: wrong length, non-hex, uppercase (the signed text is lowercase).
    CHECK(!parses(body("1", "v1", "1", "abc", SIG)));
    CHECK(!parses(body("1", "v1", "1",
                       "zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz" "zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz",
                       SIG)));
    CHECK(!parses(body("1", "v1", "1",
                       "0123456789ABCDEF0123456789ABCDEF" "0123456789ABCDEF0123456789ABCDEF",
                       SIG)));
    CHECK(!parses(body("1", "v1", "1", (std::string(SHA) + "0").c_str(), SIG)));

    // version: empty, escaped, characters git describe never emits.
    CHECK(!parses(body("1", "", "1", SHA, SIG)));
    CHECK(!parses(body("1", "v 1", "1", SHA, SIG)));
    CHECK(!parses(body("1", "a\\\"b", "1", SHA, SIG)));
    CHECK(!parses(body("1", "v|1", "1", SHA, SIG)));     // '|' would split the canonical string
    // version: 47 chars fits (version[48] - NUL), 48 does not.
    CHECK(parses(body("1", std::string(47, 'a').c_str(), "1", SHA, SIG)));
    CHECK(!parses(body("1", std::string(48, 'a').c_str(), "1", SHA, SIG)));

    // sig: not base64, bad padding, wrong DER lead, too short, too long.
    CHECK(!parses(body("1", "v1", "1", SHA, "not base64!")));
    CHECK(!parses(body("1", "v1", "1", SHA, "")));
    CHECK(!parses(body("1", "v1", "1", SHA, "AAAA")));                    // 3 bytes
    CHECK(!parses(body("1", "v1", "1", SHA, "MEYCIQ")));                  // unpadded
    CHECK(!parses(body("1", "v1", "1", SHA, "MEYC=QCL")));                // padding mid-string
    CHECK(!parses(body("1", "v1", "1", SHA,
                       ("AAAA" + std::string(SIG).substr(4)).c_str())));  // not a SEQUENCE
    CHECK(!parses(body("1", "v1", "1", SHA, (std::string(SIG) + "AAAA").c_str())));  // 75 bytes
    CHECK(!parses(body("1", "v1", "1", SHA,
                       std::string(OTA_SIG_B64_MAX + 4, 'A').c_str())));

    // Over the device's read cap.
    std::string big = body("1", "v1", "1", SHA, SIG);
    big.insert(big.size() - 1, std::string(OTA_MANIFEST_MAX, ' '));   // valid JSON, too long
    CHECK(!parses(big));

    // A failed parse leaves nothing behind.
    OtaManifest z;
    CHECK(!otaManifestParse(body("1", "v 1", "1", SHA, SIG).c_str(), &z));
    CHECK(z.valid == false && z.build == 0 && z.size == 0 && z.sigLen == 0 &&
          z.version[0] == '\0');
}

static void base64() {
    uint8_t out[8];
    size_t n = 0;
    CHECK(otaBase64Decode("TWFu", out, sizeof out, &n) && n == 3 && !std::memcmp(out, "Man", 3));
    CHECK(otaBase64Decode("TWE=", out, sizeof out, &n) && n == 2 && !std::memcmp(out, "Ma", 2));
    CHECK(otaBase64Decode("TQ==", out, sizeof out, &n) && n == 1 && out[0] == 'M');
    CHECK(otaBase64Decode("+/+/", out, sizeof out, &n) && n == 3 && out[0] == 0xFB &&
          out[1] == 0xFF && out[2] == 0xBF);
    CHECK(!otaBase64Decode("", out, sizeof out, &n));
    CHECK(!otaBase64Decode("TWF", out, sizeof out, &n));
    CHECK(!otaBase64Decode("T===", out, sizeof out, &n));
    CHECK(!otaBase64Decode("TQ=A", out, sizeof out, &n));
    CHECK(!otaBase64Decode("TQ==TWFu", out, sizeof out, &n));   // padding not last
    CHECK(!otaBase64Decode("TWFu\n", out, sizeof out, &n));
    CHECK(!otaBase64Decode("TWF-", out, sizeof out, &n));
    CHECK(!otaBase64Decode("TWFuTWFu", out, 5, &n));            // output cap
}

static void canonical() {
    OtaManifest m;
    CHECK(otaManifestParse(body("1700000000", "v0.9-3-gabc1234", "1234567", SHA, SIG).c_str(), &m));
    char buf[OTA_CANON_MAX];
    const size_t n = otaCanonical(buf, sizeof buf, m);
    CHECK(n == std::strlen(GOLDEN_CANON));
    CHECK(std::strcmp(buf, GOLDEN_CANON) == 0);
    char tiny[8];
    CHECK(otaCanonical(tiny, sizeof tiny, m) == 0);              // does not fit
    // Largest fields still fit the buffer.
    OtaManifest big;
    CHECK(otaManifestParse(body("4294967295", std::string(47, 'v').c_str(),
                                "4294967295", SHA, SIG).c_str(), &big));
    CHECK(otaCanonical(buf, sizeof buf, big) > 0);
    // Changing any signed field changes the string.
    OtaManifest a = m, b = m;
    a.build++;
    b.size++;
    char x[OTA_CANON_MAX], y[OTA_CANON_MAX];
    otaCanonical(x, sizeof x, a);
    otaCanonical(y, sizeof y, b);
    CHECK(std::strcmp(x, GOLDEN_CANON) != 0 && std::strcmp(y, GOLDEN_CANON) != 0 &&
          std::strcmp(x, y) != 0);
}

static void monotonic() {
    CHECK(otaBuildIsNewer(101, 100, 0));
    CHECK(!otaBuildIsNewer(100, 100, 0));       // same build
    CHECK(!otaBuildIsNewer(99, 100, 0));        // older than the running image
    CHECK(!otaBuildIsNewer(100, 50, 100));      // recorded by an earlier install
    CHECK(!otaBuildIsNewer(80, 50, 100));       // replayed older manifest after a rollback
    CHECK(otaBuildIsNewer(101, 50, 100));
    CHECK(otaBuildIsNewer(1, 0, 0));
    CHECK(!otaBuildIsNewer(0, 0, 0));
}

static void failMemory() {
    OtaFailMemory f;
    std::memset(&f, 0xA5, sizeof f);             // power-up garbage
    CHECK(!otaFailMemoryValid(&f));
    CHECK(!otaFailBlocks(&f, SHA));
    otaFailClear(&f);
    CHECK(!otaFailBlocks(&f, SHA));
    otaFailRemember(&f, SHA);
    CHECK(otaFailMemoryValid(&f));
    CHECK(otaFailBlocks(&f, SHA));
    std::string other(SHA);
    other[63] = other[63] == 'f' ? 'e' : 'f';
    CHECK(!otaFailBlocks(&f, other.c_str()));    // a new manifest is tried again
    f.sha256[10] = 'Z';                          // corrupted RTC contents
    CHECK(!otaFailMemoryValid(&f));
    CHECK(!otaFailBlocks(&f, SHA));
    otaFailRemember(&f, SHA);
    f.magic ^= 1;
    CHECK(!otaFailBlocks(&f, SHA));
}

static void decisions() {
    OtaManifest m;
    CHECK(otaManifestParse(body("200", "v2", "1000", SHA, SIG).c_str(), &m));
    OtaFailMemory none;
    otaFailClear(&none);
    CHECK(otaDecide(m, 100, 0, &none, 0x640000) == OTA_INSTALL);
    CHECK(otaDecide(m, 200, 0, &none, 0x640000) == OTA_NOT_NEWER);
    CHECK(otaDecide(m, 100, 250, &none, 0x640000) == OTA_NOT_NEWER);
    OtaFailMemory failed;
    otaFailRemember(&failed, SHA);
    CHECK(otaDecide(m, 100, 0, &failed, 0x640000) == OTA_PREVIOUSLY_FAILED);
    CHECK(otaDecide(m, 100, 0, &none, 999) == OTA_TOO_LARGE);
    CHECK(otaDecide(m, 300, 0, &failed, 999) == OTA_NOT_NEWER);   // not newer wins

    CHECK(otaSizeFits(1, 0x640000) == true);
    CHECK(otaSizeFits(0x640000, 0x640000) == true);
    CHECK(otaSizeFits(0, 0x640000) == false);       // 0 = unbounded write
    CHECK(otaSizeFits(0x640001, 0x640000) == false);
}

static void deadlines() {
    CHECK(!otaTimedOut(1000, 1000, OTA_TOTAL_MS));
    CHECK(!otaTimedOut(1000, 1000 + OTA_TOTAL_MS, OTA_TOTAL_MS));
    CHECK(otaTimedOut(1000, 1001 + OTA_TOTAL_MS, OTA_TOTAL_MS));
    // millis() wrapping at 2^32.
    CHECK(!otaTimedOut(0xFFFFFF00u, 0x00000100u, OTA_TOTAL_MS));
    CHECK(otaTimedOut(0xFFFFFF00u, 0x0001FFFFu, OTA_TOTAL_MS));
}

int main() {
    goodManifests();
    badManifests();
    base64();
    canonical();
    monotonic();
    failMemory();
    decisions();
    deadlines();
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("ota_policy: all checks passed\n");
    return 0;
}
