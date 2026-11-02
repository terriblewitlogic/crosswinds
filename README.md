# Crosswinds

A maritime trading game for Xteink X4 and X4 Pro e-readers. Trade goods, deliver parcels, and manage a fleet.

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

**X4:** hold the reader in landscape with the four front buttons on the right.

| Button | Action |
| --- | --- |
| Front buttons, top to bottom | UP, DOWN, OK, BACK |
| Side rocker | Previous or next port, ship, page, or tab |
| POWER | Save and shut down; short press enters standby while a ship is sailing |

BACK at the top level opens the menu. X4 voyages continue during standby.

**X4 Pro:** tap rows and buttons; use − / + for quantities. Side keys change ports and pages. Home or a left-edge swipe goes back. Swipe down from the top edge to cycle the frontlight. POWER saves and sleeps; voyages pause during sleep.

## Install

No build required. Download the image for your device:

| Device | Download | Install |
| --- | --- | --- |
| X4 · ESP32-C3 | [2026.10.0](https://github.com/terriblewitlogic/crosswinds/releases/tag/2026.10.0) | [USB instructions](docs/INSTALL.md) |
| X4 Pro · ESP32-S3 | [2026.11.0rc preview](https://github.com/terriblewitlogic/crosswinds/releases/tag/2026.11.0rc) | [USB instructions](docs/X4_PRO.md) |

Use `full.bin` for a first install; it replaces firmware and saves. Use `app.bin` only to update this standalone firmware on the same device. Saves use internal flash.

The Pro preview is build- and simulator-tested, not hardware-tested. It contains no CrossPoint reader. USB flashing must be unlocked.

## Desktop

Requires `clang++`, `pkg-config`, SDL2, and LovyanGFX (`make -C tools/host deps`).

```sh
make play-x4       # X4 buttons
make play-x4pro    # Pro touch layout
make verify        # X4 regression tests
make x4pro-test    # Pro touch journey
```

[Build from source](docs/HARDWARE.md#build-and-flash) · [MIT license](LICENSE) · [Credits](NOTICE) · [Art pipeline](docs/ART.md)

