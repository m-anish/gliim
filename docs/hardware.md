# Hardware

glim is an ATtiny814 driving three PT4115 LED drivers, taking its input from a
cheap analog thumb joystick. This document covers the pin map (and *why* it's
that map), the power tree, and how to program the chip.

## Pin map

ATtiny814, 14-pin SOIC. All of glim's I/O sits on PORTA; PORTB is left entirely
free for future expansion.

| Signal | Pin | On-chip function |
|--------|-----|------------------|
| LED channel 1 (PWM) | PA3 | TCA0 WO3 (HCMP0) → PT4115 #1 PWM/DIM |
| LED channel 2 (PWM) | PA4 | TCA0 WO4 (HCMP1) → PT4115 #2 PWM/DIM |
| LED channel 3 (PWM) | PA5 | TCA0 WO5 (HCMP2) → PT4115 #3 PWM/DIM |
| Joystick X | PA2 | ADC AIN2 (left/right → channel select) |
| Joystick Y | PA1 | ADC AIN1 (up/down → brightness) |
| Status pixel (WS2812) | PA6 | plain GPIO — colour shows the selected channel |
| Joystick SW | PA7 | digital input, internal pull-up, active-low |
| UPDI (program) | PA0 | UPDI, 1 kΩ in series |
| *free* | PB0 | I²C SDA — earmarked for the IR receiver |
| *free* | PB1 | I²C SCL |
| *free* | PB2 / PB3 | USART (TX / RX) — used for debug serial if enabled |

### Why this map, and not PWM-on-PA1/PA2

The intuitive first cut — LEDs on PA1/PA2/PA3, joystick on PB2/PB3 — **does not
work on this chip**, and the reason is worth writing down so nobody re-derives it
the hard way. From the ATtiny214/414/814 datasheet (DS40001912A):

- **PA1 and PA2 have no waveform (PWM) output at all.** TCA0's outputs are
  hardwired: WO0/WO1/WO2 → PB0/PB1/PB2, and WO3/WO4/WO5 → PA3/PA4/PA5. The only
  PORTMUX remap available for TCA0 is a single bit that moves WO0 to PB3
  (Table 5-1 / PORTMUX.CTRLC). No timer — TCA0, TCB0, or TCD0 — can produce PWM
  on PA1 or PA2. **But** PA1/PA2 *are* ADC inputs (AIN1/AIN2).
- **PB2 and PB3 have no ADC channel.** On PORTB only PB0 (AIN11) and PB1 (AIN10)
  reach the ADC; PB2/PB3 are USART/TOSC pins. So they can't read an analog
  joystick — **but** they *can* do PWM/UART.

The two roles are therefore swapped onto the pins that can physically do the
job: PWM on PA3/PA4/PA5, analog joystick on PA1/PA2. Everything else falls out of
that. If you're porting to a different tinyAVR, re-check Table 5-1 first.

### PWM engine

The three LED pins are WO3/WO4/WO5, which **only exist in TCA0 split mode** (in
normal 16-bit mode TCA0 exposes just WO0–WO2). So the firmware:

1. calls `takeOverTCA0()` — safe because megaTinyCore puts `millis()` on TCD0, so
   TCA0 is otherwise idle;
2. enables split mode, sets `HCMP0/1/2EN` (which hands PA3/PA4/PA5 to the timer),
   `HPER = 255`, clock `DIV64`;
3. writes duty straight to `HCMP0/HCMP1/HCMP2` (higher = brighter).

At `F_CPU = 20 MHz`, `DIV256` gives ~305 Hz.

### Why that frequency, and how low the dimming goes

The lowest honest brightness is set by the **driver**, not the timer. The PT4115
is a hysteretic buck: it needs enough on-time to actually build inductor current
to regulation. Too short a pulse and output stops being proportional or even
monotonic. So:

    minimum duty ≈ (PT4115 minimum on-time) × (PWM frequency)

and, with 8-bit resolution:

    (on-time per count) × (PWM frequency) = 1/256   — always

F_CPU cancels out of both. It does **not** move that curve; it only changes which
discrete points the prescaler can land on. Nor does PWM bit-depth help — a 16-bit
timer would offer a 62 ns pulse the driver can't act on. The only real levers are
lowering the PWM frequency or reducing the driver's minimum on-time.

Useful operating points (8-bit, `HPER = 255`):

