# Troubleshooting

## Common problems

| Symptom | Fix |
|---|---|
| No port in the installer, or `pio` cannot connect | Use a data cable. T-Embed: hold **BOOT** while you plug the cable in or tap reset. On Linux, stop `ModemManager` (`sudo systemctl stop ModemManager`) and add your user to `dialout`. The T-Deck never accepts a serial upload: use [T-Deck over USB-JTAG](flashing.md#t-deck-over-usb-jtag). |
| Nothing happens after the flash | Unplug the cable and plug it in again. A device with no settings shows the setup screen ([First setup](first-setup.md)). |
| No `Chirp-XXXX` network on the phone | Read the name and the password on the device screen. The network uses 2.4 GHz only. Move closer. The phone can say "no internet". Stay connected. |
| The setup page does not open | Open any plain `http://` address in the browser. Do not use `https://`. |
| "Refused: open the setup page again and resubmit" | The page stayed open across a restart or a second setup session. Reload the page and save again. |
| WiFi failed at boot | The password is wrong, the network is 5 GHz only, no network is in range, or the network has a sign-in page. Open **Setup**, then *Set up on phone* (the screen tells you how). Fix the network or add a network. |
| Venice errors | See [Venice setup](setup-venice.md#venice-problems). |
| Hermes errors | See [Hermes setup](setup-hermes.md#hermes-problems). Run `python3 host/pair.py` to check everything again. If the address of the Hermes machine changes, pair the device again. |
| You want a clean start | A full erase removes every setting and returns the device to its setup screen (see [Flashing](flashing.md#clean-start)). |
| You must report a problem | Run `pio device monitor -d firmware -e <board>` (115200 baud). It shows the boot log (`[WiFi]`, `[SelfTest]`, `[Settings]`). The log never shows the values. |

## Boot check messages

At boot, the device checks its connections and names the first problem it finds. The check stops after about 15 s at the most.

| Message | Fix |
|---|---|
| WiFi failed (T-Embed: "side key 5x = Setup"; T-Deck: "Click 5x = Setup") | No saved network joined. The name or the password is wrong, or no network is in range. Open the Setup tab (five taps or clicks), then pick *Set up on phone*. |
| Hermes key rejected | The API key of the agent is wrong. Run `host/pair.py` again, or open the Setup tab and pick *Set up on phone*. |
| Hermes profile not found | The profile name does not exist on the gateway. |
| Hermes unreachable | The gateway does not run, or the LAN cannot reach it. `host/pair.py` checks this. |
| Speech server unreachable | The speech server does not run. Run `systemctl --user status hermes-audio-shim`. |
| Venice: *Bad API key*, *Out of credit*, *Key not allowed*, *Rate limited* | See [Venice problems](setup-venice.md#venice-problems). |
| Venice unreachable | There is no internet, or Venice is down. Make sure the WiFi has internet access. |
| Venice error | Venice sent an unexpected error. Try again later. |

If a turn fails after the boot check, the bottom line says why. These messages are possible:

- *Bad API key*, *Out of credit*, *Key not allowed* or *Rate limited* (Venice only).
- *Backend not answering*. Nothing arrived for 90 s.
- *Backend unreachable*.

## Known limits

- Venice, internal memory: After a Venice answer, the free internal memory of the device is lower than in Hermes mode. If a Venice device restarts by itself during an answer, send the serial log.
- T-Deck, thin vertical flashes: Some menu repaints in the *Rosé Pine* dark theme show vertical flashes that last one refresh. They are cosmetic. Another theme can avoid them.
- T-Deck, flashing: The T-Deck flashes over USB-JTAG only ([Flashing](flashing.md#t-deck-over-usb-jtag)). It has no tested full-erase method yet.
- Update from an older build: The device can ask for WiFi once ([Updating](updating.md)).
- Venice mode: The persona is fixed, there are no chats on the server, and there are no WiFi updates.
- whisper `base` sometimes misunderstands very short clips.
- An answer must finish streaming within 240 s. A stretch of 90 s without a data event is not allowed. Tool progress counts as a data event. The keepalives of the gateway do not count. A research turn that exceeds these limits ends as "Backend not answering".
- There is no wake word. Voice mode listens for speech energy, and only while the device is idle.
- Only one chunk downloads ahead of the chunk that plays (two PSRAM slots). If a chunk takes more time to synthesize than the previous chunk takes to play, a gap occurs. The chunk sizes increase to prevent this. A very long single sentence in the second chunk can still cause a gap.
- The chat transcript (the last 40 turns) is in PSRAM. Only a few KB of bookkeeping around it use the internal heap.
- The talk key cannot interrupt an answer while the answer is still streaming. It can interrupt the answer after the streaming ends.
