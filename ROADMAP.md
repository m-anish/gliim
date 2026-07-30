# Roadmap

> **⚠ Partly superseded.** gliim pivoted to **one EC11 encoder per channel** and
> **control panels on an RS-485 bus** — see
> [`docs/architecture.md`](docs/architecture.md), which is the current plan of
> record. Tiers below that assume a single box with a joystick (channel
> indicators, the ack-blink, the 6-channel split-mode path, the "second control
> point" work) are obsolete; the bus replaces them. The resource budget, the
> Tier-0 firmware items and the gliim/lokki boundary still hold.

Where gliim can go without losing the plot. The organizing principle is **stay
tiny and tactile**: gliim is a thing you operate by feel, in the dark, with no
app and no network. Enhancements are welcome as long as they respect that.

## What gliim is (and isn't)

- **Is:** a local, physical, single-hand dimmer. Instant. No pairing, no cloud,
  no clock to set. Turn it on and it just works.
- **Isn't:** a scheduler, a networked fleet node, or an app endpoint. The moment
  a feature wants Wi-Fi, time-of-day automation, or multi-unit coordination, it
  belongs in [lokki](https://github.com/m-anish/lokki), not here. That boundary
  is what keeps gliim on a $0.60 MCU.

Everything below is sized to an ATtiny814-class part. rev1 is now at **71 % of
flash and 27 % of RAM**, and every pin is allocated — so on rev1 both headroom
*and* pins are the constraint. rev2's ATtiny3216 resets that (17.5 % of 32 KB).

## Resource budget (what's actually free)

Current pin usage and what's left to build on:

| Pin | Now | Notes |
|-----|-----|----------|
| PA0 | UPDI | (programming) |
| PA1 / PA2 | Joystick Y / X (ADC) | — |
| PA3 | **free** | — |
| PA4 / PA5 | **free** | debug SoftwareSerial in `--debug` builds |
| PA6 | Status LED (WS2812, as fitted) | ✅ — driven as a plain "system on" lamp |
| PA7 | Joystick SW | — |
| PB0/PB1/PB2 | LED PWM (TCA0 WO0–WO2) | 16-bit; + indicator LEDs piggyback here |
| PB3 | IR receiver | ✅ implemented |

**Free peripherals:** TCB0, RTC, analog comparator, DAC, CCL, EVSYS, TWI0, SPI0.
(TCD0 is millis; TCA0 is the PWM; USART0 is unusable — its TXD is PB2.)

The pin budget is now essentially full — which is the honest signal that further
growth belongs on rev2's ATtiny3216 rather than here.

**The channels/resolution trade:** TCA0 split mode has six outputs (WO0–WO2 →
PB0/1/2, WO3–WO5 → PA3/4/5) but is 8-bit; normal mode has three and is 16-bit.
gliim ships normal mode — for a dimmer, depth beats channel count. Six 8-bit
channels remain available as a config change if you ever want them.

---

## Tier 0 — firmware only (no hardware change, free)

These need nothing but a reflash. Highest value-per-effort; do these first.
**Dithering, soft transitions, the watchdog, and EEPROM versioning shipped in the
firmware — the ✅ rows below.**

| Item | Why | Notes |
|------|-----|-------|
| ⤴ **Temporal dithering** | Was the biggest win at 8-bit: dithering between adjacent duty steps bought extra effective bits. | *Superseded.* Shipped, then **removed** when the LEDs moved to 16-bit PWM, which exceeds what dithering bought and costs no flicker margin. Still the right tool if you ever take the 6-channel/8-bit option — see rev2 §4. |
| ✅ **Soft transitions** | Fade in/out on power-up, toggle, and scene changes instead of snapping. Feels premium, easier on the eye at night. | *Done:* setpoint→slewed-display model, `FADE_MS` in config. |
| ✅ **Watchdog** | It's an unattended, installed device. WDT auto-recovers from any hang. | *Done:* ~2 s WDT, kicked in `loop()`, `GLIIM_WATCHDOG` in config. |
| ✅ **EEPROM struct versioning** | A `version` byte beside the magic so future firmware can migrate saved state instead of resetting the room to defaults on update. | *Done:* `EE_VERSION` in the persist struct. |
| ✅ **Factory-reset gesture** | Hold the switch *during power-on* → wipe EEPROM to defaults. Field-recoverable without a programmer. | *Done:* hold-to-arm with swell + flash feedback, `FACTORY_HOLD_MS` in config. |
| **Per-channel min/max clamps** | Some LED strings flicker below X% or are never wanted above Y%. Config-only limits. | In `config.h`. |
| **Startup-mode option** | restore-last (current) / all-on-default / all-off, selectable. | Config flag. |
| **All-off → wake-on-tap sleep** | When every channel is off, deep-sleep the MCU and wake on a switch press. Cuts standby draw to µA (PWM can't run in sleep, so this only applies when dark). | Minor on mains, but a clean "green" default. Joystick-move won't wake it — the switch does. |

## Tier 1 — near-zero hardware (fits the current hand-soldered board)

Small parts, one or two pins each. Pick à la carte.

| Item | Cost | Value | Detail |
|------|------|-------|--------|
| **3 channel indicator LEDs** | LED + 1 kΩ per channel, **0 pins** | Live brightness meter at the joystick | Piggyback on PB0/PB1/PB2. Brightness mirrors each channel's level for free. Shows *level*, not *selection*. |
| ✅ **1 status LED** | 1 pin (PA6) | "the system is on" | *Done.* Deliberately **not** channel-coded — selecting a channel blinks that light, and the per-channel indicators show level, so a shared indicator has nothing left to say. rev1 drives its fitted WS2812 as a plain lamp; rev2 uses one discrete LED. |
| ✅ **IR receiver** | 3-pin TSOP (e.g. 38 kHz), 1 pin | **Couch control** — huge for a home | *Done:* falling-edge ISR decoding NEC on PB3, plus a learn mode binding six actions to any remote. |
| **Ambient light sensor** | LDR/phototransistor + resistor, 1 ADC pin (PA6) | Auto-cap brightness in daylight | Optional, behind a config flag — gliim stays manual-first. (This is a lokki idea scaled down.) |
| **PIR motion sensor** | 3-pin PIR module, 1 pin | Auto-on/off for halls, utility spaces | Turns gliim "automatic"; keep it opt-in so it never surprises someone who just wants a manual dimmer. |

> Recommended Tier-1 combo: **3 indicator LEDs + 1 status LED + IR receiver.**
> Three cheap parts, two pins (indicators are free — they hang off the PWM
> outputs), and it covers the two real gaps: per-channel level at a glance, and
> across-the-room control.

## Tier 2 — rev2 PCB ✅ specified (pre-pivot; see architecture.md)

**Full specification: [`hardware/`](hardware/board.md)** — board spec,
[LED driver circuit](hardware/led-driver.md), and
[input circuits](hardware/input.md).

Headline: ATtiny3216, **16-bit PWM** (the PT4115's real floor is a 2 µs on-time,
so 8-bit was wasting ~6× of dimming depth), USB-C PD power, on-board IR, a Qwiic
I²C port, and 10 kΩ DIM pulldowns so it stops flashing at power-up.

The original sketch of this tier, for reference:

A proper board is the natural home for the Tier-1 add-ons plus the boring
robustness a wall-installed device wants.

- **Consolidate** the indicator LEDs, status LED, and IR receiver onto the PCB.
- **Input protection:** reverse-polarity (P-FET or series diode), input fuse, a
  TVS on the DC rail. Screw terminals for the 6–30 V in and each LED string.
- **Up to 6 channels** using the full TCA0 split (adds PA3/PA4/PA5 as WO3–WO5
  alongside the three the board already carries). The joystick already wraps through channels,
  so the UX scales for free. Trade-off: split mode is 8-bit, so you'd give up
  the 16-bit dimming depth — for a dimmer that's the wrong trade, which is why
  rev2 ships 3 × 16-bit.
- **Cleaner programming header** for UPDI (the 1 kΩ serialUPDI node broken out).
- **Enclosure + a joystick with a decent detent** — the feel of the stick is the
  whole product; a mushy module undoes good firmware.

## Tier 3 — past a single node (know when to stop)

Where the minimalist envelope genuinely runs out. Reach for these only when the
requirement can't be met on a tiny part:

- **More than 6 channels:** step to a part with more timer outputs. Resolution is
  no longer a reason to move — TCA0 normal mode already gives the full 16 bits,
  which reaches the PT4115's own ~1640:1 limit.
- **True flicker-free analog dimming:** the DAC could drive one PT4115's CTRL pin
  for zero-PWM dimming — but only one channel (one DAC), and current-dimming
  shifts LED color vs. PWM. A curiosity, not a default.
- **Scheduling / sunrise-wake / networked or multi-room control / an app:** this
  is the graduation line. Don't bolt a radio onto gliim — that's exactly what
  **lokki** already is. Let gliim be the tactile local node and hand the rest to
  its bigger sibling.

---

## Suggested order

1. ✅ **Tier 0 polish** — dithering, soft transitions, watchdog, EEPROM
   versioning, and the factory-reset gesture are all shipped.
2. **Indicator LEDs** (your idea) — do it now; it's free and informative.
3. ✅ **Status LED** — on **PA6**, meaning "system on". Not channel-coded: the
   channels indicate themselves (ack-blink for selection, DIM-line indicators for
   level). Fitted and shipped.
   **IR receiver** — still to come: **learn-any-NEC** handling, receiver on **PB0**.
4. **rev2 PCB** — fold the above in, add protection, decide 3 vs 6 channels.
5. **Optional sensors** (ambient / PIR) — only if a given install wants automation,
   always behind a config flag.

Nothing here requires leaving the ATtiny814 until Tier 3 — and Tier 3 is mostly a
signpost pointing at lokki.
