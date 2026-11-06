<p align="center"><img src="docs/chirp-logo.png" alt="chirp!" width="288"></p>

# Chirp! Your pocket voice agent interface for ESP32 devices

Chirp runs on the LilyGO T-Embed CC1101 Plus and the LilyGO T-Deck. More
devices will follow.

Hold the talk key and speak. The device sends your question to an AI agent
and speaks the answer. The conversation shows on the screen. Small pixel
characters, the Chirpers, show what the agent does.

## What chirp does

- It works with two backends: Venice in the cloud, or Hermes on your own
  computer. You select one during setup.
- You set it up from a phone. You do not need an app.
- It saves up to 5 WiFi networks and connects to the strongest one in range.
- You can change the theme, the font, the text size and the Chirper.

## Two backends

| | Venice | Hermes |
|---|---|---|
| Answers come from | Venice cloud models | Your own [Hermes Agent](https://hermes-agent.nousresearch.com/docs/getting-started/installation) profile |
| Speech to text and voice | Venice cloud | Your own computer |
| You need | A Venice account with API credit, and an API key | A Linux computer on the same WiFi that runs Hermes and the chirp speech server |
| Cost | Venice charges credit for each answer | The cost of your Hermes model provider. Speech is free. |
| Your data | Goes to Venice over https | Stays on your network |

## Quick start

### 1. Get the parts

- A T-Embed CC1101 Plus or a T-Deck.
- A USB-C cable that carries data. A charge-only cable does not work.
- A phone with a web browser.
- A 2.4 GHz WiFi network. A network with a sign-in page does not work.
- For Venice: an API key from your
  [Venice account](https://docs.venice.ai/guides/getting-started/generating-api-key).
- For Hermes: a Linux computer. See [Hermes setup](docs/setup-hermes.md).
- On your computer: Git and [PlatformIO](https://platformio.org/install/cli)
  (`pip install platformio`). For the T-Embed, you can use the browser
  installer in Chrome or Edge instead.

### 2. Get the code

```sh
git clone <repo-url> chirp-esp32-firmware
cd chirp-esp32-firmware
```

### 3. Install the firmware

Connect the device with the USB cable. Then run the command for your device:

```sh
pio run -d firmware -e t-embed-cc1101 -t upload     # T-Embed CC1101 Plus
pio run -d firmware -e t-deck -t upload             # T-Deck
```

Always give the device name with `-e`. Without it, PlatformIO installs the
firmware of every device, one after the other.

For the browser installer, and for a T-Deck that does not accept the upload,
see [Installing the firmware](docs/flashing.md).

### 4. Set up the device from your phone

A device without settings starts its own WiFi network. The screen shows the
network name (`Chirp-XXXX`), a password and a QR code.

1. Scan the QR code with the phone, or type the password. The phone connects
   to `Chirp-XXXX`.
2. Wait for the setup page to open. If it does not open, go to any `http://`
   address in the browser.
3. Select your WiFi network and type its password.
4. Select **Venice** and type your API key. Or select **Hermes on my network**.
5. Tap **Save and restart**.

The device restarts. When the chat screen shows the talk hint, the device is
ready.

For Hermes, first run `host/setup.sh` and `host/pair.py` on the Hermes
computer. See [Hermes setup](docs/setup-hermes.md). For more about Venice, see
[Venice setup](docs/setup-venice.md).

### 5. Talk

Hold the talk key and speak. Release the key. The device speaks the answer.

## Controls

T-Embed CC1101 Plus:

| To do this | Do this |
|---|---|
| Ask a question | Hold the dial key and speak. Release the key. |
| Stop the answer | Push the dial key. |
| Start or stop hands-free mode | Push the dial key two times quickly. |
| Open the menu or go to the next tab | Push the side key. Push it 5 times to go to **Setup**. |
| Select or scroll | Turn the dial. |
| Use the selected item | Push the dial key. |
| Change the volume (0-21) | Hold the side key for 1.5 seconds. Then turn the dial. |

T-Deck:

| To do this | Do this |
|---|---|
| Ask a question | Hold the mic key and speak. Release the key. |
| Type a question | Type it. Then push **Enter**. |
| Stop the answer | Push the mic key. |
| Start or stop hands-free mode | Push the mic key two times quickly. |
| Open the menu or go to the next tab | Click the trackball. Click it 5 times to go to **Setup**. |
| Close the menu | Push **Backspace**. |
| Select or scroll | Roll the trackball up or down. |
| Use the selected item | Push **Enter**. |
| Change the volume (0-21) | Hold the trackball click. Then roll the trackball. |

All controls and screens are in [Using chirp](docs/using.md).

## Help

| Problem | What to do |
|---|---|
| The computer does not find the device | Use a data cable. Hold **BOOT** (T-Deck: the trackball click) when you connect the cable. |
| The phone does not show `Chirp-XXXX` | Read the name and the password on the device screen. Use 2.4 GHz. Move the phone nearer. |
| The phone says "no internet" | Stay connected. The setup network has no internet. |
| The setup page does not open | Go to any `http://` address. Do not use `https://`. |
| The device shows "WiFi failed" | Check the password. Use a 2.4 GHz network. Go to **Setup**, select **Set up on phone**, and add or correct the network. |
| Venice: "Bad API key" or "Out of credit" | Type the key again on the setup page, or add credit to your Venice account. |
| Hermes: "unreachable" or "key rejected" | Run `python3 host/pair.py` again. It checks all connections. |

More problems and their solutions are in [Troubleshooting](docs/troubleshooting.md).

## More information

- [Installing the firmware](docs/flashing.md)
- [First setup on the phone](docs/first-setup.md)
- [Venice setup](docs/setup-venice.md) and [Hermes setup](docs/setup-hermes.md)
- [Using chirp](docs/using.md)
- [How a question is answered](docs/how-it-works.md)
- [Troubleshooting and known limits](docs/troubleshooting.md)
- [Updating](docs/updating.md)
- [Security](docs/security.md)
- [Development](docs/development.md)

## License

The four fonts (Atkinson Hyperlegible, Inter, Paper Mono and Terminus) use the
SIL Open Font License 1.1. Each license file is next to the font data in
[`firmware/src/fonts/`](firmware/src/fonts).
