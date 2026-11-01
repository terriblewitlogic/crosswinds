# Crosswinds

A maritime trading game for the Xteink X4 e-reader. Trade goods, deliver parcels, and manage a fleet.

Runs as standalone firmware. A [CrossPoint Reader version](https://github.com/terriblewitlogic/crosspoint-crosswinds) is also available.

<table>
  <tr>
    <td><img src="docs/images/title.png" width="400" alt="Title screen"></td>
    <td><img src="docs/images/world-map.png" width="400" alt="The chart"></td>
  </tr>
  <tr>
    <td><img src="docs/images/market.png" width="400" alt="A market"></td>
    <td><img src="docs/images/incident.png" width="400" alt="An incident at sea"></td>
  </tr>
</table>

## Controls

Hold the reader in landscape with the four front buttons on the right.

| Button | Action |
| --- | --- |
| Front buttons, top to bottom | UP, DOWN, OK, BACK |
| Side rocker | Previous or next port, ship, page, or tab |
| POWER | Save and shut down; short press enters standby while a ship is sailing |

BACK at the top level opens the menu. Voyages continue during standby.

## Install

**Installation replaces the device firmware and partition table.** Keep a full flash backup. USB flashing must be unlocked.

Requires `arduino-cli`, Espressif `esp32` core 3.x (tested with 3.3.10), and LovyanGFX 1.2.24.

```sh
git clone https://github.com/terriblewitlogic/crosswinds.git
cd crosswinds
make -C tools/host deps
make upload-x4 PORT=/dev/cu.usbmodem...
```

Replace `PORT` with your device's serial port. Upload compiles, backs up the full flash, and verifies the installed firmware. A failed compile or backup stops installation.

[Hardware and recovery notes](docs/HARDWARE.md). Backup restoration has not been tested; standby battery draw and clock drift are unmeasured.

## Desktop

Requires `clang++`, `pkg-config`, SDL2, and LovyanGFX (`make -C tools/host deps`).

```sh
make play-x4    # run
make verify     # test
```

[MIT license](LICENSE) · [Credits](NOTICE) · [Art pipeline](docs/ART.md)

