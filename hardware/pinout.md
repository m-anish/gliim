# glim — numbered pinouts

Authoritative pin assignments for both boards, by **physical package pin number**,
ready to lay out against.

- **Main board (driver node)** — **ATtiny3226**, SOIC-20. Two USARTs, so RS-485
  *and* HC-12 can both be populated and both be live.
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

## 1. Main board — ATtiny3226 (driver node)

| # | Port | Function | Notes |
|---:|------|----------|-------|
| **1** | VDD | **+5 V** | 100 nF close to the pin |
| **2** | PA4 | *free* | local EC11 `B`, if fitted |
| **3** | PA5 | *free* | local EC11 `SW`, if fitted |
| **4** | PA6 | *free* | DAC-capable |
| **5** | PA7 | Qwiic 3V3 enable | **100 kΩ pulldown** — high-Z at boot |
| **6** | PB5 | **LED ch3** | TCA0 **WO2 ALT1**. 10 kΩ pulldown to DIM |
| **7** | PB4 | IR receiver | `PORTB_PORT_vect`. ⚠ see alt-reset note |
| **8** | PB3 | **RS-485 RXD** | USART0 default |
| **9** | PB2 | **RS-485 TXD** | USART0 default. **10 kΩ pull-up** — see below |
| **10** | PB1 | **LED ch2** | TCA0 WO1 default. 10 kΩ pulldown to DIM |
| **11** | PB0 | **LED ch1** | TCA0 WO0 default. 10 kΩ pulldown to DIM |
| **12** | PC0 | HC-12 `SET` | |
| **13** | PC1 | **HC-12 RXD** | USART1 **ALT1** |
| **14** | PC2 | **HC-12 TXD** | USART1 **ALT1** |
| **15** | PC3 | Status LED | + series resistor |
| **16** | PA0 | **UPDI** | 1 kΩ series to the programming pad |
| **17** | PA1 | I²C **SDA** | TWI0 **alternate** |
| **18** | PA2 | I²C **SCL** | TWI0 **alternate** |
| **19** | PA3 | *free* | local EC11 `A`, if fitted |
| **20** | GND | **GND** | |

**14 assigned, 4 free** (pins 2, 3, 4, 19) — enough for one local encoder plus a
spare.

### Why these and not others

- **LED ch3 must be PB5**, not PB2. WO2's default pin *is* PB2, which USART0 needs
  for TXD. WO2's ALT1 is PB5, and the 2-series mux is still **per output channel**
  (`PORTMUX.TCAROUTEA`), so ch1/ch2 can stay on their defaults while only ch3
  moves. Split mode is not involved and 16-bit resolution is preserved.
- **HC-12 on PORTC** because USART1's ALT1 route is `PC2 TXD / PC1 RXD` — entirely
  clear of PORTB where the LEDs and USART0 live. This is the single fact that lets
  both transports coexist without an AND gate or a co-processor.
- **I²C on PA1/PA2** because TWI0's default PB0/PB1 are LED ch1/ch2. The alternate
  is free now that the joystick is gone.

### ⚠ Do not fuse PB4 as alternate reset

The 20-pin 2-series can relocate `RESET` to **PB4**, keeping PA0 as UPDI. That is
a genuinely useful feature — and it would take the IR receiver's pin away. Leave
`SYSCFG0.RSTPINCFG` alone. Never set `board_hardware.updipin` to anything but
`updi`.

---

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
| **100 kΩ pulldown** on Qwiic enable | main, pin 5 | An LDO with a floating `EN` is undefined. Makes "off" the default. |
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
