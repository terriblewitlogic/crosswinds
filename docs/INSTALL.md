# Install prebuilt firmware

For the Xteink X4 with USB flashing unlocked. No Arduino tools or source checkout required.

## Download

Download the files from [Crosswinds 2026.10.0](https://github.com/terriblewitlogic/crosswinds/releases/tag/2026.10.0):

| File | Use |
| --- | --- |
| `crosswinds-x4-2026.10.0-full.bin` | First install. Replaces all 16 MB of flash, including firmware and saves. |
| `crosswinds-x4-2026.10.0-app.bin` | Update a device already running standalone Crosswinds. Keeps saves. |
| `crosswinds-x4-2026.10.0-sha256.txt` | SHA-256 checksums for both files. |

Check the downloads against the SHA-256 checksums before flashing. The CrossPoint Reader version uses different firmware; do not install the app-only image over CrossPoint or stock firmware.

## Set up the flashing tool

Install Python, then create a virtual environment:

```sh
python3 -m venv .venv
source .venv/bin/activate
python -m pip install esptool==5.3.0
```

On Windows, use `python -m venv .venv` and activate with `.venv\Scripts\Activate.ps1` in PowerShell.

Connect the X4 using a USB data cable. Run the following commands from the folder containing the downloads. Replace `PORT` with the device's serial port, such as `/dev/cu.usbmodem1101` on macOS, `/dev/ttyACM0` on Linux, or `COM3` on Windows.

## Back up

Use a new filename for each backup.

```sh
python -m esptool --chip esp32c3 --port PORT --baud 921600 --after no-reset read-flash 0x0 0x1000000 x4-backup.bin
python -m esptool --chip esp32c3 --port PORT --baud 921600 verify-flash 0x0 x4-backup.bin
```

The backup must be 16,777,216 bytes. Continue only after both commands succeed; move the backup somewhere safe before installing.

## First install

**This replaces the reader firmware and deletes existing saves.**

```sh
python -m esptool --chip esp32c3 --port PORT --baud 921600 --after no-reset write-flash 0x0 crosswinds-x4-2026.10.0-full.bin
python -m esptool --chip esp32c3 --port PORT --baud 921600 verify-flash 0x0 crosswinds-x4-2026.10.0-full.bin
```

The full image includes blank save storage; no separate erase command is needed. `--after no-reset` keeps the game from starting and changing saves before verification. Restart the X4 after verification if it does not start automatically.

## Update an existing Crosswinds installation

Back up first, then flash only the app to keep saves:

```sh
python -m esptool --chip esp32c3 --port PORT --baud 921600 --after no-reset write-flash 0x10000 crosswinds-x4-2026.10.0-app.bin
python -m esptool --chip esp32c3 --port PORT --baud 921600 verify-flash 0x10000 crosswinds-x4-2026.10.0-app.bin
```

Restart the X4 after verification if it does not start automatically.

## Restore a backup

Restoration has not been tested on hardware. A complete backup can be written back to the same device at offset `0x0`:

```sh
python -m esptool --chip esp32c3 --port PORT --baud 921600 write-flash 0x0 x4-backup.bin
```

[Hardware notes](HARDWARE.md) · [License](../LICENSE) · [Credits](../NOTICE)
