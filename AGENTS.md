# Agent Rules — hermes-chirp-cc1101

T-Embed CC1101 Plus device + host-side voice bridge for the hermes agent.
Two agents may work this repo concurrently: split by lane, announce cross-lane
edits via the other tmux pane.

## Layout

- `firmware/` — ESP32-S3 firmware (PlatformIO, Arduino). PTT voice device.
  `firmware/tools/` has the font generator, `avatar_preview.py`, which draws
  character sheets and GIFs from `avatar.h` itself (`firmware/test/host/avatar_preview.cpp`),
  `logo_preview.py`, the chirp! logo on all 12 theme backgrounds from `logo.h`
  (and side-by-side comparisons of the designs in `logo_options/`), and `logo_web.py`,
  which bakes the logo into `include/logo_web.h` for the setup web page.
- `host/` — workstation side, served on the LAN:
  `audio_shim.py` (systemd user unit `hermes-audio-shim`, port 8643: STT +
  TTS proxy), `local_tts.py` (resident Paradee worker, 127.0.0.1:8644),
  `setup.sh` (venv, models, service install), `pair.py` (pairs a device with
  the local Hermes gateway; `test_pair.py` tests its QR handoff;
  `avatar_look.py` derives the agent's character
  look from its persona, `test_avatar_look.py` tests it), `serial-watch.py`, `provision.py` (device
  settings over USB serial), `publish-firmware.py` (signs a build for the devices'
  boot-time WiFi update, served by the shim), `latency.py` (answer latency
  on a bench build), `smoke.py` (on-device smoke test, `test_smoke.py` its
  offline tests), `bench_stt.py` (whisper benchmark), `check-secrets.sh`.
- `flasher/` + `.github/workflows/firmware-release.yml` — browser installer
  (ESP Web Tools). Its manifest lists the flash images separately and never
  a merged image: merging pads the NVS range and would wipe device settings.

## Rules

1. **No backward compatibility. No migration history.** Code, comments, and
   docs describe the current state only. Never keep "legacy", "deprecated",
   "compat", or "replaces X" framing. When a path is replaced, delete the old
   one in the same change. (Standing directive, 2026-10-09.)
2. **Secrets never enter git or the firmware.** WiFi credentials, the hermes
   key, and LAN hosts are device settings in NVS (`firmware/include/settings.h`),
   entered through the setup portal, `host/pair.py` or `host/provision.py`.
   The key's home is the Hermes profile's `.env`; the only other workstation
   copy is the gitignored `firmware/device.env` (template:
   `firmware/device.env.example`). Never put real credentials or IPs in
   config headers, docs, plan files, comments, or commit messages. The
   hook (`host/check-secrets.sh`) blocks leaks in commits, merge results and
   commit messages: it reads the device settings, the hostname and the
   owner's Hermes profile ids (`~/.hermes/profiles/*`) at hook time, hands
   them to grep through files (never argv), prints line numbers only, and
   requires a UTC commit (`TZ=UTC git commit`, also for `git merge`). It also
   scans whole trees (`--scan-tree`) and release images (`--scan-artifact`)
   for publication checks; `host/guard-exceptions` lists the reviewed
   synthetic-test files it must not flag. Enable it in fresh clones with
   `host/setup.sh`, or link it as `pre-commit`, `pre-merge-commit` and
   `commit-msg` (`ln -sf ../../host/check-secrets.sh .git/hooks/pre-commit`).
   Never bypass with `--no-verify` without saying why out loud.
3. **Firmware lane vs host lane.** Device code (flash, build, pins, UI) and
   host code (shim, TTS, STT) are separate lanes; don't edit the other lane
   without coordinating.
4. **Verify before yielding.** Firmware: `pio run -d firmware` compiles and
   `firmware/test/host/run.sh` passes (host-side unit tests, TSan for the
   concurrent ones); never upload without the user asking. On-device
   behavior: flash a bench build (`PLATFORMIO_BUILD_FLAGS=-DBENCH_CMDS`) and
   drive it over serial (commands in `firmware/include/bench.h`; write lines
   to `/dev/ttyACM*` through a raw fd while `serial-watch.py` logs; run
   `stty -F /dev/ttyACM0 raw -echo` before starting the watcher, or bench
   lines get no reply); after every bench flash run `host/smoke.py` (add
   `--turn` / `--reboot` when the change touches turns or persisted settings)
   and report its table — set `vol 0` when nobody
   is listening, and pick "+ New chat" in the menu before any `ask`:
   bench turns otherwise land in the owner's real chat history. A device
   without usable settings (new, or flash erased) boots into setup mode:
   pair it with `host/pair.py` or `host/provision.py`. Host: exercise the real shim
   endpoints (`/health`, `/v1/audio/*`) with curl.
5. **Firmware structure.** One module per concern; `firmware/include/app.h`
   holds the module map and execution contexts (loop / turn / fetch). Every
   module header states which context owns its state; cross-context data
   moves only through the handoffs documented there. Pure logic (policies,
   decoders, packers) lives in Arduino-free headers with host tests. Keep it
   that way: new state gets one owner, new logic that can be pure is pure.
6. **LAN-only audio path.** The device talks only to the workstation.
   Do not introduce external/cloud endpoints into the audio pipeline.
7. **Nothing that profiles the owner.** No LAN IPs, hostnames, network maps,
   device inventory, or security-lab details in tracked files or commit
   messages. Private planning notes live in the gitignored `tmp/`.

