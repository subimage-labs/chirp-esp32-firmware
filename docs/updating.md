# Updating

## Over USB

Flash the device again in the same way. Use the browser installer or `pio run -d firmware -e <board> -t upload`. See [Flashing](flashing.md). Do not erase the device.

The WiFi networks, the keys, the agents, the Venice key, the voice, the theme and the font are in a separate settings partition. An upload keeps this partition.

A device that was set up with an older firmware can lose its WiFi settings once, when it first runs a newer firmware. It asks for the WiFi again in setup mode. It keeps the keys and the agents.

These devices update over USB:

- Venice devices
- devices that have a browser-installer image
- devices that have an unsigned build

A downgrade also goes over USB.

A USB flash writes the first app slot only. A device that took a WiFi update boots from the second slot (`0x650000`). It ignores the new image. The serial line `[OTA] running <version>` shows which image runs. Write the image to the second slot as well:

```sh
esptool write_flash 0x650000 firmware/.pio/build/<board>/firmware.bin
```

## Over WiFi (Hermes route, optional)

The device can pull a signed build from the speech server at boot. This works only on the Hermes route, because a Venice device has no speech server. It works only for builds that you sign and publish yourself.

After each healthy boot (the boot check passed), the device asks the speech server for `/firmware/manifest.json`. The device does not trust the speech server. You sign every manifest with an ECDSA P-256 key. Only you hold this key. The device installs an image only when all of these conditions are true:

1. The manifest is at most 512 bytes. It has exactly these members: `build`, `version`, `size`, `sha256`, `sig`.
2. `sig` verifies against the public key in the running firmware. The signed text is `chirp-ota-v1|<build>|<version>|<size>|<sha256>`.
3. `build` (unix seconds at build time) is higher than the build of the running image. It is also higher than the highest build that the device ever installed. An old signed manifest cannot roll the device back.
4. The image is not the image whose install failed last. This stays true until the manifest changes or the device loses power.
5. The downloaded image fits the spare app slot, and the download ends within 120 s. The SHA-256 of the image matches the signed manifest. The device checks this before it activates the slot.

A failed or corrupt download changes nothing. The device never touches the settings. USB stays the recovery path.

The device marks a new image as good when it joins the WiFi. So an outage of Hermes or of the speech server never rolls back a working image. An image that crashes first, or never joins the WiFi, rolls back.

To set up signing, do these steps once. Then publish each build to every device on the network:

```sh
host/publish-firmware.py --init-key  # private key: ~/.config/chirp/ota-signing.pem (0600);
                                     # public key: firmware/ota_pub.pem (gitignored)
pio run -d firmware                  # embeds ota_pub.pem + a build number; FW_VERSION = git describe
# flash this build over USB once: devices only trust the key in the image they run
host/publish-firmware.py             # signs and publishes to ~/.local/share/chirp/firmware/
```

Back up the private key. Do not share it. If you lose the key, you must flash every device again over USB. A person who has the key can push firmware to your devices.

The publisher does not run without the key. It refuses an image that does not embed the public half of the key.

A build made without `firmware/ota_pub.pem` (a release build, for example) works, but it never updates over WiFi. The device logs `[OTA] no signing key in this build; updates off`.

The publisher needs the `cryptography` package.
