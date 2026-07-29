# Bill of Materials & build plan (rev1)

> This covers the **rev1 hand-soldered board**. For the next board — ATtiny3216,
> 16-bit PWM, USB-C PD power, on-board IR — see
> [`../hardware/board.md`](../hardware/board.md).


A living parts list for glim. It covers the **core dimmer** (current rev1,
hand-soldered) plus the two add-ons worth baking in now — **channel indicator
LEDs** and the **IR remote**. Prices are rough per-unit ballparks (AliExpress /
Indian hobby suppliers) to gauge scale, not quotes.

## Variables to pin down first

These change the BOM; defaults in brackets are what the rest of this doc assumes.

| Choice | Default | Affects |
|--------|---------|---------|
| Supply voltage | 19 V / 3 A | Buck module rating, LED string sizing |
| LEDs per channel | up to 5 | LED count, current budget, driver Rsense |
| LED type | COB / star / strip | Load, mounting |
| Remote strategy | learn-any-NEC | firmware only; any remote works |
| Status LED? | optional | 1 pin + 1 part |

## Core dimmer (rev1)

| # | Part | Qty | Role | ~Unit |
|---|------|-----|------|-------|
| 1 | **ATtiny814** (SOIC-14) | 1 | MCU — 3× PWM, 2× ADC, 1 button | $0.8 |
| 2 | **PT4115 LED driver** — discrete build preferred, see [led-driver.md](../hardware/led-driver.md); modules also work | 3 | one constant-current channel each | ₹50 discrete / ₹130 module |
| 3 | **DC-DC buck module → 5 V** | 1 | logic + joystick rail | $1.0 |
| 4 | **Analog joystick module** (KY-023, 5-pin GND/5V/X/Y/SW) | 1 | the entire UI | $1.0 |
| 5 | LED load (your lighting) | ≤5 / ch | the actual light | varies |
| 6 | DC power supply, 6–30 V | 1 | [19 V / 3 A on hand] | — |

**Buck rating caveat:** size the buck's *max input* above your *max supply*. A
common **MP1584** module tops out ~28 V — fine at 19 V, unsafe if you ever push to
30 V. For full-range 6–30 V headroom use an **LM2596** (40 V) or a wide-input
module. The 5 V output feeds both ATtiny VDD and the joystick's 5V, so the ADC
stays ratiometric with the pots — good.

## Passives & decoupling

| # | Part | Qty | Role |
|---|------|-----|------|
| 7 | 100 nF ceramic | 2–3 | VDD decoupling at the ATtiny, one at the IR receiver |
| 8 | 10 µF electrolytic/ceramic | 1 | bulk on the 5 V rail |
| 9 | 1 kΩ resistor | 1 | **UPDI series** (serialUPDI) — already on the board |

The ATtiny814 runs off its internal oscillator — no crystal, no support parts
beyond decoupling.

## Add-on A — channel indicator LEDs (0 extra pins)

Piggyback straight on the PWM lines. Each indicator's brightness *mirrors* its
channel's level for free — a live 3-bar meter by the joystick.

| # | Part | Qty | Role |
|---|------|-----|------|
| 10 | Indicator LED (3 mm or 0805, your color) | 3 | one per channel |
| 11 | 1 kΩ resistor | 3 | ~3 mA @ 5 V; negligible load on the pin |

Wiring per channel: **PWM pin → LED anode, LED cathode → 1 kΩ → GND.** Lights when
the pin is high, so brightness tracks duty.

Together with the acknowledge-blink — selecting a channel blinks *that light* —
this covers both questions an indicator has to answer, which is why the status
LED (Add-on C) can stay a dumb "system on" lamp.

## Add-on B — IR remote

| # | Part | Qty | Role | ~Unit |
|---|------|-----|------|-------|
| 12 | **TSOP38238** IR receiver (38 kHz, OUT/GND/VCC) | 1 | demodulated NEC in (VS1838B = cheaper, noisier) | $0.5 |
| 13 | 100 Ω resistor | 1 | supply filter for the receiver | — |
| 14 | 4.7 µF capacitor | 1 | supply filter (100 Ω + 4.7 µF), plus the 100 nF from row 7 | — |
| 15 | **NEC IR remote** — 44-key RGB-strip remote *or* 17-key car-MP3 remote | 1 | the actual remote | $1.5 |

- **Protocol:** NEC, 38 kHz. Both suggested remotes use it. Decoded by a compact
  hand-rolled reader — a falling-edge ISR measuring edge-to-edge gaps, which is
  all NEC needs. ✅ implemented.
- **Learn mode** ✅ implemented — binds *any* NEC remote, including one your
  friend already owns, so a discontinued SKU never bricks the UI. Hold the stick
  3 s; see [./rev1-controls.md](./rev1-controls.md).
- **Noise:** three switching drivers sit nearby. Use the 100 Ω + 4.7 µF supply
  filter, the 100 nF at the receiver pins, and mount the TSOP away from the
  drivers/inductors. Prefer TSOP38238 over VS1838B for AGC/noise immunity.
- **Pin:** IR OUT → **PB3** (physical pin 6). PA6 is the status LED; PB0–PB2 are the LED PWM outputs.

## Add-on C — status LED ✅ fitted

One lamp meaning "the system is on". Deliberately not channel-coded: selecting a
channel blinks *that light*, and the per-channel indicators above show level, so
a shared indicator has nothing useful left to say. rev1 has a WS2812 fitted and
drives it as a plain single-colour lamp; rev2 uses one discrete LED.

| # | Part | Qty | Role |
|---|------|-----|------|
| 16 | WS2812B *(as fitted)* — or simply an LED + 1 kΩ | 1 | "system on" lamp |

- **Pin:** → **PA6**. megaTinyCore ships `tinyNeoPixel` for the WS2812 variant.
- Colour/brightness knobs are `STATUS_*` in `config.h`.

## Pin allocation after add-ons

| Pin | Physical | Assignment |
|-----|---|------------|
| PA0 | 10 | UPDI (program) — 1 kΩ series |
| PA1 / PA2 | 11 / 12 | Joystick Y / X (ADC) |
| PA3 | 13 | *free* |
| PA4 / PA5 | 2 / 3 | *free* — debug SoftwareSerial TX/RX in `--debug` builds |
| PA6 | 4 | **Status LED** (Add-on C) |
| PA7 | 5 | Joystick SW |
| PB0 / PB1 / PB2 | 9 / 8 / 7 | **LED PWM** (TCA0 WO0–WO2, 16-bit) **+ indicator LEDs** (Add-on A) |
| PB3 | 6 | **IR receiver OUT** (Add-on B) |

The LEDs sit on PB0–PB2 because those are the only TCA0 outputs that exist in
16-bit normal mode — worth ~6× the dimming depth of the 8-bit split mode they
started on. See [./rev1-hardware.md](./rev1-hardware.md) for the full rationale
and [../ROADMAP.md](../ROADMAP.md) for what comes next.

## Rough cost (electronics, excl. PSU & LED load)

Core ≈ $8–10 · indicator LEDs ≈ $0.3 · IR add-on ≈ $2–3 · status LED ≈ $0.3.
Call it **~$11–14** of parts for a fully-featured unit — the PSU and the light
itself dominate the real cost.
