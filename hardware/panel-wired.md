# gliim panel — wired

A control panel for the bus. Same three knobs as the mainboard, none of the
power electronics. It sends nudges and shows levels; it drives nothing.

**MCU: ATtiny3216** (SOIC-20). One USART is enough — this board only ever speaks
RS-485. Pin map and passives follow [`pinout.md`](pinout.md) §2 and §3.

---

## 1. What is on it, and what is not

| On | Not on |
|---|---|
| ATtiny3216 | LED drivers (PT4115, inductors, diodes, sense resistors) |
| RS-485 module + 2× RJ11 | HC-12 and its jumpers/debug header |
| 15 V → 5 V regulator | USB-C decoy — power arrives on the cable |
| 3× EC11 + pull-ups/caps | channel output terminals |
| WS2812 chain (status + 3 channel) | |

Power and data both arrive on the same two-pair cable, so the board has exactly
two external connectors — the pair of RJ11 jacks — and nothing else.

## 2. Regulator: buck or linear

`V_BUS` carries **15 V**; the board needs 5 V at ~30 mA.

| | Buck (MP1584EN module) | Linear (7805, DPAK) |
|---|---|---|
| Dissipation | ~0.1 W | **0.3 W** — fine in DPAK with a pour |
| Parts | one module | one part + two caps |
| Cost | ~₹40 | ~₹10 |
| Downside | **a switcher a few centimetres from the RS-485 pair** | heat |

A buck is the obvious call on efficiency and it is what the mainboard already
uses, so it is one less part number. The one reason to hesitate is noise: this
board's whole job is to hear a differential pair, and the switcher would sit
right next to it. At 30 mA the linear's 0.3 W is genuinely nothing.

**Lay out for both.** Footprint the DPAK regulator and the module header at the
same node; populate one. If the bus turns out clean, the buck stays.

## 3. Indication — one pin, four LEDs

The user-facing ask is *a status light and three channel lights*. Do not spend
four GPIOs on it: **chain four WS2812s off the single data pin** (PC1).

| Position | Shows |
|---|---|
| 1 | link state — alive, listening, no peer heard, CRC errors |
| 2–4 | each channel's current level, as brightness |

Four LEDs is 120 µs of interrupts-off per refresh, comfortably inside the
UART's ~174 µs of RX tolerance at 115200 (`pinout.md` §1). Do not extend the
chain past four without dropping the baud rate.

### ⚠ This settles an open architecture question

Channel LEDs mean the panel must **know** each channel's level, which it can only
learn by listening to `LEVELS` beacons. So `architecture.md` §10's *"do panels
display level?"* is answered **yes**, and two things follow:

- Driver nodes must broadcast the beacon **definitely**, not optionally.
- Panels are no longer write-only. They keep a small state table and redraw the
  chain when it changes.

Neither is expensive, and it makes the panel much better: you can see what the
room is set to without looking up at the lights.

## 4. Pin map (ATtiny3216)

Unchanged from [`pinout.md`](pinout.md) §2, with the wired build's spares noted:

| # | Port | Function |
|---:|---|---|
| 1 / 20 | VDD / GND | +5 V from the regulator |
| 2,3,4 | PA4,PA5,PA6 | enc2 `B`, enc3 `A`, enc3 `B` |
| 5,6,7 | PA7,PB5,PB4 | enc1, enc2, enc3 switches |
| 8,9 | PB3,PB2 | MCU RXD ← module TXD · MCU TXD → module RXD |
| 10,11 | PB1,PB0 | *free* (PB1 was HC-12 `SET`, unused here) |
| 12 | PC0 | *free* — IR receiver if wanted |
| 13 | PC1 | **WS2812 chain DIN** |
| 14,15 | PC2,PC3 | *free* |
| 16 | PA0 | UPDI |
| 17,18,19 | PA1,PA2,PA3 | enc1 `A`, enc1 `B`, enc2 `A` |

**Five spare pins.** That is the margin the mainboard does not have — worth
keeping rather than filling.

## 5. BOM

| Ref | Part | Qty | ~₹ | Notes |
|---|---|---:|---:|---|
| U1 | **ATtiny3216-SF** SOIC-20 | 1 | 150 | ATtiny1616 is a drop-in if cheaper — a panel needs nowhere near 32 KB |
| U2 | RS485→TTL auto-flow module (HW-0519) | 1 | 31 | **leave `R0` open** |
| U3 | 7805 DPAK *or* MP1584EN module | 1 | 10 / 40 | see §2 |
| RJ1,RJ2 | RJ11 6P6C PCB jack | 2 | 15 ea | both silkscreened **BUS** |
| SW1–3 | EC11 encoder + knob | 3 | 45 ea | |
| LED1–4 | WS2812B 5050 | 4 | 8 ea | chained |
| D1 | Schottky SS14/BAT54, SMA | 1 | 3 | reverse protection on `V_BUS` — Option A needs it |
| F1 | PPTC 1812 SMD, 0.3 A / 30 V (WT1812-030) | 1 | 10 | same part as the mainboard |
| R1–R9 | 10 kΩ | 9 | — | encoder pull-ups |
| R10 | 10 kΩ | 1 | — | **TXD pull-up** — stops a booting node jamming the bus |
| R11 | 470 Ω | 1 | — | WS2812 data, at the MCU end |
| C1–C6 | 10 nF | 6 | — | encoder A/B |
| C7–C9 | 100 nF | 3 | — | encoder switches |
| C10 | 470 Ω…skip | — | — | |
| C11–C14 | 100 nF | 4 | — | MCU, module, regulator out, WS2812 |
| C15 | 10 µF | 1 | — | regulator output bulk |
| C16 | 22 µF, ≥25 V | 1 | — | regulator input bulk |
| CN1 | XH-3A | 1 | 8 | UPDI: `5V · UPDI · GND` |

No bias resistors here — bias belongs on **one** node for the whole bus, and
that node is a driver.

## 6. Silkscreen

```
gliim  ·  PANEL (wired)  ·  rev A
gliim.starstucklab.com
```

Both jacks **BUS**. Knobs `CH1 CH2 CH3` to match the driver's outputs.
