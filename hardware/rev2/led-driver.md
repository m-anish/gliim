# The PT4115 LED channel

One of these per LED string. Everything here is derived from the PT4115 datasheet
(Rev 2.9, [`PT4115-datasheet.pdf`](PT4115-datasheet.pdf)) — page references are to
that document.

**Headline numbers:** 6–30 V in, up to 1.2 A out, ~97 % efficient with a long
string, and `I_LED = 0.1 V / R_S`. It is a *hysteretic* buck — no compensation
network, no oscillator, just an inductor, a diode and a sense resistor.

---

## 1. Schematic

```
   V_LED (20 V)
     ●─────────────┬───────────────────┐
     │             │                   │
    ═╪═ C_IN      │R_S│ 0.3R 1%        │      ┌──────── to next channel
    10µ/50V        │                   │      │
     │        ┌────┴────┐          ┌───┴──────┴─┐
     ├────────┤ VIN     │          │            │
     │        │     CSN ├──────────●────────────┼──►│LED string│──┐
     │        │         │       node A          │      (1–5)     │
     │        │  PT4115 │                       │                 │
     │        │         │                    ──┴──  D             │
     │        │      SW ├──────────●──────────  ▲   SS34          │
     │        │         │       node B       ──┬──                │
     │        │     DIM │          │           │                  │
     │        │     GND │          └───────────┴────[ L 68µH ]────┘
     │        └────┬──┬─┘
     │             │  │
     │             │  └──────●───── DIM  ◄─── from MCU (PB0/PB1/PB2)
     │             │         │
     │             │        │R│ 10k   ← pulldown. MANDATORY. See §5.
     │             │         │
     ●─────────────┴─────────┴──── GND
```

Current path, switch **on**: `V_LED → R_S → LED → L → SW → GND`.
Switch **off**: the inductor keeps the current going and it freewheels
`L → node B → D → node A → LED → L`, so the LED stays lit — continuous
conduction, which is why efficiency is high and ripple low.

`R_S` sits **high-side, between VIN and CSN**. The IC senses the ~100 mV across
it, turning the switch off at 115 mV and back on at 85 mV (p5). That ±15 %
hysteresis band *is* the current ripple: `ΔI ≈ 0.3 × I_LED`.

---

## 2. Sense resistor — sets the current

```
I_LED = 100 mV / R_S          (p5)
P_RS  = I²·R_S = 0.01 / R_S   ← note: lower R_S dissipates MORE
```

| I_LED | R_S (E24, 1 %) | P in R_S | Package |
|---|---|---|---|
| 100 mA | 1.0 Ω | 10 mW | 0805 |
| 200 mA | 0.5 Ω | 20 mW | 0805 |
| **330 mA** | **0.3 Ω** | **33 mW** | **0805** |
| 370 mA | 0.27 Ω | 37 mW | 0805 |
| 500 mA | 0.2 Ω | 50 mW | 0805 |
| 670 mA | 0.15 Ω | 67 mW | 0805 |
| 770 mA | 0.13 Ω | 77 mW | 1206 |
| 1.0 A | 0.1 Ω | 100 mW | 1206 |

Use a **1 % resistor** — the datasheet's ±5 % output accuracy assumes it (p5).
Above ~700 mA prefer 1206 and the **ESOP8** package (thermal pad).

**330 mA is the sensible default** for indoor lighting: cool, efficient, long
LED life, and it lands on a stock 0.3 Ω part.

> **Lowering full-scale current is also the blunt way to dim deeper.** If the
> fixture turns out too bright at 100 %, raising R_S recentres the whole range
> and is strictly better than squeezing the bottom of the PWM range.

---

## 3. Inductor

The PT4115 is self-oscillating: **L sets the switching frequency**, not the duty
cycle. With ripple fixed at `ΔI = 0.3·I_LED`:

```
t_on  = ΔI·L / (V_IN − V_LED)
t_off = ΔI·L / (V_LED + V_f)
f_sw  = 1 / (t_on + t_off)
```

**1 MHz is the hard maximum** (p3); 400–800 kHz is a comfortable target. Since
`f_sw ∝ 1/(I_LED · L)`, inductance scales inversely with current — roughly
`L × I_LED ≈ 22 µH·A`:

| I_LED | L | Isat rating | f_sw @ 20 V (1→5 LEDs) |
|---|---|---|---|
| **330 mA** | **68 µH** | ≥ 0.6 A | 440 – **757** – 478 kHz |
| 500 mA | 47 µH | ≥ 0.9 A | 421 – **723** – 456 kHz |
| 770 mA | 33 µH | ≥ 1.2 A | 389 – **669** – 422 kHz |
| 1.2 A | 22 µH | ≥ 1.8 A | 374 – **644** – 406 kHz |

