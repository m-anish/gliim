# The LED channel

Applies to **both revisions** — rev1 (hand-soldered, ATtiny814) and rev2. Building
the PT4115 circuit discretely instead of buying ready-made modules is cheaper and
gives you control over the current setpoint; the whole channel is six parts.

Everything here is derived from the PT4115 datasheet (Rev 2.9,
[`PT4115-datasheet.pdf`](PT4115-datasheet.pdf)) — page references are to that
document.

**Headline numbers:** 6–30 V in, up to 1.2 A out, ~97 % efficient with a long
string, and `I_LED = 0.1 V / R_S`. It is a *hysteretic* buck — no compensation
network, no oscillator, just an inductor, a diode and a sense resistor.

---

## 0. Pick the topology first

Two questions decide everything. Get these wrong and no amount of component
tuning helps.

**(a) What kind of LED load is it?**

- **Constant-current** — bare emitters, star PCBs, COBs, "3 W white LED". No
  resistors built in; something must regulate the *current*. → PT4115.
- **Constant-voltage** — 12 V or 24 V flexible strip. Current-limiting resistors
  are already on the strip; it just wants a stable rail. → **no CC driver at
  all**, see §9.3. This is cheaper *and* dims deeper.

**(b) Is the supply above or below the string voltage?**

The PT4115 is a **buck**: `V_IN` must exceed the string's total forward voltage
by ~3 V. White LEDs are ~3.2 V each, so on a 20 V USB-C PD rail you get 5 LEDs
per channel. Want more?

| Situation | Answer |
|---|---|
| V_IN > V_string + 3 V | **PT4115 buck** — this document |
| Need more *light*, not more LEDs | Raise `I_LED` (§2) or add channels. Usually the right answer — see §9.1 |
| Genuinely need a 20–30 V string from a lower rail | **Boost the rail, then buck per channel** — §9.2 |
| CV LED strip at 12 V / 24 V | **MOSFET per channel** — §9.3 |

---

## 1. Schematic

```
   V_LED (20 V)
     ●─────────────┬───────────────────┐
     │             │                   │
    ═╪═ C_IN      │R_S│ 0.15R 1%       │      ┌──────── to next channel
   4µ7/50V         │   1206            │      │
   (§4)            │                   │      │
     │        ┌────┴────┐          ┌───┴──────┴─┐
     ├────────┤ VIN     │          │            │
     │        │     CSN ├──────────●────────────┼──►│LED string│──┐
     │        │         │       node A          │      (1–5)     │
     │        │  PT4115 │                       │                 │
     │        │         │                    ──┴──  D             │
     │        │      SW ├──────────●──────────  ▲   SS34          │
     │        │         │       node B       ──┬──                │
     │        │     DIM │          │           │                  │
     │        │     GND │          └───────────┴────[ L 47µH ]────┘
     │        └────┬──┬─┘                            Isat ≥ 1.2 A
     │             │  │
     │             │  └──────●───── DIM  ◄─── from MCU (PB0/PB1/PB2)
     │             │         │
     │             │        │R│ 10k   ← pulldown. MANDATORY. See §5.
     │             │         │
     ●─────────────┴─────────┴──── GND
```

**Design point used throughout this document: `R_S = 0.15 Ω` ⇒ 667 mA per
channel.** That's ~10.7 W of LED per channel on a 20 V rail with five white LEDs,
32 W across three channels — a bright room fixture. Every value below follows
from it; §2 has the table if you want a different current.

> **On a capacitor across the LED string:** the datasheet does document one (p13,
> *Reducing output ripple*) and it is perfectly safe — it does *not* destabilise
> the loop, because `R_S` senses the **inductor** current and the inductor stays
> in series. glim still leaves it off, but for a different reason. See §4.3.

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

