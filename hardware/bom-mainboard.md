# gliim-3 mainboard — BOM and cost

Derived from `SCH_Schematic1_2026-07-30` (sheet title `glim-mainboard-rev0`).
Designators below are the schematic's own.

> Cross-check against EasyEDA's BOM export before ordering — that comes from the
> netlist and is authoritative on counts. This file carries the part *choices*,
> the safe substitutions, and the two decisions that have to be made first.

---

## ⚠ 1. Read this before ordering anything

### The LED driver is discrete — and the inductor is a real choice

Confirmed against the driver schematic: **PT4115 SOT-89-5 + SS34 + inductor +
150 mΩ sense + 2× 4.7 µF**, one set per channel. (An earlier sheet drew the
driver as a bare 5-pin block, which would not have worked — a hysteretic buck
needs all of these.)

**Fitted: 47 µH / 2.5 A.** It works. The note below is about what it costs and
what to change if the bottom of the range ever disappoints.

Saturation is not the constraint — peak is 767 mA (667 mA plus half the PT4115's
±15 % ripple), so even the 33 µH / 1.6 A part had 2.09× margin and the 2.5 A part
has 3.26×. Either is safe.

What *is* constrained is the inductor's ramp into a minimum `DIM` pulse. The
dimming floor is a ~2 µs on-time; when `DIM` goes high the current climbs from
zero, and how far it gets sets how linear the **bottom** of the range is — which
is the point of the 16-bit engine. Bigger L climbs slower:

| L | @ 150 mΩ / 667 mA | @ 330 mΩ / 303 mA | f_sw | Sat margin |
|---|---:|---:|---:|---:|
| 33 µH / 1.6 A | 55 % | **100 %** | 545 kHz | 2.09× |
| **47 µH / 2.5 A** ← fitted | **38 %** | **84 %** | 383 kHz | 3.26× |
| 82 µH / 0.99 A | 22 % | 55 % | 219 kHz | **1.29× — avoid** |

**With 47 µH fitted, the sense resistor is now the bigger lever.** Dropping to
**330 mΩ** (303 mA) gets the ramp to 84 % — better than 33 µH ever managed at
150 mΩ. So if the lowest settings feel steppy or the curve does not match what
the firmware thinks it is driving, change the resistor before the inductor. It is
a ₹3 part and easier to swap than an inductor.

Avoid the 82 µH regardless: thin on saturation *and* a fifth of setpoint in a
minimum pulse, which is the steppy bottom end this design exists to remove.

### The sense resistor sets the current

If discrete: **150 mΩ → 667 mA per channel**, ~2 A and roughly 18 W across three
channels. Match it to your actual LEDs.

| R_S | I_LED |
|---|---|
| 150 mΩ | 667 mA — 3 W COBs, heavy strip |
| 330 mΩ | 303 mA — 1 W emitters, most strip |
| 500 mΩ | 200 mA — small fixtures |

Buy a few of each; they are ₹3. Use **1206**, not 0805 — 67 mW is comfortable for
1206 and marginal for 0805 in still air.

---

## 2. Modules and actives

| Ref | Part | Qty | ₹ ea | ₹ |
|---|---|---:|---:|---:|
| U11 | **ATtiny3226-SF** SOIC-20 | 1 | 200 | 200 |
| U7 | **MP1584EN 5 V buck module** | 1 | 40 | 40 |
| H1 | **USB-C PD decoy**, set to 15 V | 1 | 60 | 60 |
| W1 | **HC-12** 433 MHz | 1 | 300 | 300 |
| U16 | **RS485→TTL auto-flow module** (HW-0519) | 1 | 31 | 31 |
| SW1–3 | **EC11** encoder, vertical, with push | 3 | 25 | 75 |
| — | Knobs | 3 | 20 | 60 |
| LED4 | **WS2812B** 5050 | 1 | 8 | 8 |
| LED1–3 | LED 0805, warm | 3 | 2 | 6 |
| | | | | **780** |

Buy **2–3 spare ATtiny3226** — megaTinyCore's own part notes call it "difficult
to source", and it is the one item that would stall the whole build.

## 3. LED drivers (×3) — discrete, confirmed

| Ref | Part | Qty | ₹ |
|---|---|---:|---:|
| U1–U3 | PT4115 SOT-89-5 | 3 | 54 |
| D1–D3 | SS34 Schottky, SMA | 3 | 15 |
| L1–L3 | **47 µH / 2.5 A** shielded | 3 | 75 |
| R1,R3,R5 | 150 mΩ 1 %, **1206** — sets 667 mA/ch | 3 | 9 |
| C1–C8 | 4.7 µF **35 V** 1206 (2 per channel) | 6 | 24 |
| | | | **177** |

## 4. Connectors

