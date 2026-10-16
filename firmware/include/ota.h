// ota.h — boot-time firmware update from the audio shim.
//
// Owner: loop task, during setup() only — never mid-use, never from another
// context. The bootloader is built with CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE:
// a freshly streamed image boots as pending-verify and the next reset rolls
// back to the previous image unless the app marks itself valid. Arduino's
// own early marking (before setup()) is switched off in ota.cpp, so an image
// that crashes during startup or never joins WiFi rolls back.
//
// Trust: the shim is not trusted. Every manifest is signed (ECDSA P-256 over
// SHA-256) by the key whose public half firmware/ota_build.py embeds from
// firmware/ota_pub.pem; an image built without that file never updates over
// WiFi. Installs only move forward by build number (see ota_policy.h).
#pragma once

// The image works: setup() got this far and joined WiFi. Cancels rollback.
// Hermes or the speech server being down is not the image's fault and does
// not count against it.
void otaMarkHealthy();

// FW_VERSION of the running image. Any context.
const char* otaVersion();

// After a passing self-test: logs the running version and build ("[OTA]
// running <FW_VERSION> build <FW_BUILD>"), then — if this build embeds a
// signing key — fetches the shim's firmware manifest (at most 512 bytes) and
// checks, in order: strict parse, signature, build newer than both the
// running image and the highest build recorded in NVS "chirp"/"otaBuild",
// not the image whose install failed last, fits the OTA slot. Then it
// streams the image into the inactive slot within a 120 s total deadline,
// checks its sha256 against the signed manifest, activates it, records the
// build and restarts. Any failure logs "[OTA] failed: <reason>" and returns:
// the running firmware keeps booting and USB remains the recovery path (and
// the only way to downgrade).
void otaBootCheck();
