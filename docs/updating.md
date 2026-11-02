# Updating

## Over USB

Flash again the same way (browser installer or `pio … -t upload`, see
[Flashing](flashing.md)) and **do not erase**: WiFi networks, keys, agents, the
Venice key, voice, theme and font all live in a separate settings partition
that an upload keeps. A device that was set up with an older firmware may
forget its WiFi once when it first runs a newer one (it asks again in setup
mode; keys and agents are kept).

Venice devices, browser-installer images and unsigned builds update over USB.
Downgrades also go over USB.

A USB flash writes the first app slot only. A device that has taken a WiFi
update boots from the second slot (`0x650000`) and ignores the fresh image;
the serial line `[OTA] running <version>` tells which one runs. Write the image
to that address as well:

```sh
esptool write_flash 0x650000 firmware/.pio/build/<board>/firmware.bin
```

## Over WiFi (Hermes route, optional)

A signed build can be pulled from the speech server at boot. It exists only on
the Hermes route (a Venice device has no speech server to ask), and only for
builds you sign and publish yourself.

After each healthy boot (the boot check passed) the device asks the speech
server for `/firmware/manifest.json`. The speech server is not trusted: every
manifest is signed with an ECDSA P-256 key that only you hold, and a device
installs an image only when

1. the manifest is at most 512 bytes and has exactly the members `build`,
   `version`, `size`, `sha256`, `sig`;
2. `sig` verifies, over `chirp-ota-v1|<build>|<version>|<size>|<sha256>`,
   against the public key compiled into the firmware it is running;
3. `build` (unix seconds at build time) is higher than both the running image's
   build and the highest build it ever installed, so an old signed manifest
   cannot roll a device back;
4. it is not the image whose install failed last (until the manifest changes or
   the device loses power);
5. the downloaded image fits the spare app slot, finishes within 120 s, and its
   SHA-256 matches the signed manifest — checked before the slot is activated.

A failed or corrupt download changes nothing, settings are never touched, and
USB stays the recovery path. A new image is marked good as soon as it joins
WiFi, so Hermes or the speech server being down never rolls a working image
back; an image that crashes first, or never joins WiFi, rolls back.

Set up signing once, then publish each build to every device on the network:

```sh
host/publish-firmware.py --init-key  # private key: ~/.config/chirp/ota-signing.pem (0600);
                                     # public key: firmware/ota_pub.pem (gitignored)
pio run -d firmware                  # embeds ota_pub.pem + a build number; FW_VERSION = git describe
# flash this build over USB once: devices only trust the key in the image they run
host/publish-firmware.py             # signs and publishes → ~/.local/share/chirp/firmware/
```

Back up the private key and never share it: losing it means re-flashing every
device over USB, and anyone holding it can push firmware to your devices. The
publisher refuses to run without it, and refuses an image that does not embed
its public half. A build made without `firmware/ota_pub.pem` (release builds,
for example) still works but never updates over WiFi (the device logs `[OTA] no
signing key in this build; updates off`). The publisher needs the
`cryptography` package.
