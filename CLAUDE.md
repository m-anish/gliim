# CLAUDE.md

Guidance for Claude Code (claude.ai/code) when working in this repository.

## Project

glim is a joystick-controlled 3-channel LED dimmer. An **ATtiny814** drives three
**PT4115** constant-current LED drivers via hardware PWM, and reads a cheap analog
thumb joystick for control. Purpose: simple, screenless, easily-adjustable home
lighting. It's the small sibling of `lokki` (campus-scale LED automation) in the
`starstucklab` family.

Firmware is C++ on **megaTinyCore**, built with **PlatformIO**, flashed over
**serialUPDI**.

## The pin map is load-bearing — do not "simplify" it

LEDs are on **PB0/PB1/PB2**, joystick on **PA1/PA2** (X/Y) + **PA7** (SW), IR on
**PB3**, status pixel on **PA6**. Forced by silicon, not preference (datasheet
DS40001912A, Table 5-1):

- **PA1/PA2 cannot do PWM.** No timer reaches them. TCA0 WO0/1/2 are hardwired to
  PB0/1/2; WO3/4/5 to PA3/4/5. PA1/PA2 are ADC pins (AIN1/AIN2).
- **PB2/PB3 cannot do ADC.** Only PB0/PB1 reach the ADC on PORTB.
- **Only WO0–WO2 (PB0/1/2) exist in 16-bit normal mode** — see the PWM section.

If you're tempted to move a PWM channel to PA1/PA2 or an analog input to PB2/PB3,
it will silently not work. Re-read `docs/hardware.md` first. The full datasheet is
in `docs/`.

## PWM is bare-metal TCA0, not analogWrite()

`analogWrite()` can't drive TCA0 in normal mode, so `pwmInit()` in `src/main.cpp`:

- calls `takeOverTCA0()` (safe: `millis()` is on TCD0, so TCA0 is free);
- runs TCA0 in **normal (16-bit) mode**, single-slope, `PER = 65535`, and writes
  duty to the *buffered* `CMP0BUF/CMP1BUF/CMP2BUF` (PB0/PB1/PB2), higher =
  brighter, 305 Hz at 20 MHz (DIV1).

**The LEDs must stay on PB0/PB1/PB2.** Normal mode is the only way to get 16-bit,
and normal mode only has WO0–WO2, which are hardwired to PB0/PB1/PB2. Moving them
back to PA3/PA4/PA5 forces split mode and an 8-bit floor of 0.39% — 6.4× coarser
than the PT4115 can resolve (its floor is a ~2 µs on-time, i.e. 40 counts of
65536 at 305 Hz). That's the whole reason for the pin choice.

Knock-on effect: PB2 is USART0 TXD and its only alternate (PA1) is the joystick,
so there is no hardware serial. `GLIM_DEBUG` uses SoftwareSerial on PA4, and
**IR is auto-disabled in debug builds** (config.h forces it off) because
SoftwareSerial's dispatcher defines the PORT vectors the IR ISR needs.

The clock is **20 MHz** (in spec at 5 V per datasheet Table 34-3). Changing
`board_build.f_cpu` requires a re-fuse (`utils/flash.sh --fuses`), not just a
reflash, or every timing runs wrong by the ratio.

305 Hz is the frequency *ceiling*, not a choice: full 16-bit resolution needs the
whole period, so DIV1 is as fast as it goes. The brightness-scheduled tiers only
step downward from there. The dimming floor is expressed as a time
(`DRIVER_MIN_ON_NS`, the driver's ~2 µs) and converted to counts per prescaler,
so it stays correct if you retune the tiers.

Don't reintroduce `analogWrite()` on the LED pins, and don't put `millis()` on
TCA0 (it would break the PWM takeover).

## Everything tunable is in `include/config.h`

Pins, PWM frequency/floor, ramp speed, joystick deadzones/thresholds, axis
inversion, EEPROM save delay, debug flag. Prefer changing `config.h` over
hardcoding in `main.cpp`. The firmware is deliberately one file plus config so it
stays portable to a rev2 board or a different MCU.

## Build / flash workflow

Everything goes through `utils/flash.sh` — it auto-detects the USB-serial
programmer (`utils/find-port.sh`) so no port is hardcoded in `platformio.ini`.

```bash
utils/flash.sh                    # build + upload   (rev1 = ATtiny814, default)
utils/flash.sh --rev2             # target rev2      (ATtiny3216)
utils/flash.sh --build            # compile only
utils/flash.sh --fuses            # once per fresh chip
utils/flash.sh --debug --monitor  # GLIM_DEBUG=1 build, upload, then console
utils/flash.sh --port /dev/... --slow   # override port / drop to 115200
```

**One source, two boards.** `GLIM_BOARD` in `config.h` (1 = rev1/ATtiny814,
2 = rev2/ATtiny3216) selects the pin map; `--rev2` sets it via `-DGLIM_BOARD=2`.
Both parts put the LEDs on PB0/PB1/PB2 — only the peripherals move, onto PORTC
where rev2 has room. Keep new pins inside that `#if`, not hardcoded.

`--debug` works by passing `-DGLIM_DEBUG=1`, which is why `GLIM_DEBUG` in
`config.h` is wrapped in `#ifndef` — keep that guard if you add build-time
toggles. Note `--monitor` needs the adapter's RX on **PB2**, not the UPDI node.

Never set `board_hardware.updipin` to anything but `updi` — it would lock UPDI
out of the chip.

## Verifying changes

There is **no host test suite** — this is firmware. Verification means flashing to
the ATtiny814 and driving the joystick. Compiling (`utils/flash.sh --build`)
catches syntax and register-name errors but not behaviour.

Cheap joysticks vary in orientation: after any change touching the axes, confirm
up=brighter and right=next-channel on real hardware, and flip `JOY_*_INVERT` if
not.

## Flash/RAM budget

ATtiny814 = 8 KB flash, 512 B SRAM. Keep it lean: avoid large lookup tables in
RAM (the brightness curve is computed, not tabled, on purpose), and keep `Serial`
behind `GLIM_DEBUG`.
