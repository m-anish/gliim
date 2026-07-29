# Hardware

glim is an ATtiny814 driving three PT4115 LED drivers, taking its input from a
cheap analog thumb joystick. This document covers the pin map (and *why* it's
that map), the power tree, and how to program the chip.

## Pin map

ATtiny814, 14-pin SOIC — this is the **rev1** board, and all twelve I/O are now
spoken for. rev2 (ATtiny3216) keeps the joystick on PA1/PA2 but relocates the LED
channels to PB3/PB4/PB5 so PB0–PB2 can carry I²C and a hardware UART; see
[`../hardware/board.md`](../hardware/board.md).

| Signal | Pin | On-chip function |
|--------|-----|------------------|
| LED channel 1 (PWM) | PB0 | TCA0 **WO0 (CMP0)** → PT4115 #1 PWM/DIM |
| LED channel 2 (PWM) | PB1 | TCA0 **WO1 (CMP1)** → PT4115 #2 PWM/DIM |
| LED channel 3 (PWM) | PB2 | TCA0 **WO2 (CMP2)** → PT4115 #3 PWM/DIM |
| IR receiver | PB3 | TSOP38238 OUT, falling-edge interrupt |
| Joystick X | PA2 | ADC AIN2 (left/right → channel select) |
| Joystick Y | PA1 | ADC AIN1 (up/down → brightness) |
| Status LED (WS2812, as fitted) | PA6 | plain GPIO — lit while the system is running |
| Joystick SW | PA7 | digital input, internal pull-up, active-low |
| UPDI (program) | PA0 | UPDI, 1 kΩ in series |
| Debug TX | PA4 | SoftwareSerial, **debug builds only** |
| *free* | PA3, PA5 | PA5 is the debug RX placeholder |

### Why this map

Two independent constraints pin it down. From the datasheet (DS40001912A):

- **PA1 and PA2 have no waveform (PWM) output at all.** TCA0's outputs are
  hardwired: WO0/WO1/WO2 → PB0/PB1/PB2, and WO3/WO4/WO5 → PA3/PA4/PA5. The only
  PORTMUX remap for TCA0 is a single bit moving WO0 to PB3 (Table 5-1 /
  PORTMUX.CTRLC). No timer can produce PWM on PA1 or PA2 — **but** they *are* ADC
  inputs (AIN1/AIN2), so that's where the joystick goes.
- **PB2 and PB3 have no ADC channel.** On PORTB only PB0 (AIN11) and PB1 (AIN10)
  reach the ADC. So PORTB can't read an analog joystick — but it *can* do PWM.

Then the resolution question decides *which* PWM pins. TCA0 has two modes and you
cannot have both:

| Mode | Outputs | Pins | Resolution |
|---|---|---|---|
| **Normal** (used) | WO0–WO2 | **PB0/PB1/PB2** | **16-bit** |
| Split | WO0–WO5 | PB0–2 + PA3–5 | 8-bit |

Split mode's six channels are worthless here — we only have three drivers — while
its 8-bit resolution costs real dimming depth (next section). So the LEDs live on
**PB0/PB1/PB2** and TCA0 runs in normal mode.

**Consequence:** PB2 is also USART0's TXD, and its only PORTMUX alternate (PA1)
is the joystick. So the hardware UART is gone; `GLIM_DEBUG` builds bit-bang over
SoftwareSerial on **PA4** instead, and IR is auto-disabled in those builds
(SoftwareSerial's interrupt dispatcher claims the PORT vectors the IR decoder
needs). This is a genuine 14-pin squeeze — rev2's ATtiny3216 has PORTC spare.

### PWM engine

`analogWrite()` can't drive TCA0 in normal mode, so the firmware does it directly:

1. calls `takeOverTCA0()` — safe because megaTinyCore puts `millis()` on TCD0, so
   TCA0 is otherwise idle;
2. selects single-slope PWM with `CMP0/1/2EN` (which hands PB0/PB1/PB2 to the
   timer) and `PER = 65535` for the full 16 bits;
3. writes duty to **`CMP0BUF/CMP1BUF/CMP2BUF`** — the *buffered* registers, which
   update at the period boundary so a mid-period change can't emit a runt pulse.

Single-slope sets the output at TOP and clears it on compare match (§20.3.3.4),
so duty = `CMP/(PER+1)` and **higher = brighter**, same polarity as before.

At `F_CPU = 20 MHz` with `PER = 65535`, DIV1 gives **305 Hz** — and that is the
*ceiling*, because full 16-bit resolution consumes the whole period.

### How low the dimming actually goes

The floor is set by the **driver**, not the timer. The PT4115 is a hysteretic
buck: it needs enough on-time to build inductor current to regulation. Its
datasheet quotes the usable duty range at two frequencies, and both reduce to the
same number:

| Condition | Min duty | Period | ⇒ min on-time |
|---|---|---|---|
| f_DIM = 100 Hz | 0.02 % | 10 ms | **2 µs** |
| f_DIM = 20 kHz | 4 % | 50 µs | **2 µs** |

So `minimum duty = 2 µs × PWM frequency`, and the dimming ratio is
`period / 2 µs`. Firmware expresses this as `DRIVER_MIN_ON_NS` and converts to
counts automatically, so the floor tracks whatever prescaler is in use:

| Prescaler | Frequency | Floor | Ratio | ≈ perceived |
|---|---|---|---|---|
| **DIV1** | **305 Hz** | 40 counts (0.061 %) | **1638:1** | ~3.5 % |
| DIV2 | 152 Hz | 20 counts (0.031 %) | 3277:1 | ~2.5 % |
| DIV4 | 76 Hz | 10 counts (0.015 %) | 6554:1 | ~1.8 % |

**This is why the LEDs are on PB0/PB1/PB2 and not PA3/PA4/PA5.** With 8-bit split
mode the smallest step is 1/256 = 0.39 %, which at 305 Hz is **6.4× coarser than
the driver can actually resolve** — the silicon was never the limit at these
frequencies, resolution was. Going to 16-bit recovers all of it.

`GLIM_VARIABLE_PWM_FREQ` still schedules the prescaler by brightness, but the
tiers now only step *downward* from 305 Hz (there is nothing above it): 305 Hz
for bright and mid, dropping to 152 Hz when everything is dim, where flicker is
least visible and the floor halves. Set `PWM_CLKSEL_LO` to DIV4 for the last 2×
of depth if you can live with 76 Hz. All three channels share the timer, so the
frequency follows the *brightest lit* channel — a dim channel beside a bright one
rides the higher frequency, keeping the bright one flicker-free.

Below 0.015 % you need analog current reduction (hybrid dimming) or a larger
sense resistor; no amount of PWM gets there.

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

**Fuses matter:** they select the oscillator the firmware is built against
(`board_build.f_cpu`, currently **20 MHz**). Flash a chip whose `OSCCFG` still
says 16 MHz and every timing — PWM frequency, `millis()`, baud — runs wrong by
that ratio. `--fuses` also enables EESAVE, so your saved brightness survives
reflashing, and sets BOD. It never touches the UPDI pin configuration.

## Datasheet

The full device datasheet used for the pin/timer/ADC decisions above is included
at [`ATtiny214-414-814-DS40001912A.pdf`](ATtiny214-414-814-DS40001912A.pdf) —
see Table 5-1 (I/O multiplexing), §20 (TCA timer / split mode), and §30 (ADC).