| I_LED | R_S (E24, 1 %) | P in R_S | 0805 (125 mW) | 1206 (250 mW) |
|---|---|---|---|---|
| 100 mA | 1.0 Ω | 10 mW | 8 % | — |
| 200 mA | 0.5 Ω | 20 mW | 16 % | — |
| 330 mA | 0.3 Ω | 33 mW | 27 % | — |
| 500 mA | 0.2 Ω | 50 mW | 40 % | 20 % |
| **667 mA** | **0.15 Ω** | **67 mW** | 53 % | **27 %** ✅ |
| 770 mA | 0.13 Ω | 77 mW | 62 % | 31 % |
| 1.0 A | 0.1 Ω | 100 mW | 80 % ✗ | 40 % |

**The design point is `R_S = 0.15 Ω`, 1 %, 1206.**

- **Why 1206 rather than 0805.** 67 mW is 53 % of an 0805's rating — legal, but
  it runs hot, and a hot sense resistor drifts, which shows up directly as LED
  current error. 1206 sits at 27 % and stays cool. The part costs the same.
- **1 % is not optional** — the datasheet's ±5 % output accuracy assumes a 1 %
  sense resistor (p5). A 5 % resistor makes the whole current spec meaningless.
- If you only have 0805s: **two 0.3 Ω in parallel** gives 0.15 Ω and halves the
  dissipation to 33 mW each. Perfectly good, and often cheaper than stocking a
  second package.

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

**1 MHz is the hard maximum** (p3); 300–800 kHz is a comfortable target. At
667 mA the ripple is fixed at `ΔI = 0.3 × 0.667 = 200 mA`, so `f_sw` depends only
on L and the string length:

| L | f_sw @ 20 V: 1 → 5 LEDs | Peak | Verdict |
|---|---|---|---|
| 33 µH | 449 – 687 – **772** – 706 – 487 kHz | 772 kHz | OK, smaller part |
| **47 µH** | 315 – 482 – **542** – 496 – 342 kHz | **542 kHz** | ✅ **use this** |
| 68 µH | 218 – 333 – **375** – 343 – 236 kHz | 375 kHz | fine, bulkier |

**47 µH** is the pick: comfortable margin under 1 MHz at every string length, a
physically small part, and it is what the datasheet's own efficiency curves use
at almost exactly this current (47 µH with R_S = 0.13 Ω, p6).

Worked example — 667 mA, 47 µH, 20 V rail, 5 LEDs (V_LED ≈ 16 V):
```
ΔI    = 0.3 × 0.667 A = 200 mA
t_on  = 0.200 × 47µ / (20 − 16)   = 2.35 µs
t_off = 0.200 × 47µ / (16 + 0.4)  = 0.57 µs
f_sw  = 1 / 2.92 µs ≈ 342 kHz ✓
```

> **Size L for your *shortest* string, not your longest.** `f_sw` peaks when
> `V_LED ≈ V_IN/2` — with 47 µH that's 542 kHz at three LEDs versus 342 kHz at
> five. A value that looks relaxed on a long string runs fastest on a mid-length
> one, so read the peak column, not the 5-LED column.

### Saturation current — the rating people get wrong

The hysteretic loop swings the current ±15 % about the mean (85 mV to 115 mV
across `R_S`), so the inductor actually sees:

```
I_min = 0.85 × 667 mA = 567 mA
I_max = 1.15 × 667 mA = 767 mA   ← this is what saturates the core
I_rms ≈ 667 mA                   ← this is what heats it
```

Inductors quote **two** current ratings and you need to check both:

| Rating | Meaning | Requirement | **Spec it at** |
|---|---|---|---|
| **I_sat** | current at which L has dropped (usually −30 %) | ≥ 1.5 × 767 mA peak | **≥ 1.2 A** |
| **I_rms** / I_dc | current for a ~40 °C rise | ≥ 667 mA + margin | **≥ 0.8 A** |

