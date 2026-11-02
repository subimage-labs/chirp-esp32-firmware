<p align="center"><img src="docs/chirp-logo.png" alt="chirp!" width="288"></p>

# chirp! — a pocket voice companion for the T-Embed and T-Deck

chirp turns a LilyGO **T-Embed CC1101 Plus** or a LilyGO **T-Deck** into a
push-to-talk voice companion. Hold a key, speak, hear the answer, and watch
the conversation on the screen. The little pixel-art characters you see there
are called Chirpers.

## Why it is interesting

- **Two ways to run it.** *Venice* is the cloud route: one API key, no
  computer needed. *Hermes* is your own agent on your own machine; speech
  recognition and the voice run locally and stay private.
- **Pocket-sized.** Everything fits in one small ESP32-S3 device with a
  speaker, a microphone and a screen.
- **Set up from your phone.** The device shows its own WiFi name, password
  and a QR code. Join it, and a setup page opens.
- **Remembers several WiFi networks** (up to 5) and joins the strongest one in
  range.
- **Yours to style.** Six themes (dark and light), four fonts in three sizes,
  and Chirpers that you can browse and keep.

| | **Venice** (cloud) | **Hermes** (your own machine) |
|---|---|---|
| Who answers | Venice's hosted chat, speech-to-text and voice models | your own [Hermes Agent](https://hermes-agent.nousresearch.com/docs/getting-started/installation) profile; listening (whisper) and the voice (Paradee) run on your machine |
| You need | a Venice account with API credit and one API key | a Linux machine on the same WiFi that runs Hermes and chirp's speech server |
| Cost | Venice charges credit for each answer (listening, thinking and speaking are separate calls) | whatever your Hermes model provider charges; the speech parts are free and local |
| Privacy | your voice and text go to Venice over HTTPS | stays on your own network |

## Quick start

### 1. Get what you need

- A T-Embed CC1101 Plus or a T-Deck, and a USB-C cable that carries data
  (charge-only cables do not work).
- A phone with a browser, and a **2.4 GHz** WiFi network the device can join
  (a network with a browser sign-in page will not work).
- Venice: an API key from your [Venice account](https://docs.venice.ai/guides/getting-started/generating-api-key).
  Hermes: a Linux machine, see [docs/setup-hermes.md](docs/setup-hermes.md).
- Git and [PlatformIO](https://platformio.org/install/cli) (`pip install
  platformio`), or Chrome/Edge for the T-Embed browser installer.

```sh
git clone --branch tdeck-venice <repo-url> chirp-esp32-firmware
cd chirp-esp32-firmware
```

### 2. Flash it

```sh
pio run -d firmware -e t-embed-cc1101 -t upload     # T-Embed CC1101 Plus
pio run -d firmware -e t-deck -t upload             # T-Deck
```

Always name the board with `-e`. The T-Embed can also be flashed from the
browser, and a T-Deck that will not flash over serial can use USB-JTAG: see
[docs/flashing.md](docs/flashing.md).

### 3. First setup on your phone

A device with no settings starts its own WiFi network. Its screen shows the
name `Chirp-XXXX`, a one-time password and a QR code.

1. On the phone, scan the QR code (or type the password) to join `Chirp-XXXX`.
2. The setup page opens by itself. If not, open any plain `http://` address.
3. Pick your WiFi network and enter its password.
4. Choose **Venice** and paste your API key, or **Hermes on my network**.
5. Tap **Save and restart**. The chat screen with the talk hint means it is
   ready.

For Hermes, run `host/setup.sh` and `host/pair.py` on the Hermes machine first;
see [docs/setup-hermes.md](docs/setup-hermes.md). More detail for the Venice
route is in [docs/setup-venice.md](docs/setup-venice.md).

### 4. Talk

Hold the talk key, ask something, release. The answer is spoken as it arrives.

## Controls

**T-Embed CC1101 Plus**

| Action | How |
|---|---|
| Talk (release to ask; press while it speaks to interrupt) | hold the dial key |
| Hands-free voice mode on/off | double-tap the dial key |
| Menu: next tab / back to chat (5 taps reach **Setup**) | tap the side key |
| Select / scroll | turn the dial |
| Volume (0–21) | hold the side key 1.5 s, turn the dial |

**T-Deck**

| Action | How |
|---|---|
| Talk (release to ask; press while it speaks to interrupt) | hold the **mic key** |
| Hands-free voice mode on/off | double-tap the mic key |
| Menu: next tab (5 clicks reach **Setup**) / leave | click the trackball / **Backspace** |
| Select / scroll | roll the trackball; **Enter** activates |
| Volume (0–21) | hold the ball click, roll |
| Type a question | type, **Enter** sends |

Every control and screen is described in [docs/using.md](docs/using.md).

## Help

| Problem | Fix |
|---|---|
| The computer sees no port / upload cannot connect | Use a data cable. Hold **BOOT** (T-Deck: the trackball click) while plugging in or tapping reset. |
| No `Chirp-XXXX` network on the phone | Read the name and password on the device screen. 2.4 GHz only; move closer. Stay connected if the phone says "no internet". |
| Setup page does not open | Open any plain `http://` address (not `https://`). |
| WiFi failed at boot | Wrong password, a 5 GHz-only network, or a sign-in page. Open **Setup → Set up on phone** and fix or add a network. |
| Venice: *Bad API key* or *Out of credit* | Enter the key again on the setup page, or add credit to your Venice account. |
| Hermes: unreachable or key rejected | Run `python3 host/pair.py` again; it re-checks everything. |

## Documentation

- [Flashing](docs/flashing.md): browser installer, PlatformIO, T-Deck USB-JTAG, updating
- [First setup on the phone](docs/first-setup.md): every setup page field, WiFi networks, setup over USB
- [Venice setup](docs/setup-venice.md) and [Hermes setup](docs/setup-hermes.md)
- [Using chirp](docs/using.md): tabs, views, characters, power and battery
- [How a turn works](docs/how-it-works.md)
- [Troubleshooting](docs/troubleshooting.md) and known limits
- [Security notes](docs/security.md)
- [Development](docs/development.md): building, tests, repository layout

## License

The four bundled fonts (Atkinson Hyperlegible, Inter, Paper Mono, Terminus)
are under the SIL Open Font License 1.1; each license file sits beside the
font data in [`firmware/src/fonts/`](firmware/src/fonts).
