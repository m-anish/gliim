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
| 1 | **ATtiny814 → ATtiny3216** | rev1 is at 71 % of 8 KB with IR in. 32 KB / 2 KB and 18 I/O end the pin and flash squeeze; same `txy6` pinout as the 1616, and cheaper locally. |
| 2 | **LED PWM → 16-bit** (on PB3/PB4/PB5) | The big one. See §2 — worth ~6× more dimming depth, for free. |
| 3 | **10 kΩ pulldown on every DIM line** | The PT4115 pulls DIM up internally: **a floating DIM pin means full brightness.** rev1 flashes at full while the MCU boots. |
| 4 | **USB-C PD inlet** | A decoy board gives 9/12/15/20 V from any laptop charger. No barrel jack, no wall-wart hunt. |
| 5 | **IR receiver on-board** | Couch control. The single most useful addition for the actual use case. |
| 6 | **Status LED on-board** | One LED, one meaning: system on. The channels indicate themselves — see §7. |
| 7 | **Input is a populate-time choice** | Joystick module *or* 5-way tactile + ladder, sharing one ADC pin. |
| 8 | **Qwiic I²C connector** | PB0/PB1, wired straight through — the board runs at Qwiic's own 3.3 V. Turns the ambient-light sensor and friends into plug-in modules. §5. |
| 9 | **WS2812 → one plain LED** | No interrupts-off bit-bang beside the IR decoder, works at any rail, one pin instead of two, and smaller firmware. §7. |
| 10 | **3.3 V logic rail** (was 5 V) | Makes Qwiic native and costs only flicker margin — see §4. Nothing else in the design objects. |
| 11 | **Reverse-polarity + fusing on the LED rail** | It's going in someone's home. |

---

## 2. The 16-bit PWM decision (read this before changing pins)

rev1 originally drove the LEDs from **TCA0 in split mode**, which is 8-bit — the
dimmest step is 1/256 = **0.39 %** duty. We assumed that was fine because the
driver couldn't reproduce shorter pulses anyway. **That assumption was wrong**,
and finding out is what set this whole decision. rev1 has since been rewired to
16-bit as well, so both boards now run the same scheme.

The PT4115 datasheet (p3, `D_PWM_LF` / `D_PWM_HF`) gives the usable duty range at
two frequencies, and they agree on the same underlying number:

| Condition | Min duty | Period | ⇒ minimum on-time |
|---|---|---|---|
| f_DIM = 100 Hz | 0.02 % | 10 ms | **2.0 µs** |
| f_DIM = 20 kHz | 4 % | 50 µs | **2.0 µs** |

The driver's floor is a **~2 µs on-time** — hence its advertised 5000:1 at 100 Hz
and 25:1 at 20 kHz. Compare that against what 8-bit resolution actually delivers:

| PWM config | Driver could do | 8-bit delivered | Wasted |
|---|---|---|---|
| 305 Hz | 0.061 % (1638:1) | 0.39 % (256:1) | **6.4×** |
| 76 Hz | 0.015 % (6580:1) | 0.39 % (256:1) | **25×** |

So resolution, not the driver, is the binding constraint — and more PWM bits
genuinely help. On the ATtiny3216, **TCA0 in normal (16-bit) mode** drives
`WO0/WO1/WO2`: exactly three channels, exactly what we need. rev1 takes them at
their default pins (PB0/PB1/PB2); rev2 relocates all three to PB3/PB4/PB5 to free
PB0–PB2 for I²C and the UART (§4). Either way it is the same three timer outputs,
so **one firmware source builds for both boards** — see §9.

Operating point, per board — `CLKSEL = DIV1, PER = 65535` on both, so the clock
sets everything:

```
rev1   20 MHz →  305 Hz, floor ≈ 40 counts ⇒ ~1640:1
rev2   10 MHz →  152 Hz, floor ≈ 20 counts ⇒ ~3280:1
```