| Ref | Part | Qty | ₹ |
|---|---|---:|---:|
| RJ1, RJ2 | RJ11 **6P6C** PCB jack | 2 | 30 |
| U8–U10 | 2-pin terminal 3.5 mm (CH1–3 out) | 3 | 45 |
| CN4 | 2-pin terminal (DC_IN) | 1 | 10 |
| CN2 | XH-3A (`5V · UPDI · GND`) | 1 | 8 |
| U15 | 4-pin header (HC-12 debug) | 1 | 5 |
| U13, U19 | 3-pin header + shunt | 2 | 14 |
| F1, F2 | **PPTC 0805, 30 V, 0.2 A hold** — `JK-SMD0805-020-30V` (Jinrui) | 2 | 6 |
| | | | **132** |

## 5. Passives

| Value | Refs | Qty | ₹ |
|---|---|---:|---:|
| 10 kΩ 0805 | R13–R26, R28, R29 | 16 | 16 |
| 470 Ω 0805 | R27 + 3× LED series | 4 | 4 |
| 100 nF 0805 | C10–C13, C20, C21, C23 | 7 | 7 |
| 10 nF 0805 | C14–C19 | 6 | 6 |
| 100 µF electrolytic ≥25 V | C22, C24 | 2 | 12 |
| | | | **45** |

Buy 10× on anything under ₹2 — postage costs more than the parts.

### Sizing the PPTC

**Part: `JK-SMD0805-020-30V`** (Jinrui) — 0805, **30 V**, 200 mA hold, ~₹3.

The voltage rating is the spec people get wrong. `V_BUS` is 15 V and a tripped
PPTC must stand off the whole rail, so a 6 V or 12 V part — which is what most
0805 PPTCs are — can arc and carbonise in exactly the fault it was fitted for.
**An over-voltage PPTC is worse than no PPTC.** 30 V gives proper margin.

Most 0805 PPTCs on the Indian shelves top out at 6–12 V, and the 24–30 V parts
from Littelfuse/Bourns/Weite start at 1812. Jinrui's JK series is the exception
that keeps the small footprint.

**Hold current: 200 mA is enough for every realistic configuration**, derating
the usual ~25 % at 60 °C to ~150 mA usable:

| Panels | Linear regs | Buck regs |
|---:|---:|---:|
| 2 | 60 mA ✓ | 24 mA ✓ |
| 4 | 120 mA ✓ | 47 mA ✓ |
| 6 | **180 mA — tight** | 71 mA ✓ |

The one case it does not cover is **six panels all running linear regulators in a
warm ceiling**. If that is the install, either put bucks on the panels — which
drops the same six to 71 mA — or look for a 0.35 A part in the same 0805/30 V
family.

A short is limited by the PD supply at 3 A, which is 15× hold, so it trips fast.
Trip current is ~400 mA.

The same part serves the panel boards, where a single ~30 mA load will never
trip it but a short still will. One part number across all three boards.

## 6. Cost

| | ₹ |
|---|---:|
| Modules + actives | 780 |
| LED drivers | 177 |
| Connectors | 132 |
| Passives | 45 |
| **Parts, one board** | **~1,130** |
| *without the HC-12 (wired-only node)* | *~830* |
| PCB, 5-up order amortised | ~150–250 |
| **Built board, all in** | **~₹1,300 (wired-only ~₹1,000)** |

The HC-12 is 27 % of the board on its own. Populate it only on nodes that
actually need to reach a panel without a cable.

## 7. Still to verify on the schematic

- **R25** — confirm it pulls up **`TXD_485` (pin 9)**, not `RXD_485` (pin 8).
  This is what stops a booting node asserting the bus driver and jamming
  everything. On RXD it is harmless and does nothing.
- **R26** — appears to pull `SET` to the switched `HC12_5V` rather than always-on
  `+5 V`. Correct if so; confirm on the net.
- **UPDI series resistor** — no 1 kΩ on the schematic between CN2 and PA0.
  Fine if it lives in your programming cable; otherwise add it.
- **R28/R29** (A+/B− bias, 10 kΩ to +5 V and GND) — correct values and rails, but
  mark them **DNP**. Bias belongs on **exactly one node on the whole bus**;
  populated on every board they stack in parallel and load the line.

## 8. Assembly order

1. **Buck module alone.** Power it, set the output to 5.00 V, verify. Before
   anything else goes on the board.
2. MCU + C10 + UPDI header. Flash a blink. If UPDI does not answer, stop here.
3. One driver channel. Confirm **the DIM pulldown holds it dark at power-up,
   before firmware runs** — that is the whole reason R13–R15 exist.
4. Remaining channels, encoders, WS2812.
5. RS-485 module last. Then measure **A–B: it must read open, not 120 Ω** (leave
   the module's `R0` jumper alone).
