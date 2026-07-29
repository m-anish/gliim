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
- Idle bias resistors are mandatory; 120 Ω termination at **both ends only** is
  good practice but optional at this length with slew-limited parts.
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
| **CAT5e/CAT6 UTP** | Default. Twisted, 8 conductors, RJ45 ecosystem everywhere. The price gap to phone cable is small — check locally before optimising it away. |
| **Round twisted telephone (2–3 pair)** | **Fine.** If it is twisted, the objection disappears. |
| **Flat 6-core "silver satin"** | Workable with all three changes above. Use the conductor order shown. |
| **Flat 4-core** | Workable, no spare conductors, no room for a second ground. |
| **4/6-core alarm or CCTV cable** | Common and cheap; often shielded, which substitutes well for twist. Tie the shield to GND **at one end only**. |

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
moves or a button changes. Events are human-paced and rare; collisions are
unlikely, and when they happen the CRC drops the frame — which, given §6.1,
costs nothing. Retransmit after a short random backoff.

**Fallback if that misbehaves at scale: polled master.** One node (a driver)
polls each panel in turn; panels reply with the same counters. Deterministic, no
collisions, ~10 ms cycle for 8 panels at 115200. The counter model is unchanged —
only who-talks-when differs, which is exactly why §6.1 is the load-bearing
decision.

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

Store the node address in **EEPROM**, not DIP switches — the pins are worth more
than the convenience, and a commissioning gesture (hold the encoder at power-on,
count the blinks) costs nothing. Ship a factory default that is guaranteed
unique-ish (e.g. derived from the chip's serial number) so an unconfigured bus
still works.

---

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
| **MAX483 / MAX487** (5 V) | every node | ~₹17 | half-duplex RS-485, **slew-limited** — prefer over MAX485, see cabling |
| 120 Ω 1 % | 2 (bus ends only) | — | termination; optional at this length with slew-limited parts |
| 680 Ω ×2 | 1 place on the bus | — | idle bias, A high / B low. **Not optional.** |
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
| **Free-running vs polled** | Start free-running (§6.3). Only move to polled if collisions actually bite. |
| **Isolation** | Only needed if panels sit on different mains circuits. Costs ~₹200/node. |
| **Zone count** | 16 is almost certainly plenty for one hall. |
| **Does the driver keep IR?** | It is 1 pin and already written. Probably yes, as a per-node override. |
| **Panel power** | 12 V + a local buck on CAT5; **5 V direct, no buck** on cheap cable. The 5 V route is simpler and quieter — consider it the default unless a panel grows a real load. |

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
