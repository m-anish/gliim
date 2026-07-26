# Input: joystick module *or* 5-way switch + resistor ladder

rev2 supports both. They share **PA1** (analog) and **PA7** (switch), so the board
carries both footprints, you populate one, and firmware picks with a config flag.

| | Joystick module | 5-way tactile + ladder |
|---|---|---|
| Pins used | PA1 + PA2 + PA7 (**3**) | PA1 + PA7 (**2**) |
| Directions | proportional (analog) | on/off (digital) |
| Brightness feel | *push harder = ramp faster* | hold-to-accelerate |
| Mechanics | tall, wobbly, module on wires | small SMD part, crisp detent |
| Cost | ~₹80 | ~₹40 + 4 resistors |
| Diagonals | native | resolvable, see §2.4 |

**Recommendation: the 5-way switch.** The mechanical feel is the product here —
a cheap thumbstick's mush undoes good firmware — and it saves a pin. The one real
loss is proportional ramping, which hold-to-accelerate replaces acceptably.

---

## 1. Option A — analog joystick module (rev1-compatible)

The usual 5-pin KY-023-style module: two 10 kΩ potentiometers plus a push switch.

```
   5V ──────────● VCC
                │
        ┌───────┴────────┐
        │ joystick module│
        │  (2× 10k pot)  │
        └─┬────┬────┬────┘
          │    │    │
         X│   Y│  SW│
          │    │    │
   PA1 ●──┴─┬  │    │        PA2 ●──┴─┬       PA7 ●──┴──── (INPUT_PULLUP,
            │  │    │                 │                     active low)
          ═╪═ 100n  │               ═╪═ 100n
            │       │                 │
   GND ●────┴───────┴─────────────────┴────
```

- Wiper impedance peaks at ~2.5 kΩ (pot centre) — well inside the ADC's 10 kΩ
  limit. The 100 nF caps are optional but cheap noise insurance.
- The module's 5 V must be the **same rail as the MCU's VDD** so the readings stay
  ratiometric — the pots divide whatever you feed them.
- Firmware centres both axes at boot; leave the stick released at power-up.

> **rev1 lesson:** X and Y came out transposed relative to the schematic, and one
> axis was inverted. Keep `JOY_X_PIN`/`JOY_Y_PIN` and `JOY_*_INVERT` in
> `config.h` — expect to flip at least one on a new build.

---

## 2. Option B — 5-way tactile switch + resistor ladder

Four directions on **one ADC pin**. The trick is old and reliable (it's how the
DFRobot LCD keypad shield works): a fixed pull-up to VDD, and each direction
pulls the node to GND through a *different* resistor, giving each button its own
voltage.

The centre push stays on its own GPIO — see §2.4 for why that matters.

### 2.1 Circuit

```
                    5V
                     │
                    │R0│ 4.7k 1%
                     │
   PA1 ●─────────────●──────────┬─────┬─────┬─────┐
   (AIN1)            │          │     │     │     │
                   ═╪═ 100n    │R│   │R│   │R│   │R│
                     │       470  1k0  2k2  4k7
                     │          │     │     │     │   ← all 1 %
                     │         UP   DOWN  LEFT RIGHT
                     │          │     │     │     │
                     │        ──┴──  ─┴──  ─┴──  ─┴──   5-way switch
                     │         ┌─┴─────┴─────┴─────┴─┐  direction contacts
                     │         │      COMMON         │
                     │         └──────────┬──────────┘
                     │                    │
   PA7 ●─────────────┼────────────────────┼──── CENTRE push contact
   (INPUT_PULLUP)    │                    │
   GND ●─────────────┴────────────────────┴────
```

Each direction resistor runs from the ADC node to its switch contact; the
switch's common goes to GND. Pressing a direction therefore puts that resistor in
series to ground, forming a divider with `R0`.

### 2.2 Values and the resulting ADC bands

```
count = 1023 × R_btn / (R0 + R_btn)          R0 = 4.7 kΩ
```

| Button | R_btn | V (at 5 V) | **ADC count** | Source impedance | Current when pressed |
|---|---|---|---|---|---|
| **UP** | 470 Ω | 0.455 V | **93** | 427 Ω | 967 µA |
| **DOWN** | 1.0 kΩ | 0.877 V | **179** | 824 Ω | 877 µA |
| **LEFT** | 2.2 kΩ | 1.594 V | **326** | 1.5 kΩ | 725 µA |
| **RIGHT** | 4.7 kΩ | 2.500 V | **512** | 2.35 kΩ | 532 µA |
| *idle* | ∞ | 5.000 V | **1023** | 4.7 kΩ | 0 |