Worked example — 330 mA, 68 µH, 20 V rail, 5 LEDs (V_LED ≈ 16 V):
```
ΔI    = 0.3 × 0.33 A = 99 mA
t_on  = 0.099 × 68µ / (20 − 16)   = 1.68 µs
t_off = 0.099 × 68µ / (16 + 0.4)  = 0.41 µs
f_sw  = 1 / 2.09 µs ≈ 478 kHz ✓
```

> **Size L for your *shortest* string, not your longest.** `f_sw` peaks when
> `V_LED ≈ V_IN/2` — with 68 µH at 330 mA on a 20 V rail that's 757 kHz at three
> LEDs, versus 478 kHz at five. A value that looks relaxed on a long string can
> run near the 1 MHz limit on a short one.
>
> The datasheet's typical-application pairing (68 µH with R_S = 0.13 Ω, i.e.
> 770 mA) targets a lower-voltage rail; on a 20 V/5-LED rail that combination
> gives only ~205 kHz. Use the table above for this design.

**Requirements:** rate `Isat ≥ 1.5 × I_LED` (peak is 1.15× and saturation is a
cliff, not a slope); use a **shielded** part — unshielded drums radiate into the
IR receiver; low DCR (< 0.5 Ω) for efficiency.

---

## 4. Diode and capacitors

**D — Schottky, and it must be Schottky.** It carries the freewheel current for
most of the cycle (duty is high when V_LED is close to V_IN), so its forward drop
dominates the loss budget.

| | Requirement | Part |
|---|---|---|
| V_R | ≥ V_IN + margin | 40 V for a 20 V rail |
| I_F(av) | ≥ I_LED | 1 A is plenty at 330 mA |
| Type | Schottky, low V_f | **SS34** (40 V / 3 A) — cheap and stocked |

**C_IN — "must be locally bypassed"** (p2, pin description). Per channel: **10 µF
/ 50 V X7R ceramic** as close to VIN/GND as the layout allows, plus one shared
**100 µF / 35 V** electrolytic on the rail for bulk.

> Ceramic DC-bias derating is real: a 10 µF/25 V X7R at 20 V can be under 4 µF of
> actual capacitance. Spec **50 V** parts on a 20 V rail and you keep most of it.

---

## 5. The DIM pin — and the pulldown that is not optional

From p3 and p5:

| Parameter | Value |
|---|---|
| Internal pull-up | **200 kΩ to an internal 5 V regulator** |
| `V_DIM_H` (full on) | ≥ 2.5 V |
| `V_DIM_L` (off) | ≤ 0.3 V |
| DC brightness control | 0.5 V – 2.5 V |
| Max DIM frequency | 50 kHz |
| Abs. max on DIM | 6 V |
| Quiescent when off | 95 µA |

**A floating DIM pin means full brightness.** The MCU's pins are high-impedance
from power-on until firmware configures them — through the ~30 ms start-up fuse
delay plus the buck's ramp — so without a pulldown every channel blasts at 100 %
on every power cycle. This is the visible flash on rev1.

Size the pulldown so the internal divider stays below `V_DIM_L`:

```
V_DIM = 5 V × R_pd / (200 kΩ + R_pd)  <  0.3 V
      ⇒ R_pd < 12.7 kΩ

R_pd = 10 kΩ:  V_DIM = 5 × 10/210 = 0.238 V  ✓ safely off
```

**Use 10 kΩ**, placed at the driver. Cost to the MCU when driving high: 0.5 mA.

### Driving it

5 V logic straight from the MCU is correct (2.5 V threshold, 6 V abs max). A
~100 Ω series resistor is optional for EMI/ESD; put the indicator LED tap on the
**MCU side** of it.

**PWM dimming keeps colour constant** — the LED runs at full amplitude or not at
all — whereas analog current dimming shifts white LEDs warm. That's why glim
dims with PWM.

### Minimum on-time — the number that sets your dimming floor

The datasheet quotes the usable duty range at two frequencies, and both reduce to
the same physical limit:

| Condition (p3/p4) | Min duty | Period | Min on-time |
|---|---|---|---|
| f_DIM = 100 Hz | 0.02 % | 10 ms | **2.0 µs** |
| f_DIM = 20 kHz | 4 % | 50 µs | **2.0 µs** |

