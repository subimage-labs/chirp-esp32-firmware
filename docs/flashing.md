# Flashing

You flash the device once over USB. After that, settings are kept across
updates (see [Updating](updating.md)).

## What you need

- A **T-Embed CC1101 Plus** or a **T-Deck**, and a USB-C cable that carries
  data (charge-only cables do not work).
- Chrome or Edge on a desktop for the T-Embed's browser installer. For the
  T-Deck, or if you prefer to build yourself: Git and
  [PlatformIO](https://platformio.org/install/cli) (`pip install platformio`;
  releases are built with Python 3.11, so use 3.10–3.12 if your system Python
  is much newer). The first build downloads the ESP32 toolchain and libraries,
  so it needs internet and a few minutes.
- Access to the repository and its `tdeck-venice` branch, which holds the
  T-Deck and Venice support:

```sh
git clone --branch tdeck-venice <repo-url> chirp-esp32-firmware
cd chirp-esp32-firmware
```

For the Hermes route, clone it on the Hermes machine.

## T-Embed from the browser (no build tools)

Download the `flasher` artifact of the latest **firmware-release** run
(GitHub → Actions), unpack it and serve it on your desktop:

```sh
unzip flasher.zip -d flasher && cd flasher && python3 -m http.server   # open http://localhost:8000
```

In Chrome or Edge, plug the device in, click **Install** and pick its port.
If no port shows up, hold **BOOT** while plugging the cable in. Leave **Erase
device** unticked: erasing wipes the saved settings (a new device does not
need it). The installer carries **T-Embed images only**, and its builds do not
update over WiFi (see [Updating](updating.md)).

## Either board, with PlatformIO

Always name the board; without `-e`, `upload` flashes every board's image in
turn:

```sh
pio run -d firmware -e t-embed-cc1101 -t upload     # T-Embed CC1101 Plus
pio run -d firmware -e t-deck -t upload             # T-Deck
```

The port is found automatically. Add `--upload-port /dev/ttyACM1` if it is
not (the name changes after a reset). If the upload cannot connect, put the
board in download mode:

- **T-Embed:** hold **BOOT** while plugging the cable in.
- **T-Deck:** hold the trackball click, tap the board's reset button, release.
  The screen stays dark.

On Linux, stop `ModemManager` while you flash (`sudo systemctl stop
ModemManager`) and make sure your user may use the serial port (group
`dialout` on most distributions).

After flashing, a device with no settings restarts into its **setup screen**:
continue with [First setup](first-setup.md).

## T-Deck that will not flash over serial

On some T-Deck units the ESP32-S3 ROM loader never answers `esptool` or Web
Serial, in any download mode. If yours behaves that way, flash over USB-JTAG
with Espressif's `openocd-esp32`. The T-Deck's USB port is the chip's built-in
USB-JTAG interface (USB ID `303a:1001`), so no extra hardware is needed.

1. **Install `openocd-esp32`**, Espressif's OpenOCD build with ESP32-S3
   support.
2. **Let your user open the USB device (Linux).** Create
   `/etc/udev/rules.d/60-esp32-jtag.rules` with:

   ```
   SUBSYSTEM=="usb", ATTR{idVendor}=="303a", ATTR{idProduct}=="1001", MODE="0660", TAG+="uaccess", ENV{ID_MM_DEVICE_IGNORE}="1"
   ```

   The `ID_MM_DEVICE_IGNORE` part keeps ModemManager from grabbing the port.
   Reload with `sudo udevadm control --reload && sudo udevadm trigger`, then
   replug the T-Deck.
3. **Build the firmware** so the flash images exist:

   ```sh
   pio run -d firmware -e t-deck
   ```

   You need four files: `bootloader.bin`, `partitions.bin` and `firmware.bin`
   in `firmware/.pio/build/t-deck/`, and `boot_app0.bin` in
   `framework-arduinoespressif32/tools/partitions/` inside your PlatformIO
   packages folder (`~/.platformio/packages/` by default).
4. **Flash the four images.** Run this from the folder that holds them (or use
   full paths):

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

   If two boards are plugged in, pin the T-Deck's USB location so the other
   board is never touched: add `-c 'adapter usb location <bus>-<port>'`
   before the `-c 'set ESP32S3 1'` option (find the bus and port with
   `lsusb -t`).

The device restarts into its setup screen (or its chat, if it already has
settings).

## Clean start

A full erase wipes every setting and returns the device to its setup screen:
tick *Erase device* in the browser installer, or run
`pio run -d firmware -e <board> -t erase`.
