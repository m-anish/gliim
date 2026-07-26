# Roadmap

Where glim can go without losing the plot. The organizing principle is **stay
tiny and tactile**: glim is a thing you operate by feel, in the dark, with no
app and no network. Enhancements are welcome as long as they respect that.

## What glim is (and isn't)

- **Is:** a local, physical, single-hand dimmer. Instant. No pairing, no cloud,
  no clock to set. Turn it on and it just works.
- **Isn't:** a scheduler, a networked fleet node, or an app endpoint. The moment
  a feature wants Wi-Fi, time-of-day automation, or multi-unit coordination, it
  belongs in [lokki](https://github.com/m-anish/lokki), not here. That boundary
  is what keeps glim on a $0.60 MCU.

Everything below is sized to an ATtiny814-class part. We're using **27% of flash
and 10% of RAM**, so software headroom is not the constraint — pins and restraint
are.

## Resource budget (what's actually free)

Current pin usage and what's left to build on:

| Pin | Now | Free for |
|-----|-----|----------|
| PA0 | UPDI | (programming) |
| PA1 | Joystick X (ADC) | — |
| PA2 | Joystick Y (ADC) | — |
| PA3/PA4/PA5 | LED PWM (TCA0 WO3/4/5) | + indicator LEDs piggyback here |
| PA6 | **free** | ADC / DAC / status LED |
| PA7 | Joystick SW | — |
| PB0/PB1 | **free** | I²C (SDA/SCL), ADC, or 2 more PWM (WO0/WO1) |
| PB2/PB3 | **free** | UART (TX/RX), 1 more PWM (WO2), or 32 kHz crystal |

**Free peripherals:** TCB0, TCD0, RTC, analog comparator, DAC, CCL, EVSYS,
USART0, TWI0, SPI0. Plenty.

One number worth remembering: **TCA0 split mode has six outputs** (WO0–WO2 →
PB0/PB1/PB2, WO3–WO5 → PA3/PA4/PA5). The 814 can drive **6 hardware PWM channels**
before you ever need a bigger chip — see Tier 2.

---

## Tier 0 — firmware only (no hardware change, free)

These need nothing but a reflash. Highest value-per-effort; do these first.
**Dithering, soft transitions, the watchdog, and EEPROM versioning shipped in the
firmware — the ✅ rows below.**

| Item | Why | Notes |
|------|-----|-------|
| ✅ **Temporal dithering** | Biggest perceived-quality win. 8-bit PWM is coarse at the bottom; dithering between adjacent duty steps buys extra effective bits, so deep dimming stops stair-stepping. | *Done:* first-order sigma-delta in the `TCA0_HUNF` ISR, `DITHER_BITS` in config. |
| ✅ **Soft transitions** | Fade in/out on power-up, toggle, and scene changes instead of snapping. Feels premium, easier on the eye at night. | *Done:* setpoint→slewed-display model, `FADE_MS` in config. |
| ✅ **Watchdog** | It's an unattended, installed device. WDT auto-recovers from any hang. | *Done:* ~2 s WDT, kicked in `loop()`, `GLIM_WATCHDOG` in config. |
| ✅ **EEPROM struct versioning** | A `version` byte beside the magic so future firmware can migrate saved state instead of resetting the room to defaults on update. | *Done:* `EE_VERSION` in the persist struct. |
| ✅ **Factory-reset gesture** | Hold the switch *during power-on* → wipe EEPROM to defaults. Field-recoverable without a programmer. | *Done:* hold-to-arm with swell + flash feedback, `FACTORY_HOLD_MS` in config. |
| **Per-channel min/max clamps** | Some LED strings flicker below X% or are never wanted above Y%. Config-only limits. | In `config.h`. |
| **Startup-mode option** | restore-last (current) / all-on-default / all-off, selectable. | Config flag. |
| **All-off → wake-on-tap sleep** | When every channel is off, deep-sleep the MCU and wake on a switch press. Cuts standby draw to µA (PWM can't run in sleep, so this only applies when dark). | Minor on mains, but a clean "green" default. Joystick-move won't wake it — the switch does. |

## Tier 1 — near-zero hardware (fits the current hand-soldered board)

Small parts, one or two pins each. Pick à la carte.

| Item | Cost | Value | Detail |
|------|------|-------|--------|
| **3 channel indicator LEDs** | LED + 1 kΩ per channel, **0 pins** | Live brightness meter at the joystick | Piggyback on PA3/PA4/PA5. Brightness mirrors each channel's level for free. Shows *level*, not *selection*. |
| **1 status LED / pixel** | 1 pin (PA6) | Persistent "which channel is selected" | A single bicolor LED, or one WS2812 (megaTinyCore has `tinyNeoPixel`) → selected channel = color, plus all-off/booting states. This is the missing half of the indicator story. |
| **IR receiver** | 3-pin TSOP (e.g. 38 kHz), 1 pin | **Couch control** — huge for a home | Decode NEC with TCB0 input-capture. Map remote keys to brightness / channel / on-off / scenes alongside the joystick. Probably the single most useful add for the actual use case. |
| **Ambient light sensor** | LDR/phototransistor + resistor, 1 ADC pin (PA6) | Auto-cap brightness in daylight | Optional, behind a config flag — glim stays manual-first. (This is a lokki idea scaled down.) |
| **PIR motion sensor** | 3-pin PIR module, 1 pin | Auto-on/off for halls, utility spaces | Turns glim "automatic"; keep it opt-in so it never surprises someone who just wants a manual dimmer. |

> Recommended Tier-1 combo: **3 indicator LEDs + 1 status pixel + IR receiver.**
> Three cheap parts, three pins (PA6 + one for IR, indicators are free), and it
> covers the two real gaps — persistent selection feedback and across-the-room
> control — without touching the minimalist spirit.

## Tier 2 — rev2 PCB ✅ specified

**Full specification: [`hardware/rev2/`](hardware/rev2/README.md)** — board spec,
[PT4115 driver circuit](hardware/rev2/led-driver.md), and
[input circuits](hardware/rev2/input.md).

Headline: ATtiny1616, **16-bit PWM** (the PT4115's real floor is a 2 µs on-time,
so 8-bit was wasting ~6× of dimming depth), USB-C PD power, on-board IR and
status pixel, and 10 kΩ DIM pulldowns so it stops flashing at power-up.

The original sketch of this tier, for reference:

A proper board is the natural home for the Tier-1 add-ons plus the boring
robustness a wall-installed device wants.

- **Consolidate** the indicator LEDs, status pixel, and IR receiver onto the PCB.
- **Input protection:** reverse-polarity (P-FET or series diode), input fuse, a
  TVS on the DC rail. Screw terminals for the 6–30 V in and each LED string.
- **Up to 6 channels on the same ATtiny814** using the full TCA0 split (adds
  PB0/PB1/PB2 as WO0/WO1/WO2). The joystick already wraps through channels, so
  the UX scales for free. Trade-off: you spend the I²C/UART pins — decide per
  build whether you'd rather have 6 channels or a spare bus.
- **Cleaner programming header** for UPDI (the 1 kΩ serialUPDI node broken out).
- **Enclosure + a joystick with a decent detent** — the feel of the stick is the
  whole product; a mushy module undoes good firmware.

## Tier 3 — past the 814 (know when to stop)

Where the minimalist envelope genuinely runs out. Reach for these only when the
requirement can't be met on a tiny part:

- **More than 6 channels / higher-res PWM:** step to a 20-pin tinyAVR (e.g.
  ATtiny817) for more timer outputs, or a part with 16-bit PWM for cinema-smooth
  dimming without dithering.
- **True flicker-free analog dimming:** the DAC could drive one PT4115's CTRL pin
  for zero-PWM dimming — but only one channel (one DAC), and current-dimming
  shifts LED color vs. PWM. A curiosity, not a default.
- **Scheduling / sunrise-wake / networked or multi-room control / an app:** this
  is the graduation line. Don't bolt a radio onto glim — that's exactly what
  **lokki** already is. Let glim be the tactile local node and hand the rest to
  its bigger sibling.

---

## Suggested order

1. ✅ **Tier 0 polish** — dithering, soft transitions, watchdog, EEPROM
   versioning, and the factory-reset gesture are all shipped.
2. **Indicator LEDs** (your idea) — do it now; it's free and informative.
3. ✅ **Status pixel** — WS2812 on **PA6**, colour = selected channel, dim
   locator glow when all-off. Fitted and shipped.
   **IR receiver** — still to come: **learn-any-NEC** handling, receiver on **PB0**.
4. **rev2 PCB** — fold the above in, add protection, decide 3 vs 6 channels.
5. **Optional sensors** (ambient / PIR) — only if a given install wants automation,
   always behind a config flag.

Nothing here requires leaving the ATtiny814 until Tier 3 — and Tier 3 is mostly a
signpost pointing at lokki.
