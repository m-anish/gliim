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
| **8** | PB3 | **RS-485 RXD** | USART0 default |
| **9** | PB2 | **RS-485 TXD** | USART0 default. **10 kΩ pull-up** |
| **10** | PB1 | **LED ch2** | TCA0 WO1 default. 10 kΩ pulldown to DIM |
| **11** | PB0 | **LED ch1** | TCA0 WO0 default. 10 kΩ pulldown to DIM |
| **12** | PC0 | encoder 3 `SW` | |
| **13** | PC1 | **HC-12 RXD** | USART1 **ALT1** |
| **14** | PC2 | **HC-12 TXD** | USART1 **ALT1** |
| **15** | PC3 | Status LED | + series resistor |
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

### ⚠ Zero spare pins

Every usable pin is committed. A first-revision board with no margin is how
respins happen — the classic sequence is "I just need one more pin for a
mode/test/indicator line." Two ways to buy margin back:

**Ladder variant — switches on one ADC pin.** Put the three encoder switches on a
resistor ladder into **PA7 (AIN7)** instead of three GPIOs. That is 7 pins for
three encoders instead of 9, and it lets you keep I²C:

| | 3 encoders on GPIO | 3 encoders, ladder switches |
|---|---|---|
| Encoder pins | 9 | **7** |
| I²C / Qwiic | impossible | **PA1 / PA2** |
| Spare | 0 | 0 (with I²C) or **2** (without) |

The ladder analysis is in [`input.md`](input.md) §2. Two caveats: the 2-series ADC
is 12-bit with a PGA and different registers, so that decode needs porting; and a
ladder cannot resolve two switches pressed at once — irrelevant here, since nobody
presses two knobs simultaneously.

**Or populate fewer encoders.** Two encoders on GPIO leaves 3 spare. The
footprints cost nothing unpopulated.

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
| **8** | PB3 | **UART RXD** | ← module TXD (RS-485 *or* HC-12) |
| **9** | PB2 | **UART TXD** | → module RXD. **10 kΩ pull-up** — see below |
| **10** | PB1 | HC-12 `SET` | unused in the RS-485 build |
| **11** | PB0 | *free* | |
| **12** | PC0 | **IR receiver** | `PORTC_PORT_vect` — see §9a of architecture.md |
| **13** | PC1 | Status LED | + series resistor |
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

## 3. External parts that are not optional

| Part | Where | Why |
|---|---|---|
| **10 kΩ pulldown** per LED channel | main, pins 6/10/11 | The PT4115 pulls DIM up internally through 200 kΩ. MCU pins are high-Z from power-on until firmware runs, so **without this every light blasts at 100 % on every power cycle**, before any code executes. |
| **10 kΩ pull-up on UART TXD** | both, pin 9 | The RS-485 auto-flow module keys its driver by sniffing TXD. A floating TXD during the MCU's boot window can **assert the driver and jam the whole bus**. Holds it idle-high until firmware takes over. |
| **1 kΩ series** on UPDI | both, pin 16 | Standard serialUPDI wiring. |
| **100 nF + 10 µF** | both, pin 1 | Decoupling. |
| **Series Schottky** on the panel's +12 V in | panel | A miswired cable then means "does not power up", not a dead board. |
| **≥470 µF** at the HC-12 | RF builds | Rides out the ~100 mA transmit burst. |

---

## 4. Firmware deltas between the two parts

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
