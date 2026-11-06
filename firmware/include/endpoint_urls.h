// endpoint_urls.h — the URLs and Authorization value the device talks to,
// derived from the stored settings. Pure (no Arduino types), host-tested by
// endpoint_urls_test.cpp.
//
// Local mode: chat + sessions go to the Hermes gateway (the active agent's
// profile path), speech / health / OTA to the workstation shim, all plain
// HTTP. Venice mode: every URL is under the configured backend host's
// /api/v1 (https unless http:// was spelled out); nothing from hermes, shim
// or the agents is read, and sessions and OTA are off.
//
// Ownership: a pure function; settings.cpp calls it once per load and keeps
// the result (Endpoints, settings.h) in the loop context.
#pragma once

#include <stdint.h>

#include <string>

#include "setup_form.h"

struct EndpointUrls {
    std::string chatUrl, sessionsUrl, sttUrl, ttsUrl, shimHealthUrl,
                otaManifestUrl, otaBinUrl, shimHost, auth;
    uint16_t shimPort = 0;
};

// Precondition: settingsUsable(s). In local mode that guarantees
// s.agents[s.active] exists and hermes/shim parse; in venice mode that
// s.venice parses.
inline EndpointUrls deriveEndpointUrls(const DeviceSettings& s) {
    EndpointUrls u;
    if (s.backend == "venice") {
        HostPort vp{"", 0, Scheme::Https};
        parseHostPort(s.venice, VENICE_PORT_DEFAULT, vp);
        const std::string base = hostSchemeString(vp) + "/api/v1";
        u.chatUrl = base + "/chat/completions";
        u.sttUrl = base + "/audio/transcriptions";
        u.ttsUrl = base + "/audio/speech";
        u.shimHealthUrl = base + "/models";               // the self-test probe
        // sessionsUrl: no server sessions; otaManifestUrl / otaBinUrl: no
        // shim, USB-only updates (all stay empty).
        u.shimHost = vp.host;
        u.shimPort = vp.port;
        u.auth = "Bearer " + s.veniceKey;
        return u;
    }
    const Agent& agent = s.agents[s.active];
    HostPort hermes{"", 0, Scheme::Bare}, shim{"", 0, Scheme::Bare};
    parseHostPort(s.hermes, HERMES_PORT_DEFAULT, hermes);
    if (s.shim.empty() || !parseHostPort(s.shim, SHIM_PORT_DEFAULT, shim))
        shim = HostPort{hermes.host, SHIM_PORT_DEFAULT, Scheme::Bare};
    std::string hermesBase = "http://" + hermes.host + ":" + std::to_string(hermes.port);
    if (!agent.profile.empty()) hermesBase += "/p/" + agent.profile;
    const std::string shimBase = "http://" + shim.host + ":" + std::to_string(shim.port);
    u.chatUrl = hermesBase + "/v1/chat/completions";
    u.sessionsUrl = hermesBase + "/api/sessions";
    u.sttUrl = shimBase + "/v1/audio/transcriptions";
    u.ttsUrl = shimBase + "/v1/audio/speech";
    u.shimHealthUrl = shimBase + "/health";
    u.otaManifestUrl = shimBase + "/firmware/manifest.json";
    u.otaBinUrl = shimBase + "/firmware/chirp.bin";
    u.shimHost = shim.host;
    u.shimPort = shim.port;
    u.auth = "Bearer " + agent.key;
    return u;
}
