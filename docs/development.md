# Development

Contributors should also read [AGENTS.md](../AGENTS.md).

## Build and test

Firmware is PlatformIO (`espressif32@6.13.0`):

```sh
pio run -d firmware -e t-deck                                      # build (or -e t-embed-cc1101)
pio run -d firmware -e t-deck -t upload --upload-port /dev/ttyACM1 # flash (always name the board)
firmware/test/host/run.sh                                          # host tests
```

Settings live in the ESP32's NVS partition: uploads keep them; a full flash
erase or a partition-table change clears them. CI (`.github/workflows/ci.yml`)
runs the host tests, a firmware build and `py_compile` over `host/*.py` on
every push and pull request.

The speech server on its own: `host/setup.sh --install-service` (venv, pinned
deps, models, secret hook, systemd unit), then
`curl http://localhost:8643/health`. `host/test_audio_shim.py` checks its
request limits offline.

## Releases

Pushing a `v*` tag runs `.github/workflows/firmware-release.yml`: it builds the
firmware (both boards), attaches the four **T-Embed** flash images to the
GitHub Release, and publishes a `flasher` artifact (T-Embed installer page +
images) for any HTTPS static host; the T-Deck is flashed with PlatformIO.
Locally, `python -m http.server` inside the unpacked artifact works too (Web
Serial needs HTTPS or localhost).

## Repository layout

| Path | What |
|---|---|
| `firmware/` | Device firmware, one module per concern (map and execution contexts: `firmware/include/app.h`); pure logic host-tested by `firmware/test/host/run.sh` |
| `firmware/tools/make_font.py` | Generates the device fonts (`firmware/src/fonts/font_data.cpp`): four families x S/M/L from pinned upstream sources (`font-sources.txt`, SHA-256 checked), each under the SIL OFL 1.1 with its license beside the data; needs Pillow + fontTools |
| `firmware/tools/avatar_preview.py` | Draws contact sheets (one per part vocabulary) and per-state GIFs of the characters with the firmware's own `avatar.h` (compiles `firmware/test/host/avatar_preview.cpp`; needs g++ and Pillow) |
| `firmware/tools/logo_preview.py` | Draws the chirp! logo on all twelve theme backgrounds from `logo.h` (needs g++ and Pillow); `--art FILE` draws a design from a text file of art rows, `--compare` lays the shipped logo and each `--art` side by side. Alternative designs live in `firmware/tools/logo_options/` |
| `firmware/tools/logo_web.py` | Bakes the logo into `firmware/include/logo_web.h` (a 4-bit PNG for the setup page; reproducible, `--check` verifies it). `logo.h` stays the one source of the art |
| `flasher/` | Browser installer page and its ESP Web Tools manifest (T-Embed images only) |
| `host/pair.py` | Pairs a device with the Hermes gateway on this machine |
| `host/audio_shim.py` | STT + TTS service (systemd user unit `hermes-audio-shim`) |
| `host/local_tts.py` | Paradee TTS: resident worker (`--serve`) or one-shot CLI |
| `host/setup.sh` | Speech server bootstrap |
| `host/provision.py` | Device settings over USB serial from `firmware/device.env` |
| `host/publish-firmware.py` | Signs and publishes a build for the devices' WiFi update (`--init-key` once); see [Updating](updating.md) |

The character vocabularies (heads, hats, palettes, …) only ever grow at the
end, so an existing index keeps its meaning: persona looks and kept looks stay
valid.
