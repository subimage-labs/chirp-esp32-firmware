#include "endpoint_urls.h"
#include <vector>

#include <cstdio>
static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { \
    std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
    ++failures; } } while (0)

static std::vector<std::string> urls(const EndpointUrls& u) {
    return {u.chatUrl, u.sessionsUrl, u.sttUrl, u.ttsUrl, u.shimHealthUrl,
            u.otaManifestUrl, u.otaBinUrl};
}
static void absent(const EndpointUrls& u, const std::string& needle) {
    auto fields = urls(u);
    fields.push_back(u.shimHost);
    fields.push_back(u.auth);
    for (const auto& f : fields) CHECK(f.find(needle) == std::string::npos);
}
static DeviceSettings settings() {
    DeviceSettings s;
    s.hermes = "gw:8642";
    s.shim = "";
    s.agents = {Agent{"alpha", "ACTIVE-A"}, Agent{"beta", "ACTIVE-B"}};
    s.active = 0;
    s.venice = "evil.example:443";
    s.veniceKey = "VENICE-ONLY";
    return s;
}
int main() {
    auto s = settings();
    s.agents = {Agent{"alpha", "AK"}};
    auto u = deriveEndpointUrls(s);
    CHECK(u.chatUrl == "http://gw:8642/p/alpha/v1/chat/completions");
    CHECK(u.sessionsUrl == "http://gw:8642/p/alpha/api/sessions");
    CHECK(u.sttUrl == "http://gw:8643/v1/audio/transcriptions");
    CHECK(u.ttsUrl == "http://gw:8643/v1/audio/speech");
    CHECK(u.shimHealthUrl == "http://gw:8643/health");
    CHECK(u.otaManifestUrl == "http://gw:8643/firmware/manifest.json");
    CHECK(u.otaBinUrl == "http://gw:8643/firmware/chirp.bin");
    CHECK(u.shimHost == "gw" && u.shimPort == 8643 && u.auth == "Bearer AK");
    s.agents[0].profile = "";
    u = deriveEndpointUrls(s);
    CHECK(u.chatUrl == "http://gw:8642/v1/chat/completions");
    CHECK(u.sessionsUrl == "http://gw:8642/api/sessions");

    struct Host { const char* input; const char* base; };
    for (const char* mode : {"local", "", "Venice", "VENICE"})
    for (const Host hermes : {Host{"gw", "http://gw:8642"},
         Host{"gw:9000", "http://gw:9000"}, Host{"http://gw:9000", "http://gw:9000"},
         Host{"https://gw:9000", "http://gw:9000"}})
    for (const Host shim : {Host{"box", "http://box:8643"},
         Host{"box:9000", "http://box:9000"}, Host{"https://box:9000", "http://box:9000"},
         Host{"", "http://gw:8643"}, Host{"bad host", "http://gw:8643"}})
    for (int active : {0, 1}) {
        s = settings(); s.backend = mode; s.hermes = hermes.input;
        s.shim = shim.input; s.active = active;
        u = deriveEndpointUrls(s);
        const std::string base = std::string(hermes.base) + (active ? "/p/beta" : "/p/alpha");
        CHECK(u.chatUrl == base + "/v1/chat/completions");
        CHECK(u.sessionsUrl == base + "/api/sessions");
        CHECK(u.sttUrl == std::string(shim.base) + "/v1/audio/transcriptions");
        CHECK(u.ttsUrl == std::string(shim.base) + "/v1/audio/speech");
        CHECK(u.shimHealthUrl == std::string(shim.base) + "/health");
        CHECK(u.otaManifestUrl == std::string(shim.base) + "/firmware/manifest.json");
        CHECK(u.otaBinUrl == std::string(shim.base) + "/firmware/chirp.bin");
        CHECK("http://" + u.shimHost + ":" + std::to_string(u.shimPort) == shim.base);
        CHECK(u.auth == (active ? "Bearer ACTIVE-B" : "Bearer ACTIVE-A"));
        absent(u, active ? "ACTIVE-A" : "ACTIVE-B");
        absent(u, "evil.example"); absent(u, "VENICE-ONLY");
        for (const auto& url : urls(u)) CHECK(url.find("http://") == 0);
    }
    struct Venice { const char* input; const char* base; const char* host; unsigned port; };
    for (const Venice v : {
        Venice{"https://api.venice.ai:443", "https://api.venice.ai:443", "api.venice.ai", 443},
        Venice{"api.venice.ai", "https://api.venice.ai:443", "api.venice.ai", 443},
        Venice{"host:8443", "https://host:8443", "host", 8443},
        Venice{"http://198.51.100.7:9000", "http://198.51.100.7:9000", "198.51.100.7", 9000}})
    for (const char* key : {"VK", ""})
    for (int count : {0, 4}) {
        s = settings(); s.backend = "venice"; s.venice = v.input; s.veniceKey = key;
        s.hermes = "hermes-only"; s.shim = "shim-only"; s.agents.clear();
        for (int i = 0; i < count; ++i)
            s.agents.push_back(Agent{"profile-only" + std::to_string(i), "agent-only" + std::to_string(i)});
        s.active = count ? 3 : 0;
        u = deriveEndpointUrls(s);
        CHECK(u.chatUrl == std::string(v.base) + "/api/v1/chat/completions");
        CHECK(u.sttUrl == std::string(v.base) + "/api/v1/audio/transcriptions");
        CHECK(u.ttsUrl == std::string(v.base) + "/api/v1/audio/speech");
        CHECK(u.shimHealthUrl == std::string(v.base) + "/api/v1/models");
        CHECK(u.sessionsUrl.empty() && u.otaManifestUrl.empty() && u.otaBinUrl.empty());
        CHECK(u.shimHost == v.host && u.shimPort == v.port);
        CHECK(u.auth == std::string("Bearer ") + key);
        absent(u, "hermes-only"); absent(u, "shim-only");
        for (int i = 0; i < count; ++i) {
            absent(u, "profile-only" + std::to_string(i));
            absent(u, "agent-only" + std::to_string(i));
        }
    }
    return failures ? 1 : 0;
}
