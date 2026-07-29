# glim — numbered pinouts

Authoritative pin assignments for both boards, by **physical package pin number**,
ready to lay out against.

- **Main board (driver node)** — **ATtiny3226**, SOIC-20. Two USARTs, so RS-485
  *and* HC-12 can both be populated and both be live, **plus three local EC11s**.
- **Daughter board (control panel)** — **ATtiny3216**, SOIC-20. One USART, so it
  takes **either** RS-485 **or** HC-12 — never both.

Both parts share the same SOIC-20 package pinout, so the two boards can use an
identical footprint and silkscreen. Package pin 1 is at the dot/notch; numbering
runs down the left side and back up the right.

Every peripheral route below was read out of `iotn3226.h` / `iotn3216.h` and
megaTinyCore's `txy6` variant, not assumed. The package numbering matches
DS40002205A Table 5-1 (1-series); confirm against **DS40002345A** for the 3226
before committing copper.

---

## 1. Main board — ATtiny3226 (driver node + 3 local encoders)

**All three encoders fit.** Dropping the Qwiic port and moving IR to the panels
(where it belongs — see architecture.md §9a) frees exactly the nine pins three
EC11s need, while keeping 3 LED channels and **both** transports live.

| # | Port | Function | Notes |
|---:|------|----------|-------|
| **1** | VDD | **+5 V** | 100 nF close to the pin |
| **2** | PA4 | encoder 2 `B` | |
| **3** | PA5 | encoder 3 `A` | |
| **4** | PA6 | encoder 3 `B` | |
| **5** | PA7 | encoder 1 `SW` | |
| **6** | PB5 | **LED ch3** | TCA0 **WO2 ALT1**. 10 kΩ pulldown to DIM |
| **7** | PB4 | encoder 2 `SW` | ⚠ do not fuse as alt-reset |
| **8** | PB3 | **MCU RXD** ← module `TXD` | USART0 default |
| **9** | PB2 | **MCU TXD** → module `RXD` | USART0 default. **10 kΩ pull-up** |
| **10** | PB1 | **LED ch2** | TCA0 WO1 default. 10 kΩ pulldown to DIM |
| **11** | PB0 | **LED ch1** | TCA0 WO0 default. 10 kΩ pulldown to DIM |
| **12** | PC0 | encoder 3 `SW` | |
| **13** | PC1 | **MCU RXD** ← HC-12 `TXD` | USART1 **ALT1** |
| **14** | PC2 | **MCU TXD** → HC-12 `RXD` | USART1 **ALT1** |
| **15** | PC3 | **WS2812 DIN** | status/link. 100 nF at the LED, 330 Ω in series |
| **16** | PA0 | **UPDI** | 1 kΩ series to the programming pad |
| **17** | PA1 | encoder 1 `A` | |
| **18** | PA2 | encoder 1 `B` | |
| **19** | PA3 | encoder 2 `A` | |
| **20** | GND | **GND** | |

**17 of 17 usable pins. Zero spare** — see the warning below.

| Encoder | `A` | `B` | `SW` |
|---|---|---|---|
| 1 | pin 17 (PA1) | pin 18 (PA2) | pin 5 (PA7) |
| 2 | pin 19 (PA3) | pin 2 (PA4) | pin 7 (PB4) |
| 3 | pin 3 (PA5) | pin 4 (PA6) | pin 12 (PC0) |

The **A/B pairs are identical to the panel's** (PA1/PA2, PA3/PA4, PA5/PA6), so the
quadrature decoder is literally the same code on both boards. Only the switch
pins differ, because PB5 is LED ch3 here.

### What this costs, and why each is acceptable

| Given up | Why it is fine |
|---|---|
| **Qwiic / I²C** | Forced, not chosen — TWI0's only two routes are PB0/PB1 (LED ch1/ch2) and PA1/PA2 (encoder 1). Both are consumed. See the ladder variant below if you want it back. |
| **IR on the main board** | Already the plan: §9a puts IR on panels, at eye level, where it relays to every driver. A driver node in a ceiling can't see a remote anyway. |
| **HC-12 `SET` pin** | Tie it high. Channel, baud and TX power are set-once on a bench jig; nothing changes them at runtime. |

