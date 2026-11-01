# Standalone Crosswinds for X4 Pro

Preview for the ESP32-S3 Xteink X4 Pro (16 MB flash, 8 MB PSRAM).
This replaces the reader with the game. For CrossPoint with Crosswinds, use
[crosspoint-crosswinds](https://github.com/terriblewitlogic/crosspoint-crosswinds).

## Install

Download the X4 Pro files from [2026.11.0rc](https://github.com/terriblewitlogic/crosswinds/releases/tag/2026.11.0rc).
Check them against `crosswinds-x4pro-2026.11.0rc-sha256.txt`.
USB flashing must be unlocked. No hardware installation or backup restoration has been tested.

Install [esptool 5.3.0](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/installation.html)
in a Python virtual environment. Connect a USB data cable and replace `PORT`
with the serial port (for example `/dev/cu.usbmodem1101`, `/dev/ttyACM0` or `COM3`).

Back up the complete flash before installing. Use a new filename each time:

```sh
python -m esptool --chip esp32s3 --port PORT --baud 921600 --after no-reset read-flash 0x0 0x1000000 x4pro-backup.bin
python -m esptool --chip esp32s3 --port PORT --baud 921600 verify-flash 0x0 x4pro-backup.bin
```

Keep the verified 16,777,216-byte backup somewhere safe.

**First install deletes existing firmware and saves.** The full image includes the bootloader,
partition table and blank save storage. Do not use an X4/C3 image on the Pro.

```sh
python -m esptool --chip esp32s3 --port PORT --baud 921600 --after no-reset write-flash 0x0 crosswinds-x4pro-2026.11.0rc-full.bin
python -m esptool --chip esp32s3 --port PORT --baud 921600 verify-flash 0x0 crosswinds-x4pro-2026.11.0rc-full.bin
```

Restart after verification. The game opens directly; no SD card is required.

For later updates to this standalone Pro firmware, back up first and write only
`app.bin` at `0x10000` to retain saves. Do not install the app-only image over stock
firmware or CrossPoint. To restore the original device, write its full backup at `0x0`.

## Controls

- Tap cards, rows and buttons. Use **− / +** to set a trade amount.
- Swipe lists to page; drag a zoomed chart to pan after release.
- Side keys change ports and pages. Home or a left-edge swipe returns.
- Swipe down from the top edge to cycle frontlight brightness in 25% steps.
- POWER saves and sleeps. The game also sleeps after five idle minutes.

Voyages pause during sleep. Saves use internal LittleFS; full-image installation resets them.
Frontlight brightness starts at 25% after each boot. Sound is disabled.

## Build and test

```sh
git clone --recurse-submodules https://github.com/terriblewitlogic/crosswinds.git
cd crosswinds
python -m pip install pioarduino==6.1.19
pio run -e x4pro
python tools/device/package_x4pro.py --tag local
```

The app and full images are written to `tools/device_build/x4pro`.
The SDK is pinned by the `freeink-sdk` submodule; update it explicitly.
`make play-x4pro` runs the desktop touch layout; `make x4pro-test` tests the first voyage,
trade confirmation, save reload and immediate repaint after a port upgrade.
The simulator does not exercise display, touch, frontlight or power hardware.

Before relying on this preview, check touch alignment at all screen edges, side keys,
Home/back, frontlight, save/sleep/wake, and a full buy–sail–sell journey on the device.
The sampler uses a 16-event queue and a 4 KB task stack so input can be recorded during
E-Ink refresh. Stack margin, battery draw and physical refresh behavior remain unmeasured.
