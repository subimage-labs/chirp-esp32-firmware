# Development

Contributors must also read [AGENTS.md](../AGENTS.md).

## Build and test

The firmware uses PlatformIO (`espressif32@6.13.0`). Use these commands:

```sh
pio run -d firmware -e t-deck                                      # build (or -e t-embed-cc1101)
pio run -d firmware -e t-deck -t upload --upload-port /dev/ttyACM1 # flash (always name the board)
firmware/test/host/run.sh                                          # host tests
```

The settings live in the NVS partition of the ESP32. An upload keeps them. A full flash erase or a change to the partition table clears them.

On every push and pull request, CI (`.github/workflows/ci.yml`) runs these jobs:

- the host tests
- a firmware build
- `py_compile` over `host/*.py`

Developer bench builds (`BENCH_CMDS`) can use `http://` for a local test server, for example `host/venice_mock.py`. Release builds refuse `http://` for the Venice host (see [Security notes](security.md)).

To run the speech server alone, do these steps:

1. Run `host/setup.sh --install-service`. It makes the venv, installs the pinned dependencies and models, adds the secret hook and installs the systemd unit.
2. Run `curl http://localhost:8643/health`.

`host/test_audio_shim.py` checks the request limits of the speech server offline.

## Releases

A push of a `v*` tag runs `.github/workflows/firmware-release.yml`. The workflow does these things:

- It builds the firmware for both boards.
- It attaches the four **T-Embed** flash images to the GitHub Release.
- It publishes a `flasher` artifact. The artifact holds the T-Embed installer page and the images. Any HTTPS static host can serve it.

The workflow does not publish T-Deck images. Flash the T-Deck with PlatformIO.

To serve the artifact locally, unpack it and run `python -m http.server` inside it. Web Serial needs HTTPS or localhost.

## Repository layout

| Path | Content |
|---|---|
| `firmware/` | Device firmware, one module per concern. The map and the execution contexts are in `firmware/include/app.h`. `firmware/test/host/run.sh` tests the pure logic on the host. |
| `firmware/tools/make_font.py` | Makes the device fonts (`firmware/src/fonts/font_data.cpp`): four families in sizes S, M and L. The sources are pinned upstream files (`font-sources.txt`, SHA-256 checked). Each font has the SIL OFL 1.1 license beside the data. Needs Pillow and fontTools. |
| `firmware/tools/avatar_preview.py` | Draws contact sheets (one per part vocabulary) and one GIF per state of the characters. It uses `avatar.h` of the firmware and compiles `firmware/test/host/avatar_preview.cpp`. Needs g++ and Pillow. The README's `docs/media/chirper-*.gif` are its `nova` GIFs (`--gifs`), and `docs/media/chirpers.png` is its `contact-dark.png` (`--sheets`). |
| `firmware/tools/logo_preview.py` | Draws the chirp! logo on all twelve theme backgrounds from `logo.h`. Needs g++ and Pillow. `--art FILE` draws a design from a text file of art rows. `--compare` shows the shipped logo and each `--art` design side by side. Other designs are in `firmware/tools/logo_options/`. The README's `docs/media/themes.png` is its default sheet. |
| `firmware/tools/logo_web.py` | Bakes the logo into `firmware/include/logo_web.h`, a 4-bit PNG for the setup page. The result is reproducible. `--check` verifies it. `logo.h` stays the one source of the art. |
| `flasher/` | The browser installer page and its ESP Web Tools manifest (T-Embed images only). |
| `host/pair.py` | Pairs a device with the Hermes gateway on this machine. |
| `host/audio_shim.py` | The STT and TTS service (systemd user unit `hermes-audio-shim`). |
| `host/local_tts.py` | Paradee TTS: a resident worker (`--serve`) or a one-shot CLI. |
| `host/setup.sh` | Bootstrap for the speech server. |
| `host/provision.py` | Sends the device settings from `firmware/device.env` over USB serial. |
| `host/publish-firmware.py` | Signs a build and publishes it for the WiFi update of the devices. Run it with `--init-key` once. See [Updating](updating.md). |

The character vocabularies (heads, hats, palettes and so on) only grow at the end. An existing index keeps its meaning. Persona looks and kept looks stay valid.
