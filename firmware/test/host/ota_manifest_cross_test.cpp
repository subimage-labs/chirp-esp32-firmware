#include "ota_policy.h"
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>

#include <cstdio>
static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { \
    std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
    ++failures; } } while (0)

static std::string read(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read fixture: " + path);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}
int main(int argc, char** argv) {
    if (argc != 2) { std::fprintf(stderr, "usage: ota_manifest_cross_test OUTDIR\n"); return 2; }
    try {
        const std::string dir = std::string(argv[1]) + "/";
        std::map<std::string, std::string> expect;
        std::ifstream facts(dir + "expect.txt");
        if (!facts) throw std::runtime_error("missing expect.txt");
        std::string line;
        while (std::getline(facts, line)) {
            auto pos = line.find('=');
            if (pos == std::string::npos) throw std::runtime_error("invalid expectation line");
            CHECK(expect.emplace(line.substr(0, pos), line.substr(pos + 1)).second);
        }
        OtaManifest m{};
        CHECK(OTA_MANIFEST_MAX == std::stoul(expect.at("manifest_max")));
        CHECK(sizeof m.version - 1 == std::stoul(expect.at("version_max")));
        for (const std::string stem : {"good", "max", "dirty"}) {
            const auto body = read(dir + stem + ".json");
            auto fact = [&](const char* field) -> const std::string& { return expect.at(stem + "." + field); };
            const bool parsed = otaManifestParse(body.c_str(), &m);
            CHECK(parsed && m.valid);
            if (!parsed) continue;
            CHECK(m.build == std::stoul(fact("build")) && m.size == std::stoul(fact("size")));
            CHECK(std::string(m.version) == fact("version") && std::string(m.sha256) == fact("sha256"));
            const auto hex = fact("sig_hex");
            CHECK(hex.size() % 2 == 0 && m.sigLen == hex.size() / 2);
            CHECK(m.sigLen > 0 && m.sigLen <= OTA_SIG_MAX && m.sig[0] == 0x30);
            if (m.sigLen <= OTA_SIG_MAX && m.sigLen == hex.size() / 2)
                for (size_t i = 0; i < m.sigLen; ++i)
                    CHECK(m.sig[i] == std::stoul(hex.substr(i * 2, 2), nullptr, 16));
            CHECK(fact("sig_b64").size() <= OTA_SIG_B64_MAX);
            CHECK(body.size() == std::stoul(fact("bytes")) && body.size() <= OTA_MANIFEST_MAX);
            char buf[OTA_CANON_MAX] = {};
            CHECK(otaCanonical(buf, sizeof buf, m) == fact("canonical").size());
            CHECK(std::string(buf) == fact("canonical"));
            if (stem == "max") {
                CHECK(std::strlen(m.version) == 47 && m.build == 4294967295u);
                CHECK(fact("canonical").size() < OTA_CANON_MAX);
            }
        }
        CHECK(!otaManifestParse(read(dir + "v48.json").c_str(), &m) && !m.valid);
        const auto fit = read(dir + "pad512.json"), over = read(dir + "pad513.json");
        CHECK(fit.size() == 512 && over.size() == 513);
        CHECK(otaManifestParse(fit.c_str(), &m) && m.valid);
        CHECK(!otaManifestParse(over.c_str(), &m) && !m.valid);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what()); return 1;
    }
    return failures ? 1 : 0;
}
