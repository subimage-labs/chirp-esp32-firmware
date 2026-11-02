# Troubleshooting

## Common problems

| Symptom | Fix |
|---|---|
| No port in the installer / `pio` cannot connect | Use a data cable. Hold **BOOT** (T-Deck: the trackball click) while plugging in or tapping reset. On Linux, stop `ModemManager` (`sudo systemctl stop ModemManager`) and add your user to `dialout`. T-Deck still silent: the USB-JTAG route in [Flashing](flashing.md). |
| Nothing happens after flashing | Unplug and replug the cable. A device with no settings shows the setup screen ([First setup](first-setup.md)). |
| No `Chirp-XXXX` network on the phone | Look at the device screen: it shows the name and password. 2.4 GHz only; move closer. The phone may call it "no internet": stay connected. |
| Setup page does not open | Open any plain `http://` address in the browser (not `https://`). |
| "Refused: open the setup page again and resubmit" | The page was left open across a restart or a second setup session: reload it and save again. |
| WiFi failed at boot | Wrong password, 5 GHz-only network, none in range, or a network with a sign-in page. Open **Setup → Set up on phone** (the screen says how) and fix or add a network. |
| Venice errors | See [Venice setup](setup-venice.md#venice-problems). |
| Hermes errors | See [Hermes setup](setup-hermes.md#hermes-problems); `python3 host/pair.py` re-checks everything. A changed address for the Hermes machine needs a re-pair. |
| Want a clean start | A full erase wipes every setting and returns the device to its setup screen (see [Flashing](flashing.md#clean-start)). |
| Reporting a problem | `pio device monitor -d firmware -e <board>` (115200 baud) shows the boot log (`[WiFi]`, `[SelfTest]`, `[Settings]`; never the values). |

## Boot check messages

At boot the device checks its connections and names the first problem it
finds (it gives up after about 15 s at most):

| Message | Fix |
|---|---|
| WiFi failed (T-Embed: "side key 5x = Setup"; T-Deck: "Click 5x = Setup") | No saved network joined (wrong name or password, or none in range): Setup tab (five taps or clicks away), *Set up on phone* |
| Hermes key rejected | Wrong API key for the agent: run `host/pair.py` again, or Setup tab, *Set up on phone* |
| Hermes profile not found | The profile name doesn't exist on the gateway |
| Hermes unreachable | The gateway isn't running or not reachable on the LAN (`host/pair.py` checks) |
| Speech server unreachable | The speech server isn't running: `systemctl --user status hermes-audio-shim` |
| Venice: *Bad API key*, *Out of credit*, *Key not allowed*, *Rate limited* | See [Venice problems](setup-venice.md#venice-problems) |
| Venice unreachable | No internet, or Venice is down: check the WiFi has internet access |
| Venice error | Venice answered with an unexpected error: try again later |

A turn that fails afterwards says why on the bottom line: *Bad API key*, *Out
of credit*, *Key not allowed* or *Rate limited* (Venice only), *Backend not
answering* (nothing arrived for 90 s) or *Backend unreachable*.

## Known limits

- **Venice: internal memory.** After a Venice answer the device's free internal
  memory falls lower than in Hermes mode. If a Venice device ever restarts by
  itself mid-answer, send the serial log.
- **T-Deck: thin vertical flashes.** Some menu repaints in the *Rosé Pine* dark
  theme show one-refresh vertical flashes. It is cosmetic; another theme may
  avoid it.
- **T-Deck: flashing may need USB-JTAG** ([Flashing](flashing.md)).
- **Updating from an older build** can ask for WiFi once ([Updating](updating.md)).
- **Venice mode:** the persona is fixed, there are no server-side chats, and
  there are no WiFi updates.
- whisper `base` occasionally mishears very short clips.
- An answer must finish streaming within 240 s, with no 90 s stretch without a
  data event (tool progress counts; the gateway's keepalives don't); research
  turns that exceed it end as "Backend not answering".
- No wake word: voice mode listens for speech energy, and only while idle.
- Only one chunk downloads ahead of the one playing (two PSRAM slots): a chunk
  that synthesizes slower than the previous one plays leaves a gap. Chunk
  sizes ramp up to avoid it, but a very long single sentence in the second
  chunk still can.
- The chat transcript (last 40 turns) lives in PSRAM; only the few KB of
  bookkeeping around it use the internal heap.
- While the answer is still streaming, the dial key can't interrupt it; it can
  once streaming ends.
