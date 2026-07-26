# glim rev2 — hardware specification

The rev1 board is a hand-soldered proof that the idea works: a joystick, three
PT4115 drivers, an ATtiny814. rev2 keeps that shape and fixes the things rev1
taught us — deeper dimming, an IR remote, a real power inlet, and no floating
DIM lines at power-up.

Design brief, unchanged: **a screenless, tactile, local light dimmer** that a
non-technical person can operate in the dark. Anything wanting Wi-Fi, schedules,
or multi-room coordination belongs in [lokki](https://github.com/m-anish/lokki),
not here.

Companion documents:
- [`../led-driver.md`](../led-driver.md) — the PT4115 channel, with component values
- [`input.md`](input.md) — joystick module *or* 5-way switch + resistor ladder

---

## 1. What changes from rev1, and why

| # | Change | Why |
|---|---|---|
| 1 | **ATtiny814 → ATtiny1616** | rev1 is at 58% of 8 KB before IR decoding and learn-mode exist. 16 KB / 2 KB and 18 I/O ends the pin and flash squeeze. |
| 2 | **LED PWM moves to PB0/PB1/PB2, 16-bit** | The big one. See §2 — it's worth ~6× more dimming depth, for free. |
| 3 | **10 kΩ pulldown on every DIM line** | The PT4115 pulls DIM up internally: **a floating DIM pin means full brightness.** rev1 flashes at full while the MCU boots. |
| 4 | **USB-C PD inlet** | A decoy board gives 9/12/15/20 V from any laptop charger. No barrel jack, no wall-wart hunt. |
| 5 | **IR receiver on-board** | Couch control. The single most useful addition for the actual use case. |
| 6 | **Status pixel on-board** | rev1 proved it: with no screen, "which channel am I steering?" needs an answer that persists. |
| 7 | **Input is a populate-time choice** | Joystick module *or* 5-way tactile + ladder, sharing one ADC pin. |
| 8 | **Reverse-polarity + fusing on the LED rail** | It's going in someone's home. |

---

## 2. The 16-bit PWM decision (read this before changing pins)

rev1 drives the LEDs from **TCA0 in split mode**, which is 8-bit — the dimmest
step is 1/256 = **0.39 %** duty. We assumed that was fine because the driver
couldn't reproduce shorter pulses anyway. **That assumption was wrong.**

The PT4115 datasheet (p3, `D_PWM_LF` / `D_PWM_HF`) gives the usable duty range at
two frequencies, and they agree on the same underlying number:

| Condition | Min duty | Period | ⇒ minimum on-time |
|---|---|---|---|
| f_DIM = 100 Hz | 0.02 % | 10 ms | **2.0 µs** |
| f_DIM = 20 kHz | 4 % | 50 µs | **2.0 µs** |

The driver's floor is a **~2 µs on-time** — hence its advertised 5000:1 at 100 Hz
and 25:1 at 20 kHz. Compare that against what 8-bit resolution actually delivers:

| PWM config | Driver could do | 8-bit delivers | Wasted |
|---|---|---|---|
| 305 Hz (rev1 today) | 0.061 % (1638:1) | 0.39 % (256:1) | **6.4×** |
| 76 Hz (rev1 low tier) | 0.015 % (6580:1) | 0.39 % (256:1) | **25×** |

So resolution, not the driver, is the binding constraint — and more PWM bits
genuinely help. On the ATtiny1616, **TCA0 in normal (16-bit) mode** drives
`WO0/WO1/WO2` → **PB0/PB1/PB2**: exactly three channels, exactly what we need.

Recommended operating point — same frequency as today, 6.4× deeper:

```
F_CPU 20 MHz, TCA0 normal mode, CLKSEL = DIV1, PER = 65535
  → 305 Hz, 16-bit, driver-limited floor ≈ 40 counts ⇒ ~1640:1
```

Drop to DIV2 (152 Hz) when everything is dim and it's ~3280:1. That is the whole
"smooth ramp from near zero" wish, without any analog-dimming hardware.

**The trade-off:** normal mode gives 3 channels; split mode gives 6 but only
8-bit. The board routes both sets of pins, so it's a firmware choice plus which
drivers you populate:

| Mode | Channels | Resolution | Pins |
|---|---|---|---|
| **TCA0 normal** (default) | 3 | 16-bit | PB0, PB1, PB2 |
| TCA0 split | 6 | 8-bit | PB0–PB2 + PA3, PA4, PA5 |

For a home dimmer, dimming *quality* beats channel count. Ship normal mode.

> Consequence for firmware: temporal dithering becomes pointless (16-bit far
> exceeds what dithering bought at 8-bit) and should be compiled out. Gamma
> correction must be computed in 16-bit space.

---

## 3. Block diagram

```
        USB-C PD charger
               │
        ┌──────┴───────┐
        │ PD decoy brd │  solder jumper → 9 / 12 / 15 / 20 V
        └──────┬───────┘
               │ V_LED  (20 V recommended)
       ┌───────┼────────────────────────┬─────────────┐
       │       │                        │             │
   [fuse+RP]   │                        │             │
       │   ┌───┴────┐  ┌───┴────┐  ┌────┴───┐    ┌────┴─────┐
       │   │PT4115#1│  │PT4115#2│  │PT4115#3│    │ 5 V buck │
       │   └───┬────┘  └───┬────┘  └────┬───┘    └────┬─────┘
       │    LED str 1   LED str 2    LED str 3        │ 5 V
       │       ▲           ▲            ▲             │
       │       │DIM        │DIM         │DIM          │
       │    PB0│        PB1│         PB2│             │
       │       └───────────┴────────────┘             │
       │                   │                          │
       │            ┌──────┴───────┐                  │
       └────────────┤  ATtiny1616  ├──────────────────┘
                    └──┬───┬───┬───┘
              PA1/PA7 ─┘   │   └─ PC1 → WS2812 status pixel
              input        └───── PC0 ← TSOP38238 IR receiver
        (joystick OR 5-way ladder)
```

All grounds common. See §5 for why the 5 V rail is drawn off the LED rail rather
than from a second PD board.

---

## 4. MCU: ATtiny1616

**ATtiny1616, 20-pin SOIC** (`ATTINY1616-SNR`). Pin-compatible upgrade path:
**ATtiny3216** (32 KB) if firmware outgrows 16 KB.

| | ATtiny814 (rev1) | **ATtiny1616 (rev2)** |
|---|---|---|
| Flash / SRAM | 8 KB / 512 B | **16 KB / 2 KB** |
| I/O pins | 12 | **18** |
| TCA0 outputs | WO3–WO5 only (PA3–5) | **WO0–WO5** (PB0–2 + PA3–5) |
| 16-bit PWM pins | none broken out | **PB0/PB1/PB2** |
| Package | SOIC-14 | SOIC-20 (hand-solderable) |

Choose SOIC-20 over VQFN-20 unless you're reflowing — it hand-solders fine.

> Not the 2-series (ATtiny1626 etc.): its 12-bit ADC is nice but the 1-series is
> what rev1 firmware and megaTinyCore are proven on, and the ADC is not our
> bottleneck.

### Pin map

| Pin | Function | Notes |
|---|---|---|
| PA0 | UPDI | 1 kΩ series to the programming header |
| PA1 | **Input ADC** | 5-way ladder node, *or* joystick X (AIN1) |
| PA2 | Joystick Y | (AIN2) — unused when the ladder is fitted |
| PA3 | LED ch4 *(expansion)* | TCA0 WO3, split mode only |
| PA4 | LED ch5 *(expansion)* | TCA0 WO4 |
| PA5 | LED ch6 *(expansion)* | TCA0 WO5 |
| PA6 | *free* | DAC-capable — optional analog dim reference |
| PA7 | **Switch input** | joystick SW / ladder centre push, `INPUT_PULLUP` |
| PB0 | **LED ch1 PWM** | TCA0 **WO0** — 16-bit in normal mode |
| PB1 | **LED ch2 PWM** | TCA0 **WO1** |
| PB2 | **LED ch3 PWM** | TCA0 **WO2** (also USART0 TXD default — see below) |
| PB3 | *free* | USART0 RXD default |
| PB4 | *free* | AIN9 |
| PB5 | *free* | AIN8 |
| PC0 | **IR receiver OUT** | TSOP38238 |
| PC1 | **WS2812 data** | status pixel |
| PC2 | *free* | debug serial TX (SoftwareSerial) |
| PC3 | *free* | |

**Known conflict:** PB2 is both LED ch3 and the default USART0 TX. Debug
telemetry therefore uses **SoftwareSerial on PC2** (megaTinyCore ships it).
Moving USART0 via PORTMUX lands on PA1/PA2, which the input needs — so
SoftwareSerial is the clean answer, and it costs nothing in the shipping config.

**PORTC has no ADC** on this part (ADC reaches PA0–PA7, PB0, PB1, PB4, PB5 only).
Keep analog inputs on PORTA.

---

## 5. Power

### LED rail — USB-C PD decoy board

A generic **PD/QC decoy / trigger board** (CH224K-class, ~₹70) negotiates
9/12/15/20 V from any USB-C PD charger; the voltage is set by bridging one of the
labelled solder pads. No PD controller to design.

**Use 20 V.** The PT4115 is a buck, so V_IN must exceed the LED string's total
forward voltage, and *efficiency rises steeply with string length* (datasheet p6:
~80 % with 1 LED, ~93 % with 3, ~97 % with 7). Longer strings at higher voltage
are both more efficient and cheaper per lumen.

| Rail | Max LEDs/string (≈3.2 V white) | Driver efficiency |
|---|---|---|
| 12 V | 3 | ~93 % |
| 15 V | 4 | ~95 % |
| **20 V** | **5** | **~96 %** |

Leave ≥3 V of headroom between V_IN and the string's Vf so the driver stays in
regulation as the LEDs warm up.

Budget at 20 V, 3 channels × 5 LEDs at the **667 mA** design point: **≈32 W of
LED power, ≈1.7 A input**. That needs a **45 W or larger** PD charger — a
20 V/1.5 A (30 W) one is *not* enough. At the gentler 330 mA it is ≈16 W / 0.85 A
and 30 W suffices.

### 5 V logic rail

The board in hand outputs 9/12/15/20 V only — **it has no 5 V setting**, so a
second identical decoy board can't feed the logic. Three options:

| Option | Verdict |
|---|---|
| **Buck from the LED rail** (MP1584 / LM2596 module or an on-board buck) | **Recommended.** One cable, one charger, guaranteed common ground, and the LED rail is always up before logic. |
| Plain USB-C breakout with 2× 5.1 kΩ CC pulldowns | Valid — USB-C default *is* 5 V with no PD contract needed. Costs a second cable and port. |
| Second PD decoy set to 9 V + LDO/buck | Pointless; strictly worse than bucking the 20 V. |

If you do use two supplies: **tie the grounds** and note that supply sequencing is
now arbitrary — which is exactly why the DIM pulldowns below are mandatory.

**Buck input rating matters:** MP1584 tops out at ~28 V — fine at 20 V, no margin
if you ever move up. LM2596 (40 V) is the safer pick.

### Protection

- **Fuse** on the LED rail: 2 A slow-blow (or PTC) after the decoy board.
- **Reverse polarity:** P-channel MOSFET in the high side (near-zero drop) or a
  series Schottky if you'll accept ~0.4 V.
- **TVS** across the LED rail: SMAJ26A for a 20 V rail.
- **Bulk cap:** ≥100 µF electrolytic at the input, plus per-driver ceramics
  ([`../led-driver.md`](../led-driver.md)).

---

## 6. Subsystems

### LED channels ×3 (expandable to 6)
Full circuit, component values, and layout rules in
[**`led-driver.md`**](../led-driver.md). The three things that matter most:

1. **10 kΩ pulldown on each DIM line, right at the driver.** Non-optional — see
   §1 item 3 and the divider maths in the driver doc.
2. Sense resistor sets current: `I_LED = 0.1 V / R_S`. 0.3 Ω ⇒ ~330 mA.
3. Keep the D–L–LED–SW loop tight; that's the fast-switching loop.

### Input — pick one at populate time
Both options share **PA1** (ADC) and **PA7** (switch), so the firmware only needs
a config flag. See [**`input.md`**](input.md).

- **Joystick module** (KY-023 style, 5-pin): proportional — "push harder = faster"
  ramping, which rev1 users like. Bulky, wobbly, needs a 2nd ADC pin (PA2).
- **5-way tactile switch + resistor ladder**: 4 directions on *one* ADC pin, tiny
  SMD part, far nicer mechanically. Digital only, so brightness becomes
  hold-to-accelerate rather than proportional.

Fit both footprints; populate one.

### IR receiver
**TSOP38238** (38 kHz) on **PC0**, with the datasheet-recommended supply filter:
**100 Ω series + 4.7 µF to GND**, plus 100 nF at the pins. Mount it as far from
the PT4115 inductors as the board allows — they switch at 200 kHz–1 MHz and the
broadband noise is what upsets IR front-ends. Prefer TSOP38238 over the cheaper
VS1838B for AGC/noise immunity.

Remote: any **NEC** remote. Firmware plan is **learn-any-NEC** (bind buttons in a
learn mode, store codes in EEPROM), so no specific remote SKU is load-bearing —
including the user's own TV remote. A 44-key RGB-strip remote is the best default:
it already has Bright± / ON / OFF / presets.

### Status pixel
One **WS2812B** on **PC1** (`tinyNeoPixel` ships with megaTinyCore). Colour =
selected channel; dim glow when everything is off, so the control is findable in
a dark room. 100 nF decoupling at the pixel.

### Channel indicator LEDs
LED + 1 kΩ to GND on each **DIM** line — **zero extra pins**, and brightness
mirrors each channel's level for free. Keep them on the MCU side of any series
resistor in the DIM path.

### Programming
6-pin header: `UPDI (via 1 kΩ), GND, 5 V, ` + spare. serialUPDI wiring is in
[`../../docs/hardware.md`](../../docs/hardware.md). Never fuse `RSTPINCFG` to
anything but UPDI.

---

## 7. BOM (one 3-channel unit)

| # | Part | Qty | Notes |
|---|---|---|---|
| 1 | ATtiny1616-SNR (SOIC-20) | 1 | or ATtiny3216 |
| 2 | PT4115 (SOT89-5 or ESOP8) | 3 | ESOP8 has a thermal pad — prefer it >500 mA |
| 3 | Inductor 47 µH, **I_sat ≥ 1.2 A**, I_rms ≥ 0.8 A | 3 | shielded; scale with current per driver doc |
| 4 | Schottky SS34 (40 V/3 A) | 3 | |
| 5 | Sense resistor, 1 %, **1206** | 3 | **0.15 Ω ⇒ 667 mA** (see table in driver doc) |
| 6 | **4.7 µF / 50 V X7R 1206** | 3 | one per driver, at V_IN — 50 V matters, see driver doc |
| 7 | 100 µF / 35 V electrolytic | 1 | bulk on the LED rail |
| 8 | 10 kΩ resistor | 3 | **DIM pulldowns — mandatory** |
| 9 | USB-C PD decoy board | 1 | jumper to 20 V |
| 10 | 5 V buck module (LM2596) | 1 | from the LED rail |
| 11 | TSOP38238 | 1 | + 100 Ω, 4.7 µF, 100 nF |
| 12 | WS2812B | 1 | + 100 nF |
| 13 | 5-way tactile switch | 1 | *or* joystick module — see input doc |
| 14 | Ladder resistors 470 Ω/1 k/2.2 k/4.7 k, 1 % | 1 ea | if ladder fitted |
| 15 | Indicator LED + 1 kΩ | 3 ea | optional |
| 16 | 100 nF ceramic | 4–5 | MCU, pixel, IR |
| 17 | 1 kΩ | 1 | UPDI series |
| 18 | Fuse 2 A + SMAJ26A TVS + P-FET | 1 ea | protection |
| 19 | Screw terminals, 2-pin | 3 | LED string outputs |

Electronics ≈ **₹900–1200 / $11–15** per unit, excluding LEDs and charger.

---

## 8. Firmware work this implies

Roughly in order:

1. **TCA0 normal-mode 16-bit PWM** on PB0/PB1/PB2 (replaces split-mode
   `HCMP0/1/2` writes). Gamma in 16-bit; delete the dither ISR.
2. **Retune** `PWM_MIN_DUTY` to the driver's real 2 µs floor (~40 counts at
   305 Hz) instead of the 8-bit fudge.
3. **Pin/config changes** for the ATtiny1616 (`config.h` only, by design).
4. **IR NEC decode + learn mode** — TCB0 input capture, codes in EEPROM.
5. **Ladder input decode** behind a `GLIM_INPUT_LADDER` flag, with
   hold-to-accelerate ramping.
6. Keep: soft transitions, boot fade, watchdog, EEPROM versioning, factory reset,
   variable PWM frequency (now with more room, since 16-bit tolerates a lower
   frequency without losing steps).

---

## 9. Open decisions

| Question | Default if unanswered |
|---|---|
| 3 channels at 16-bit, or 6 at 8-bit? | **3 × 16-bit** — dimming depth beats channel count here |
| Joystick or 5-way ladder? | Fit both footprints, populate the **5-way** |
| One cable (buck) or two (second USB-C)? | **One cable**, buck from the LED rail |
| LED current per channel? | **667 mA** (R_S = 0.15 Ω) — revisit against the actual fixture |
| Analog deep-dimming (§2 of driver doc)? | **Skip.** 16-bit PWM already reaches the driver's own 5000:1 limit |
