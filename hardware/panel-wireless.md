# gliim panel — wireless

A control panel with no cable at all. Same three knobs, a battery, and a radio.
For the wall where a cable genuinely cannot go — and the one place in the system
where the design gets meaningfully harder.

**MCU: ATtiny3216.** Pin map follows [`pinout.md`](pinout.md) §2.

---

## 1. Against the wired panel

| Added | Removed |
|---|---|
| HC-12 433 MHz radio | RS-485 module |
| 18650 holder + cell | both RJ11 jacks |
| TP4056 charger **with protection** | 15 V → 5 V regulator |
| Physical ON/OFF switch | PPTC fuse |
| Boost converter to 5 V | reverse-protection Schottky |
| 5th WS2812 — battery state | |

## 2. ⚠ The rail problem

The cell gives **3.0–4.2 V**. Of the loads:

| Part | Needs | On a bare cell |
|---|---|---|
| ATtiny3216 @ 10 MHz | ≥2.7 V | fine |
| HC-12 | 3.2–5.5 V | fine |
| **WS2812B** | ~3.5–5.3 V | **fails below ~3.5 V** |

So a bare 18650 runs everything right up until the cell drops below ~3.5 V, at
which point the LEDs die first and the panel appears broken while still working.

**Fit a boost to 5 V** (MT3608 class). Everything then runs at one voltage for
the whole discharge curve. The cost is quiescent draw — see below, it matters
more than it looks.

## 3. ⚠ Battery life is a design constraint, not a footnote

2500 mAh, and each decision is worth more than the one before it:

| Design | Draw | Runtime |
|---|---:|---:|
| Everything always on, HC-12 in FU3 | 28 mA | **3.7 days** |
| HC-12 idling in FU2 (~80 µA) | 12 mA | 8.6 days |
| \+ WS2812 chain power-gated when idle | 7 mA | 15 days |
| \+ MCU asleep between touches | 2.6 mA | 40 days |
| \+ low-quiescent boost | 0.6 mA | **165 days** |

Three things follow, and none is optional if you want more than a week:

- **Power-gate the WS2812 chain with a P-FET.** Five of them idle at ~1 mA each
  even when dark — the controller IC never sleeps. That is the single largest
  load on an otherwise quiet board.
- **Sleep the MCU**, waking on encoder pin-change. It has nothing to do between
  touches.
- **Do not leave the radio in FU3.** FU2 idles at microamps; FU3 at ~16 mA.

### The consequence nobody expects

**A sleeping panel cannot hear the `LEVELS` beacon.** So its channel LEDs cannot
show live level the way the wired panel's do.

The resolution is the natural one: **wake on touch, show, then sleep.** You only
look at the LEDs when your hand is on a knob, and that is exactly when the panel
is awake and listening. Budget ~10 s awake after the last touch. A wired panel
displays continuously; a wireless one displays on demand. Say so in the firmware
comments so nobody later "fixes" it.

### WS2812 brightness and current — what actually matters

**The status LED is always driven dim, and never near white.** Colour is used to
mean something (link state, battery state), never to be bright. Treat full-white
as a bug.

⚠ But be clear about *why*, because the obvious reason is wrong: **the LED
current does not come from the GPIO.** The MCU pin drives only the `DIN` data
line, which is a CMOS input drawing microamps regardless of how bright the LED
is. All the LED current comes from the WS2812's own VDD pin, straight off the
rail. So the 20 mA per-pin limit has nothing to do with WS2812 brightness and
cannot be exceeded by it.

Keeping it dim buys two real things instead:

- **Supply current.** One WS2812 at full white is ~60 mA. On the battery panel
  that is the difference between weeks and days.
- **It is a status light in a dark room.** At full brightness it stops being an
  indicator and becomes a nuisance.

**Where GPIO current genuinely does matter on these boards** is the channel
indicator LEDs, which hang directly off MCU pins through 470 Ω: about 6.4 mA
each at 5 V, ~19 mA for three. That is inside the per-pin limit but is a real
share of the total I/O budget — and it is the number to watch if the indicators
are ever changed to something brighter.

## 4. Charging and the switch

