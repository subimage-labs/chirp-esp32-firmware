# First setup on the phone

A device with no saved settings starts its own WiFi network. This applies to a new device and to an erased device. The device does not start the app. The screen shows these items:

- the network name `Chirp-XXXX`
- a one-time password of 12 characters
- a QR code

If the device is plugged into the Hermes machine, you can skip the phone. `pair.py` does the whole setup over the cable. See [Hermes setup](setup-hermes.md).

## Steps

1. On the phone, join the network `Chirp-XXXX`. Scan the QR code on the screen to join, or type the password.
2. If the phone warns that the network has no internet, stay connected.
3. Wait for the setup page to open. If it does not open, open any plain `http://` address in the phone browser. Do not use `https://`. Every address leads to the setup page.
4. Under **WiFi**, pick your network from the list of nearby networks, or type its name.
5. Type the WiFi password. If the network has no password, tick *Open network*.
6. Under **Backend**, choose **Venice** or **Hermes on my network**.
7. Follow [Venice setup](setup-venice.md) or [Hermes setup](setup-hermes.md).
8. Tap **Save and restart**. The setup page shows *Saved*. The device screen shows *All set! Restarting*.
9. Connect the phone to its usual WiFi again.

The device joins your network and checks its backend. The bottom line shows "Checking Venice" or "Checking Hermes". The device is ready when the chat screen shows the talk hint.

The setup network stays open until the device saves a settings change. After the device has settings, you can start a setup session from the menu. The session ends after 10 minutes without a phone. It ends after 15 minutes at the most.

## Fields on the setup page

| Field | Description |
|---|---|
| WiFi: saved networks | The networks that chirp remembers. The list holds up to 5 networks. It shows names only, never passwords. Tick *Forget* to remove a network. chirp joins the strongest saved network in range, at boot and at each reconnect. This lets you move from one network to another. |
| WiFi: add a network, password | Adds a network to the list. A blank name adds no network. The page offers the nearby networks. Tick *Open network* for a network without a password. A name that is already saved is edited. A blank password keeps the saved password. The 6th network replaces the network that the device joined longest ago. |
| Backend | **Hermes on my network** or **Venice** (cloud). A switch keeps the stored settings of the other mode. |
| Venice API key | Venice only. The box is write-only. A blank box keeps the stored key. Tick *Show password and key* to check your input. |
| Venice host (advanced) | Venice only. A blank box means `api.venice.ai:443`. A different host removes the stored key, because a key belongs to the host that issued it. The setup page refuses a host that starts with `http://`. |
| Hermes host | Hermes only. The Hermes machine as `host` or `host:port`. The default port is 8642. |
| Agents | Hermes only. The list of agents that are on the device. Pick the agent to *use*. Tick *remove* to remove an agent. |
| Add an agent: profile name, API key | Hermes only. The name of a profile that **already exists** on Hermes, and its `API_SERVER_KEY`. The key is in `~/.hermes/profiles/<name>/.env`. The key of the default profile is in `~/.hermes/.env`. A blank name means the default profile. A new name does not create a profile. `host/pair.py --create NAME` creates a profile and sends it to the device. |
| Audio shim host | Hermes only. Optional. A blank box means the Hermes host with port 8643. |

The device stores the settings, and they stay after a firmware update. To change them later, do these steps:

1. Open the **Setup** tab. Tap the side key five times (T-Deck: click the ball five times).
2. Pick *Set up on phone*.

The page shows the saved values. The page never shows the password or the keys again. A blank password or key box keeps the saved value. To leave setup without a change, press the side key (T-Deck: Backspace).

## WiFi networks

The device keeps up to 5 networks. It joins the strongest saved network in range, at boot and at each reconnect.

To add a network:

1. Open **Setup**, then pick *Set up on phone*.
2. Add the network on the setup page. The page lists the saved networks.

The 6th network replaces the network that the device joined longest ago.

To remove a network, use one of these methods:

- Tick *Forget* next to the network on the setup page.
- Send `settings {"forgetWifi":"NETWORK NAME"}` over serial. Use `pio device monitor -d firmware -e <board>` at 115200 baud.

## Setup without a phone (USB)

1. Copy `firmware/device.env.example` to `firmware/device.env`. Git ignores this file.
2. Fill in the values for one route. The comments in the file say which values to use.
3. Plug the device in.
4. Run this command:

```sh
python3 host/provision.py [--port /dev/ttyACM0]
```

The script sends the settings over serial and never prints them. The device restarts.

On Linux, your user must have access to the serial port. On most distributions, add the user to the group `dialout`. `host/provision.py` runs on Linux only.