### ⚠ Zero spare pins — and why the WS2812 is the pressure valve

Every usable pin is committed. Normally that is how respins happen, because the
next thing you want is always "one more pin for an indicator."

**The WS2812 is exactly what defuses that.** It is addressable, so **one data pin
drives a chain of them.** Extra indicators no longer cost pins:

| Chain position | Shows |
|---|---|
| 1 | system + link state (see below) |
| 2–4 | per-channel level or on/off, if you want it |

So the board has no spare *pins*, but it does have spare *output capability* —
which is what the spare pins would mostly have been spent on. Genuine spare GPIO
is only recoverable by populating fewer encoders; the footprints cost nothing
left unpopulated.

### Why a WS2812 makes sense now, when it did not before

rev1 had one and the design deliberately dropped it for a plain LED, because the
only thing it was encoding — which channel is selected — was better said by the
channels themselves.

**The bus changed that.** Link state is new information that nothing else can
show, and it is exactly what you need during install and when something is wrong:

| Colour | Meaning |
|---|---|
| green, steady | alive, peers seen recently |
| green, brief flash | frame sent / received |
| amber | no peer heard for N seconds |
| red | CRC errors above threshold — suspect cable |

That last one turns the *"measure rather than guess"* advice about cheap cable
into something visible on the wall, with no scope and no serial console.

### ⚠ WS2812 timing vs the UART

Driving a WS2812 means **bit-banging with interrupts disabled** — 30 µs per LED
(24 bits at 800 kHz). That is fine here, but it has a ceiling:

- **TCA0 PWM is unaffected** — it is hardware, and does not care about interrupts.
- `millis()` on TCB just services a pending interrupt late; no ticks are lost.
- The 1 kHz encoder poll shifts by 30 µs out of 1000. Irrelevant.
- **The USART is the binding constraint.** At 115200 a byte is 87 µs, and
  `RXDATA` + the shift register buffer two — so about **174 µs** of tolerance.

| Chain | Blocking | |
|---|---|---|
| 1 LED | 30 µs | fine |
| 4 LEDs | 120 µs | fine |
| 6+ LEDs | 180 µs+ | **RX overrun risk** |

**Keep the chain to 4 or fewer**, or drop the baud rate, or defer updates to idle.
Use megaTinyCore's bundled **`tinyNeoPixel_Static`** (no malloc, and its timing is
already tuned for 20 MHz).

At **5 V** the WS2812B's DIN threshold of 0.7 × VDD = 3.5 V is met directly by the
MCU output — which is precisely why this is possible now and was not at 3.3 V.
Budget ~1 mA quiescent per LED even when dark; the controller IC never sleeps.

### Do you actually want three encoders here?

Worth asking, because it decides the board's role. Driver nodes sit near the
lights — in ceilings, inside fixtures — where local knobs are decoration. Three
local encoders pay off in exactly one configuration: **a standalone unit**, one
box on a shelf driving three lights with knobs on the front and no bus at all.

