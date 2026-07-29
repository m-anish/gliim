# glim — system architecture

**Status: planning.** This document is the design under discussion, not something
built. Nothing here is implemented in firmware yet.

---

## 1. The pivot

glim was a box with a joystick. It is now a small **distributed lighting control
network**: driver nodes near the lights, control panels on the walls, one bus
between them.

Three changes drive it:

| | Was | Is |
|---|---|---|
| Control | one joystick, multiplexed across channels | **one EC11 rotary encoder per channel** |
| Topology | single box, control on the box | **driver nodes + wall panels, up to ~15 m apart** |
| Scale | one fixture | a hall: several panels, several driver nodes |

The encoder change matters more than it looks. Selecting a channel and *then*
adjusting it is a **mode**, and modes are exactly what a screenless device should
not have — you have to remember which channel is live, and the device has to tell
you. A knob per channel has no mode: the thing you grab *is* the thing you
change. Everything the old design spent effort on — the ack-blink, channel
indicator LEDs, the status colour scheme — existed to paper over that mode. All
of it disappears.

---

## 2. Topology

```
                    ┌──────────── RS-485 twisted pair (+ power) ────────────┐
                    │                                                        │
   ┌────────────────┴─────┐   ┌──────────────────┐   ┌────────────────────┐ │
   │  DRIVER NODE          │   │  CONTROL PANEL   │   │  CONTROL PANEL     │ │
   │  ATtiny3216           │   │  ATtiny1616      │   │  ATtiny1616        │ │
   │  3 × PT4115 + PWM     │   │  3 × EC11        │   │  3 × EC11          │ │
   │  20 V LED rail        │   │  bus-powered     │   │  bus-powered       │ │
   └───────────────────────┘   └──────────────────┘   └────────────────────┘ │
   ┌───────────────────────┐                                                  │
   │  DRIVER NODE (more    ├──────────────────────────────────────────────────┘
   │  lights, more zones)  │
   └───────────────────────┘
```

Two node types, one bus. **Scale by adding nodes, not by cramming channels into
one node** — that is the whole point of moving to a bus, and it answers the
channel-count question below.

---

## 3. Channel count — does the pivot force a reduction?

**No, but settle on 3 anyway.** The pin budget survives 6 (see §8), but:

| | 3 channels | 6 channels |
|---|---|---|
| TCA0 mode | normal, **16-bit** | split, 8-bit |
| Dimming floor | 0.061 % → **1638:1** | 0.39 % → 256:1 |
| Local encoders on the driver | room for one | none |

Six channels costs **6.4× of dimming depth** — the exact thing three rounds of
work went into winning, because the PT4115's real floor is a 2 µs on-time and
8-bit resolution wastes it. Before the pivot, that trade was at least arguable
because more channels meant one less box. Now it isn't: **a second driver node is
cheap, and gives you six 16-bit channels instead of six 8-bit ones.**

So: **3 × 16-bit per driver node.** Route PA3/PA4/PA5 to expansion footprints if
you want the option, but the answer is another node.

---

## 4. The control surface

**EC11 rotary encoder per channel.** 11 mm shaft encoder, quadrature A/B plus a
push switch, typically 20 detents per revolution with one full quadrature cycle
per detent. Cheap, everywhere, and the right feel for brightness — continuous,
tactile, no end-stop.

| Gesture | Action |
|---|---|
| turn | that channel brighter / dimmer |
| press | toggle that channel |
| press + turn | *(reserved — e.g. colour temperature if channels are paired)* |
| long-press | *(reserved — e.g. scene save)* |

**Decoding:** do **not** use pin-change interrupts. EC11s are mechanical and
bounce badly; edge interrupts turn a single detent into a burst. Sample A/B in a
timer ISR at ~1 kHz and run the standard 4-state quadrature machine (a 16-entry
lookup on `prev<<2 | now`). It is smaller, has bounded worst-case cost, and
rejects bounce structurally. Add an RC (1 kΩ + 10 nF) on each line for the
electrically noisy case.

**Acceleration** carries over from the old ramp work: slow near the bottom of the
range so dim settings trim finely, faster when spun. `RAMP_LOW_FACTOR` and the
16-bit gamma curve both apply unchanged.

---

## 5. Transport — why RS-485

40–50 ft (12–15 m), multiple panels, next to three switching regulators.

**Recommendation: half-duplex RS-485.** Twisted pair is the safe default, but
the run is short and slow enough that cheap untwisted cable also works — see
*Cheaper cable* below.

