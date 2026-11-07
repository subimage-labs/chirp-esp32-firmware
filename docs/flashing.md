# Flashing

Flash the device once over USB. After that, the settings stay across updates. See [Updating](updating.md).

## What you need

- A **T-Embed CC1101 Plus** or a **T-Deck**.
- A USB-C cable that carries data. A charge-only cable does not work.
- For the T-Embed browser installer: Chrome or Edge on a desktop computer.
- To build the firmware yourself, and for the T-Deck: Git and [PlatformIO](https://platformio.org/install/cli). Install PlatformIO with `pip install platformio`.
- For the T-Deck: `openocd-esp32` from Espressif. See [T-Deck over USB-JTAG](#t-deck-over-usb-jtag).
- Releases are built with Python 3.11. If your system Python is much newer, use Python 3.10 to 3.12 for PlatformIO.
- Internet access. The first build downloads the ESP32 toolchain and the libraries. It takes a few minutes.

Clone the repository:

```sh
git clone <repo-url> chirp-esp32-firmware
cd chirp-esp32-firmware
```

For the Hermes route, clone the repository on the Hermes machine.

## T-Embed from the browser (no build tools)

1. Download the `flasher` artifact of the latest **firmware-release** run (GitHub, then Actions).
2. Unpack the artifact and serve it on your desktop computer:

   ```sh
   unzip flasher.zip -d flasher && cd flasher && python3 -m http.server   # open http://localhost:8000
   ```

3. In Chrome or Edge, plug the device in.
4. Click **Install**.
5. Pick the port of the device. If no port shows, hold **BOOT** and plug the cable in again.
6. Do not tick **Erase device**. An erase removes the saved settings. A new device does not need an erase.

The installer has T-Embed images only. Its builds do not update over WiFi. See [Updating](updating.md).

## T-Embed with PlatformIO

Always name the board. Without `-e`, `upload` flashes the image of every board in turn.

```sh
pio run -d firmware -e t-embed-cc1101 -t upload
```

PlatformIO finds the port by itself. If it does not, add `--upload-port /dev/ttyACM1`. The port name changes after a reset.

If the upload cannot connect, put the board in download mode: hold **BOOT** and plug the cable in.

On Linux, do these steps before you flash:

1. Stop `ModemManager`: `sudo systemctl stop ModemManager`.
2. Make sure your user can use the serial port. On most distributions, add the user to the group `dialout`.

After the flash, a device with no settings restarts into its **setup screen**. Continue with [First setup](first-setup.md).

## T-Deck over USB-JTAG

The ESP32-S3 ROM loader of the T-Deck never answers `esptool` or Web Serial, in any download mode. So `pio ... -t upload` and the browser installer do not work on it. Flash the T-Deck over USB-JTAG with `openocd-esp32` from Espressif.

The USB port of the T-Deck is the built-in USB-JTAG interface of the chip (USB ID `303a:1001`). You do not need extra hardware.

1. Install `openocd-esp32`. This is the OpenOCD build from Espressif with ESP32-S3 support.
2. On Linux, let your user open the USB device. Create `/etc/udev/rules.d/60-esp32-jtag.rules` with this line:

   ```
   SUBSYSTEM=="usb", ATTR{idVendor}=="303a", ATTR{idProduct}=="1001", MODE="0660", TAG+="uaccess", ENV{ID_MM_DEVICE_IGNORE}="1"
   ```

   The `ID_MM_DEVICE_IGNORE` setting stops ModemManager from taking the port.
3. Reload the rules: `sudo udevadm control --reload && sudo udevadm trigger`.
4. Plug the T-Deck in again.
5. Build the firmware so that the flash images exist:

   ```sh
   pio run -d firmware -e t-deck
   ```

6. Find the four image files:
   - `bootloader.bin`, `partitions.bin` and `firmware.bin` are in `firmware/.pio/build/t-deck/`.
   - `boot_app0.bin` is in `framework-arduinoespressif32/tools/partitions/`, inside your PlatformIO packages folder. The default folder is `~/.platformio/packages/`.
7. Flash the four images. Run the command in the folder that holds them, or use full paths:

   ```sh
   openocd -f interface/esp_usb_jtag.cfg \
     -c 'set ESP32S3 1' -f target/esp32s3.cfg \
     -c "init; reset halt; \
         program_esp bootloader.bin 0x0 verify; \
         program_esp partitions.bin 0x8000 verify; \
         program_esp boot_app0.bin 0xe000 verify; \
         program_esp firmware.bin 0x10000 verify; \
         reset run; shutdown"
   ```

If two boards are plugged in, pin the USB location of the T-Deck. Then OpenOCD does not touch the other board. Add `-c 'adapter usb location <bus>-<port>'` before the `-c 'set ESP32S3 1'` option. Use `lsusb -t` to find the bus and the port.

The device restarts into its setup screen. If it already has settings, it restarts into the chat.

## Clean start

A full erase removes every setting and returns the device to its setup screen. On the T-Embed, use one of these methods:

- Tick *Erase device* in the browser installer.
- Run `pio run -d firmware -e t-embed-cc1101 -t erase`.

The T-Deck has no tested full-erase method yet.