The 1.5× on saturation is not padding. Saturation is a **cliff, not a slope**: as
L collapses, ΔI grows, which raises the peak, which saturates it further — and
the PT4115 has no cycle-by-cycle current limit to catch that runaway. Add
power-up inrush and the fact that I_sat falls with temperature, and 1.2 A is the
honest number for a 667 mA design. **A part whose saturation rating is "1 A" is
not enough** — look for 1.2–1.5 A.

### What the datasheet itself specifies (p12)

Worth checking your choice against PowTech's own table rather than only the
formula:

| Load current | Inductor | Saturation current |
|---|---|---|
| I_out > 1 A | 27–47 µH | \ |
| 0.8 A < I_out ≤ 1 A | 33–82 µH | 1.3–1.5 × load current |
| **0.4 A < I_out ≤ 0.8 A** | **47–100 µH** ← we are here | / |
| I_out ≤ 0.4 A | 68–220 µH | |

667 mA lands in the 47–100 µH band, so **47 µH sits right at the fast end of the
recommended range** — which is what we want (smaller part, still 531 kHz peak).
Their saturation rule is 1.3–1.5 × *load* (0.87–1.0 A) plus "higher than the peak
output current"; **our 1.2 A spec is deliberately more conservative than both**,
for the runaway reason above. Keep it.

The datasheet also names real parts, with saturation ratings — all comfortably
past our requirement:

| Part | L | DCR | I_sat | I²R at 667 mA |
|---|---|---|---|---|
| MSS1038-333 | 33 µH | 0.093 Ω | 2.3 A | 41 mW |
| **MSS1038-473** | **47 µH** | **0.128 Ω** | **2.0 A** | **57 mW** ← the match |
| MSS1038-683 | 68 µH | 0.213 Ω | 1.6 A | 95 mW |

Coilcraft parts are hard to buy in India, but they give you the **spec shape to
match**: 47 µH, DCR ≈ 0.13 Ω, I_sat ≈ 2 A, shielded. Any equivalent will do.

### Parasitics knock ~12 % off

The formulas above ignore resistive drops. The datasheet's full versions (p12)
don't:

```
T_ON  = L·ΔI / (V_IN − V_LED − I_avg·(R_S + r_L + R_SW))
T_OFF = L·ΔI / (V_LED + V_D + I_avg·(R_S + r_L))
        R_SW ≈ 0.6 Ω,  ΔI internally fixed at 0.3 × I_avg
```

With `R_S` = 0.15 Ω and `r_L` = 0.128 Ω, the 5-LED case drops from 342 kHz to
**301 kHz** — the parasitic term eats into the already-small `V_IN − V_LED`
headroom. The error is largest on long strings and negligible on short ones
(3 LEDs: 542 → 531 kHz). It doesn't change the choice of 47 µH; it just means
expect ~300 kHz on the bench, not 342 kHz.

So: **47 µH, I_sat ≥ 1.2 A, I_rms ≥ 0.8 A, shielded.**

Use a **shielded** part — unshielded drum cores radiate straight into the IR
receiver's band at 300–800 kHz — and keep DCR under ~0.3 Ω. At 667 mA a 0.5 Ω
DCR burns 220 mW, more than the IC itself dissipates.

---

## 4. Diode and capacitors

**D — Schottky, and it must be Schottky.** It carries the freewheel current for
most of the cycle (duty is high when V_LED is close to V_IN), so its forward drop
dominates the loss budget.

| | Requirement | Part |
|---|---|---|
| V_R | ≥ V_IN + margin | 40 V for a 20 V rail |
| I_F(av) | ≥ I_LED × (1−D) | 560 mA worst case (1 LED); 133 mA at 5 LEDs |
| Type | Schottky, low V_f | **SS34** (40 V / 3 A) — cheap, stocked, heavily over-rated |

Note the diode carries *most* of the current on **short** strings (low duty), not
long ones. SS34 covers every case here with room to spare.

### C_IN — will one 4.7 µF 1206 X7R do?