- **Differential**, so it rejects exactly the common-mode noise a switching LED
  driver injects. This is the deciding factor, not the distance.
- 15 m is nothing — RS-485 does 1200 m at low rates. At **115200 baud** the
  baud × distance product is ~1.7 × 10⁶ against a ~10⁸ rule of thumb, so there is
  a ~50× margin. Even 250 kbaud (DMX's rate) is comfortable.
- **32 nodes** on a standard transceiver; more with fractional-unit-load parts.
- Transceivers are ~₹17 and stocked locally. Prefer the **slew-limited**
  **MAX483/MAX487** (5 V, our rail) over the MAX485 — same price and pinout, and
  it is what makes cheap cable viable (see below). SP3485 boards are on the shelf
  at Hubtronics if you go 3.3 V.
- **Neither termination nor bias resistors are needed** at this speed and length,
  given slew-limited, true-failsafe transceivers — which is what keeps every
  board identical. See *Plug-and-play* below.
- Daisy-chain, not star. Stubs short.

### What was rejected, and why

| Option | Why not |
|---|---|
| **I²C / Qwiic over distance** | Designed for on-board, < 1 m. Single-ended, no noise margin, and it hangs the whole bus when a node glitches. Even with P82B715 extenders this is the wrong tool. |
| **CAN** | Genuinely more robust — arbitration and error handling in hardware. But the ATtiny has no CAN controller, so it means an MCP2515 + transceiver per node over SPI. Real cost and complexity for a benefit a lighting panel doesn't need. |
| **Wireless (nRF24 / ESP-NOW / LoRa)** | Panels need power anyway, so the cable is already there. Wireless adds pairing, a commissioning step, and a class of failure that is invisible to the user. Against glim's "no pairing, instant" premise. Reconsider only if cable is genuinely impossible. |
| **DMX512** | Is RS-485 at 250 kbaud with a lighting protocol on top — but unidirectional controller → fixture, and our panels are *inputs*. Worth knowing about if glim ever needs to sit in a stage-lighting rig. |
| **DALI** | The actual industry standard for this exact job: 2-wire, polarity-insensitive, 1200 baud, designed for building lighting. Needs specific transceivers and a real stack. Right answer for a commercial product, over-heavy for this one. |

### Wiring

One cable carries data and panel power. **CAT5/CAT6** is the default:

| Pair | Use |
|---|---|
| 1 | RS-485 **A / B** — twisted, this is the pair that matters |
| 2 | **GND** (both conductors) |
| 3 + 4 | **+12 V** to power the panels (both conductors each, paralleled) |

Panels buck 12 V down locally. Voltage drop is a non-issue: 15 m of 24 AWG is
~1.2 Ω per conductor, halved by pairing, so a 50 mA panel drops ~60 mV.

Ground is shared, which RS-485 needs — it is differential, not isolated. If
panels end up on different circuits, add isolated transceivers (ADM2582 class)
and the problem goes away, at a cost.

#### Daisy-chaining: two jacks per node, wired in parallel

Yes — every node gets an **in** and an **out** jack, and the chain runs
host → panel → panel → panel. But the two jacks are **soldered directly together
inside the enclosure**. The node *taps* a bus that passes through it; it does not
receive on one port and retransmit on the other.

That distinction is the whole design:

| | **Parallel pass-through** (this design) | **Store-and-forward chain** |
|---|---|---|
| Electrically | one continuous bus; every node hears every frame at the same instant | N point-to-point links |
| A node loses power | **bus unaffected** — the pass-through is passive copper | everything downstream goes dark |
| Latency | none per hop | one frame time per hop, accumulating |
| Adding a node | plug it in anywhere; nothing else changes | routing/forwarding logic, and every hop must agree |
| Protocol needs to know the chain order | **no** | yes |

So the cabling you described is exactly right, and the protocol needs no support
for it whatsoever. **The protocol has no concept of position, order, or
neighbours** — a frame is broadcast and every node hears it. That is what makes
this scale: adding a panel is a wiring operation, not a configuration one.

#### Plug-and-play: making every board identical

Termination, bias and addressing are the three things that are normally
*position-dependent*, and they are exactly what makes an installation fiddly.
At this speed and length, all three can be designed away. **Every panel should
be interchangeable, orientation-free, and configuration-free.**

**1. Omit termination entirely.**

Termination exists to absorb reflections. Reflections only matter if they are
still ringing when the receiver samples — and here they are not, by three orders
of magnitude. A slew-limited transceiver has edges of roughly half a microsecond
against a 150 ns round trip at 15 m, so the line is electrically *short*; even
if it rings, it settles inside ~500 ns, while the bit is 8.7 µs long and is
sampled at 4.3 µs.

So: **no terminators, no jumpers, no "which end am I" decision.** This is where
the slew-limited transceiver earns its keep — a full-speed MAX485 with ~10 ns
edges over the same cable genuinely would ring, and would genuinely need
terminating. Do not substitute one back in.

This holds for **≤115200 baud and runs up to ~100 m**. If you ever exceed that,
the fix is still not a board change: put a 120 Ω resistor in a 6P4C plug and
push it into the spare jack at each end of the chain. Termination as a *plug*
keeps the boards identical.

**2. Use a true-failsafe transceiver, and drop the bias resistors too.**

Idle bias exists because a floating differential pair leaves the receiver output
undefined — which on a tiny MCU means the UART sees phantom start bits and
interrupts continuously. That is a real problem, and the textbook fix (one bias
network somewhere on the bus) is position-dependent.

**True fail-safe** receivers solve it in silicon: the input threshold is offset
(roughly −200 mV to −50 mV rather than ±200 mV) so an idle or open line reads as
a defined logic HIGH with no external resistors at all. Look for "fail-safe
receiver" or "open-circuit fail-safe" in the datasheet — candidates worth
pricing are **MAX3082** (5 V, slew-limited) and **SN65HVD3082E** or **THVD1410**
(3.3 V). Verify the spec on the part you actually source; the plain
MAX483/485/487 and SP3485 do **not** have it.

If only non-failsafe parts are available, fall back to **one** bias network on
the driver node — the driver is a different board type anyway, so panels stay
identical. Never repeat bias per node; it progressively loads the line.

**3. Both jacks are the same jack.**

Because they are soldered together, there is no "in" and no "out". Label them
both **BUS**. Any node works anywhere in the chain, including at either end, and
either cable can go in either socket.

**4. Make cable orientation harmless.**

A reversed RJ11 cord swaps both pairs (§ *2-pair CAT5 on RJ11*). Two cheap parts
make that a non-event instead of a dead panel:

- **A Schottky bridge rectifier on the power pair.** Either polarity now works.
  Four Schottkys cost ~0.6 V, so distribute **6–7 V** rather than 5 V to leave
  the 3.3 V LDO its headroom.
- **A/B swap corrected in firmware.** Swapping A and B simply inverts the
  received signal, and the ATtiny can undo that in hardware by setting
  `PORT_INVEN_bm` in the `PINnCTRL` of the USART's RX and TX pins. Detection is
  trivial with a fail-safe receiver: a correct idle line sits HIGH, a swapped one
  sits LOW. If RX has been continuously low for >20 ms — far longer than any
  valid byte — flip `INVEN` and carry on.

Together these mean the cable **cannot be plugged in wrong**, in either socket,
either way round.

**5. No addresses to assign.**

Every ATtiny has a unique serial number in `SIGROW`. Hash it to 16 bits and use
that as the node ID. With 6 nodes in a 65536-space the collision probability is
~0.02 %, so there is no claim protocol, no DIP switch, no commissioning step, and
the ID survives reboots and EEPROM erasure. See §6.5.

**6. Sensible zero-config zone defaults.**

The one genuinely install-specific thing left is *which knob runs which light*.
Default it positionally — **panel encoder N ↔ driver channel N** (i.e. zone = index)
— and a fresh set of boards works the moment they are plugged together, with
knob 1 running channel 1 everywhere. Only if you want the east and west walls
independent do you need the learn gesture in §6.5, and that is a choice rather
than a prerequisite.

**Stubs and stars.** Textbook RS-485 forbids branching. Slew-limited drivers make
this far more forgiving than the literature implies: with ~1 µs edges, a stub has
to run well past 10 m before it is electrically visible. Keep the run linear
because it is *tidier* and because it makes termination obvious — but a short
spur to a panel is not going to break anything.

**What actually runs out first: power, not the bus.** The +5 V passes down the
same chain, so current accumulates toward the host. Ten panels at ~15 mA over
40 m of 24 AWG is ~150 mA through ~3.4 Ω, or ~0.5 V — still inside the 3.3 V
regulator's headroom, but that is the constraint you will hit before any
electrical or protocol limit. Past that, inject power at the far end or raise the
distribution voltage.

**One inherent cost of daisy chain:** unplugging a *cable* mid-run drops
everything downstream. Powering a *node* down does not, since the pass-through is
copper. For a fixed installation that trade is worth it — it buys the stub-free
linear topology RS-485 wants.

#### Cheaper cable — 4/6-core telephone works, with conditions

The usual "you must use twisted pair" advice is aimed at long, fast buses. Ours
is short and slow, and two things change the calculus:

- **Reflections never reach the sampling instant.** Propagation is ~5 ns/m, so
  15 m is a 150 ns round trip. A bit at 115200 baud is 8.7 µs and is sampled at
  mid-bit — ringing has settled roughly 8× over. Uncontrolled cable impedance
  simply does not matter at this speed and length.
- **The counter protocol absorbs errors.** A corrupted frame is dropped and the
  next one carries the correct cumulative value (§6.1). A 1 % frame error rate
  is invisible to a human turning a knob.

What twist actually buys is **noise rejection**, and that is the one real loss.
Untwisted parallel conductors have a defined loop area, so induced interference
arrives as *differential* noise the receiver cannot reject. Three changes make
that acceptable:

1. **Use a slew-rate-limited transceiver.** **MAX483** or **MAX487** (both
   limited to ~250 kbps) instead of MAX485, or SN65HVD3082. Same price, same
   pinout. Edges stretch from tens of nanoseconds to ~1 µs, which is far longer
   than the 150 ns round trip — the line becomes electrically *short*, so there
   are no reflections at all and radiated EMI drops in both directions. **This is
   the single highest-value change and is worth doing even on CAT5.**
2. **Drop the baud rate to 19200 or 38400.** Nothing here is throughput-bound —
   it is a human turning a knob. At 19200 a bit is 52 µs, ~350× the round trip.
3. **Send 5 V, not 12 V**, so the panel needs no buck converter. A switcher
   sitting on untwisted conductors right beside the data pair is the most likely
   source of trouble, and deleting it deletes the problem. A panel is light —
   ATtiny at a reduced clock (it has nothing to compute), a transceiver, an LED:
   ~15 mA. Over 15 m of 26 AWG that is ~2.1 Ω per conductor, so ~60 mV of drop.
   Even four panels on one trunk stay under ~250 mV.

**Conductor order matters on flat cable** — keep A and B adjacent to minimise
loop area, and put ground between the data and the power conductors:

```
  6-core:   +5V │ GND │  A  │  B  │ GND │ +5V      ← ideal
  4-core:         GND │  A  │  B  │ +5V           ← fine if +5V is quiet
```

Decoupling at each panel (100 nF + 10 µF across +5 V/GND) keeps the power
conductors from becoming an aggressor.

**Termination becomes optional** with slew-limited drivers at this length — try
without it first. The **idle bias resistors are not optional**; they define the
line state when no one is driving, and without them the receiver floats and
invents start bits.

| Cable | Verdict |
|---|---|
| **CAT5e/CAT6 UTP** | Twisted, 8 conductors, RJ45 ecosystem everywhere. Buy this if you want spare pairs. |
| **2-pair CAT5 telecom** | **The pick.** Twisted, 4 conductors — exactly what the design uses — at roughly half the price of 4-pair. See below. |
| **Round twisted telephone (2–3 pair)** | Fine. If it is twisted, the objection disappears. |
| **Flat 6-core "silver satin"** | Workable with all three changes above. Use the conductor order shown. |
| **Flat 4-core** | Workable, no spare conductors, no room for a second ground. |
| **4/6-core alarm or CCTV cable** | Common and cheap; often shielded, which substitutes well for twist. Tie the shield to GND **at one end only**. |

#### 2-pair CAT5 on RJ11 — the recommended build

e.g. RODEL i10 "CAT5 2 pair copper", 90 m for ~₹660 (~₹7.3/m). Twisted, four
conductors, and cheap enough that flat cable saves nothing. Map it:

| Pair | RJ11 (6P4C) positions | Use |
|---|---|---|
| 1 | **3, 4** — the centre pair | RS-485 **A / B** |
| 2 | **2, 5** | **GND** / **+5 V** |

Modular jacks assign pairs centre-outward, so putting data on positions 3/4 keeps
the two data conductors adjacent and minimises the ~10 mm of untwist the crimp
imposes. That untwisted stub is irrelevant at these edge rates, but it costs
nothing to get right.

Twisting the power conductors as a pair is a bonus, not a compromise — it shrinks
the power loop area, so the supply radiates less into the data pair than two
parallel conductors would.

**⚠ The one real hazard: reversed RJ11 cords.** Telephone handset and line cords
are frequently wired **pin 1 → pin 6** on purpose, so the cord works either way
up. Plug one of those into this bus and positions 2/5 swap — which swaps **GND
and +5 V** and destroys the panel. A/B swapping is harmless by comparison (the
receiver just sees inverted data and reports framing errors). Two defences, take
both:

- **Crimp your own, straight-through**, and mark one end. Do not use salvaged
  phone cords.
- **Fit a series Schottky** on the panel's +5 V input regardless. It is one part
  and ~0.3 V, and it turns a destroyed board into a board that simply does not
  power up.

**Other buying checks:**

- **Insist on solid copper, not CCA.** Copper-clad aluminium is common at this
  price point and often mislabelled — it has higher resistance and cracks when
  flexed. This listing says copper; confirm on arrival by weight and by scraping
  a strand end.
- **Match the crimp to the conductor.** CAT5 is solid-core; RJ11 plugs come in
  solid and stranded versions with differently shaped IDC teeth. The wrong one
  gives intermittent contact that will look like a protocol bug for a whole
  evening.
- Whether this cable really meets CAT5's 100 Ω impedance spec **does not matter
  here** — see the reflection arithmetic above. Buy it for the twist and the
  copper.

**Daisy-chain with two jacks per node**, wired in parallel inside the enclosure —
in and out. Do **not** use RJ11 splitters: they create stubs, which is the one
topology RS-485 genuinely dislikes. The final node's spare jack is a natural home
for a termination plug (a 6P4C plug with 120 Ω across positions 3–4) if you
decide you want one.

*If RJ45 hardware is cheaper or more available where you are, use it instead* —
populate 4 of the 8 positions, keep A/B on a single twisted pair, and you get the
standardised, never-reversed T568B patch-lead ecosystem for free.

**Panel power on 4 conductors.** Send **5 V**, regulate to **3.3 V** locally with
a low-dropout part (HT7333 class, ~0.1 V dropout), and run the panel MCU at
**≤10 MHz** with a **3.3 V transceiver** (MAX3483 or SP3483 — both slew-limited).
The reason is headroom: a 20 MHz ATtiny needs ≥4.5 V and a MAX485 needs ≥4.75 V,
so a few hundred millivolts of cable drop plus the Schottky puts you out of spec.
At 10 MHz the ATtiny is happy down to 2.7 V and the whole question evaporates. A
panel has nothing to compute — it reads encoders and a UART.

Mixing a 5 V transceiver on the driver node with 3.3 V transceivers on the panels
is **fine and standard**: RS-485 is a differential line spec, and the receive
thresholds are compatible in both directions.

Sanity check on drop: 24 AWG is ~0.084 Ω/m, so a 40 m trunk is ~3.4 Ω per
conductor. Four panels at ~15 mA each is 60 mA, giving ~0.4 V round-trip — well
inside the 3.3 V regulator's headroom.

**With the twist restored**, 115200 baud and conventional termination are back on
the table. The slew-limited transceiver is still worth choosing (lower EMI, free),
but it stops being load-bearing.

---

Whichever you pick, **do not run it parallel to mains** for long stretches —
cross at right angles where you can. This matters much more without twist.

**Measure rather than guess.** Environment decides this, not arithmetic. Put a
CRC-error counter in the panel firmware and read it over the debug UART, so
step 1 of §11 produces a number instead of an impression. If the error rate is
low single-digit percent, the counter protocol hides it entirely; if it is
worse, you have a specific reason to upgrade the cable rather than a hunch.

---

## 6. Protocol

The important decision is the **data model**, not the framing. Get this right and
the arbitration scheme becomes swappable.

### 6.1 Panels publish counters, not events

Each panel keeps a **monotonic 16-bit counter per encoder** and a press counter
per button. It transmits the *current counter value*, never a delta and never an
event.

Drivers keep `last_seen[panel][encoder]` and apply the difference.

This one choice buys almost everything:

- **A lost message costs nothing.** The next message carries the correct
  cumulative value, so the delta self-heals. No ACKs, no retransmit logic, no
  sequence numbers.
- **Idempotent.** A duplicated message produces a zero delta.
- **Two panels on one zone just work** — each is differenced independently and
  the deltas add.
- **A human is in the loop.** They are watching the light. Anything the protocol
  drops, they correct by turning the knob a little more. Designing for
  bit-perfect delivery here is effort spent in the wrong place.

Two edge cases, both cheap:

- **Driver boots mid-session** and has no `last_seen`: record the first counter
  without applying it.
- **Panel reboots** and its counter resets to 0, implying a huge negative delta:
  **clamp** — reject any |delta| > ~64. Nobody turns a knob 64 detents between
  two messages 50 ms apart. Clamping also catches corrupted frames that pass CRC.

### 6.2 Zones

Binding panels to lights by *node* would be brittle. Bind by **zone** instead:

- every driver **channel** is assigned a zone (0–15), stored in EEPROM;
- every panel **encoder** is assigned a zone;
- an encoder's delta applies to *every channel in that zone*, on any node.

"This knob runs the west wall" is then a property of configuration, not wiring,
and adding a driver node to a zone needs no change anywhere else.

### 6.3 Arbitration — start simple

**v1: free-running broadcast, no master.** Panels transmit only when an encoder
moves or a button changes. The CRC drops a collided frame — which, given §6.1,
costs nothing.

### When does a panel transmit?

Four states, and the last two are the ones that are easy to forget:

1. **First detent → transmit immediately.** No waiting for the next cadence
   slot; this is what makes the knob feel connected.
2. **While the counter keeps changing → every ~20 ms** (§6.6), never per detent.
3. **Motion stops → send ~3 more frames** at the same cadence. See below.
4. **Idle → a heartbeat every ~2 s** carrying current counters.

### ⚠ The end-of-motion gap

The "a human is in the loop, so losses self-correct" argument in §6.1 holds
*during* a turn and **fails at the end of one**. If the final frame is lost and
the panel then goes silent, the driver's `last_seen` stays behind the panel's
counter — and nothing is left to correct it, because the human has stopped
turning. The light sits a detent or two off, and then *jumps* to catch up the
next time someone touches that knob.

States 3 and 4 above close it, and both are nearly free:

- **Tail frames.** Three extra reports after motion stops. All three colliding is
  a sub-1 % event even with several panels active.
- **Idle heartbeat.** Every panel re-publishes its counters every ~2 s. Six nodes
  at 0.5 Hz is **0.26 % bus load** — nothing — and it buys three things beyond
  closing the gap: a driver that reboots re-syncs within two seconds, a driver
  that was unplugged and returns picks up the current state, and a panel that has
  died becomes *detectable* because its heartbeat stops.

### ⚠ Jitter the cadence — do not use a fixed 20 ms

Two panels transmitting on identical fixed intervals can phase-lock and collide
on **every** attempt, indefinitely. This is the classic failure that survives
bench testing (where you turn one knob at a time) and appears in the installed
system.

Randomise each interval — 20 ms ± 4 ms — and seed the offset from the node
address so nodes start out spread. Back off by a random interval after any
collision you detect.

### How bad are collisions, really?

A frame is 0.87 ms at 115200; a panel reporting at 50 Hz occupies **4.3 %** of
the bus. A frame collides if another active panel starts within ±one frame time,
so per other active panel the risk is ~2T/P ≈ 8.7 %:

| Panels turning **at the same instant** | Collision rate | Effective update rate |
|---|---|---|
| 2 | 8.7 % | 46 Hz |
| 3 | 16.6 % | 42 Hz |
| 4 | 23.8 % | 38 Hz |
| 6 (all of them) | 36.5 % | 32 Hz |

**Collisions cost update rate, not correctness.** There is no lost input, no
drift and no desync — the next counter carries the accumulated total regardless.
Even the absurd case of six people simultaneously spinning knobs degrades a 50 Hz
stream to 32 Hz, which nobody can perceive on a dimming light. At the realistic
2–6 node scale with one or two people touching knobs, the bus is idle almost all
the time.

### Optional: real collision detection

RS-485 transceivers have **separate** DE and RE pins. Tying them together (the
usual trick to save a pin) means a transmitting node is deaf. Keep them on two
pins and a panel can **read back its own transmission** — if what returns differs
from what was sent, someone else is talking, and it can abort and back off
immediately instead of waiting for a CRC failure that it never sees anyway.

Both node types have the spare pin (§8), so this is close to free. It also makes
a shorted or stuck bus diagnosable rather than merely silent.

### If you would rather have determinism: polled master

Still an easy retrofit, and at this scale it is cheap: **~9.5 ms round-robin for
6 nodes** at 115200. Zero collisions, bounded latency, and trivially debuggable —
you know exactly what should be on the wire at every instant.

The costs are a master (obviously the driver node), constant traffic instead of a
near-idle bus, and a latency floor of one poll cycle even when only one person is
using the system. At 2–6 nodes free-running wins on simplicity; the numbers above
say it comfortably wins on behaviour too.

**The counter model is unchanged under either.** That is exactly why §6.1 is the
load-bearing decision and arbitration is not.

### 6.4 Frame sketch

```
  [0x55 sync][len][src_addr][type][payload…][crc16]
```

| Type | Direction | Payload |
|---|---|---|
| `REPORT` | panel → all | per encoder: `zone`, `counter16`, `press_count8` |
| `LEVELS` | driver → all | `zone`, `level16` — optional state beacon, ~1 Hz, for panels that display level |
| `ALL_OFF` | panel → all | — |
| `IDENT` | any | node type, firmware version, address |

CRC-16 (CCITT). No addressing of *destinations* — everything is broadcast and
receivers filter by zone. That keeps drivers and panels symmetrical and makes
adding a listener (a logger, a display) free.

### 6.5 Addressing

**Do not assign addresses at all.** Every ATtiny carries a unique serial number in
`SIGROW`; hash it to 16 bits and use that as the node ID. Six nodes in a
65536-space collide with probability ~0.02 %, so there is no DIP switch, no claim
protocol, no commissioning step, and the ID is stable across reboots and EEPROM
erasure. A node is identified by what it *is*, not by where it was installed.

The only genuinely install-specific setting is **zone binding** — which knob runs
which light. Default it positionally (encoder N ↔ channel N) so a fresh set of
boards works on first plug-in, and keep EEPROM for the override: a learn gesture
(hold a driver channel's button, then turn the knob you want bound to it) covers
the case where the east and west walls should be independent.

---

### 6.6 Bus loading — rate-limit reports, don't send per detent

A panel must **not** transmit on every detent. It should transmit its counters on
a fixed cadence — every ~20–30 ms — for as long as they are changing, and go
silent when they are not.

This matters because it bounds bus load per panel *independently of how fast
someone spins a knob*. Someone cranking an encoder at 2 rev/s generates 40
detents/s; if each one sent a frame, load would scale with enthusiasm. At a fixed
50 Hz cadence it doesn't.

The arithmetic: a single-encoder report is ~10 bytes ≈ 0.87 ms at 115200. At 50
reports/s that is **~4 % bus utilisation per actively-turning panel**. Idle
panels contribute nothing. Realistically one or two people touch knobs at once,
so the bus sits near 10 % — collisions are rare, and §6.1 means the rare one
costs nothing.

Rate-limiting is free here precisely *because* panels publish counters rather
than events: batching 20 ms of detents into one frame loses no information, since
the counter already carries the accumulated total. An event-based protocol could
not do this.

### 6.7 How far this scales

| Limit | Ceiling | Notes |
|---|---|---|
| **Transceiver loading** | **~128 nodes** | MAX483/MAX487 are ¼-unit-load parts (the plain MAX485 is 1 UL → 32). Worth confirming in the datasheet's unit-load column when you order. |
| **Address space** | 256 | one byte of `src_addr`; zones are 4 bits → 16 |
| **Bus bandwidth** | ~20 simultaneously-active panels | at 4 % each; idle panels are free |
| **Cable length** | several hundred m | the baud × distance rule gives ~870 m at 115200; drop the baud if you ever need more |
| **Panel power** | **~10 panels / 40 m** | the real constraint — see the daisy-chain notes in §5 |

Practical guidance: one bus segment comfortably serves a hall. If you ever
outgrow it, the cheap escape is a second segment with its own driver node rather
than a repeater.

## 7. What this deletes

Worth stating, because it is most of the UI complexity:

- **channel selection** — no longer a concept;
- **the ack-blink** — nothing to acknowledge;
- **channel indicator LEDs on the DIM lines** — each knob is next to its own
  channel;
- **the joystick, the 5-way switch, the resistor ladder, and the whole
  second-control-point analysis** — superseded by panels on a bus. That work is
  in [`../deprecated/`](../deprecated/) and in `hardware/input.md`.

The status LED survives, meaning what it already means: *this node is alive*. On
a bus it gains a second job — showing link state.

---

## 8. Pin budgets

### Driver node — ATtiny3216, 18 I/O

USART0's default pins (PB2/PB3) are needed for RS-485, so WO2 moves to its
alternate (PB5) and WO0/WO1 stay at PB0/PB1. TWI0 moves to its **alternate**
PA1/PA2 — free now that the joystick is gone.

| Pin | Function |
|---|---|
| PA0 | UPDI |
| PA1 / PA2 | I²C SDA / SCL (TWI0 **alt**) → Qwiic |
| PA3 / PA4 / PA5 | free — LED ch4–6 footprints, *or* one local EC11 |
| PA6 / PA7 | free |
| PB0 / PB1 | **LED ch1 / ch2** (TCA0 WO0 / WO1) |
| PB2 / PB3 | **RS-485 TXD / RXD** (USART0) |
| PB4 | **RS-485 DE+/RE** (tied) |
| PB5 | **LED ch3** (TCA0 WO2 **alt**) |
| PC0 | IR receiver |
| PC1 | Status LED |
| PC2 | Qwiic 3V3 enable |
| PC3 | free |

Comfortable, with room for a local encoder and the Qwiic port intact.

### Control panel — ATtiny1616 (or 3216), 18 I/O

| Pins | Function |
|---|---|
| 9 | 3 × EC11 (A, B, SW each) |
| 3 | RS-485 TXD / RXD / DE |
| 1 | Status LED |
| 1 | UPDI |
| **14 of 18** | 4 spare — a fourth encoder needs 3 of them |

A 4-encoder panel fits at 17/18. If it gets tight, put the encoder *buttons* on a
resistor ladder — that analysis is already done in `hardware/input.md` §2 and
recovers 2 pins per 3 buttons.

Panels need no ADC, no PWM and little flash, so **ATtiny1614** (14-pin) also
works for a 2-encoder panel and is cheaper. Using the **same ATtiny3216
everywhere** is worth considering purely for BOM and toolchain simplicity.

---

## 9. Parts (new to this design)

| Part | Where | ~Cost | Note |
|---|---|---|---|
| **MAX3082** (5 V) / **SN65HVD3082E**, **THVD1410** (3.3 V) | every node | ~₹17–40 | half-duplex RS-485, **slew-limited + true fail-safe**. Slew limiting removes termination; fail-safe removes bias. MAX483/487/SP3485 work but need one bias network. |
| 120 Ω 1 % | **none** | — | termination is unnecessary at ≤115200 / ~100 m with slew-limited parts; if ever needed, fit it in a 6P4C *plug*, not on a board |
| Bias resistors | **none** | — | eliminated by choosing a true-failsafe receiver; otherwise one network on the driver node only |
| Schottky bridge | per panel | ~₹5 | makes cable polarity harmless; distribute 6–7 V to cover its ~0.6 V |
| **EC11 encoder** + knob | per channel | ₹25 | 20 detents, with push |
| 1 kΩ + 10 nF | per encoder line | — | RC debounce |
| RJ45 jacks or 4-pin screw terminals | per node | — | CAT5 in / CAT5 out for daisy-chain |
| 12 V → 3.3/5 V buck | per panel | ₹60 | local regulation |
| TVS + series resistors on A/B | per node | — | the bus leaves the enclosure |

---

## 10. Open questions

| Question | Notes |
|---|---|
| **Do panels display level?** | If yes they need an indicator per encoder (a small LED, or an LED ring) and must listen to `LEVELS` beacons. If no, panels are write-only and the protocol gets simpler. **Decide this first — it changes the panel BOM and half the protocol.** |
| **Bus speed** | 115200 is safe on twisted pair. Drop to 19200–38400 on untwisted cable — nothing here is throughput-bound. |
| **Free-running vs polled** | Free-running, per the collision numbers in §6.3. Polled stays an easy retrofit (~9.5 ms cycle at 6 nodes) if you want determinism during bring-up. |
| **Isolation** | Only needed if panels sit on different mains circuits. Costs ~₹200/node. |
| **Fail-safe transceiver sourcing** | The plug-and-play story leans on it. If MAX3082/SN65HVD3082E/THVD1410 are hard to get locally, fall back to MAX483/487 plus one bias network on the driver node — panels stay identical either way. |
| **Zone count** | 16 is almost certainly plenty for one hall. |
| **Does the driver keep IR?** | It is 1 pin and already written. Probably yes, as a per-node override. |
| **Panel power** | **5 V down the pair → 3.3 V LDO on the panel, MCU at ≤10 MHz.** No buck converter anywhere. Revisit only if a panel grows a real load. |

---

## 11. Suggested build order

1. **Two nodes, one link.** One driver, one panel, 15 m of CAT5. Prove the
   electrical layer and the counter model before designing anything else.
2. **Zones.** Add a second driver node; confirm one knob moves both.
3. **Multi-panel.** Add a second panel on the same zone; confirm deltas add and
   nothing fights.
4. **Then** decide about level display, scenes, and the reserved gestures in §4.

Step 1 is the one that de-risks the whole design. Everything after it is
incremental.
