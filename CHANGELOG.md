# Changelog

## Unreleased

chirp is a pocket voice companion for the LilyGO T-Embed CC1101 Plus and the
LilyGO T-Deck. This section describes what the project does now.

### Device

- Push-to-talk voice: hold the talk key, ask, release. The answer is spoken
  and shown as text. Hands-free mode is off until you turn it on with a
  double-tap of the talk key.
- The Chirper, a pixel character drawn from the name of the agent, shows
  listening, thinking and speaking. 64 characters, six themes in dark and light,
  four fonts in three sizes.
- Setup from a phone through the device's own WiFi network and web page. Up to
  5 WiFi networks.
- The T-Deck also takes typed questions.

### Backends

- Venice: chat, speech to text and voice from Venice over HTTPS with a pinned
  root certificate.
- Hermes: your own Hermes Agent profile. Speech to text and voice run on your
  computer through the chirp speech server (`host/`). Chats can be resumed from
  the **Chats** tab.

### Install and update

- Browser installer for the T-Embed (ESP Web Tools). The manifest lists the
  four flash images separately, so an update keeps the device settings.
- The T-Deck is flashed over USB-JTAG with OpenOCD.
- Release images ship with a `SHA256SUMS` file and a build provenance
  attestation. A release build starts only when CI passed on the tagged commit
  and the tag matches the firmware version.
- Hermes-route devices can pull a signed build from the speech server at boot.

### Safety

- No secret is in the firmware or in git. A commit hook, a CI scan of the tree
  and a scan of every release image block leaks.
- The security notes are in [docs/security.md](docs/security.md).

### License

- GPL-3.0-only.