**Yes — provided it is rated 50 V, not 25 V.** That caveat is the whole answer,
because MLCC capacitance collapses under DC bias and the effect is much worse
when you run near the part's rating.

What the capacitor has to do, at 667 mA with 5 LEDs (D = 0.80, f_sw = 342 kHz):

```
ripple current   I_rms = I_LED × √(D(1−D)) = 267 mA   (333 mA worst case at D=0.5)
charge per cycle Q     = I_LED × (1−D) × t_on = 312 nC
input ripple     ΔV    = Q / C_eff
```

Now the DC-bias reality for a 1206 X7R at 20 V:

| Part | Typical retention @ 20 V | C_eff | ΔV | Verdict |
|---|---|---|---|---|
| 4.7 µF **50 V** | ~60 % | 2.8 µF | **111 mV** (0.6 %) | ✅ **fine — use this** |
| 2 × 4.7 µF 50 V | ~60 % | 5.6 µF | 56 mV (0.3 %) | nicer, optional |
| 4.7 µF **25 V** | ~30 % | 1.4 µF | 223 mV (1.1 %) | works, but 2× the ripple |
| 4.7 µF 16 V | falls off a cliff | — | — | ✗ don't |

So **one 4.7 µF / 50 V / X7R / 1206 per channel is enough.** 111 mV of ripple on
a 20 V rail is a non-issue, and 267 mA of ripple current is trivial for an MLCC.
A second in parallel is a cheap improvement if you have the board space, and
becomes worthwhile if you can only source 25 V parts.

Three things that matter more than the exact value:

1. **Voltage rating, not capacitance, is the lever.** Going 4.7 µF/25 V →
   4.7 µF/50 V roughly *doubles* the effective capacitance at 20 V. Going
   4.7 µF → 10 µF at the same 25 V rating buys much less than the label suggests.
   1206 also derates more gently than 0805 for the same value — more dielectric
   volume — which is another reason to stay at 1206 here.
2. **Placement beats size.** The cap must sit within a few millimetres of the VIN
   and GND pins; loop inductance past ~10 mm undoes the benefit entirely. See §6.
3. **You still need bulk.** Add one shared **100 µF / 35 V electrolytic** on the
   rail. It is not there for ripple — the ceramics handle that — but to damp the
   LC ringing between your supply cable's inductance and an all-ceramic input,
   which can overshoot well past 20 V on hot-plug. With three channels sharing a
   rail this is doubly worth it.

**Summary for one channel:** `4.7 µF / 50 V / X7R / 1206` at the pin +
`100 µF / 35 V` electrolytic shared across the board.

> The datasheet agrees on all three counts (p12): *"A minimum value of 4.7 µF is
> acceptable if the DC input source is close to the device"*; X7R/X5R or better,
> with **"capacitors with Y5V dielectric … should NOT be used"**; and its own
> suggested part, `GRM42-2X7R475K-50`, is a 4.7 µF X7R rated **50 V**.

### 4.3 C_LED — the shunt capacitor across the LEDs

The datasheet documents this (p13, *Reducing output ripple*) and it is a real,
safe technique — an earlier draft of this document wrongly warned against it:

> *"Peak to peak ripple current in the LED(s) can be reduced, if required, by
> shunting a capacitor C_LED across the LED(s) … A value of 1 µF will reduce the
> supply ripple current by a factor three (approx.). **Note that the capacitor
> will not affect operating frequency or efficiency**, but it will increase
> start-up delay and **reduce the frequency of dimming**, by reducing the rate of
> rise of LED voltage. By adding this capacitor the current waveform through the
> LED(s) changes from a triangular ramp to a more sinusoidal version without
> altering the mean current value."*

**Why it doesn't destabilise anything.** `R_S` sits between VIN and CSN and
senses the **inductor** current, and the inductor stays in series. `C_LED` is in
parallel with the LEDs *only*, so it shunts the AC component away from the diodes
while the inductor keeps its 200 mA triangular ramp. The hysteretic comparator
sees exactly the waveform it saw before — hence "will not affect operating
frequency or efficiency". The loop never knows the cap is there.

