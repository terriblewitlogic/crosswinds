# Xteink X4 hardware and build notes

The game targets one device, the Xteink X4: an ESP32-C3 (no PSRAM, 16 MB flash) driving a
4.26" 800x480 GDEQ0426T82 e-paper panel through an SSD1677 controller. It is played in
landscape. Turn the reader anticlockwise from upright so the four front buttons run down
the right edge of the screen and the rocker sits along the top edge.

## Build and flash

Requirements: `arduino-cli` with the Espressif `esp32` core 3.x (built here with 3.3.10), and
LovyanGFX 1.2.24, which includes the `Panel_SSD1677` driver. `make -C tools/host deps` clones
it into `tools/host/vendor`, where the Makefile looks first. Set `LGFX=/path/to/LovyanGFX` to
use another copy.

```sh
make device-x4                            # compile; output in tools/device_build/x4
make backup-x4 PORT=/dev/cu.usbmodem...   # full 16 MB flash backup
make upload-x4 PORT=/dev/cu.usbmodem...   # compile, back up, upload, verify
```

`upload-x4` stops unless the compile succeeds and a full backup is written. After uploading it
reads the app back and compares it with the build. Backups go to
`tools/device_backups/<timestamp>/` with a SHA-256 manifest and are excluded from git.

The board options matter:

| Option | Value | Why |
| --- | --- | --- |
| Board | `esp32:esp32:esp32c3` | ESP32-C3 Dev Module |
| `CDCOnBoot` | `cdc` | the X4 exposes the C3's native USB, so Serial is USB CDC |
| `FlashMode` | `dio` | GPIO12 and GPIO13 are the SD chip select and the battery latch, which QIO would claim |
| `FlashSize` | `16M` | the X4 has 16 MB |
| `PartitionScheme` | `huge_app` | a 3 MB app slot and a 896 KB LittleFS partition for saves |
| `compiler.cpp.extra_flags` | `-DSH_X4_VOYAGE_STANDBY=1 -DSH_X4_STANDBY_DIAGNOSTICS=1` | enable [voyage standby](STANDBY.md) and its retained diagnostics (`X4_BUILD_FLAGS` in the Makefile) |

Uploading writes the bootloader, the partition table and the app, so the device no longer has the
layout it shipped with. Keep the backup. Writing it back at offset 0 (`esptool write-flash 0x0
flash.bin`) should restore the device, but that has not been tried with one of these backups.

## Saves

Saves live in the internal LittleFS partition, not on the SD card, whose chip select is held high.
The save format is version 15. A complete company is written as one snapshot,
`/sh_company.sav`, from five CRC-protected components, and the previous generation is kept as
`/sh_company.bak`. Older saves load and migrate. A save that cannot be read is renamed with an
`.orphan` suffix before a new company starts, and a failed filesystem mount never formats the flash.
Normal app uploads do not erase LittleFS. The one exception on the game's side: if the partition
is completely blank (a device that never held saves) it is formatted at first start. A partition
holding anything else that will not mount is left alone and the game runs without saving, so a
device coming from other firmware may need `esptool erase-region 0x310000 0xE0000` first.

## Pin map

| Function | GPIO | Notes |
| --- | --- | --- |
| EPD SCLK / MOSI | 8 / 10 | SPI2 at 20 MHz, the SSD1677 write maximum |
| EPD CS / DC / RST / BUSY | 21 / 4 / 5 / 6 | BUSY is active high |
| SD CS / MISO | 12 / 7 | not used by the game; CS is held high |
| Battery latch | 13 | driven high at boot to keep the battery rail on; low powers off |
| Front buttons | 1 (ADC) | resistor ladder: BACK, CONFIRM, LEFT, RIGHT |
| Rocker | 2 (ADC) | resistor ladder: UP, DOWN |
| POWER | 3 | active low, deep-sleep wake source |
| Battery voltage | 0 (ADC) | half the cell voltage |
| USB detect | 20 | high while USB is connected |

Sources: the Adafruit "CircuitPython on the Xteink X4" pinout guide, the FreeInk SDK board profile
used by CrossPoint, and the xteink-x4-sample sketch.

