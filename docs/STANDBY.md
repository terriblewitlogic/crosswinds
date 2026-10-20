# Voyage standby

Voyage standby lets ships finish their legs while the reader is asleep. It is implemented and
covered by host tests, and the X4 build enables it (`SH_X4_VOYAGE_STANDBY=1`, set by the Makefile;
the header default is `0`). Progress made during standby has been seen on a real reader. Battery
draw, the battery latch at cutoff and RTC drift have not been measured, so this document makes no
battery-life claim.

## Player behavior

Waking completes startup even when Power has already been released. The wake
gesture, including holds and extra presses during the initial full refresh,
cannot also request sleep. After the first screen is ready, Power must remain
released for 80ms before a fresh press is accepted. Ordinary navigation is
ready with that first screen.

With the gate explicitly disabled in a custom build, POWER performs a true
shutdown. Voyages do not advance while power is off.

In the current X4 build, a short POWER press while an unpaused ship sails
enters voyage standby. Holding POWER for two seconds requests true shutdown.
A paused company or one with no sailing ships uses true shutdown. A battery
at or below the existing 3% threshold also uses true shutdown. A voluntary
shutdown with a failed save stays awake and preserves the working company;
a depleted battery or an unrecoverable save read still shuts down after the
best available recovery attempt.

Standby completes ships already dispatched. It never dispatches another
voyage. Booked orders use the same settlement rules as awake play. Existing
legacy decisions remain waiting for the player. Once no ship is sailing,
the timer stops and the reader remains in deep sleep with its power latch held.
It does not automatically cut battery power or change the standby picture at
arrival. A subsequent POWER press after waking, with no sailing ships, performs
full shutdown. The e-paper image remains visible even after full shutdown;
a blank screen is not a shutdown test.

## Power and clock

GPIO13 stays HIGH in standby, and GPIO12 keeps the shared SD chip select
inactive. Both use GPIO holds. Wake restores the output registers before
releasing holds, preventing the latch from briefly dropping LOW. Standby
wakes on POWER (GPIO3) or on a timer set to the earliest sailing leg's arrival, capped at 15
minutes. Timer boots load and commit
the company without initializing or refreshing the display. Normal user
boots keep the existing full refreshes of the title and first menu.

The installed ESP32-C3 core uses `CONFIG_LIBC_TIME_SYSCALL_USE_RTC_HRT`.
`gettimeofday()` therefore supplies elapsed time through deep sleep; the
high-resolution `esp_timer` alone would reset on deep wake. The RTC's default
internal oscillator can drift with temperature. No network clock, calendar
date or host wall clock is used to award absence time. These properties come
from Espressif's [system-time documentation](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/api-reference/system/system_time.html),
[ESP Timer documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/api-reference/system/esp_timer.html),
and [GPIO hold documentation](https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32c3/api-reference/peripherals/gpio.html).

## Commit and replay rules

Before sleep, the active clock is reconciled and a complete company
snapshot records a new session identifier and zero accounted seconds. Only
after that commit succeeds is an `RTC_NOINIT_ATTR` record installed with the
session, company seed, RTC origin, format marker and checksum.

Each wake validates the record against the saved company. Deep-sleep,
software, panic and watchdog resets may retain the clock. Power-on,
brownout, power glitch, USB/JTAG and unknown resets do not qualify. A
backward clock, mismatched session/company, corrupted record or elapsed time
over seven days is refused; the game resumes saved progress with an explicit
clock notice. A completed standby has already cleared its saved session, so
returning later cannot produce additional income.

The difference between RTC elapsed seconds and the saved watermark is
reconciled in chunks no larger than 15 real minutes. Internal simulation
saves are deferred. The outer save atomically commits the new watermark,
coins, cargo, delivered units and all other company state. A crash before
commit replays from the previous complete checkpoint; a crash after commit
finds no already-accounted time to repeat. User wake closes the session in
another complete snapshot before starting the awake clock.

A persistent returned save error reloads the last coherent snapshot,
abandons uncommitted elapsed time and reports that saved progress was kept.
It does not expose an unsaved arrival payout to interactive play. If even
the coherent reload fails, the disk records are preserved and the reader
shuts down rather than saving partially loaded RAM.

## Gated diagnostic capture

`SH_X4_STANDBY_DIAGNOSTICS` is a separate flag, also defaulting to `0`.
The diagnostic test build uses a bounded, checksummed RTC ring to record
timer versus interactive reconciliation, trusted elapsed time, completed or
failed snapshot writes, and wake/GPIO setup results. It adds no company
fields, save operations, or wakeups. Normal firmware has no diagnostic
allocation or serial interface.

After an interactive wake, the diagnostic code waits for USB Serial to be
connected before dumping retained records. USB can therefore be attached
after the battery test rather than during a fleeting timer boot. Uppercase
`D` repeats the dump without resetting the reader or issuing game inputs.
Diagnostic dumping never delays a background timer boot.

Accepted GPIO setup in a record is software evidence, not a measurement of the battery rail or
current.

## Validation

`tools/headless/x4_standby_test.cpp` tests elapsed arithmetic, repeated wake,
the atomic crash model, invalid clock/session data and wake scheduling under
UBSan. `SH_X4_STANDBY_TEST=1` in the host uses real board reconciliation,
booked voyages and real save files. It exercises partial travel, reset before
the outer commit, exactly one payment and delivered-unit award, waiting
arrivals, pause, legacy decisions, unavailable clocks, injected snapshot
write failure, voluntary shutdown refusal and depleted-battery shutdown.
These are simulation and persistence checks, not hardware measurements.

Hardware checks still open:

1. Back up the saved company and install the opt-in build. Confirm the
   ordinary startup stamps and button sampler still work.
2. On battery, dispatch a short booked voyage, tap POWER, release it, and
   verify GPIO13 remains HIGH across sleep and automatic wake. A USB-only
   test cannot establish battery-latch retention.
3. Measure actual standby current and elapsed time against an independent
   clock through several timer/user wakes and temperature conditions. Record
   results; do not substitute the ESP32 chip's advertised current for the
   complete reader's consumption.
4. Confirm arrival cash and delivered units once, then repeatedly wake/reset
   and verify the same saved receipt cannot pay again. Test two ships with
   different arrival times and a legacy waiting incident.
5. Verify long POWER truly shuts down, loses no committed assets, and awards
   no time while off. Exercise the depleted-battery branch with an appropriate
   controlled hardware setup; avoid intentionally corrupting the user's save.
