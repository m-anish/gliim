# gliim-3 mainboard — BOM and cost

Derived from `SCH_Schematic1_2026-07-30` (sheet title `glim-mainboard-rev0`).
Designators below are the schematic's own.

> Cross-check against EasyEDA's BOM export before ordering — that comes from the
> netlist and is authoritative on counts. This file carries the part *choices*,
> the safe substitutions, and the two decisions that have to be made first.

---

## ⚠ 1. Read this before ordering anything

### The PT4115 blocks have no external components

On the schematic each driver is a five-pin block — `DIM · VDD · GND · LED+ ·
LED-` — with nothing else attached. A **bare PT4115 cannot work like that**: it
is a hysteretic buck and needs an inductor, a Schottky, and a sense resistor to
function at all. The earlier 3D render *did* show `SS34`, a shielded inductor and
a `150 mΩ` sense resistor per channel.

So one of two things is true, and they cost different money:

| | What to buy |
|---|---|
| **They are modules** (ready-made PT4115 boards) | 3 × driver module, ~₹60 ea. Nothing else. |
| **They are bare ICs** and the passives live elsewhere in the design | 3 × PT4115 SOT-89-5 + inductor + SS34 + sense resistor + caps |

**Confirm which before you order.** If they are bare ICs and the passives are
genuinely absent from the netlist, the boards will not light anything.

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

## 3. LED drivers (×3) — pick one column

| If modules | ₹ | | If discrete | ₹ |
|---|---:|---|---|---:|
| PT4115 driver module ×3 | 180 | | PT4115 SOT-89-5 ×3 | 54 |
| | | | SS34 SMA ×3 | 15 |
| | | | 100 µH shielded, ≥1 A sat ×3 | 75 |
| | | | 150 mΩ 1 % **1206** ×3 | 9 |
| | | | 4.7 µF **35 V** 1206 ×3 | 12 |
| | | | 100 nF 50 V ×3 | 3 |
| **subtotal** | **180** | | **subtotal** | **168** |

## 4. Connectors

| Ref | Part | Qty | ₹ |
|---|---|---:|---:|
| RJ1, RJ2 | RJ11 **6P6C** PCB jack | 2 | 30 |
| U8–U10 | 2-pin terminal 3.5 mm (CH1–3 out) | 3 | 45 |
| CN4 | 2-pin terminal (DC_IN) | 1 | 10 |
| CN2 | XH-3A (`5V · UPDI · GND`) | 1 | 8 |
| U15 | 4-pin header (HC-12 debug) | 1 | 5 |
| U13, U19 | 3-pin header + shunt | 2 | 14 |
| F1, F2 | PPTC ~200 mA | 2 | 20 |
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

## 6. Cost

| | ₹ |
|---|---:|
| Modules + actives | 780 |
| LED drivers | ~175 |
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