Three properties worth noting:

- **Source impedance never exceeds 4.7 kΩ**, comfortably inside the AVR ADC's
  ~10 kΩ recommendation, so no sample-and-hold error. Don't scale these resistors
  up.
- **Idle reads full-scale.** A disconnected or unpopulated ladder reads 1023 =
  "nothing pressed", which is the safe failure mode.
- **UP and DOWN get the two lowest resistances** deliberately — see §2.4.

Tolerance is a non-issue: with 1 % parts the worst-case shift is **±5 counts** at
the RIGHT band (the most sensitive), against band gaps of 86–511 counts. Even 5 %
resistors would decode correctly.

### 2.3 Decoding — use windows, not thresholds

The obvious decode is nearest-midpoint. **Don't** — use a narrow acceptance
window around each nominal value and treat anything outside as "no valid press":

| Reading | Meaning |
|---|---|
| 63 – 123 | UP |
| 149 – 209 | DOWN |
| 296 – 356 | LEFT |
| 482 – 542 | RIGHT |
| ≥ 950 | idle |
| *anything else* | **ignore** |

`±30` counts is wide enough for tolerance and noise (needs ±5) and narrow enough
that the gaps — 86, 147, 186, 511 counts — never overlap. The payoff is in the
next section: an ambiguous input produces *no action* instead of a wrong one.

Debounce in software (~25 ms, as rev1 already does); the 100 nF adds a 470 µs
electrical smoothing that helps but doesn't debounce on its own.

### 2.4 Simultaneous presses — the one real weakness

Two buttons pressed together put their resistors **in parallel**, giving a
*lower* reading than either alone. On switches that permit diagonals:

| Combination | Parallel R | Count | Decodes as |
|---|---|---|---|
| UP + RIGHT | 427 Ω | 85 | **UP** ✓ |
| UP + LEFT | 387 Ω | 78 | **UP** ✓ |
| DOWN + RIGHT | 824 Ω | 153 | **DOWN** ✓ |
| DOWN + LEFT | 687 Ω | 130 | *(no window)* → **ignored** |
| LEFT + RIGHT | 1.5 kΩ | 247 | *(no window)* → ignored — mechanically impossible anyway |

This is why UP/DOWN carry the smallest resistors: a diagonal press resolves to
the **brightness** axis, which is the action people actually want mid-gesture,
and the one genuinely ambiguous case (DOWN+LEFT) lands in a gap and is discarded
rather than firing the wrong channel change.

**And it's why the centre push stays on its own GPIO.** "Push while nudging a
direction" is a natural gesture and a very plausible combination — putting it in
the ladder would make it collide with everything. On PA7 with an internal pull-up
it's independent, unambiguous, and free.

### 2.5 Firmware sketch

```c
// Returns UP/DOWN/LEFT/RIGHT/NONE; ambiguous readings decode as NONE.
static Dir readLadder(void) {
  uint16_t a = analogRead(LADDER_PIN);
  if (a >= 950)                return DIR_NONE;
  if (a >=  63 && a <= 123)    return DIR_UP;
  if (a >= 149 && a <= 209)    return DIR_DOWN;
  if (a >= 296 && a <= 356)    return DIR_LEFT;
  if (a >= 482 && a <= 542)    return DIR_RIGHT;
  return DIR_NONE;             // between bands: diagonal or in transit
}
```

Brightness then becomes **hold-to-accelerate**: start at the slow end of the
existing ramp and increase the rate the longer UP/DOWN is held, reusing the
`RAMP_LOW_FACTOR` easing so dim settings still trim finely. LEFT/RIGHT stay
edge-triggered channel selection, exactly as now.

### 2.6 Parts

A 5-way navigation switch has 4 direction contacts, a centre-push contact, and a
common. Search terms:

- **ALPS SKQUCAA010** — the classic, widely stocked
- **Panasonic EVQ-Q2** series
- generic *"5-way tactile navigation switch SMD"* / *"5 direction button switch"*

Check the datasheet for whether the part **mechanically permits diagonals** — many
don't, which makes §2.4 moot.

---

## 3. Pin summary

| Signal | Pin | Joystick | Ladder |
|---|---|---|---|
| Analog in | **PA1** (AIN1) | X axis | ladder node |
| Analog in | **PA2** (AIN2) | Y axis | *unused* |
| Switch | **PA7** | stick push | centre push |

Both need VDD and GND from the **same 5 V rail as the MCU**.