rev2's slower clock (§4, a consequence of the 3.3 V rail) actually *doubles* the
depth: the driver's fixed 2 µs floor is a smaller slice of a longer period. That
is the whole "smooth ramp from near zero" wish, with no analog-dimming hardware.

**The trade-off:** normal mode gives 3 channels; split mode gives 6 but only
8-bit. The board routes both sets of pins, so it's a firmware choice plus which
drivers you populate:

| Mode | Channels | Resolution | Pins |
|---|---|---|---|
| **TCA0 normal** (default) | 3 | 16-bit | **PB3, PB4, PB5** |
| TCA0 split | 6 | 8-bit | PB3, PB4, PB5 + PA3, PA4, PA5 |

For a home dimmer, dimming *quality* beats channel count — ship normal mode. The
6-channel option and what 8-bit actually costs (less than you'd think, with
dithering) are quantified in §4.

> On rev2 all three sit on TCA0's *alternate* pins (PB3/PB4/PB5), freeing
> PB0/PB1 for I²C and PB2 for the UART. See §4.

> Consequence for firmware, now implemented: temporal dithering was deleted
> (16-bit far exceeds what dithering bought at 8-bit) and gamma is computed in
> 16-bit space.

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
       │   │PT4115#1│  │PT4115#2│  │PT4115#3│    │3V3 buck │
       │   └───┬────┘  └───┬────┘  └────┬───┘    └────┬─────┘
       │    LED str 1   LED str 2    LED str 3        │ 3.3 V
       │       ▲           ▲            ▲             │
       │       │DIM        │DIM         │DIM          │
       │    PB3│        PB4│         PB5│             │
       │       └───────────┴────────────┘             │
       │                   │                          │
       │            ┌──────┴───────┐                  │
       └────────────┤  ATtiny3216  ├──────────────────┘
                    └──┬───┬───┬───┘
              PA1/PA7 ─┘   │   ├─ PC1 → status LED ("system on")
              input        │   └─ PC0 ← TSOP38238 IR receiver
        (joystick OR       └───── PB0/PB1 → Qwiic I²C (direct — the
         5-way ladder)                       board is 3.3 V, §5)
