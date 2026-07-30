# gliim — numbered pinouts

Authoritative pin assignments for both boards, by **physical package pin number**,
ready to lay out against.

- **Main board (driver node)** — **ATtiny3226**, SOIC-20. Two USARTs, so RS-485
  *and* HC-12 can both be populated and both be live, **plus three local EC11s**.
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

## 1. Main board — ATtiny3226 (driver node + 3 local encoders)

**All three encoders fit.** Dropping the Qwiic port and moving IR to the panels
(where it belongs — see architecture.md §9a) frees exactly the nine pins three
EC11s need, while keeping 3 LED channels and **both** transports live.

| # | Port | Function | Notes |
|---:|------|----------|-------|
| **1** | VDD | **+5 V** | 100 nF close to the pin |
| **2** | PA4 | encoder 2 `B` | |
| **3** | PA5 | encoder 3 `A` | |
| **4** | PA6 | encoder 3 `B` | |
| **5** | PA7 | encoder 1 `SW` | |
| **6** | PB5 | **LED ch3** | TCA0 **WO2 ALT1**. 10 kΩ pulldown to DIM |
| **7** | PB4 | encoder 2 `SW` | ⚠ do not fuse as alt-reset |
| **8** | PB3 | **MCU RXD** ← module `TXD` | USART0 default |
| **9** | PB2 | **MCU TXD** → module `RXD` | USART0 default. **10 kΩ pull-up** |
| **10** | PB1 | **LED ch2** | TCA0 WO1 default. 10 kΩ pulldown to DIM |
| **11** | PB0 | **LED ch1** | TCA0 WO0 default. 10 kΩ pulldown to DIM |
| **12** | PC0 | encoder 3 `SW` | |
| **13** | PC1 | **MCU RXD** ← HC-12 `TXD` | USART1 **ALT1** |
| **14** | PC2 | **MCU TXD** → HC-12 `RXD` | USART1 **ALT1** |
| **15** | PC3 | **WS2812 DIN** | status/link. 470 Ω in series at the MCU, 100 nF at the LED |
| **16** | PA0 | **UPDI** | 1 kΩ series to the programming pad |
| **17** | PA1 | encoder 1 `A` | |
| **18** | PA2 | encoder 1 `B` | |
| **19** | PA3 | encoder 2 `A` | |
| **20** | GND | **GND** | |

**17 of 17 usable pins. Zero spare** — see the warning below.

| Encoder | `A` | `B` | `SW` |
|---|---|---|---|
| 1 | pin 17 (PA1) | pin 18 (PA2) | pin 5 (PA7) |
| 2 | pin 19 (PA3) | pin 2 (PA4) | pin 7 (PB4) |
| 3 | pin 3 (PA5) | pin 4 (PA6) | pin 12 (PC0) |

The **A/B pairs are identical to the panel's** (PA1/PA2, PA3/PA4, PA5/PA6), so the
quadrature decoder is literally the same code on both boards. Only the switch
pins differ, because PB5 is LED ch3 here.

### What this costs, and why each is acceptable

| Given up | Why it is fine |
|---|---|
| **Qwiic / I²C** | Forced, not chosen — TWI0's only two routes are PB0/PB1 (LED ch1/ch2) and PA1/PA2 (encoder 1). Both are consumed. See the ladder variant below if you want it back. |
| **IR on the main board** | Already the plan: §9a puts IR on panels, at eye level, where it relays to every driver. A driver node in a ceiling can't see a remote anyway. |
| **HC-12 `SET` pin** | Tie it high. Channel, baud and TX power are set-once on a bench jig; nothing changes them at runtime. |

### ⚠ Zero spare pins — and why the WS2812 is the pressure valve

Every usable pin is committed. Normally that is how respins happen, because the
next thing you want is always "one more pin for an indicator."

**The WS2812 is exactly what defuses that.** It is addressable, so **one data pin
drives a chain of them.** Extra indicators no longer cost pins:

| Chain position | Shows |
|---|---|
| 1 | system + link state (see below) |
| 2–4 | per-channel level or on/off, if you want it |