Hence "5000:1" at 100 Hz and 25:1 at 20 kHz. **Dimming depth is therefore
`period / 2 µs` — lower PWM frequency buys depth, higher frequency spends it.**

What that means for glim's 20 MHz / 16-bit / 305 Hz plan:

```
period      = 3.279 ms
2 µs        = 40 counts of 65536
min duty    = 0.061 %   ⇒  ~1640:1
```

Set `PWM_MIN_DUTY` to **~40 counts** (16-bit). Below that the driver stops
regulating and output goes non-monotonic. At 152 Hz (DIV2) it's ~20 counts and
~3280:1.

> This is the finding that drove rev2 to 16-bit: at 8-bit the smallest step
> (0.39 %) is **6.4× coarser than the driver can actually resolve**. The silicon
> was never the limit at these frequencies — resolution was.

### Optional: analog range switching (probably unnecessary)

An external resistor from DIM to GND divides against the internal 200 kΩ to set a
DC level, scaling full-scale current (p5):

| Target V_DIM | R_ext | Effect |
|---|---|---|
| 2.5 V | 200 kΩ | 100 % current |
| 1.5 V | 86 kΩ | ~60 % |
| 1.0 V | 50 kΩ | ~35 % |
| 0.5 V | 22 kΩ | ≈ off |

Switching R_ext in with a small MOSFET gives a "night range" beneath the PWM
range. **Skip it:** 16-bit PWM already reaches the driver's own 5000:1 ceiling,
and analog dimming costs you the constant colour temperature.

---

## 6. Layout

Ranked by how much it matters:

1. **C_IN → VIN/GND loop: minimal area.** This loop carries the switched current;
   its inductance is what rings the SW node and radiates.
2. **D close to the SW pin and to node A.** It's the other half of the fast loop.
3. **Keep the SW node copper small.** It's the dv/dt aggressor — just enough
   copper for current, no more. Never run a signal trace under it.
4. **Kelvin-sense R_S.** You're measuring 100 mV; take VIN and CSN from the pads
   themselves, keep both traces short, matched, and away from the SW node.
   Trace resistance here is a direct current error.
5. **Thermal pad to a ground pour** (ESOP8). θ_JA is 40 °C/W with the pad, 45 °C/W
   for SOT89-5 (p2).
6. **Put the inductors as far from the IR receiver as the board allows**, and use
   shielded parts. 200 kHz–1 MHz switching noise is the classic cause of an IR
   front-end that "randomly" triggers.

---

## 7. Thermals

Loss in the IC is mostly conduction through the switch:

```
P ≈ I_LED² × R_SW × D        R_SW = 0.4 Ω @ 24 V, 0.6 Ω @ 12 V  (p4)
                             D    = V_LED / V_IN
```

At 330 mA, 20 V, 5 LEDs (D = 0.8): `P = 0.33² × 0.4 × 0.8 = 35 mW` — negligible,
~1.5 °C rise. Even at 1 A it's ~0.32 W ⇒ ~14 °C. Thermal shutdown is at 160 °C
with 20 °C hysteresis (p4), and `P_DMAX` is 1.5 W (p2).

**Conclusion: at ≤500 mA per channel, thermal design is a non-issue.** Give each
IC a modest ground pour and move on.

---

## 8. Sanity checklist per channel

- [ ] `V_IN ≥ V_LED + 3 V` at the hottest the LEDs will run
- [ ] `R_S` 1 %, correct package for `0.01/R_S` watts
- [ ] `L`: Isat ≥ 1.5 × I_LED, shielded, f_sw lands in 300 kHz–1 MHz
- [ ] `D`: Schottky, V_R ≥ V_IN + margin
- [ ] `C_IN`: 10 µF/50 V ceramic *at the pin*
- [ ] **10 kΩ DIM pulldown fitted**
- [ ] Kelvin sense traces on `R_S`
- [ ] MCU pin idles LOW in firmware before anything else runs

---

## 9. On the AL8862

The AL8862 (Diodes Inc.) is the same hysteretic-buck idea at 40 V / 1.5 A and is
often suggested as a higher-voltage PT4115. **Do not assume it drops in.** Before
substituting, check three things in *its* datasheet:

1. **Pinout and package** — verify against the PT4115 footprint pad-for-pad.
2. **Sense threshold** — if it isn't 100 mV, every `R_S` value above changes.
3. **DIM idle state and minimum on-time** — the 2 µs figure and the 200 kΩ
   pull-up are PT4115 numbers. The pulldown value and the firmware's
   `PWM_MIN_DUTY` both depend on them.

The board can host either if the footprints match, but the values in this
document are PT4115 values.
