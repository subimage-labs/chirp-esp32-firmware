# Security policy

## Report a problem in private

Use a private GitHub security advisory:
<https://github.com/subimage-labs/chirp-esp32-firmware/security/advisories/new>

Do not open a public issue for a security problem. Do not post keys,
WiFi passwords or serial logs that show them. If you need to show a log, remove
every secret first.

Please tell us:

- the board (T-Embed CC1101 Plus or T-Deck) and the firmware version (the
  `[OTA] running <version>` line in the serial log)
- the route (Venice or Hermes)
- the steps that show the problem, and what you expect to happen

We read every report. We will tell you if we accept it, and we will agree a
date with you before we publish a fix.

## What is in scope

- The firmware in `firmware/`: the setup portal, the audio path, the settings
  store, the WiFi update (OTA) check.
- The speech server and tools in `host/`: `audio_shim.py`, `pair.py`,
  `provision.py`, `publish-firmware.py`, `check-secrets.sh`.
- The browser installer in `flasher/`, the project web site in `site/`, and the
  release workflows in `.github/workflows/`. Release images carry a SHA-256
  list (`SHA256SUMS`) and a build provenance attestation.
- A secret that is in the repository, in a release image or in a release
  asset.

## What is not in scope

- Problems in Venice, Hermes Agent, ESP Web Tools or another upstream project.
  Report those to the project. Tell us if chirp uses them in a risky way.
- A person who holds the device and a USB cable. The settings partition is
  plain flash, and this is documented.
- The Hermes route that sends the agent key over plain HTTP on your LAN. This is
  documented, and the fix is on the list of known limits.
- Bench builds (`BENCH_CMDS`). They are for developers and accept `http://`.

## What chirp already does

[docs/security.md](docs/security.md) has the full list. In short:

- No secret is in the firmware or in git. Settings live in the device (NVS).
  A commit hook and a CI scan block leaks.
- The setup network uses WPA2 with a new password each time, and the setup
  form needs a token and refuses a foreign `Origin`.
- The Venice route uses HTTPS with a pinned root certificate.
- The Hermes route uses plain HTTP on your LAN. Firewall the gateway port.
- WiFi updates install only a manifest that you signed (ECDSA P-256). The
  device checks the signature, the build number and the SHA-256 of the image.