```

All grounds common. See §6 for why the 3.3 V rail is bucked off the LED rail
rather than taken from a second PD board.

---

## 4. MCU: ATtiny3216

**ATtiny3216, 20-pin SOIC** (`ATTINY3216-SNR`) — chosen over the ATtiny1616
simply because it is **cheaper locally right now**. It is the same die family in
the same package: PlatformIO reports both as variant `txy6`, so the **pinout is
identical** and picking one over the other is purely a flash-size decision.

| | ATtiny814 (rev1) | ATtiny1616 | **ATtiny3216 (rev2)** |
|---|---|---|---|
| Flash | 8 KB | 16 KB | **32 KB** |
| SRAM | 512 B | 2 KB | **2 KB** |
| EEPROM | 128 B | 256 B | **256 B** |
| I/O pins | 12 | 18 | **18** |
| Variant | txy4 | txy6 | **txy6** (same pinout as 1616) |
| Package | SOIC-14 | SOIC-20 | **SOIC-20** (hand-solderable) |

Firmware today is **5.7 KB — 17.5 % of the 3216's flash**, against 71 % of the
814's. That headroom is the point: IR learn mode, the ladder decoder, scenes and
whatever else can land without ever re-running this arithmetic.

The **256 B EEPROM** (double rev1's 128 B) matters too — the persist struct with
six learned IR codes is ~35 B, so there is room for several stored scenes.

**Clock: 10 MHz, because the board is 3.3 V.** Table 36-3 needs V_DD ≥ 4.5 V for
20 MHz, and the maximum is linear from 10 MHz at 2.7 V to 20 MHz at 4.5 V — so
3.3 V allows ~13.3 MHz, and 10 MHz is the highest option below that. BOD stays at
2.6 V (BODLEVEL2), exactly the condition that table specifies for the 10 MHz row.

That halves the PWM frequency versus rev1 — and *deepens* the dimming, because
the driver's fixed 2 µs floor is a smaller slice of a longer period:

| | rev1 — 5.0 V / 20 MHz | **rev2 — 3.3 V / 10 MHz** |
|---|---|---|
| PWM frequency | 305 Hz | **152 Hz** |
| Floor | 40 counts (0.061 %) | **20 counts (0.031 %)** |
| Dimming ratio | 1638:1 | **3277:1** |
| Qwiic | needs LDO + shifter | **native** |

152 Hz is well clear of flicker fusion; what you give up is stroboscopic margin —
a waving hand or a phone camera will show it more readily than at 305 Hz. That is
the one real cost of the 3.3 V decision, taken deliberately in exchange for a
native I²C port, a rail-agnostic status LED, and twice the dimming depth.

Choose SOIC-20 over VQFN-20 unless you're reflowing — it hand-solders fine.

> Not the 2-series (ATtiny1626 etc.): its 12-bit ADC is nice, but the 1-series is
> what the firmware and megaTinyCore are proven on here, and the ADC is not our
> bottleneck.

### Pin map — exact, SOIC-20

Physical numbering from datasheet **DS40002205A Table 5-1** ("SOIC 20-Pin"
column), not inferred. The board routes all six LED channels; populate three or
six.

| Pin | Port | 3-channel build (default) | 6-channel build | Peripheral |
|----:|------|---------------------------|-----------------|------------|
| 1 | **VDD** | +5 V | +5 V | |
| 2 | PA4 | *free* | **LED ch5** | TCA0 WO4 |
| 3 | PA5 | *free* | **LED ch6** | TCA0 WO5 · VREFA |
| 4 | PA6 | *free* | *free* | DAC out |
| 5 | PA7 | **Switch** | **Switch** | joystick SW / ladder centre, `INPUT_PULLUP` |
| 6 | PB5 | **LED ch3** | **LED ch3** | TCA0 WO2 *(alt)* |
| 7 | PB4 | **LED ch2** | **LED ch2** | TCA0 WO1 *(alt)* |
| 8 | PB3 | **LED ch1** | **LED ch1** | TCA0 WO0 *(alt)* |
| 9 | PB2 | **UART TXD** | **UART TXD** | USART0 TxD — adapter's RX here |
| 10 | PB1 | **I²C SDA** | **I²C SDA** | TWI0 SDA → Qwiic |
| 11 | PB0 | **I²C SCL** | **I²C SCL** | TWI0 SCL → Qwiic |
| 12 | PC0 | **IR receiver** | **IR receiver** | TSOP38238 OUT |
| 13 | PC1 | **Status LED** | **Status LED** | single LED — "system on" |
| 14 | PC2 | *free* | *free* | |
| 15 | PC3 | *free* | *free* | (WO3 alt — leave clear) |
| 16 | PA0 | **UPDI** | **UPDI** | 1 kΩ series to header |
| 17 | PA1 | **Joystick X / ladder** | same | AIN1 |
| 18 | PA2 | **Joystick Y** | same | AIN2 — unused with the ladder |
| 19 | PA3 | *free* | **LED ch4** | TCA0 WO3 · EXTCLK |
| 20 | **GND** | GND | GND | |

Only PA3/PA4/PA5 change role between the two builds, so **one PCB serves both** —
populate three drivers or six and let firmware pick the mode. PA6, PC2 and PC3
stay free either way.

#### Why the LEDs are on PB3/PB4/PB5, not PB0/PB1/PB2

Because **I²C has nowhere else to go.** Table 5-1 puts TWI0 on exactly two pin
pairs: `PB1/PB0` (default) or `PA1/PA2` (alternate). PA1/PA2 are the joystick's
ADC inputs, so the alternate is unavailable — which leaves PB0/PB1, and those
were the LED outputs.

TCA0's outputs relocate instead: `PORTMUX.CTRLC` bits `TCA00/TCA01/TCA02` move
WO0/WO1/WO2 to **PB3/PB4/PB5**. §15.3.3 restricts only TCA03/04/05 to split mode,
so all three work in the 16-bit normal mode we actually use. One register write
in `pwmInit()` buys three things the 14-pin rev1 could never have:

- **I²C / Qwiic** on PB0/PB1 (§5 below);
- a **real hardware UART** on PB2 for debug telemetry;
- consequently **IR and telemetry at the same time** — SoftwareSerial's interrupt
  dispatcher, which collides with the IR ISR on rev1, is no longer involved.

**The one thing it costs:** USART0's RXD is PB3, now LED ch1 — so debug serial is
**transmit-only**. That's all telemetry needs; firmware clears `RXEN` so the
receiver can't sit interrupting on the PWM waveform.

### 6 channels means 8-bit — and how much that costs

You're right that six channels forces 8-bit: WO3/WO4/WO5 only exist in **split
mode**, and split mode is two 8-bit timers. There is no 6 × 16-bit option on this
part — it has a single TCA.

| Build | Mode | Resolution | Floor | Ratio | ≈ perceived |
|---|---|---|---|---|---|
| **3 channels** | TCA0 normal | 16-bit | 0.031 % | **3277:1** | ~2.5 % |
| 6 channels | TCA0 split | 8-bit | 0.391 % | 256:1 | ~8.1 % |
| 6 channels **+ dithering** | TCA0 split | 8+2 bits | 0.098 % | **1024:1** | ~4.4 % |

Straight 8-bit is **6.4× shallower** — the exact gap that motivated 16-bit in the
first place. But temporal dithering (which glim had at 8-bit, and dropped as
redundant once 16-bit landed) buys most of it back:

- run split mode at **DIV32 = 1221 Hz** (rev2's 10 MHz clock; DIV64 on rev1's
  20 MHz), where one count is 3.2 µs — still above the PT4115's 2 µs floor, so
  single-count pulses remain honest;
- dither 2 bits → **10-bit effective, 1024:1**, and the dither pattern repeats at
  1221/4 = **305 Hz**, comfortably invisible.

That lands 6-channel at 1024:1 against the 3-channel 3277:1 — a factor of 3.2
rather than 12.8. Don't push the prescaler further down (one count below ~2 µs):
the pulse falls under the driver's minimum on-time and the bottom of the range
stops being monotonic.

> **Not yet implemented.** The firmware today is normal-mode/16-bit only. A
> 6-channel build needs the split-mode path and the dither ISR restored — both
> existed before and are in git history. Budget ~1 KB of the 32 KB.

**PORTC has no ADC** on this part (ADC reaches PA0–PA7, PB0, PB1, PB4, PB5 only).
Keep analog inputs on PORTA.

---

## 5. Qwiic / I²C — native, because the board is 3.3 V

TWI0 comes out on **PB1 (SDA)** and **PB0 (SCL)**, straight onto a standard
**Qwiic connector: JST-SH 4-pin, 1 mm pitch**:

| Qwiic pin | Signal | Wire colour |
|---|---|---|
| 1 | GND | black |
| 2 | **3.3 V** | red |
| 3 | SDA | blue |
| 4 | SCL | yellow |

**No LDO, no level shifter, no pull-up gymnastics** — the board's logic rail *is*
Qwiic's rail. Four wires from the MCU and the regulator to the socket, plus
4.7 kΩ pull-ups on SDA/SCL (many breakouts carry their own; fit these anyway so
the bus works with a bare sensor).

That is the main reason rev2 runs at 3.3 V rather than 5 V. The alternative was a
3.3 V LDO plus a two-MOSFET bidirectional translator with pull-ups on both sides
— seven parts to bridge a gap that simply doesn't exist if the board already
speaks 3.3 V.

> Historical note, in case you're tempted back to 5 V: a shared 3.3 V pull-up
> does **not** substitute for a level shifter. I²C is open-drain, so the pull-up
> sets the high level — pull to 3.3 V and the bus idles at 3.3 V, but an ATtiny at
> V_DD = 5 V needs **V_IH ≥ 0.7 × 5 = 3.5 V** to read a high. It half-works on the
> bench and drifts with temperature. See §4 for what going back to 5 V would cost
> and gain.

### What it's for

An I²C port turns several roadmap items into plug-in modules rather than board
respins: an **ambient-light sensor** (VEML7700, BH1750) to cap brightness in
daylight, an **RTC** for time-of-day behaviour, a small **OLED** if glim ever
wants a screen, or an **I/O expander** for more buttons. It's the cheapest
future-proofing on the board.

Firmware exposes nothing on the bus yet — `GLIM_I2C` in `config.h` just records
that the pins are reserved.

---

## 6. Power

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

### 3.3 V logic rail

The board in hand outputs 9/12/15/20 V only — **it has no 5 V setting**, so a
second identical decoy board can't feed the logic. Three options:

**Buck the 20 V rail down to 3.3 V** — one cable, one charger, guaranteed common
ground, and the LED rail is always up before the logic. An adjustable module
(MP1584, LM2596) set to 3.3 V is fine.

> **Do not use an LDO from 20 V.** At 20 V in, 3.3 V out and ~30 mA you would burn
> 0.5 W in the regulator to deliver 0.1 W. It's a switching job.

The 3.3 V rail feeds the MCU, the joystick (or ladder), the IR receiver, the
status LED **and the Qwiic connector** — one rail, no translation anywhere.

**Buck input rating matters:** MP1584 tops out at ~28 V — fine at 20 V, no margin
if you ever move up. LM2596 (40 V) is the safer pick.

Everything downstream is happy at 3.3 V: the PT4115's `V_DIM_H` is **2.5 V**, so
3.3 V logic clears it with margin; the TSOP38238 runs from 2.5 V; and the joystick
and resistor ladder are *ratiometric* — the pot divides V_DD and the ADC
references V_DD, so they return identical counts at 3.3 V and 5 V. No
recalibration, no lost resolution.

### Protection

- **Fuse** on the LED rail: 2 A slow-blow (or PTC) after the decoy board.
- **Reverse polarity:** P-channel MOSFET in the high side (near-zero drop) or a
  series Schottky if you'll accept ~0.4 V.
- **TVS** across the LED rail: SMAJ26A for a 20 V rail.
- **Bulk cap:** ≥100 µF electrolytic at the input, plus per-driver ceramics
  ([`../led-driver.md`](../led-driver.md)).

---

## 7. Subsystems

### LED channels ×3 (expandable to 6)
Full circuit, component values, and layout rules in
[**`led-driver.md`**](../led-driver.md). The three things that matter most:

1. **10 kΩ pulldown on each DIM line, right at the driver.** Non-optional — see
   §1 item 3 and the divider maths in the driver doc.
2. Sense resistor sets current: `I_LED = 0.1 V / R_S`. **0.15 Ω ⇒ 667 mA**, the design point; 0.3 Ω ⇒ 330 mA if you want it gentler.
3. Keep the D–L–LED–SW loop tight; that's the fast-switching loop. A capacitor across the LED string is safe (it doesn't affect the loop) but costs PWM dimming depth — see §4.3 of the driver doc.

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

### Status LED — one LED, one meaning

**A single LED on PC1** through a resistor. It means exactly one thing: *the
system is on*. It does not encode the selected channel, and that is the point.

The channels indicate themselves, in two ways that are strictly better than a
shared indicator:

1. **Selection** — picking a channel blinks *that light* (`ackBlink`). The thing
   you're about to adjust identifies itself, across the room, with no legend to
   learn and no colour code to remember.
2. **Level** — a small indicator LED on each driver's **DIM line** mirrors that
   channel's brightness, because it hangs directly off the PWM output. Three
   channels give you a live three-bar meter at the control, for **zero extra
   pins**. See *Channel indicator LEDs* below.

So the status LED is just a lamp, and one GPIO is all it costs. Brightness is
software PWM (`STATUS_LED_DIV`, "lit 1 ms in every N" — default 12.5 % at
125 Hz), so it consumes no timer and can be turned down if it's distracting at
night. Set `STATUS_LED_ACTIVE_LOW` if you wire it to V_DD rather than GND.

`GLIM_STATUS_HEARTBEAT` optionally makes it pulse slowly instead of sitting
steady. Steady says "powered"; pulsing says "firmware is running", which
distinguishes a live board from one the watchdog keeps resetting. Off by
default — useful on the bench, busier in a bedroom.

> During **IR learn mode** the status LED is the only channel of feedback, so it
> speaks in blink *counts* rather than colours: *n+1* blinks to prompt for
> action *n*, one long flash to accept, four rapid flashes to reject a duplicate,
> three slow flashes when saved. Identical on rev1's WS2812 and rev2's plain LED.

This replaced a WS2812. Three reasons, in order of how much they matter:

- **No interrupts-off window.** The WS2812 bit-bang blocks interrupts ~30 µs per
  update, right next to an IR decoder whose ISR times edges to a few µs.
- **Rail-agnostic.** The WS2812 needs V_DD ≥ 3.5 V, which is what pinned rev2 to
  5 V in §5. An LED and a resistor work anywhere.
- **Smaller and simpler.** Dropping `tinyNeoPixel` and the colour logic saved
  **434 B** on rev2 and freed a pin.

### Channel indicator LEDs — the other half of the story

**LED + 470 Ω to GND on each driver's DIM line** (470 Ω at 3.3 V gives ~3 mA;
use 1 kΩ on a 5 V board). Zero extra pins: the line is
already a PWM output, so the indicator's brightness *mirrors that channel's
level* for free — a live three-bar meter beside the control.

This is what lets the status LED stay dumb. Between "that light just blinked at
me" and "this bar shows how bright that channel is", the channels answer both
questions a channel-coded indicator would have tried to answer.

It does not disturb the DIM signal:

| MCU state | DIM line | Indicator |
|---|---|---|
| drives HIGH | 5 V — the MCU sources ~3 mA extra | lit |
| drives LOW | 0 V | off |
| high-Z (boot) | 0.24 V, held by the 10 kΩ pulldown | off — 0.24 V is far below V_f |

That last row matters: the indicator can't defeat the pulldown, because an LED
simply doesn't conduct at 0.24 V. Tap it on the **MCU side** of any series
resistor in the DIM path.

### Programming
6-pin header: `UPDI (via 1 kΩ), GND, 5 V, ` + spare. serialUPDI wiring is in
[`../../docs/hardware.md`](../../docs/hardware.md). Never fuse `RSTPINCFG` to
anything but UPDI.

---

## 8. BOM (one 3-channel unit)

| # | Part | Qty | Notes |
|---|---|---|---|
| 1 | **ATtiny3216-SNR** (SOIC-20) | 1 | ATtiny1616 is pin-identical if cheaper on the day |
| 2 | PT4115 (SOT89-5 or ESOP8) | 3 | ESOP8 has a thermal pad — prefer it >500 mA |
| 3 | Inductor 47 µH, **I_sat ≥ 1.2 A**, I_rms ≥ 0.8 A | 3 | shielded; scale with current per driver doc |
| 4 | Schottky SS34 (40 V/3 A) | 3 | |
| 5 | Sense resistor, 1 %, **1206** | 3 | **0.15 Ω ⇒ 667 mA** (see table in driver doc) |
| 6 | **4.7 µF / 50 V X7R** ×2 per channel (0805) | 6 | one at V_IN, one at the diode — see driver doc |
| 7 | 100 µF / 35 V electrolytic | 1 | bulk on the LED rail |
| 8 | 10 kΩ resistor | 3 | **DIM pulldowns — mandatory** |
| 9 | USB-C PD decoy board | 1 | jumper to 20 V |
| 10 | **3.3 V buck module** (LM2596/MP1584, adjustable) | 1 | from the LED rail — not an LDO |
| 11 | TSOP38238 | 1 | + 100 Ω, 4.7 µF, 100 nF |
| 12 | LED + 1 kΩ | 1 | status indicator, "system on" — §7 |
| 12b | LED + 1 kΩ | 3 (or 6) | per-channel indicators on the DIM lines — §7 |
| 13 | 5-way tactile switch | 1 | *or* joystick module — see input doc |
| 14 | Ladder resistors 470 Ω/1 k/2.2 k/4.7 k, 1 % | 1 ea | if ladder fitted |
| 15 | Indicator LED + 1 kΩ | 3 ea | optional |
| 16 | 100 nF ceramic | 4–5 | MCU, pixel, IR |
| 17 | 1 kΩ | 1 | UPDI series |
| 18 | Fuse 2 A + SMAJ26A TVS + P-FET | 1 ea | protection |
| 19 | Screw terminals, 2-pin | 3 | LED string outputs |
| 20 | 4.7 kΩ | 2 | I²C pull-ups on SDA/SCL |
| 21 | JST-SH 4-pin SMD socket | 1 | Qwiic / STEMMA QT — wires straight through |

Electronics ≈ **₹900–1200 / $11–15** per unit, excluding LEDs and charger.

---

## 9. Firmware

**One source builds both boards.** `include/config.h` carries a `GLIM_BOARD`
switch (1 = rev1/ATtiny814, 2 = rev2/ATtiny3216) selecting the pin map; every
tunable below it is shared. The build environment sets it, so nothing needs
editing to change target:

```bash
utils/flash.sh                    # rev1 (ATtiny814) — the default
utils/flash.sh --rev2             # rev2 (ATtiny3216)
utils/flash.sh --rev2 --fuses     # fuses for a fresh 3216
utils/flash.sh --rev2 --debug     # telemetry build (IR auto-disabled)
```

Current footprint: **6 430 B of 32 768 (19.6 %)**, 137 B of 2 048 RAM (6.7 %).

### Already done (shared with rev1)

- TCA0 normal-mode **16-bit PWM** on PB0/PB1/PB2, duty via the buffered
  `CMPnBUF` registers; dithering deleted; gamma in 16-bit.
- Dimming floor expressed as a **time** (`DRIVER_MIN_ON_NS`, the driver's ~2 µs)
  and converted to counts per prescaler, so the brightness-scheduled frequency
  tiers stay correct.
- **IR NEC decode + learn mode** — falling-edge ISR, six learnable actions,
  codes in EEPROM.
- Status indicator (WS2812 on rev1, **plain LED** on rev2 — same "system on"
  meaning either way), soft transitions,
  fixed-duration boot fade, watchdog,
  EEPROM struct versioning, factory-reset gesture.

### Still to do for rev2

1. **Ladder input decode** behind a `GLIM_INPUT_LADDER` flag, with
   hold-to-accelerate ramping ([`input.md`](input.md) §2.5 has the decoder).
2. **Retune** `JOY_*` deadzones/thresholds for whichever input gets populated.
3. *Optional:* move the IR decoder to `attachInterrupt()` so IR and debug
   telemetry can run together — see §4. Purely a convenience; 32 KB affords it.

## 10. Open decisions

| Question | Default if unanswered |
|---|---|
| 3 channels at 16-bit, or 6 at 8-bit? | **3 × 16-bit** — dimming depth beats channel count here |
| Joystick or 5-way ladder? | Fit both footprints, populate the **5-way** |
| One cable (buck) or two (second USB-C)? | **One cable**, buck from the LED rail |
| LED current per channel? | **667 mA** (R_S = 0.15 Ω) — revisit against the actual fixture |
| Analog deep-dimming (§2 of driver doc)? | **Skip.** 16-bit PWM already reaches the driver's own 5000:1 limit |
