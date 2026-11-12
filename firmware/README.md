# Blipper firmware: Garmin Edge page-turn remote (nice!nano v2)

A two-button remote that turns data pages on a Garmin Edge 840 (and other Edges
that accept the Garmin Edge Remote).

## Why ANT+ instead of Bluetooth

Garmin Edges don't accept Bluetooth HID (keyboard / media-remote) devices. Page
turning from a remote goes through the **ANT+ Controls** profile, the one the
Garmin Edge Remote uses:

- The Edge is the channel master (device type 16, 2457 MHz, 4 Hz).
- The remote is the slave. On each press it sends **Generic Command data page 73**
  as an acknowledged message. Menu Down (`0x0001`) / Menu Up (`0x0000`) turn the
  page. The open-source TSDZ2 wireless remote uses the same mechanism on Edge
  530/830.

The nRF52840 can do ANT, but only with Nordic's **S340** SoftDevice (ANT + BLE).
The nice!nano ships with S140 (BLE only) plus a UF2 bootloader, so this firmware
is flashed over **SWD** and replaces both.

## Wiring

| Function | nice!nano pin | nRF pin |
|---|---|---|
| Button A: next page (short), lap (long, ≥0.7 s) | `017` (D2) | P0.17 |
| Button B: previous page | `020` (D3) | P0.20 |
| Status LED | on-board blue | P0.15 |

Wire each button from the pin to **GND**. No resistors are needed because the
internal pull-ups are on. Pins and mappings are set in [src/config.h](src/config.h).

## What you need

1. **ARM GCC**: [Arm GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads)
   (`arm-none-eabi-gcc`), plus `make` (Git-Bash/MSYS2 is fine on Windows).
2. **nRF5 SDK 17.1.0** from Nordic. Only the MDK startup/system files and the
   CMSIS headers are used. Unzip it next to this repo, or pass `SDK_ROOT=...`.
3. **S340 SoftDevice for nRF52840 (v7.0.1)** from
   [thisisant.com](https://www.thisisant.com/developer/components/nrf52832/)
   (free account, accept the ANT license). Copy its headers into
   `$(SDK_ROOT)/components/softdevice/s340/headers` (with the `nrf52`
   subfolder) and the `.hex` into `.../s340/hex/`. You can also point
   `S340_INC=` / `S340_HEX=` at them.
4. **Keys**: copy `src/keys.h.example` to `src/keys.h` and fill in:
   - `ANT_LICENSE_KEY`: the evaluation key is in a comment in the S340
     `nrf_sdm.h`. It's for non-commercial use only.
   - `ANTPLUS_NETWORK_KEY`: free after you sign up as an ANT+ Adopter on
     thisisant.com.
5. **An SWD probe**: J-Link / nRF52-DK (`nrfjprog`), or a Raspberry Pi Pico
   running *debugprobe* / any CMSIS-DAP probe (`pip install pyocd`). Solder to
   the SWDIO / SWCLK / GND pads on the back of the nice!nano.

## Build and flash

```bash
make SDK_ROOT=/path/to/nRF5_SDK_17.1.0_ddde560
make flash          # J-Link: erase all, S340, app
make flash-pyocd    # CMSIS-DAP / Pico debugprobe
make flash-app      # later updates, S340 already present
```

If the chip reports it is protected (newer nRF52840 revisions), run
`nrfjprog --recover` once (or `pyocd erase --chip`), then flash again.

On the first boot the firmware sets the nice!nano 3.3 V rail (UICR REGOUT0) and
resets itself once. The chip erase puts that setting back to 1.8 V, which is too
low to light the blue LED.

## Pairing with the Edge 840

1. Press a button on the remote so it's awake. The LED gives a short blink every
   2 s while it searches.
2. On the Edge: **Menu → Sensors → Add Sensor → Search All** (or the
   Remote/Controls category).
3. When the remote locks on, the LED double-blinks. Save the sensor.
4. Ride screen: button A shows the next page, button B the previous one.

## LED codes

| Pattern | Meaning |
|---|---|
| 1 long blink | boot / wake |
| short blink every 2 s | searching for the Edge |
| double blink | connected to the Edge |
| 1 quick blink | command acknowledged by the Edge |
| 3 quick blinks | command failed (Edge not found within 5 s, or no ACK after 3 retries) |

## Power

- While the remote is connected to the Edge it stays awake (ANT slave at 4 Hz).
- When the Edge is gone (turned off, out of range), the remote searches for 30 s.
  After 2 minutes without the Edge it goes to **System OFF** (~2 µA).
- Any button press wakes it up. It reboots, searches again, and sends the press
  once connected. A quick tap made while asleep usually only wakes it, so press
  again once the LED shows it's searching.

## Tuning

Everything is in [src/config.h](src/config.h):

- If the pages scroll the wrong way, swap `CMD_MENU_DOWN` / `CMD_MENU_UP`.
- `BTN_A_LONG = CMD_NONE` makes button A fire as soon as you press it (no
  long-press delay).
- Other commands to try: `CMD_LAP`, `CMD_START`, `CMD_STOP`, `CMD_MENU_SELECT`,
  `CMD_MENU_BACK`.

## Status / caveats

- **Not yet compiled or tested on hardware.** No ARM toolchain or S340 package
  was available when it was written. Expect to fix small compile issues on the
  first build.
- Menu Up/Down for page turning is confirmed on Edge 530/830 by the TSDZ2
  project, not yet on the 840. Lap via a generic remote isn't verified.
- The app start address assumes S340 occupies flash up to `0x31000` (true for
  S340 6.1.1 / 7.0.1). Check the release notes of your S340 version and adjust
  [blipper_s340.ld](blipper_s340.ld) if it differs.
- To go back to stock nice!nano (ZMK etc.), flash the nice!nano bootloader
  `.hex` (with S140) over SWD.
- The same firmware should port to the Blipper nRF52832 board with S332, a
  different linker script, and `NRF52832_XXAA`.
