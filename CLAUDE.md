# CLAUDE.md

Guidance for Claude Code (claude.ai/code) when working in this repository.

## Project

glim is a knob-controlled LED dimmer, being reshaped into a small distributed
lighting network. An **ATtiny3216** drives three **PT4115** constant-current LED
drivers via hardware PWM. Purpose: simple, screenless, easily-adjustable home and
hall lighting. It's the small sibling of `lokki` (campus-scale LED automation) in
the `starstucklab` family.

Firmware is C++ on **megaTinyCore**, built with **PlatformIO**, flashed over
**serialUPDI**.

## Read `docs/architecture.md` before designing anything

The project pivoted. The target design is **one EC11 rotary encoder per channel**
(no channel selection, no modes) and **control panels on an RS-485 bus** up to
~15 m from the driver nodes. `docs/architecture.md` has the topology, the
transport comparison, the counter-based protocol, and pin budgets.

**It is not implemented.** `src/main.cpp` and `include/config.h` are the
*pre-pivot* firmware: joystick UI, and a pin map that predates the bus. They
build and run; they are not the design. Don't extend the joystick UI.

**rev1 (ATtiny814) is retired**, in `deprecated/`. Don't add rev1 support back,
don't reintroduce a `GLIM_BOARD` switch. `deprecated/README.md` lists six
findings that survive the pivot — read them before re-deriving any of it.

## The pin map is load-bearing — do not "simplify" it

Forced by silicon, not preference (DS40002205A, Table 5-1):

- **PA1/PA2 cannot do PWM.** No timer reaches them; they are ADC pins
  (AIN1/AIN2). They are also TWI0's *alternate* pins — which is what
  `architecture.md` §8 uses now that the joystick is gone.
- **PB2/PB3 cannot do ADC.** Only PB0/PB1 reach the ADC on PORTB. PB2/PB3 are
  USART0 TXD/RXD — needed as a pair for RS-485.
- **Only WO0–WO2 exist in 16-bit normal mode** — see the PWM section.

If you're tempted to move a PWM channel to PA1/PA2 or an analog input to PB2/PB3,
it will silently not work.

## PWM is bare-metal TCA0, not analogWrite()

`analogWrite()` can't drive TCA0 in normal mode, so `pwmInit()` in `src/main.cpp`:

- calls `takeOverTCA0()` (safe: `millis()` is on TCD0, so TCA0 is free);
- runs TCA0 in **normal (16-bit) mode**, single-slope, `PER = 65535`, and writes
  duty to the *buffered* `CMP0BUF/CMP1BUF/CMP2BUF`, higher = brighter, 305 Hz at
  20 MHz (DIV1).

**The LEDs must stay on WO0–WO2.** Normal mode is the only way to get 16-bit, and
normal mode has only those three outputs. Using WO3–WO5 instead forces split mode
and an 8-bit floor of 0.39 % — 6.4× coarser than the PT4115 can resolve (its floor
is a ~2 µs on-time, i.e. 40 counts of 65536 at 305 Hz). That is the whole reason
for the pin choice — and the reason the answer to "more channels" is **another
driver node**, not split mode.

*Which* pins WO0–WO2 land on is a `PORTMUX.CTRLC` choice. `config.h` currently
puts all three at the alternates PB3/PB4/PB5; `architecture.md` §8 moves ch3 only
(PB0/PB1/PB5) to free PB2+PB3 as a USART pair for RS-485. Keep them selectable
via `PWM_PORTMUX`, never hardcoded.

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

Pins, PWM frequency/floor, ramp speed, input deadzones/thresholds, EEPROM save
delay, debug flag. Prefer changing `config.h` over hardcoding in `main.cpp`. The
firmware is deliberately one file plus config so it stays portable — which is
what makes the encoder/bus rewrite tractable.

## Build / flash workflow

Everything goes through `utils/flash.sh` — it auto-detects the USB-serial
programmer (`utils/find-port.sh`) so no port is hardcoded in `platformio.ini`.

```bash
utils/flash.sh                    # build + upload
utils/flash.sh --build            # compile only
utils/flash.sh --fuses            # once per fresh chip
utils/flash.sh --debug --monitor  # GLIM_DEBUG=1 build, upload, then console
utils/flash.sh --port /dev/... --slow   # override port / drop to 115200
```

Envs are `glim` (default) and `fuses`. `--debug` works by passing
`-DGLIM_DEBUG=1`, which is why `GLIM_DEBUG` in `config.h` is wrapped in `#ifndef`
— keep that guard if you add build-time toggles. `--monitor` needs the adapter's
RX on the debug TX pin (**PB2**), not the UPDI node — and note PB2 becomes the
RS-485 TXD under the new architecture, so debug telemetry will have to share the
bus or move.

Never set `board_hardware.updipin` to anything but `updi` — it would lock UPDI
out of the chip.

## Verifying changes

There is **no host test suite** — this is firmware. Verification means flashing
to the chip and driving the hardware. Compiling (`utils/flash.sh --build`)
catches syntax and register-name errors but not behaviour.

For the bus work specifically, `architecture.md` §11 says to prove **two nodes
and one 15 m link** before designing anything else. That step de-risks the rest.

## Flash/RAM budget

ATtiny3216 = 32 KB flash, 2 KB SRAM — roomy compared to rev1's 8 KB, but keep it
lean anyway: avoid large lookup tables in RAM (the brightness curve is computed,
not tabled, on purpose), and keep `Serial` behind `GLIM_DEBUG`. Current build is
5986 B / 32768 (18.3 %).