The ladders are decoded in `decode_front` and `decode_side` in `board_x4.cpp`. On the 12-bit
readings, the front buttons split at 3800 (none), 3100 (BACK), 2090 (CONFIRM) and 750 (LEFT), with
anything lower being RIGHT. The rocker splits at 3300 (none) and 1120 (UP), with anything lower
being DOWN. If a unit's buttons misread, start there.

## Controls

| Physical button | Game action |
| --- | --- |
| Front column, top to bottom: RIGHT, LEFT, CONFIRM, BACK | UP, DOWN, OK, BACK |
| Rocker UP / DOWN | shortcuts: previous or next port, ship, page or tab. The top bar shows what they do on the current screen |
| POWER | save and power off, or voyage standby while a ship is at sea |

UP and DOWN move a cursor through everything on the screen, OK activates, and BACK closes a card;
at the top level it opens the ship's bell menu. Presses never auto-repeat, because the display
refresh blocks the main loop. A dedicated sampler reads the buttons every 10 ms, debounces them
for 20 ms, and queues presses while the screen redraws. The game handles one per frame.

`X4_ACTION_OF_BUTTON` in `x4.h` maps buttons to actions. If a unit's front buttons read in a
different order, edit it and `X4_LEGEND_ORDER`; the title screen's legend highlights whichever
physical button was pressed last, which makes a wrong entry obvious. To hold the reader the other
way round, change `DISPLAY_ROTATION` in `lgfx_config.h` from 0 to 2 and reverse `X4_LEGEND_ORDER`.

## Display

`render_x4.cpp` draws straight into the panel driver's buffer in pure black and white. There is no
separate framebuffer, because the C3 has no PSRAM. The bezel hides about 9 px of the left edge and
3 px of the others (CrossPoint's measured inset), so everything sits inside a safe area with a
16 px left margin and 6 to 8 px elsewhere.

The renderer redraws only when a hash of the visible state changes.

- A button press or a structural change (a card, scene, notice, incident or arrival) refreshes at once.
- Simulation-only changes, such as ships moving or the clock, are rate limited.
- The title and first playable screen get a full half refresh.
- A full half refresh clears ghosting after 30 partial refreshes. It waits for the next change of
  screen so the panel does not flash under a still image, and is forced after 150.

## Power

- `board_x4_begin_early()` runs first in `setup()`. It latches GPIO13 high so the board stays on
  after the power button is released, and parks the SD chip select. Power presses during startup
  are ignored, and a fresh press needs a stable release first.
- POWER saves, shows a sleep screen, drops the battery latch, and on USB enters deep sleep with
  the power button armed as the wake source. While a ship is at sea, a short press enters
  [voyage standby](STANDBY.md) instead. Holding POWER for two seconds always shuts down.
- The game powers off after 15 minutes without input when every ship is in port or the game is
  paused, and after 3 hours while a voyage is under way, which enters standby.
- On battery, a LOW badge and a notice appear at 10%, and at 3% the game saves and powers off.
  USB power cancels both.
- The top bar shows the battery percentage and a lightning mark while USB is connected.

The X4 has no LED or speaker, so there is no sound or signal feedback.

## Desktop preview and tests

The same game runs in an 800x480 SDL window. The tests drive it with scripted presses and check
saves, navigation and the simulation. They need `pkg-config` and SDL2.

```sh
make play-x4          # open the game in a window
make gallery-x4       # capture every scene and card as PPM frames
make x4-test          # merchant journey, navigation, Watch, goal moments, story and atlas
make headless-test    # simulation and economy checks without a display
make verify           # everything
```

Keys emulate the buttons. Right and Left arrows are the top two front buttons (UP and DOWN in the
game), Return or Space is OK, Backspace or Escape is BACK, Up and Down arrows are the rocker, and
`P` is POWER. `SH_KEYS="ms:K;..."` scripts presses with U, D, L, R, O, B and P, and `SH_BATTERY=42`
and `SH_USB=1` fake the gauge. Mouse clicks still work as taps.
