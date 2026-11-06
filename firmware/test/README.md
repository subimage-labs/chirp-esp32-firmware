# Tests

## Why these tests exist

The device is hard to debug. A bug on the device means a flash, a reboot and a serial log. A test on a computer runs in seconds.

Pure logic is in headers that need no Arduino. The tests compile those headers on a computer and run them fast.

The tests stop regressions in rules that users depend on:

- Settings and security rules: which host and key a request may use, and what the setup page may change.
- WiFi choice: which saved network the device joins first.
- Layout: every screen fits every supported panel and font size.
- Speech chunking: how an answer is split so playback has no gaps.
- Power and sleep: when the screen dims and the device sleeps.
- OTA safety: the device installs only a signed, newer, complete image.

## What the tests cover

| Area | What is checked | Where |
|---|---|---|
| Settings and setup | Form merge rules, host parsing, serial settings, the address and key each request uses, access to the setup page, setup rows | `setup_form`, `serial_settings`, `endpoint_urls`, `setup_view`, `setup_view_voice`, `portal_layout` tests |
| WiFi | Network order, strongest first, hidden networks | `wifi_pick` test |
| Input | Wheel, ball, keyboard, tap and hold gestures, action routing | `wheel`, `balltap`, `keymap`, `gesture`, `keyring`, `route` tests |
| Layout and fonts | Geometry on all panel sizes and font cuts, text fitting | `ui_layout`, `font`, `bubbles`, `anchor` tests |
| Look and theme | Colors, contrast, avatar parts, logo | `theme`, `avatar`, `logo` tests and `test_logo_web.py` |
| Speech and turns | Answer stream reading and its end decision, chunking, memory playback, answer text handoff, history | `sse_stream`, `sentence_packer`, `mem_source`, `turntext`, `venice_policy`, `line_store` tests |
| Voice detection | Hands-free start and stop, noise floor, the speech kept from before the start | `voice_detect` test |
| Power | Dim, sleep and wake timeline, battery gauge | `power_policy`, `battery`, `expiry` tests |
| Boot and OTA | Boot check messages, manifest parsing, install decision, a manifest made by the publish tool parsed by the device code | `selftest_policy`, `ota_policy`, `ota_manifest_cross` tests, `host/test_publish_firmware.py` |
| Build | Release images do not carry build machine paths | `test_build_paths.py` |
| Host tools | Pairing, provisioning, serial port choice and replies, the audio shim limits, the secret guard | `host/test_*.py` |

Some tests run with a thread checker or a memory checker. Those check the code that two tasks share.

## What these tests do not cover

They do not run on the device. They do not check the radio, the audio hardware, the display driver, the WiFi stack or the TLS stack.

They do not check code that needs Arduino, such as network streams and the turn worker.

The on-device smoke test checks that part. It needs a bench build and a device.

1. Build with `PLATFORMIO_BUILD_FLAGS=-DBENCH_CMDS` and flash one board.
2. Run `host/smoke.py`. Add `--turn` for answers and `--reboot` for saved settings.

`host/test_smoke.py` tests the parsers of `smoke.py` offline. It does not need a device.

## How to run them

Run all firmware host tests:

    firmware/test/host/run.sh

Run the host tool tests:

    cd host && python3 -m unittest

Both commands need no device. Run both before you hand in a change. The firmware must also build: `pio run -d firmware`.

## Rules for a good test

1. Test behavior. Test what a user or another module depends on.
2. Test boundaries and error paths. Test the value at the limit and the value just past it.
3. Do not test wording. Do not test wiring. Do not copy a constant from the code into the test.
4. Do not compare a result with the formula that makes it. Use an independent bound.
5. Make every test deterministic. No clock, no network, no random seed without a fixed value.
6. Keep each test fast. Use a small sweep that covers each branch.
7. A new pure rule gets a test before the code.
8. Put new logic that can be pure in an Arduino-free header. Then it can be tested here.
9. Never change a test to make a failing build pass. First find out why it fails.
10. A new panel size or font cut goes into the layout test matrix first.