That is a legitimate and appealing product (it is rev1's use case, done right),
and this pin map serves both roles from one PCB — populate the encoders, the bus
modules, or all of them. But if every main board is going in a ceiling, spend
those nine pins on margin instead.

## 2. Daughter board — ATtiny3216 (control panel)

One USART, so **populate the RS-485 module or the HC-12, never both.** With only
one fitted there is no bus contention and the RX select jumper is unnecessary.

| # | Port | Function | Notes |
|---:|------|----------|-------|
| **1** | VDD | **+5 V** | from the 12 V → 5 V regulator |
| **2** | PA4 | encoder 2 `B` | |
| **3** | PA5 | encoder 3 `A` | |
| **4** | PA6 | encoder 3 `B` | |
| **5** | PA7 | encoder 1 `SW` | |
| **6** | PB5 | encoder 2 `SW` | |
| **7** | PB4 | encoder 3 `SW` | |
| **8** | PB3 | **MCU RXD** ← module `TXD` | RS-485 *or* HC-12 |
| **9** | PB2 | **MCU TXD** → module `RXD` | **10 kΩ pull-up** — see below |
| **10** | PB1 | HC-12 `SET` | unused in the RS-485 build |
| **11** | PB0 | *free* | |
| **12** | PC0 | **IR receiver** | `PORTC_PORT_vect` — see §9a of architecture.md |
| **13** | PC1 | **WS2812 DIN** | status/link. 100 nF at the LED, 330 Ω in series |
| **14** | PC2 | *free* | |
| **15** | PC3 | *free* | |
| **16** | PA0 | **UPDI** | 1 kΩ series to the programming pad |
| **17** | PA1 | encoder 1 `A` | |
| **18** | PA2 | encoder 1 `B` | |
| **19** | PA3 | encoder 2 `A` | |
| **20** | GND | **GND** | |

**14 assigned, 3 free** (pins 11, 14, 15) — exactly a fourth encoder if you want
one.

### Encoder grouping

| Encoder | `A` | `B` | `SW` |
|---|---|---|---|
| 1 | pin 17 (PA1) | pin 18 (PA2) | pin 5 (PA7) |
| 2 | pin 19 (PA3) | pin 2 (PA4) | pin 6 (PB5) |
| 3 | pin 3 (PA5) | pin 4 (PA6) | pin 7 (PB4) |

The A/B lines need **no interrupt capability** — they are polled from a ~1 kHz
timer ISR and run through a 4-state quadrature machine, which rejects contact
bounce structurally where edge interrupts amplify it. So they can sit anywhere.
IR keeps PORTC to itself: the other PORTC pins are an output or unused, so
`PORTC_PORT_vect` fires only for IR.

**An ATtiny1616 is a drop-in** if you want the panel cheaper — same package, same
pinout, same variant, and a panel needs nowhere near 32 KB.

---

## 3. Wiring an EC11 encoder

The EC11 has five contacts plus a shell. Every one is a plain mechanical short —
there is no active circuitry inside — so **all three signals are active-low** and
all three need pull-ups.

| EC11 pin | Goes to |
|---|---|
| **A** | MCU encoder `A` pin |
| **B** | MCU encoder `B` pin |
| **C** | **GND** — the common for A and B |
| **D** / **E** | switch — **non-polarised**, so either one to GND and the other to the MCU `SW` pin |
| **O** | **GND** — metal shell / mounting tabs |

Per encoder, ×3 on each board:

```
                    VDD
                     │
            ┌────────┼────────┐
           10k      10k      10k
            │        │        │
   A ───────┴──┬──── ┴──┬──── ┴──┬─────── E  (switch)
              10n      10n      100n
               │        │        │
              GND      GND      GND
            (to MCU) (to MCU) (to MCU)

   C ── GND      D or E ── GND     O(shell) ── GND
```

`C` (quadrature common) and whichever of `D`/`E` you ground are both returns, so
they share the ground net — that is correct, not a shortcut.

### Net names → package pins

Nine nets per board. The `A`/`B` pairs are deliberately identical on both boards
so the decoder is the same code; only the switches differ.

| Net | Main board (3226) | Panel (3216) |
|---|---|---|
| `ENC1_A` | pin 17 (PA1) | pin 17 (PA1) |
| `ENC1_B` | pin 18 (PA2) | pin 18 (PA2) |
| `ENC1_SW` | pin 5 (PA7) | pin 5 (PA7) |
| `ENC2_A` | pin 19 (PA3) | pin 19 (PA3) |
| `ENC2_B` | pin 2 (PA4) | pin 2 (PA4) |
| `ENC2_SW` | pin 7 (PB4) | pin 6 (PB5) |
| `ENC3_A` | pin 3 (PA5) | pin 3 (PA5) |
| `ENC3_B` | pin 4 (PA6) | pin 4 (PA6) |
| `ENC3_SW` | pin 12 (PC0) | pin 7 (PB4) |

### Layout note: use resistor arrays

Three encoders means **nine 10 kΩ pull-ups and nine caps** per board. All nine
pull-ups are the same value to the same rail, so two 4-element **resistor arrays**
plus one discrete (or one 8-element array plus one) replaces nine parts with
three — worth it on a panel PCB that has to fit behind a wall plate.

**Pull-ups: fit external 10 kΩ, do not rely on the internal ones.** The ATtiny's
internal pull-ups are 20–50 kΩ and only loosely specified, which leaves the RC
time constant undefined and the line soft against noise pickup on a panel that
fingers touch.

**Caps: 10 nF on A and B, 100 nF on the switch.**

| | τ | Rise to threshold | Verdict |
|---|---|---|---|
| 10 kΩ + **10 nF** | 100 µs | ~90 µs | **use on A/B** |
| 10 kΩ + **100 nF** | 1 ms | ~900 µs | too slow for A/B; **right for the switch** |

At a fast spin (~3 rev/s, 60 detents/s) quadrature edges arrive every **~4.2 ms**,
so a 90 µs rise is ~2 % of the interval — invisible. A 100 nF cap would eat ~20 %
of it and visibly round the waveform. The switch has no such constraint and
benefits from the longer constant.

**Optional 1 kΩ in series** between each contact node and the MCU pin. It limits
the capacitor's discharge current through the contact to ~5 mA, which helps
contact life, and adds a little ESD margin. Worth fitting on panels; skip it on a
main board where the encoder sits on the same PCB.

**Ground the shell (`O`).** It shields the contacts and gives ESD from a user's
fingers a path that is not through the MCU.

### How this meets the firmware

Hardware debounce here is belt-and-braces, not the primary mechanism. The
decoder samples A/B from a **~1 kHz timer ISR** and runs a 4-state quadrature
machine, which rejects bounce structurally. That sampling rate tracks up to
**~125 detents/s (≈6 rev/s)** before aliasing — roughly 2× the fastest a hand
manages. If you find counts dropping on a hard flick, raise the ISR to 2 kHz
before touching the RC.

Two things to leave configurable rather than hard-code:

- **Direction.** Whether clockwise counts up depends only on which contact landed
  on `A` versus `B`. Swap in the lookup table, not in copper.
- **Counts per detent.** EC11 variants differ — some rest with one full quadrature
  cycle per detent, others give two. If one click moves brightness two steps,
  divide by two. Check your part's *pulses per revolution* against its *detents
  per revolution* and make the divisor a `config.h` constant.

---

## 3a. Wiring the HC-12

```
         +5V ──┬──────────────┬── 10k
               │              │
              100n         ┌──┴──┐
               │           │ SET │ ── 2-pin header ── GND   (config only)
              GND          └─────┘
                             │
   VCC ── +5V              (pin 5)
   GND ── GND     ← all GND pins: 2, 7, 8
   RXD ── MCU TXD  (main pin 14 / PC2)
   TXD ── MCU RXD  (main pin 13 / PC1)
   ANT ── no-connect on the PCB; antenna fits the module
```

### `SET` — pull it high, and give yourself a way to pull it low

`SET` is **active-low**: hold it low and the module enters AT command mode;
leave it high and it is a transparent serial link. It has a weak internal
pull-up, so a floating pin *usually* behaves — but "usually" is the problem. A
noise spike that dips `SET` drops the radio into AT mode, where it silently stops
forwarding traffic and looks exactly like a dead link.

**So:**

1. **10 kΩ pull-up to +5 V.** Defines the state. This is the part that must be on
   the board.
2. **A 2-pin header (or solder jumper) from `SET` to GND.** Fit a shunt to
   configure, pull it to run. Costs nothing and means you never have to desolder
   a module to change its channel.

The main board has **no spare MCU pin** for `SET` (all 17 are committed), which is
fine because nothing changes it at runtime — channel, baud and TX power are
set-once. The panel *does* have a pin for it (3216 pin 10 / PB1, unused in the
RS-485 build), so there you can drive it from firmware if you prefer.

**Configuring:** short the `SET` header, power up, and send AT commands over the
module's own `RXD`/`TXD` — either from a USB-serial adapter on those nets, or by
having the MCU relay bytes from its other UART. Remove the shunt to return to
normal operation.

### Baud rate: the HC-12 path must be slower

The HC-12 defaults to **9600 baud**, and unlike RS-485 that is not a number to
casually raise — in FU3 mode a lower serial rate buys better receiver sensitivity
and more range, and the radio's real throughput is well below the UART's anyway.

At 9600 a 10-byte report takes **10.4 ms**. Against the 20 ms cadence of §6.6 that
is >50 % occupancy from a single panel — far too much. **Run the RF path at a
~50 ms cadence (20 Hz)** instead, which drops it to ~17 %.

20 Hz is still smooth on a dimmer, and nothing is lost: the counter protocol
carries the accumulated total regardless of how often it is sent. Two USARTs
means the wired path keeps its 115200 and its 20 ms cadence independently — the
two transports do not have to agree.

Budget end-to-end latency of ~100 ms on the RF path once the module's own
buffering is included. Worth measuring before assuming it feels right.

### Power

Fit **≥100 µF** at the module — 470 µF if it is fed from the 12 V → 5 V linear
regulator — to ride out the ~100 mA transmit burst. Consider turning TX power
down from the default 20 dBm; across one hall you need nothing like 100 mW, and
it cuts the burst.

---

## 4. External parts that are not optional

| Part | Where | Why |
|---|---|---|
| **10 kΩ pulldown** per LED channel | main, pins 6/10/11 | The PT4115 pulls DIM up internally through 200 kΩ. MCU pins are high-Z from power-on until firmware runs, so **without this every light blasts at 100 % on every power cycle**, before any code executes. |
| **10 kΩ pull-up on UART TXD** | both, pin 9 | The RS-485 auto-flow module keys its driver by sniffing TXD. A floating TXD during the MCU's boot window can **assert the driver and jam the whole bus**. Holds it idle-high until firmware takes over. |
| **10 kΩ pull-up on HC-12 `SET`** | RF builds | Floating `SET` can dip into AT mode on noise, where the radio silently stops forwarding. See §3a. |
| **330 Ω series + 100 nF** at each WS2812 | main pin 15, panel pin 13 | Damps the data edge and holds the LED's rail steady through colour changes. |
| **1 kΩ series** on UPDI | both, pin 16 | Standard serialUPDI wiring. |
| **100 nF + 10 µF** | both, pin 1 | Decoupling. |
| **Series Schottky** on the panel's +12 V in | panel | A miswired cable then means "does not power up", not a dead board. |
| **≥470 µF** at the HC-12 | RF builds | Rides out the ~100 mA transmit burst. |

---

## 5. Firmware deltas between the two parts

Same source, two targets. What differs:

| | 3216 (panel) | 3226 (main) |
|---|---|---|
| TCA0 port mux | `PORTMUX.CTRLC` | **`PORTMUX.TCAROUTEA`** |
| USART port mux | `PORTMUX.CTRLB` | **`PORTMUX.USARTROUTEA`** |
| USARTs | 1 | **2** |
| `millis()` lands on | TCD0 | **TCB1** (no TCD0 on 2-series) |
| SRAM | 2 KB | **3 KB** |
| ADC | 10-bit | 12-bit + PGA, different registers |

`takeOverTCA0()` works on both — `millis()` is never on TCA0 — so the PWM engine,
gamma curve and `DRIVER_MIN_ON_NS` floor arithmetic are shared unchanged. The
panel does not use TCA0 at all.