So the board has no spare *pins*, but it does have spare *output capability* —
which is what the spare pins would mostly have been spent on. Genuine spare GPIO
is only recoverable by populating fewer encoders; the footprints cost nothing
left unpopulated.

### Why a WS2812 makes sense now, when it did not before

rev1 had one and the design deliberately dropped it for a plain LED, because the
only thing it was encoding — which channel is selected — was better said by the
channels themselves.

**The bus changed that.** Link state is new information that nothing else can
show, and it is exactly what you need during install and when something is wrong:

| Colour | Meaning |
|---|---|
| green, steady | alive, peers seen recently |
| green, brief flash | frame sent / received |
| amber | no peer heard for N seconds |
| red | CRC errors above threshold — suspect cable |

That last one turns the *"measure rather than guess"* advice about cheap cable
into something visible on the wall, with no scope and no serial console.

### ⚠ WS2812 timing vs the UART

Driving a WS2812 means **bit-banging with interrupts disabled** — 30 µs per LED
(24 bits at 800 kHz). That is fine here, but it has a ceiling:

- **TCA0 PWM is unaffected** — it is hardware, and does not care about interrupts.
- `millis()` on TCB just services a pending interrupt late; no ticks are lost.
- The 1 kHz encoder poll shifts by 30 µs out of 1000. Irrelevant.
- **The USART is the binding constraint.** At 115200 a byte is 87 µs, and
  `RXDATA` + the shift register buffer two — so about **174 µs** of tolerance.

| Chain | Blocking | |
|---|---|---|
| 1 LED | 30 µs | fine |
| 4 LEDs | 120 µs | fine |
| 6+ LEDs | 180 µs+ | **RX overrun risk** |

**Keep the chain to 4 or fewer**, or drop the baud rate, or defer updates to idle.
Use megaTinyCore's bundled **`tinyNeoPixel_Static`** (no malloc, and its timing is
already tuned for 20 MHz).

At **5 V** the WS2812B's DIN threshold of 0.7 × VDD = 3.5 V is met directly by the
MCU output — which is precisely why this is possible now and was not at 3.3 V.
Budget ~1 mA quiescent per LED even when dark; the controller IC never sleeps.

### Do you actually want three encoders here?

Worth asking, because it decides the board's role. Driver nodes sit near the
lights — in ceilings, inside fixtures — where local knobs are decoration. Three
local encoders pay off in exactly one configuration: **a standalone unit**, one
box on a shelf driving three lights with knobs on the front and no bus at all.