**Why glim still leaves it off.** The clause that matters for us is *"reduce the
frequency of dimming, by reducing the rate of rise of LED voltage."* glim's whole
proposition is deep PWM dimming — a **2 µs minimum on-time**, which is a single
switching cycle at ~300 kHz. A 1 µF cap across a 16 V string has to be charged
and discharged before LED current can follow, so pulses that short get smeared
away. You would be trading the bottom of the dimming range — the thing we moved
to 16-bit PWM to win — against ripple you cannot see: ±15 % at **300 kHz** is
invisible to the eye, harmless to the LEDs, and irrelevant to colour.

**Fit C_LED when:**

- you dim with a **DC voltage on DIM** instead of PWM, where the smearing costs
  nothing;
- the light will be filmed by a **high-speed or machine-vision camera** that can
  resolve 300 kHz;
- you want the EMI reduction, or the LED wiring is long enough to radiate.

**Don't fit it when** — as here — PWM dimming depth is the point. If you want it
anyway, start at 100 nF rather than 1 µF and measure the dimming floor on the
bench: ripple reduction scales with capacitance, and so does the damage.

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

> **Put no capacitance on the DIM node.** The datasheet (p13, *Soft-start*) notes
> that a capacitor from DIM to ground adds delay at roughly **0.8 ms per nF** — so
> even 100 pF of well-meant "filtering" buys 80 µs, which is 40× our 2 µs minimum
> pulse and would flatten the bottom of the dimming range. The pulldown is a
> 10 kΩ *resistor* for exactly this reason. (If you ever want a soft-start ramp on
> a non-dimming build, this is the knob to turn.)

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
   Trace resistance here is a direct current error. The datasheet is specific
   (p14): *"connect VIN directly to one end of RS and CSN directly to the
   opposite end of RS with no other currents flowing in these tracks."*
5. **Keep the Schottky's cathode current out of the R_S–VIN track.** Easy to miss,
   and the datasheet calls it out (p14): if the diode's return current shares
   copper with the sense path, its IR drop adds to the sensed voltage and *"may
   give an apparent higher measure of current than is actual"*. The driver then
   silently regulates **low**, and no amount of component-swapping will find it.
6. **Star ground** (p14): bring the high-current returns, the input capacitor's
   ground lead, and the output-filter ground to a single point rather than
   daisy-chaining them.
7. **Thermal pad to a ground pour** (ESOP8). θ_JA is 40 °C/W with the pad, 45 °C/W
   for SOT89-5 (p2).
8. **Put the inductors as far from the IR receiver as the board allows**, and use
   shielded parts. 200 kHz–1 MHz switching noise is the classic cause of an IR
   front-end that "randomly" triggers.

---

## 7. Thermals

Loss in the IC is mostly conduction through the switch:

```
P ≈ I_LED² × R_SW × D        R_SW = 0.4 Ω @ 24 V, 0.6 Ω @ 12 V  (p4)
                             D    = V_LED / V_IN
```

At **667 mA**, 20 V, 5 LEDs (D = 0.8): `P = 0.667² × 0.4 × 0.8 = 142 mW`. With
SOT89-5's θ_JA of 45 °C/W (p2) that is a **6.4 °C rise** — nothing. ESOP8 would
give 5.7 °C, so its thermal pad buys you almost nothing at this current; **plain
SOT89-5 is the right package here**, and it hand-solders far more easily.

Thermal shutdown is at 160 °C with 20 °C hysteresis (p4), and `P_DMAX` is 1.5 W
(p2) — we're at 9 % of it.

**Conclusion: at 667 mA thermal design is a non-issue.** Give each IC a modest
ground pour and move on. Only past ~1 A does the package choice start to matter.

---

## 8. Sanity checklist per channel