8. **Text goes through the ui font API.** Four font families (Atkinson
   Hyperlegible, Inter, Paper Mono, Terminus; all SIL OFL) in three sizes
   S/M/L, chosen live in the Text tab and stored in NVS by name (`fontName`,
   `fontSize`), never by table index. The anti-aliased TFT_eSPI smooth fonts
   (Terminus: 1-bit, taken from its BDF strikes at the native 8x14, 8x16 and
   10x20 cell sizes: a pixel font is only drawn at sizes its designer drew)
   are generated into `firmware/src/fonts/font_data.cpp`
   by `firmware/tools/make_font.py` from pinned, hash-checked upstream files
   (`font-sources.txt`); each license file sits beside the data.
   `uiFont()` / `uiText()` / `uiTextWidth()` / `uiFold()` / `uiFit()` in `ui.h`
   are the only place fonts are chosen and text drawn: no `setTextFont`,
   `drawString` or `textWidth` elsewhere. Text is composed in a line sprite
   against the caller's background ink and pushed in one block. Fold outside
   text with `uiFold` first (it never lets a box glyph through).
   **No hard-coded text heights**: line pitch, menu rows, status strip, tab
   header and the character card's rows all derive from the Body cut's line box
   (`uiLineHeight()`, `uiRowHeight()`, `uiStatusHeight()`, ... in `ui.h`;
   `uiMetricsFor` in `fontpick.h`), so every size reflows and fits. The setup
   portal draws in `UiFont::Fixed` (Atkinson M, 18 px lines) so its layout never
   depends on the choice. Pure font logic (name/size lookup, VLW reads, fitting,
   header layout) is in `fontpick.h` / `textfold.h`; `font_test.cpp` checks every
   family x size. Adding a family: edit `FAMILIES` and `font-sources.txt`, run the
   generator, commit the data and license.

## Flashing the device

Hard-won procedure — every step below exists because skipping it broke a flash.

1. **Free the port.** `pkill -f serial-watch.py` (and any other reader of
   `/dev/ttyACM*`). An open handle aborts esptool mid-write ("serial noise",
   `interrupted`) and can strand the chip in ROM download mode.
2. **Build, check, then upload — separate foreground commands.**
   `pio run -d firmware` → confirm `SUCCESS` → only then
   `pio run -d firmware -t upload --upload-port /dev/ttyACM1` → confirm
   `Hash of data verified` + `SUCCESS`. Never `&&`-chain build→upload→commit
   behind a trailing `&`/`disown`: a failed build still uploaded the stale
   binary, and backgrounding swallowed the commit step silently.
3. **Port name drifts.** The S3 re-enumerates after resets (ACM0 → ACM1). If
   the upload can't open the port: `ls /dev/ttyACM*`.
4. **Stuck in download mode, or silent after an upload** (`waiting for
   download` on serial, or no boot lines in the log after a `pio` upload):
   `python -m esptool --chip esp32s3 --port /dev/ttyACM1 --after hard_reset flash_id`.
5. **Restart the watcher detached**, then confirm boot:
   `setsid nohup host/serial-watch.py >/dev/null 2>&1 </dev/null &` →
   `/tmp/chirp-serial.log` shows `PSRAM enabled`, `[WiFi] <device-ip>` and
   `[SelfTest] ok`.
6. **Commit after the device boots clean** — not before, not chained.
7. **Never flash while the user is mid-test.** Each flash reboots the panel and
   silently ends the conversation; it reads as a firmware bug.

Known-good tags: `speech-stable` (reliable TTS playback), `ux-stable`
(bottom-pinned chat), `scroll-stable` (ISR wheel, speed-scaled scrolling),
`v1.0.0` (first release: smoke 11/11, signed OTA verified end to end).
Serial diagnostics: `[Timing]` (incl. per-turn
`publish`/`snapread` lock latency), `[Heap]` (free vs largest block =
fragmentation, `min` = low-water), `[Turn]`, `[TTSF]` (chunk fetch),
`[Chunk]` (play), `[Speak]` (playback end + reason: done / abort /
fetch-fail), `[Power]` (source, backlight, sleep/wake), `[Settings]` (load /
save, never values), `[Setup]` (setup mode), `[SelfTest]` (boot reachability
check), `[OTA]` (boot-time WiFi update: running version, progress, failures),
`[Lat]` (answer latency milestones from the Hermes request; `host/latency.py`
reads them), `[Bench]` (bench builds).

WiFi updates: `firmware/version.py` stamps `FW_VERSION` (`git describe`, or
the `FW_VERSION` env var for builds outside the repo) and must rebuild
`ota.cpp` whenever it changes; `firmware/ota_build.py` stamps `FW_BUILD`
(unix seconds) and embeds `firmware/ota_pub.pem` (gitignored; made by
`host/publish-firmware.py --init-key`, private half at
`~/.config/chirp/ota-signing.pem`, never in the repo or on a command line),
with the same rebuild rule. A build without that file works but never
updates over WiFi. Manifests are signed; the device checks, in order,
parse, signature, `build` > max(own, NVS `chirp`/`otaBuild`), not the image
that failed last, fit, 120 s download, sha256 before `Update.end` — see
`ota.h`/`ota_policy.h`. Downgrades go over USB only. Rollback rule: the
device marks a new image valid as soon as WiFi joins (`otaMarkHealthy`,
before the Hermes self-test), so Hermes or shim outages never roll back a
good image; an image that crashes before that, or never joins WiFi, rolls
back at the next reset.