That is a legitimate and appealing product (it is rev1's use case, done right),
and this pin map serves both roles from one PCB — populate the encoders, the bus
modules, or all of them. But if every main board is going in a ceiling, spend
those nine pins on margin instead.

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
| **8** | PB3 | **MCU RXD** ← module `TXD` | RS-485 *or* HC-12 |
| **9** | PB2 | **MCU TXD** → module `RXD` | **10 kΩ pull-up** — see below |
| **10** | PB1 | HC-12 `SET` | unused in the RS-485 build |
| **11** | PB0 | *free* | |
| **12** | PC0 | **IR receiver** | `PORTC_PORT_vect` — see §9a of architecture.md |
| **13** | PC1 | **WS2812 DIN** | status/link. 470 Ω in series at the MCU, 100 nF at the LED |
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

## 3. Wiring an EC11 encoder

The EC11 has five contacts plus a shell. Every one is a plain mechanical short —
there is no active circuitry inside — so **all three signals are active-low** and
all three need pull-ups.

| EC11 pin | Goes to |
|---|---|
| **A** | MCU encoder `A` pin |
| **B** | MCU encoder `B` pin |
| **C** | **GND** — the common for A and B |
| **D** / **E** | switch — **non-polarised**, so either one to GND and the other to the MCU `SW` pin |
| **O** | **GND** — metal shell / mounting tabs |

Per encoder, ×3 on each board:

```
                    VDD
                     │
            ┌────────┼────────┐
           10k      10k      10k
            │        │        │
   A ───────┴──┬──── ┴──┬──── ┴──┬─────── E  (switch)
              10n      10n      100n
               │        │        │
              GND      GND      GND
            (to MCU) (to MCU) (to MCU)

   C ── GND      D or E ── GND     O(shell) ── GND
```

`C` (quadrature common) and whichever of `D`/`E` you ground are both returns, so
they share the ground net — that is correct, not a shortcut.

### Net names → package pins

Nine nets per board. The `A`/`B` pairs are deliberately identical on both boards
so the decoder is the same code; only the switches differ.

| Net | Main board (3226) | Panel (3216) |
|---|---|---|
| `ENC1_A` | pin 17 (PA1) | pin 17 (PA1) |
| `ENC1_B` | pin 18 (PA2) | pin 18 (PA2) |
| `ENC1_SW` | pin 5 (PA7) | pin 5 (PA7) |
| `ENC2_A` | pin 19 (PA3) | pin 19 (PA3) |
| `ENC2_B` | pin 2 (PA4) | pin 2 (PA4) |
| `ENC2_SW` | pin 7 (PB4) | pin 6 (PB5) |
| `ENC3_A` | pin 3 (PA5) | pin 3 (PA5) |
| `ENC3_B` | pin 4 (PA6) | pin 4 (PA6) |
| `ENC3_SW` | pin 12 (PC0) | pin 7 (PB4) |

### Layout note: use resistor arrays

Three encoders means **nine 10 kΩ pull-ups and nine caps** per board. All nine
pull-ups are the same value to the same rail, so two 4-element **resistor arrays**
plus one discrete (or one 8-element array plus one) replaces nine parts with
three — worth it on a panel PCB that has to fit behind a wall plate.

**Pull-ups: fit external 10 kΩ, do not rely on the internal ones.** The ATtiny's
internal pull-ups are 20–50 kΩ and only loosely specified, which leaves the RC
time constant undefined and the line soft against noise pickup on a panel that
fingers touch.

**Caps: 10 nF on A and B, 100 nF on the switch.**

| | τ | Rise to threshold | Verdict |
|---|---|---|---|
| 10 kΩ + **10 nF** | 100 µs | ~90 µs | **use on A/B** |
| 10 kΩ + **100 nF** | 1 ms | ~900 µs | too slow for A/B; **right for the switch** |

At a fast spin (~3 rev/s, 60 detents/s) quadrature edges arrive every **~4.2 ms**,
so a 90 µs rise is ~2 % of the interval — invisible. A 100 nF cap would eat ~20 %
of it and visibly round the waveform. The switch has no such constraint and
benefits from the longer constant.

**Optional 1 kΩ in series** between each contact node and the MCU pin. It limits
the capacitor's discharge current through the contact to ~5 mA, which helps
contact life, and adds a little ESD margin. Worth fitting on panels; skip it on a
main board where the encoder sits on the same PCB.

**Ground the shell (`O`).** It shields the contacts and gives ESD from a user's
fingers a path that is not through the MCU.

### How this meets the firmware

Hardware debounce here is belt-and-braces, not the primary mechanism. The
decoder samples A/B from a **~1 kHz timer ISR** and runs a 4-state quadrature
machine, which rejects bounce structurally. That sampling rate tracks up to
**~125 detents/s (≈6 rev/s)** before aliasing — roughly 2× the fastest a hand
manages. If you find counts dropping on a hard flick, raise the ISR to 2 kHz
before touching the RC.

Two things to leave configurable rather than hard-code:

- **Direction.** Whether clockwise counts up depends only on which contact landed
  on `A` versus `B`. Swap in the lookup table, not in copper.
- **Counts per detent.** EC11 variants differ — some rest with one full quadrature
  cycle per detent, others give two. If one click moves brightness two steps,
  divide by two. Check your part's *pulses per revolution* against its *detents
  per revolution* and make the divisor a `config.h` constant.

---

## 3b. Wiring the RS-485 module and the RJ45 jacks

### Module (HW-0519 class)

| Module pin | Goes to | Notes |
|---|---|---|
| **VCC** | **+5 V** | 100 nF at the module |
| **GND** | **GND** | |
| **TXD** | **MCU RXD** — main pin 8 / PB3 | module output |
| **RXD** | **MCU TXD** — main pin 9 / PB2 | module input. **10 kΩ pull-up** here |
| **A+** | RJ45 **pin 4** on both jacks | |
| **B−** | RJ45 **pin 5** on both jacks | |
| **RGND** | see below | |

⚠ **Check the silkscreen before committing the footprint.** The header order on
these boards is commonly `GND · RXD · TXD · VCC`, which is the reverse of how the
symbol is usually drawn. Symbols get redrawn from memory; the silkscreen does not.

Also: leave the module's **`R0` jumper open**. That is its 120 Ω terminator, and
per §*Plug-and-play* we want none anywhere.

### `RGND` — tie it to GND through a jumper

`RGND` is the module's surge-return pad (the *"connect to earth"* terminal). The
vendor says it may be left unconnected for short indoor runs, and that is true as
far as it goes — but **the on-board TVS diodes return to `RGND`.** Float it and
they can still clamp A-against-B, while common-mode surges — the ones a 15 m cable
across a hall actually picks up — have nowhere to go.

There is no earth in this system: everything runs from a USB-C PD supply, and the
cable already carries a shared GND on pin 7. So tying `RGND` to board GND creates
no new loop and gives the TVS a defined return.

**Fit a 0 Ω / solder jumper between `RGND` and GND, populated by default.** If a
future install ever does have a real earth, you can lift it.

### Connector: RJ11 (6P4C), not RJ45

**Use a 6P4C modular jack.** The 2-pair cable has exactly four conductors, and an
**RJ45 plug physically cannot enter a 6-position jack** — which is a mechanical
interlock, not a warning label.

That matters because the failure mode is asymmetric. Our 15 V reaching someone's
network port is *unlikely* to do damage — Ethernet ports are transformer-isolated
and block DC, which is exactly what makes PoE possible. The dangerous direction is
inbound: a **passive PoE injector** (common in cheap wireless gear, and unlike
802.3af/at it performs no detection handshake) would blindly push 24–48 V into
A/B and into a regulator rated for 15 V. Standard PoE switches look for a 25 kΩ
signature first and would not energise us, so this is a narrow case — but it is a
real one, and a connector that cannot mate removes it entirely.

The wider argument is simpler: **phone cable is effectively extinct.** Nobody has
a spare RJ11 lead to plug in by accident, and nothing else in the building uses
the socket. A dedicated bus deserves a connector that means only one thing.

Two workable assignments. Both put data on the **centre positions (3/4)**, which
minimises the ~10 mm of untwist the crimp imposes.

**Option A — power on 2/5 (works with common 6P4C plugs)**

| Position | Signal |
|---:|---|
| 1, 6 | no connect |
| **2** | **GND** |
| **3** | **RS-485 A** |
| **4** | **RS-485 B** |
| **5** | **V_BUS** |

Uses exactly the four positions a standard **6P4C** plug contacts, so the
commonest crimps and plugs fit. **Requires the series Schottky** — see below.

**Option B — power on 1/2 (reversal-immune, needs 6P6C plugs)**

| Position | Signal |
|---:|---|
| **1** | **V_BUS** |
| **2** | **GND** |
| **3** | **RS-485 A** |
| **4** | **RS-485 B** |
| 5, 6 | no connect |

A modular reversal maps 1↔6 and 2↔5, so with 5/6 unconnected **both power
conductors land on dead pins** and the panel simply does not come up — the same
trick RJ45 gave us. A/B still swap harmlessly. **No Schottky needed.**

The cost is that a 6P4C plug only contacts positions 2–5, so this needs **6P6C
plugs** throughout. Since you are crimping custom leads anyway, put the blue pair
in 3/4 and the orange pair in 1/2 — the telephony pair convention (1-6, 2-5, 3-4)
does not bind you when both ends are your own.

**Pick B if 6P6C plugs are easy to source locally; otherwise A plus the Schottky.**
Do not mix the two across one installation.

### ⚠ The series Schottky (Option A only)

With power on 2/5, a modular reversal is symmetric about the centre and swaps
GND against V_BUS — there is no dead pin for it to land on. Option B above avoids
this structurally; if you take Option A, fit the diode.

At 15 V that is cheap to insure against: a **series Schottky on `V_BUS`** costs
~0.3 V, which is **2 %** of a 15 V rail rather than the 6 % it cost of a 5 V one.
The reason it was dropped no longer applies. Use a 30–40 V part (BAT54, SS14).

Two things it does *not* protect, worth knowing:

- A reversed cord still lifts the panel's ground 15 V above the bus, which
  stresses the transceiver's A/B pins. The module's TVS clamps it, and if the
  part does die it is a ₹31 module on castellated pads rather than a fine-pitch
  IC — genuinely reworkable (§3b).
- It cannot make a reversed cable *work*. That is fine; "does not power up" is a
  good failure.

**Crimp your own leads, straight-through, and mark one end.** The reversal risk
comes almost entirely from ready-made telephone cords, which are frequently wired
pin 1 → pin 6 deliberately — and which you now have no reason to own.

### Two jacks, wired in parallel

Both jacks are the same jack — there is no "in" and no "out" (see
`docs/architecture.md` §5). Wire them straight across:

```
   J1.3 ──┬── J2.3 ── module A+
   J1.4 ──┼── J2.4 ── module B−
   J1.2 ──┼── J2.2 ── GND
   J1.5 ──┴── J2.5 ── V_BUS (15 V)
```

Silkscreen both **BUS**, not IN/OUT. The pass-through is passive copper, so a
node losing power does not break the chain.

### Bias resistors — footprints only

The module carries a plain MAX485 with no true fail-safe receiver, so an idle
bus may leave the receiver undefined. Provide **unpopulated** footprints on the
main board only:

```
   +5V ── 10k ── A          B ── 10k ── GND
```

Populate on **exactly one node on the whole bus**, and only if the arrival check
in §*Use a generic RS-485 module* shows idle RXD chattering. 10 kΩ rather than the
textbook 680 Ω because we do not terminate: with no 120 Ω loads, 680 Ω would burn
~3.7 mA continuously and load the driver for no benefit, while 10 kΩ still holds
roughly 0.45 V across the line.

---

## 3c. Panel supply — 5 V or 12 V down the cable?

**5 V works for small installs and is simpler. 12 V is needed once the run gets
long or the panel count grows.** Design the panel to take either.

The binding constraint is the **MAX485's 4.75 V minimum** — not the MCU, which at
10 MHz is happy down to 2.7 V, and not the WS2812B at ~3.5 V. Run the panel MCU
at **10 MHz**, not 20; a panel has nothing to compute, and 20 MHz would need
4.5 V and become a second constraint for no benefit.

Voltage at the panel with 5 V distributed on 24 AWG:

| Run | Panels | Current | Drop | At the panel | |
|---|---|---|---|---|---|
| 15 m | 1 | 30 mA | 0.05 V | **4.95 V** | fine |
| 15 m | 2 | 60 mA | 0.09 V | **4.91 V** | fine |
| 25 m | 3 | 90 mA | 0.23 V | **4.77 V** | at the limit |
| 40 m | 4 | 120 mA | 0.49 V | **4.52 V** | **under spec** |
| 40 m | 6 | 180 mA | 0.73 V | **4.27 V** | **under spec** |

So:

- **≤2 panels, ≤20 m → distribute 5 V.** No regulator on the panel at all. This
  is the fewest parts, no heat, and nothing to go wrong.
- **3+ panels or >25 m → distribute the raw 15 V** from the PD trigger and drop
  it with a linear regulator at each panel. This also decouples the panel's supply
  from the main board's logic rail, which is worth something on a long cable
  carrying a switching WS2812.

### Distributing 15 V

The board already has 15 V (PD trigger) and 5 V (buck). **Send the 15 V** — there
is no reason to make an intermediate 12 V, and the higher the rail the less the
drop matters:

| Bus voltage | Drop, 15 m @ 20 mA | As a fraction |
|---|---|---|
| 5 V | 51 mV | 1.0 % |
| **15 V** | 51 mV | **0.34 %** |

The current is the same either way — a linear regulator draws its output current
at the input — so the higher rail simply swamps the drop. Cable length stops
being a design constraint.

**Regulator dissipation** is (15 − 5) × I, so package choice matters more than
part choice:

| Load | Power | SOT-23 | SOT-89 | **SOT-223** | DPAK |
|---|---|---|---|---|---|
| 20 mA typical | 0.20 W | +50 °C | +30 °C | **+15 °C** | +10 °C |
| 40 mA peak | 0.40 W | +100 °C | +60 °C | **+30 °C** | +20 °C |

**Use SOT-223 or DPAK with a copper pour.** SOT-23 is not acceptable at 15 V in —
it was fine at 12 V and is not here.

**⚠ Name the bus power net `V_BUS`, never `VDD`.** `VDD` is the MCU's own supply
pin name and will collide with the 5 V rail on the schematic — and this net may
carry **15 V**. A net-name collision between a 15 V bus conductor and a 5 V logic
rail is a board-destroying class of error, and it is invisible in a netlist until
smoke.

**⚠ Check the regulator's input rating, not just its dropout.** Several common
parts die on a 15 V rail:

| Part | V_in max | |
|---|---|---|
| MCP1702 | 13.2 V | **destroyed** |
| AMS1117 | 15 V | at its absolute limit — do not |
| **7805 / LM317 (DPAK)** | 35 / 40 V | **fine** |

**RF-only panels are not on this cable at all.** A panel with an HC-12 and no
RS-485 has no bus connection, so it needs its own supply — which is exactly the
"needs a mains socket at every panel" cost noted in architecture.md §5. It also
means no panel ever has to survive the HC-12's 100 mA transmit burst through this
regulator: HC-12 panels are not the ones being fed from the cable.

No "NOT ETHERNET" label is needed: the 6P4C jack is its own interlock, since an
RJ45 plug will not fit it. Silkscreen both jacks **BUS** and leave it at that.

**Fit both.** Put a small linear-regulator footprint on the panel plus a
**bypass jumper** across it. Jumper fitted = 5 V straight through; regulator
fitted = 12 V in, 5 V out. One board, decided at build time — the same approach
the transport modules already use.

### Drop the series Schottky

Earlier drafts specified a series Schottky on the panel's supply as
reverse-polarity insurance. **The RJ45 pin assignment makes it redundant**, and
it costs 0.3 V — roughly six times the cable drop at 15 m, which is what pushed
the earlier conclusion toward 12 V in the first place.

With pins 1/2/3/6 unconnected, a reversed lead maps the panel's `V_BUS` (pin 8)
onto the far end's pin 1, which is not connected to anything. **The panel simply
does not power up.** That is exactly the failure mode the Schottky was there to
guarantee, achieved with copper instead of a 0.3 V tax. (RJ45 patch leads are
straight-through in practice anyway — reversal is a telephone-cord problem.)

---

## 3a. Wiring the HC-12

```
         +5V ──┬──────────────┬── 10k
               │              │
              100n         ┌──┴──┐
               │           │ SET │ ── 2-pin header ── GND   (config only)
              GND          └─────┘
                             │
   VCC ── +5V              (pin 5)
   GND ── GND     ← all GND pins: 2, 7, 8
   RXD ── MCU TXD  (main pin 14 / PC2)
   TXD ── MCU RXD  (main pin 13 / PC1)
   ANT ── no-connect on the PCB; antenna fits the module
```

### `SET` — pull it high, and give yourself a way to pull it low

`SET` is **active-low**: hold it low and the module enters AT command mode;
leave it high and it is a transparent serial link. It has a weak internal
pull-up, so a floating pin *usually* behaves — but "usually" is the problem. A
noise spike that dips `SET` drops the radio into AT mode, where it silently stops
forwarding traffic and looks exactly like a dead link.

**So:**

1. **10 kΩ pull-up to +5 V.** Defines the state. This is the part that must be on
   the board.
2. **A 2-pin header (or solder jumper) from `SET` to GND.** Fit a shunt to
   configure, pull it to run. Costs nothing and means you never have to desolder
   a module to change its channel.

The main board has **no spare MCU pin** for `SET` (all 17 are committed), which is
fine because nothing changes it at runtime — channel, baud and TX power are
set-once. The panel *does* have a pin for it (3216 pin 10 / PB1, unused in the
RS-485 build), so there you can drive it from firmware if you prefer.

**Configuring:** short the `SET` header, power up, and send AT commands over the
module's own `RXD`/`TXD` — either from a USB-serial adapter on those nets, or by
having the MCU relay bytes from its other UART. Remove the shunt to return to
normal operation.

### Baud rate: the HC-12 path must be slower

The HC-12 defaults to **9600 baud**, and unlike RS-485 that is not a number to
casually raise — in FU3 mode a lower serial rate buys better receiver sensitivity
and more range, and the radio's real throughput is well below the UART's anyway.

At 9600 a 10-byte report takes **10.4 ms**. Against the 20 ms cadence of §6.6 that
is >50 % occupancy from a single panel — far too much. **Run the RF path at a
~50 ms cadence (20 Hz)** instead, which drops it to ~17 %.

20 Hz is still smooth on a dimmer, and nothing is lost: the counter protocol
carries the accumulated total regardless of how often it is sent. Two USARTs
means the wired path keeps its 115200 and its 20 ms cadence independently — the
two transports do not have to agree.

Budget end-to-end latency of ~100 ms on the RF path once the module's own
buffering is included. Worth measuring before assuming it feels right.

### Power

Fit **≥100 µF** at the module — 470 µF if it is fed from the 12 V → 5 V linear
regulator — to ride out the ~100 mA transmit burst. Consider turning TX power
down from the default 20 dBm; across one hall you need nothing like 100 mW, and
it cuts the burst.

---

## 4. External parts that are not optional

| Part | Where | Why |
|---|---|---|
| **10 kΩ pulldown** per LED channel | main, pins 6/10/11 | The PT4115 pulls DIM up internally through 200 kΩ. MCU pins are high-Z from power-on until firmware runs, so **without this every light blasts at 100 % on every power cycle**, before any code executes. |
| **10 kΩ pull-up on UART TXD** | both, pin 9 | The RS-485 auto-flow module keys its driver by sniffing TXD. A floating TXD during the MCU's boot window can **assert the driver and jam the whole bus**. Holds it idle-high until firmware takes over. |
| **10 kΩ pull-up on HC-12 `SET`** | RF builds | Floating `SET` can dip into AT mode on noise, where the radio silently stops forwarding. See §3a. **Pull up to the *switched* HC-12 rail, not the always-on +5 V** — otherwise, with the module's power jumper removed, the pull-up feeds current into its `SET` pin through the ESD diode and parasitically part-powers it. |
| **PPTC ~200 mA** in series with `V_BUS` to the jacks | main board | A reversed or damaged lead shorts `V_BUS` to GND through the cable. Without a fuse that short is limited only by the PD supply's 3 A. |
| **470 Ω series on WS2812 `DIN` + 100 nF at the LED** | both | Easy to omit because the part "works" without them; the resistor damps the data edge and the cap holds the LED's rail through colour changes. |
| **470 Ω series + 100 nF** at each WS2812 | main pin 15, panel pin 13 | Damps the data edge and holds the LED's rail steady through colour changes. Anything 220–470 Ω works — 470 Ω matches the indicator-LED resistors, saving a BOM line. Place the resistor at the **MCU** end; it is source damping. |
| **1 kΩ series** on UPDI | both, pin 16 | Standard serialUPDI wiring. |
| **100 nF + 10 µF** | both, pin 1 | Decoupling. |
| **≥470 µF** at the HC-12 | RF builds | Rides out the ~100 mA transmit burst. |

---

## 5. Firmware deltas between the two parts

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
