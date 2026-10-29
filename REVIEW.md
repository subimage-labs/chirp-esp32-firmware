# Review: 8da671e (phase 1, venice settings/portal/provisioning)

Reviewer: Opus 5.5 (independent, per repo review rule), 2026-10-16. Static review
of the parent-to-commit diff and consuming paths; no builds run by the reviewer.

1. **Blocker / P1 — Skip agent mutations when merging Venice settings.** Patch anchor: `firmware/include/setup_form.h:289-292`. The Venice branch preserves `cur.hermes`, but execution still reaches the unconditional agent removal/add/re-key logic at lines 320-372. Thus a submission containing `backend=venice`, Hermes host B and B's agent key can save that key under the preserved Hermes host A. Switching back to local A retains it, and `settings.cpp:106-124` / `hermes.cpp:81` send B's key to A. This is reachable through USB pairing (`host/pair.py:473-486`), and through scanning a QR for B then selecting Venice: `portal.cpp:207` excludes the now-enabled pairing inputs from mode disabling. Preserve the roster and active index without processing local-only agent inputs in Venice mode. Add a regression covering a different submitted Hermes host/key while Venice is selected. Confidence: 0.99.

2. **Should-fix / P2 — Preserve the Venice host when its form input is absent.** Patch anchor: `firmware/src/portal.cpp:292-293`. In local mode, the script disables both Venice inputs (lines 209-211), so the browser omits them. `onSave()` converts the absent host to an empty string; `mergeSettings()` treats that as the default host and drops the stored key when the previous host differs (`setup_form.h:299-312`). Switching a custom-host Venice device to local therefore silently erases its backend host/key, as does any subsequent local-mode save. Use the stored host when the argument is absent; retain the explicit-blank-means-default behavior. Test the actual disabled-field submission path, not just fully populated `SettingsInput` values. Confidence: 1.0.

3. **Should-fix / P2 — Make local provisioning and wired pairing select local explicitly.** Patch anchor: `host/provision.py:30-31`; consuming change: `firmware/src/settings.cpp:72-73`. A normal local `device.env` has no backend member; `read_env()` fills it with an empty string (line 58), which the new merge interprets as keeping the stored backend (`setup_form.h:263-264`). On a device already configured for default-host Venice, provisioning a valid local environment reports success but remains in Venice and ignores the requested Hermes host. `host/pair.py:473` likewise omits backend, so USB pairing cannot switch it back, unlike QR pairing's explicit local selection. Default the local provisioning payload to `backend=local` and add that member to the wired pairing payload; keep absent-member preservation for generic partial serial updates. Confidence: 0.99.

4. **Should-fix / P2 — Allow the phase-1 key-only pre-provisioning flow.** Patch anchor: `host/provision.py:50-51`. The phase-1 plan explicitly requires provisioning the backend/key before WiFi is known, then entering the portal for WiFi. The new Venice required-fields list still requires `ssid`; bypassing the tool does not help because `console.cpp:17-22` only saves after `mergeSettings()`, which rejects an empty SSID at `setup_form.h:249`. Consequently this named phase-1 flow cannot persist the key at all. Separate valid incomplete provisioning from ready-to-run settings, while continuing to make `settingsReady()` false until WiFi is configured. Test persistence followed by WiFi completion. Confidence: 0.99.

## Verification and coverage

Static review of the full changed files, parent-to-commit diff, and consuming settings/console/pairing/menu/network paths. No tests, compiler invocations, PlatformIO builds, or flashing were run: this review role restricts shell commands to read-only Git inspection and prohibits builds.

The added tests cover mode selection, default and explicit backend hosts, key retention/change, and empty-agent Venice usability. They do not exercise the portal script/POST boundary, provisioning payloads, or the real `recordFromJson`/`toJson` and endpoint derivation functions. Add tests against production implementations, not copied serializers. The NVS field names match by inspection; missing round-trip coverage alone is not a separate proven bug. No additional C++11-only compilation defect was established by inspection. The IIFE's reused `i` is sequential and `upd` has its own function scope; no variable-collision defect was established.

TLS, model fields, working Venice turns, and history remain outside this phase's review findings.

**Verdict: incorrect; not merge-worthy as phase 1.** Fix the credential-host association defect and the provisioning/form state-loss paths, and implement the documented pre-provisioning flow. Overall confidence: 0.98.

---

## Outcome (coder, after the review)

All four findings verified real and fixed on `venice-mode` (see the follow-up
commit); verdict "not merge-worthy" applied to 8da671e as-is, which is why the
fixes are a separate commit rather than a rewrite of history:

1. Venice merges now skip agent mutations entirely: roster and active index are
   preserved verbatim; submitted agent keys/forget/use are ignored in venice
   mode. Regression: `veniceIgnoresAgentSubmissions`.
2. `onSave` keeps the stored backend host when the disabled input is absent
   (`hasArg` check); explicit blank still means the default host.
3. `provision.py` defaults a backend-less local env to `backend="local"`, and
   pair.py's wired handoff sends it explicitly; absent members still preserve
   for generic partial serial updates.
4. `mergeSettings` allows an empty SSID in venice mode (key-only
   pre-provision) while `settingsUsable` requires an SSID, so such settings
   store but boot into the setup portal until WiFi is completed.
   `provision.py` venice requirements drop to `veniceKey` alone. Regression:
   `venicePreProvision`.

Known remaining gap (accepted for phase 1): the portal script/POST boundary and
the NVS round-trip stay untested at the host layer; the former is covered by
the `hasArg` fix and manual portal use, the latter by bench smoke when phase 2
flashes.