Values below are the 667 mA design point (`R_S` = 0.15 Ω, 20 V rail, ≤5 LEDs):

- [ ] `V_IN ≥ V_LED + 3 V` at the hottest the LEDs will run
- [ ] `R_S` = **0.15 Ω, 1 %, 1206** (or 2 × 0.3 Ω 0805 in parallel)
- [ ] `L` = **47 µH, I_sat ≥ 1.2 A, I_rms ≥ 0.8 A, shielded**, DCR < 0.3 Ω
- [ ] `D` = **SS34** (40 V Schottky)
- [ ] `C_IN` = **4.7 µF / 50 V / X7R / 1206** *at the pin* — 50 V, not 25 V
- [ ] One shared **100 µF / 35 V** electrolytic on the rail
- [ ] No `C_LED` across the string — safe, but it costs PWM dimming depth (§4.3)
- [ ] **10 kΩ DIM pulldown fitted** — and *no capacitance* on the DIM node (§5)
- [ ] Kelvin sense traces on `R_S`
- [ ] Supply can deliver **1.7 A at 20 V** for three channels (≥45 W charger)
- [ ] MCU pin idles LOW in firmware before anything else runs

---

## 9. When the supply is below the string

### 9.1 First: do you actually need a longer string?

Usually not. "More light" and "more LEDs in series" are different things, and the
PT4115 gives you the first for free — it drives up to **1.2 A**. On a 20 V rail:

| Per channel | R_S | Power/ch | 3 channels |
|---|---|---|---|
| 5 LEDs @ 330 mA | 0.3 Ω | 5.3 W | 16 W |
| **5 LEDs @ 667 mA** | **0.15 Ω** | **10.7 W** | **32 W** ← design point |
| 5 LEDs @ 1.0 A | 0.1 Ω | 16 W | 48 W |

34 W of white LED is a *lot* of light for a room. Raising current costs one
resistor value and needs no new topology; boosting costs a converter stage,
~10 % efficiency, extra noise near the IR receiver, and board area. **Try the
resistor first.**

Adding channels is the other cheap answer — the ATtiny1616 in rev2 has six PWM
outputs, and the joystick UI already wraps through however many exist.

### 9.2 If you do: boost the rail, then buck per channel

Dedicated *boost* constant-current LED driver ICs with a proper dimming pin
(CAT4139, TPS61165, AP3031 and friends) are excellent, but they are not stocked
by the Indian hobby shops — they're distributor parts. Rather than design around
something you can't buy, use two stages:

```
  12–20 V in ──► [ XL6019 boost module ] ──► 28 V rail ──┬──► PT4115 #1 ──► 8 LEDs
                   set output to 28 V                    ├──► PT4115 #2 ──► 8 LEDs
                                                         └──► PT4115 #3 ──► 8 LEDs
```

The boost only makes a rail; each PT4115 still does per-channel constant current
**and keeps its 5000:1 PWM dimming**. Nothing about the firmware changes.

Boost modules stocked in India, all commodity parts:

