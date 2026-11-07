# Security notes

No secret is compiled into the firmware or tracked in git. Builds carry no settings.

## On the device

The device stores these items in the settings partition (NVS) of the ESP32:

- the WiFi credentials
- the Hermes or Venice API key
- the hosts

This partition is plain flash. A person who has the device and a USB cable can read these items. Make a Venice key for this device only. Revoke the key if you lose the device.

The setup network uses WPA2. It has a new password of 12 characters each time. The setup form needs a token. Only the page of that run carries the token. The form refuses a foreign `Origin`. A setup session that you open from the menu ends after 15 minutes at the most.

On the Hermes machine, the key is in the `.env` file of the profile. `firmware/device.env` is optional and is used by `provision.py`. Git ignores this file.

## Venice route

The device sends your voice and text to Venice over HTTPS.

- The device sends the Venice key only over https. It sends the key to the configured host. The default host is `api.venice.ai`.
- The setup page refuses a Venice host that starts with `http://`. This applies to every host.
- Only developer bench builds accept `http://`, for a local test server.
- The device checks each certificate against a pinned root certificate. This build does not check the expiry dates of certificates.

## Hermes route

**The keys travel in the clear.** The device talks to the Hermes gateway over plain HTTP on your LAN. It sends the API key of the agent with every request. A person who can capture traffic on that network can read the key.

Limit access to the gateway port (8642) with a firewall rule on the Hermes machine. Allow only the address of the device. TLS with a certificate pinned on the device would close this gap. This is not built yet.

Pairing over USB (`host/pair.py`) sends no key over WiFi or to a screen. The QR code from `--no-usb` carries the API key of the profile. Do not share the code.

## What runs on the Hermes machine

| Process | Port | Who can connect | Notes |
|---|---|---|---|
| Hermes gateway (yours) | 8642 | Your LAN | Each device request carries the API key of its agent. |
| chirp speech server (`host/audio_shim.py`) | 8643, all interfaces | Your LAN | Speech to text, text to speech, and published firmware. |
| Voice worker (`host/local_tts.py`) | 8644, localhost only | This machine | Started by the speech server. |

**The speech server is open by default.** Anyone who can reach port 8643 can use it to transcribe audio, make speech and download your published firmware. Set `SHIM_TOKEN` in the environment of the service to require `Authorization: Bearer <token>` on every `/v1/` and `/firmware/` request. `/health` stays open. Firmware that sends this token is not built yet, so a token blocks your devices too. Until then, limit port 8643 with a firewall rule to the addresses of your devices.

The speech server writes a recording to a temporary file only while it transcribes, and deletes it afterwards. It keeps no audio and no text. It sends nothing off the machine: whisper and the voice run locally.
