# Security notes

No secret is compiled into the firmware or tracked in git. Builds carry no
settings.

## On the device

WiFi credentials, the Hermes or Venice API key and the hosts live in the
ESP32's settings partition (NVS). That is plain flash, so anyone with the unit
and a USB cable can read them back. Use a Venice key made for this device and
revoke it if the device is lost.

The setup network is WPA2 with a fresh 12-character password each time. The
setup form needs a token that only that run's page carries, and refuses a
foreign `Origin`. A setup session opened from the menu ends after 15 minutes at
most.

On the Hermes machine, the key lives in the profile's `.env`.
`firmware/device.env` (optional, for `provision.py`) is git-ignored.

## Venice route

Your voice and text go to Venice over HTTPS. The Venice key is sent only over
https, to the configured host (`api.venice.ai` by default); the setup page
refuses a plain `http://` host unless it is a local one. Certificates are
checked against a pinned root certificate, but their expiry dates are not
checked by this build.

## Hermes route

**Keys travel in the clear.** The device talks to the Hermes gateway over plain
HTTP on your LAN and sends the agent's API key with every request, so anyone
who can capture traffic on that network can read it. Limit the gateway port
(8642) with a firewall rule on the Hermes machine so only the device's address
can reach it. TLS with a certificate pinned on the device would close this; it
is not built yet.

Pairing over USB (`host/pair.py`) sends no key over WiFi or to a screen. The QR
code from `--no-usb` carries the profile's API key: don't share it.
