# First setup on the phone

A device with no saved settings (new, or erased) starts its own WiFi network
instead of the app. The screen shows the network name `Chirp-XXXX`, a
one-time 12-character password and a QR code.

Hermes route with the device plugged into the Hermes machine: you can skip the
phone. `pair.py` does the whole setup over the cable; see
[Hermes setup](setup-hermes.md).

1. On the phone, join `Chirp-XXXX`: scan the screen's QR code (it joins the
   network for you) or type the password. If the phone warns that the network
   has no internet, stay connected.
2. The setup page opens by itself. If it does not, open any plain `http://`
   address in the phone's browser (not `https://`): every address lands on
   the page.
3. **WiFi:** pick your network from the nearby list (or type its name), type
   the password; tick *Open network* if it has none.
4. **Backend:** choose **Venice** or **Hermes on my network**, then follow
   [Venice setup](setup-venice.md) or [Hermes setup](setup-hermes.md).
5. Tap **Save and restart**. The page says *Saved*; the device screen says
   *All set! Restarting*. Reconnect the phone to its usual WiFi. The device
   joins your network and checks its backend ("Checking Venice" or "Checking
   Hermes"); the chat screen with the talk hint means it is ready.

The setup network stays open until something is saved. Once the device has
settings, a setup session you start from the menu ends by itself after 10
minutes without a phone (15 at most).

## Every field on the setup page

| Field | |
|---|---|
| WiFi: saved networks | the networks chirp remembers (up to 5, names only, never passwords): tick *Forget* to remove one. chirp joins the strongest saved network in range, at boot and whenever it reconnects, so walking from one to another just works |
| WiFi: add a network / password | adds a network to that list (blank name = add none; nearby networks are offered; tick *Open network* for one without a password; a name already saved is edited, a blank password keeps its saved one). The 6th network pushes out the one joined longest ago |
| Backend | **Hermes on my network** or **Venice** (cloud). Switching keeps the other mode's settings stored |
| Venice API key | Venice only. Write-only: a blank box keeps the stored key. Tick *Show password and key* to check what you typed |
| Venice host (advanced) | Venice only; blank = `api.venice.ai:443`. A different host drops the stored key (a key belongs to the host that issued it) |
| Hermes host | Hermes only: your Hermes machine as `host` or `host:port` (default port 8642) |
| Agents | Hermes only: the agents already on the device: pick the one to *use*, tick *remove* to drop one |
| Add an agent: profile name + API key | Hermes only: a profile that **already exists** on Hermes and its `API_SERVER_KEY` (in `~/.hermes/profiles/<name>/.env`; the default profile's is in `~/.hermes/.env`). Blank name = the default profile. Typing a new name does not create a profile: `host/pair.py --create NAME` does, and sends it here |
| Audio shim host | Hermes only; optional; blank = the Hermes host, port 8643 |

Settings are stored on the device and survive firmware updates. To change
them later, open the **Setup** tab (five side-key taps on the T-Embed, five
ball clicks on the T-Deck) and pick *Set up on phone*. Saved values are filled
in; the password and keys are never shown again, and leaving the password or a
key blank keeps it. The side key (T-Deck: Backspace) leaves setup without
changes.

## WiFi networks

The device keeps up to 5 networks and joins the strongest saved one in range,
at boot and whenever it reconnects. To add one, open **Setup → Set up on
phone** and add it on the page (the saved ones are listed; the 6th pushes out
the one joined longest ago). To remove one, tick *Forget* next to it on that
page, or send `settings {"forgetWifi":"NETWORK NAME"}` over serial
(`pio device monitor -d firmware -e <board>`, 115200 baud).

## Without a phone (USB)

Copy `firmware/device.env.example` to `firmware/device.env` (git-ignored), fill
in the values for one route (comments in the file say which), plug the device
in and run:

```sh
python3 host/provision.py [--port /dev/ttyACM0]
```

It sends the settings over serial, never prints them, and the device restarts.
On Linux your user must be allowed to use the serial port (group `dialout` on
most distributions). `host/provision.py` is Linux-only.