| Module | In | Out | Current | ~Price | Notes |
|---|---|---|---|---|---|
| **XL6019** | 3–35 V | 5–40 V | 5 A | ₹230 | Best fit. 220 kHz. [Robu](https://robu.in/product/xl6019-dc-dc-5a-adjustable-boost-power-supply-module/) |
| XL6009 | 3–32 V | 5–35 V | 4 A | ₹150 | Fine, slightly less headroom |
| MT3608 | 2–24 V | ≤28 V | 2 A | ₹60 | Only for one small channel |
| "400 W" CC/CV boost | 8–60 V | 10–60 V | high | ₹450 | Overkill; use if you want >30 W |

**Rules for this arrangement:**

1. **Set the boost to 28 V, not 30 V+.** The PT4115's *recommended* max input is
   30 V (45 V absolute). 28 V leaves margin for boost overshoot and still drives
   ~8 white LEDs (25.6 V) with the 3 V headroom the buck wants.
2. **Efficiency compounds:** ~90 % (boost) × ~95 % (buck) ≈ **85 %** overall,
   versus ~95 % for a direct buck. That is the real price of boosting.
3. **Fuse the input.** A boost converter has a DC path — inductor and diode —
   straight from input to output, so it *cannot* current-limit an output short.
   The switcher shutting down does not save you.
4. **Add bulk between the stages:** ≥220 µF plus a 10 µF ceramic at the boost
   output, and keep the module physically away from the IR receiver. Two
   switchers (220 kHz and ~500 kHz) is twice the broadband noise for a 38 kHz
   front-end to reject.

### 9.3 CV LED strip: skip the driver entirely

If the load is a 12 V or 24 V flexible strip, it already has its resistors. A
constant-current driver is the wrong tool — you want a rail at the strip's
voltage and one low-side N-MOSFET per channel, switched straight from the MCU:

```
   V_strip (12 V / 24 V)
        │
   ┌────┴─────┐
   │  strip   │
   └────┬─────┘
        │
        ├──────────────●  drain
        │           ┌──┴──┐
   PWM ─┤100R├──────┤ G   │  AO3400 (30 V, 5.7 A, logic-level)
        │           └──┬──┘
       │R│ 10k         │ source
        │              │
       GND ───────────GND
```

- **Any logic-level N-MOSFET** with `V_GS(th)` well under 5 V: **AO3400** (SOT-23,
  ≤3 A) or **IRLZ44N / AOD4184** (TO-220, higher current). Ordinary IRF540 is
  *not* logic-level — it won't fully enhance at 5 V.
- **100 Ω gate resistor**, and a **10 kΩ gate pulldown** — same reasoning as the
  DIM pulldown in §5: it holds the channel off while the MCU boots.
- At 305 Hz, switching losses are irrelevant; the FET runs cold.

**The bonus:** a MOSFET has no minimum on-time. The PT4115's ~2 µs floor caps
dimming at ~1640:1 at 305 Hz — a FET is limited only by PWM resolution, so
16-bit gives you the full **65536:1**, smoothly, right down to a single count.
If deep dimming matters more than using bare emitters, a CV strip plus MOSFETs is
the deepest-dimming option available here, and by far the cheapest per channel.

---

## 10. Sourcing (India)

Everything for a discrete channel, from hobby stores rather than distributors:

| Part | Where |
|---|---|
| **PT4115** (SOT89-5) | [Sunrom](https://www.sunrom.com/p/pt4115-led-driver-with-dimming), [Hubtronics](https://hubtronics.in/1785) |
| **Inductor 47 µH shielded, I_sat ≥ 1.2 A** | Robu, Evelta, Quartz Components — check the *saturation* spec, not just "1 A" |
| SS34 Schottky (40 V) | any of them |
| **0.15 Ω 1 % 1206** sense resistor | any of them (or 2 × 0.3 Ω 0805 in parallel) |
| **4.7 µF / 50 V X7R 1206** | any of them — insist on **50 V** |
| 10 kΩ, 100 nF, 100 µF / 35 V electrolytic | any of them |
| XL6019 boost module | [Robu](https://robu.in/product/xl6019-dc-dc-5a-adjustable-boost-power-supply-module/), Zbotic, Quartz Components |
| AO3400 / IRLZ44N | Robu, Evelta |

**SOT89-5 hand-solders easily** — it's a large package with 1.5 mm pitch and a
tab, far friendlier than the SOIC-8. That's what makes the discrete build
realistic for rev1's hand-soldered board.

Rough cost per discrete channel: **PT4115 ~₹25 + inductor ~₹15 + SS34 ~₹5 +
passives ~₹5 ≈ ₹50**, against ~₹120–150 for a ready-made module — and you get to
choose `R_S` instead of accepting whatever the module was built for.

---

## 11. On the AL8862

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