| Config | PWM freq | on-time per count |
|---|---|---|
| 16 MHz, DIV64 | 976 Hz | 4.0 µs |
| 16 MHz, DIV256 | 244 Hz | 16.0 µs |
| **20 MHz, DIV256** | **305 Hz** | **12.8 µs** ← current |
| 20 MHz, DIV64 | 1221 Hz | 3.2 µs |

305 Hz (DIV256) is the mid tier of a **brightness-scheduled frequency**
(`GLIM_VARIABLE_PWM_FREQ`): the firmware runs DIV64 (~1221 Hz) when a channel is
bright, DIV256 (~305 Hz) in the middle, and DIV1024 (~76 Hz, a 51 µs pulse the
driver reproduces cleanly) when everything is dim. All three channels share the
one timer, so the frequency tracks the *brightest lit* channel — a lone dim
night-light gets the low tier; a dim channel beside a bright one rides the higher
one, so the bright channel never flickers. 76 Hz is the hard floor (no prescaler
past 1024, and split-mode PER is capped at 255).

This does **not** lower the 0.39% floor — that's the 8-bit resolution limit,
independent of frequency (the minimum step is always 1 count in 256). It makes
the lowest steps clean and monotonic and cuts flicker up top. Going genuinely
below 0.39% needs analog current reduction (hybrid dimming), not frequency.

To go meaningfully lower you'd need hybrid dimming — drive the PT4115's analog
CTRL pin to scale full-scale current down (TCA0's unused WO0/WO1/WO2 on
PB0/PB1/PB2, RC-filtered) with PWM on top, for ~1280:1 — or simply reduce the
full-scale LED current with a larger sense resistor.

## Power tree

```
  6–30 V DC in  (19 V / 3 A supply used here)
      │
      ├────────────────► PT4115 #1 IN+  ─► LED string 1
      ├────────────────► PT4115 #2 IN+  ─► LED string 2
      ├────────────────► PT4115 #3 IN+  ─► LED string 3
      │
      └─► buck module ─► 5 V ─┬─► ATtiny814 VDD
                              └─► joystick module 5V
  GND common to all.
```

The PT4115 is a hysteretic step-down constant-current driver; it runs straight
off the high-voltage rail and sets LED current with its sense resistor. Its
PWM/DIM pin is what glim toggles — **high = LED on**, low = off — so the duty
cycle the ATtiny writes *is* the brightness. Each channel can carry a string of
LEDs (roughly up to ~5, depending on the string's forward voltage vs. your
supply). Size the supply for the total LED current across all three channels
plus headroom.

> The joystick's 5 V and the ATtiny's VDD share the same 5 V rail, so the ADC
> readings are ratiometric to VDD — good, since the joystick pots divide that
> same rail.

## Programming (serialUPDI)

The ATtiny814 is programmed over its single UPDI pin (PA0). The cheapest reliable
way is **serialUPDI**: an ordinary USB-to-serial adapter with a resistor from TX
to UPDI.

```
  USB-serial TX ──[ 1 kΩ ]──┬──► UPDI (PA0)
  USB-serial RX ────────────┘
  USB-serial GND ───────────► GND
```

TX drives UPDI through the series resistor; RX listens on the same node. The
1 kΩ already on the board is exactly this resistor (470 Ω–4.7 kΩ all work; 1 kΩ is
fine). Power the board separately (or from the adapter's 5 V if it can source
enough) — serialUPDI does not power the target.

Then:

```bash
utils/flash.sh --fuses   # once on a fresh chip: clock source, BOD, EESAVE
utils/flash.sh           # build + flash firmware
```

The adapter is auto-detected — `utils/flash.sh --list` shows what it can see,
`--port` overrides it, and `--slow` drops the upload to 115200 if it's flaky.

**Fuses matter:** a factory chip is fused for a 20 MHz base oscillator, while the
firmware is built for 16 MHz — without `--fuses` every timing (PWM frequency,
`millis()`, baud) runs ~25% fast. `--fuses` also enables EESAVE, so your saved
brightness survives reflashing. It never touches the UPDI pin configuration.

## Datasheet

The full device datasheet used for the pin/timer/ADC decisions above is included
at [`ATtiny214-414-814-DS40001912A.pdf`](ATtiny214-414-814-DS40001912A.pdf) —
see Table 5-1 (I/O multiplexing), §20 (TCA timer / split mode), and §30 (ADC).