- **TP4056 module with protection** — the DW01 + dual-MOSFET variant, not the
  bare charger. The bare one has no over-discharge cutoff, and an 18650 taken
  below ~2.5 V is damaged. Confirm the module has the protection IC on it; the
  two versions look nearly identical and cost the same.
- **The switch goes between the protection output and the boost input**, so
  "off" is genuinely off and not just a sleeping MCU. A slide switch the user can
  feel is worth more here than any firmware state.
- Charge over the TP4056's own USB-C. There is no reason to route it through the
  board.

## 5. Indication — one pin, five LEDs

Chain them off the single WS2812 data pin (PC1):

| Position | Shows |
|---|---|
| 1 | link — heard the driver / did not |
| 2–4 | channel levels, on demand (§3) |
| 5 | **battery** — green, amber, red, and a slow pulse while charging |

Five LEDs is ~150 µs of interrupts-off per refresh. At the HC-12's 9600 baud a
byte takes 1.04 ms, so there is an order of magnitude of headroom — the four-LED
ceiling from `pinout.md` §1 is a *115200 RS-485* limit and does not bind here.

Battery state needs an ADC reading of the cell: divide it with 100 kΩ/100 kΩ into
a spare pin (PB0), and gate the divider with the same P-FET as the LEDs so it is
not a permanent 20 µA drain.

## 6. BOM

| Ref | Part | Qty | ₹ ea | ₹ |
|---|---|---:|---:|---:|
| U1 | ATtiny3216-SF SOIC-20 | 1 | 150 | 150 |
| W1 | **HC-12** 433 MHz | 1 | 300 | 300 |
| U2 | **TP4056 module with protection** | 1 | 40 | 40 |
| U3 | **MT3608** boost module (or low-Iq equivalent) | 1 | 45 | 45 |
| BT1 | 18650 holder, PCB mount | 1 | 30 | 30 |
| — | 18650 cell, protected | 1 | 250 | 250 |
| SW4 | Slide switch, SPST | 1 | 10 | 10 |
| SW1–3 | EC11 + knob | 3 | 45 | 135 |
| LED1–5 | WS2812B 5050 | 5 | 8 | 40 |
| Q1 | P-channel MOSFET (AO3401 class) | 1 | 5 | 5 |
| R1–R9 | 10 kΩ — encoder pull-ups | 9 | 1 | 9 |
| R10 | 10 kΩ — TXD pull-up | 1 | 1 | 1 |
| R11 | 10 kΩ — HC-12 `SET` pull-up | 1 | 1 | 1 |
| R12 | 470 Ω — WS2812 data | 1 | 1 | 1 |
| R13,R14 | 100 kΩ — battery divider | 2 | 1 | 2 |
| R15 | 100 kΩ — P-FET gate pull-up | 1 | 1 | 1 |
| C1–C6 | 10 nF — encoder A/B | 6 | 1 | 6 |
| C7–C9 | 100 nF — encoder switches | 3 | 1 | 3 |
| C10–C12 | 100 nF — MCU, radio, LEDs | 3 | 1 | 3 |
| C13 | 220 µF — at the HC-12 | 1 | 8 | 8 |
| CN1 | XH-3A — UPDI | 1 | 8 | 8 |
| U4 | 3-pin header + shunt — HC-12 `SET` | 1 | 7 | 7 |
| | | | | **~₹1,055** |
| | *without the cell* | | | *~₹805* |

## 7. Pin map notes

Against `pinout.md` §2, this build uses:

- **PB1** — HC-12 `SET` (the wired panel leaves this free)
- **PB0** — battery sense (ADC)
- **PC0** — P-FET gate, power-gating the WS2812 chain and the divider
- **PC1** — WS2812 chain
- **PB2/PB3** — MCU TXD → HC-12 `RXD`, MCU RXD ← HC-12 `TXD`
- **PC2, PC3** — free

The encoder A/B pairs are unchanged from every other board in the family, so the
quadrature decoder is the same code everywhere.

## 8. Silkscreen

```
gliim  ·  PANEL (wireless)  ·  rev A
gliim.starstucklab.com
```

`CH1 CH2 CH3` under the knobs. `BATT` by the fifth LED. `CHARGE` by the TP4056's
USB-C. Mark the cell holder **+** clearly — it is the one mistake on this board
that ends in smoke.
