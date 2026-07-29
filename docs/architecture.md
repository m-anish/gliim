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
- **Use an HW-0519-class auto-flow module** (~₹31) rather than placing a bare
  transceiver: TVS protection included, termination jumper-disabled by default,
  auto-direction so there is no DE pin. See *Use a generic RS-485 module* below.
- **Termination is not needed** at this speed and length — with *any* transceiver,
  reflections settle three orders of magnitude before the sampling instant. Remove
  the 120 Ω many modules ship with. Bias is needed only if the part lacks a
  fail-safe receiver. See *Plug-and-play* below.
- Daisy-chain, not star. Stubs short.

### What was rejected, and why

| Option | Why not |
|---|---|
| **I²C / Qwiic over distance** | Designed for on-board, < 1 m. Single-ended, no noise margin, and it hangs the whole bus when a node glitches. Even with P82B715 extenders this is the wrong tool. |
| **CAN** | Genuinely more robust — arbitration and error handling in hardware. But the ATtiny has no CAN controller, so it means an MCP2515 + transceiver per node over SPI. Real cost and complexity for a benefit a lighting panel doesn't need. |
| **Wireless (nRF24 / HC-12 / ESP-NOW)** | Wins only if panels are **battery**-powered; otherwise you are running a wire anyway and two more conductors are free. Full comparison in *Wireless — when it actually wins* below. |
| **DMX512** | Is RS-485 at 250 kbaud with a lighting protocol on top — but unidirectional controller → fixture, and our panels are *inputs*. Worth knowing about if glim ever needs to sit in a stage-lighting rig. |
| **DALI** | The actual industry standard for this exact job: 2-wire, polarity-insensitive, 1200 baud, designed for building lighting. Needs specific transceivers and a real stack. Right answer for a commercial product, over-heavy for this one. |

### Wireless — when it actually wins

Worth a real answer rather than a one-line dismissal, because the wired-vs-RF
question has exactly one hinge:

> **Does the panel need a power wire regardless?**
> If yes, wireless saves nothing — the hard part of the install is already done,
> and two extra conductors in the same cable are nearly free.
> If no — a battery panel — wireless enables something wired cannot: a knob you
> stick on any wall with no wiring at all.

Everything else is secondary to that. Rough costs for six nodes:

| Approach | ~Cost | The catch |
|---|---|---|
| **Wired RS-485** | ~₹1400 | Labour of pulling cable. Includes a one-off ₹300 crimper. |
| RF, mains-powered panels | ~₹1200 | **Needs a mains socket at every panel location** — and walls where you want a light switch rarely have one. Usually a worse install than the cable. |
| RF, **battery** panels | ~₹800 | Zero install labour. Sleep firmware, and a dead battery means a dead light switch. |
| HC-12 (UART-transparent) | ~₹1800 | Drop-in — same protocol, same firmware. Just expensive. |

**The honest case for RF:** if the hall is already finished, with no conduit and
no easy route, pulling cable can dominate the entire cost and effort of the job.
That is a real argument and it is about the building, not the electronics.

**The honest case against:** a light switch that stops working because of a
battery is worse than one that needed a wire. Commercial battery wall controls
exist and work well, but they are carefully engineered around exactly this
problem. And RF is invisible when you are debugging — the wired module has TX/RX
LEDs you can watch.

One point genuinely in RF's favour, which cuts against my earlier dismissal:
**the counter protocol (§6.1) already tolerates packet loss.** A dropped RF
packet is exactly as harmless as a dropped RS-485 frame, and the next report
carries the correct cumulative value. Wireless is far more palatable under this
design than it would be under an event-based one.

If you go RF:

- **nRF24L01+** (~₹80) — cheapest, hardware auto-ACK and retransmit, but it costs
  5 SPI-ish pins against the module's 2 (panel budget: 16/18 instead of 13/18),
  and it is famously fussy about supply decoupling. 2.4 GHz is fine across one
  open hall, poor through brick.
- **HC-12** (~₹300) — 433 MHz, ~1 km, and **transparent over UART**, so it is a
  literal drop-in for the RS-485 module: same pins, same frames, same firmware.
  Check the latency of whichever FU mode you use — some buffer enough to make a
  knob feel laggy.
- Avoid bare 433 MHz ASK modules (FS1000A class). No addressing, no error
  detection, and the band is full of doorbells and car remotes.

**This is not a one-way door.** The protocol is framed bytes over a UART — it has
no idea what carries them. Building wired now and bridging a battery panel over
RF later requires no protocol change at all.

#### Fit both footprints — but select with a jumper, not power

Putting an **HC-12 footprint next to the RS-485 module footprint** on every board
is the right call. It costs board area and nothing else, one PCB design then
serves every node, and each install picks its medium at build time. Do it.

Two things make it work, and the second is where this pattern usually goes wrong.

**1. Both modules share the one USART.** The ATtiny3216/1616 have a single
USART0, so "both populated and both live" is not automatic — they would both
drive the MCU's RX line. The MCU's **TX** side is fine driving both modules' RX
in parallel (one output, two inputs).

**2. ⚠ A power switch alone does not isolate the unused module.** An unpowered
CMOS output does not go high-Z — it clamps through its ESD diode into a dead VCC
rail. The unpowered module will **drag the shared RX line toward ground and draw
parasitic current through its own TX pin.** This is the standard failure of
"just switch the power" and it looks like a mysteriously dead UART.

**Do this instead:** a **3-pin jumper on the RX line** selecting which module's
TX reaches the MCU. Zero parts, zero ambiguity, and it works whether or not the
unused module has power.

Keep the power switch as well, for a different reason: **HC-12 idle current is
real** — roughly 16 mA in FU3, ~3.6 mA in FU1, ~80 µA in FU2. Cutting power to an
unused radio is worth more than cutting power to an unused RS-485 module.

**Budget for the HC-12's transmit burst.** It pulls ~100 mA at full power, which
through a 12 V → 5 V linear regulator is a ~0.7 W transient. Fit **≥470 µF** of
bulk capacitance at the module, and consider configuring lower TX power — across
one hall you need nothing like 100 mW. The HC-12 also wants a `SET` pin for
configuration; a panel budget of 9 (encoders) + 2 (UART) + 1 (SET) + 1 (status) +
1 (UPDI) = **14 of 18** still leaves room.

#### If you want both live at once: use a 2-series ATtiny, not a co-processor

**Short answer to "is TWI0 master or slave": both.** The peripheral has separate
master (`MCTRLA`) and slave (`SCTRLA`) register sets, so an ATtiny can be either
— `Wire.begin()` for master, `Wire.begin(addr)` for slave in megaTinyCore. The
1-series TWI also has a `DUALCTRL` register for running master and slave at once
on split pins; verify in DS40002205A §22 before relying on that part.

So the co-processor scheme *works*: a second ATtiny owns the radio's UART and
presents it to the main MCU over I²C. It is a legitimate pattern. But there is a
cheaper answer to the same problem.

**The ATtiny3226 (2-series, 20-pin SOIC) has two USARTs.** Same package, similar
price, and it also brings **3 KB SRAM** instead of 2 KB and a 1 MHz-capable TWI.
One chip, one firmware, no I²C link, no AND gate, no second UPDI header — wired
and wireless simply get a hardware UART each.

Against the co-processor that is: ~₹50 of MCU saved, a whole second firmware not
written, and no inter-chip protocol to debug at 3 a.m.

**Verify before switching parts:**

- **TCA0 still behaves the same.** The 2-series keeps TCA0 with normal (16-bit)
  and split modes, so the WO0–WO2 constraint and the whole PWM engine carry over
  unchanged. Confirm against the 2-series datasheet.
- **The millis timer.** The 2-series drops TCD0 and has TCB0/TCB1 instead, so
  megaTinyCore puts `millis()` on a TCB. That is fine — the thing that matters is
  that **TCA0 stays free** for `takeOverTCA0()`, and it does.
- **USART1's pins** on the 20-pin part — check Table 5-1 of the 2-series datasheet
  against the §8 budget before committing the layout.
- **Local availability and price.** The 3216 was chosen partly because it was
  cheap and in stock; confirm the 3226 is too.

**Keep the co-processor idea in reserve** for one case it genuinely wins: if you
want the radio to be a **swappable daughterboard** — HC-12 today, LoRa or nRF24
later — plugging into the existing Qwiic/I²C port with no main-board respin. That
is a real product feature, not just a workaround. If you go that way, **add an
attention/interrupt GPIO** from the radio board to the main MCU so it reads on
demand rather than polling I²C constantly.

**The cheap fallback, if you stay on the 3216.** Merge the two modules'
TX lines into MCU RX with a **74LVC1G08 AND gate** (~₹8) instead of the jumper.
UART idles high and data pulls low, so ANDing two idle-high lines passes either
stream through correctly. Simultaneous traffic on both media garbles a frame, but
the CRC drops it and §6.1 recovers — and that case is rare. Two firmware rules
are then mandatory:

- **Discard any frame whose source ID is your own** (already required, since the
  auto-flow module echoes your own transmissions back).
- **Add a "forwarded" bit or a TTL** so a frame is relayed at most once.
  Otherwise the bridge re-forwards its own echo and loops the bus forever.

Fit the AND-gate footprint on every board and populate it only on the one node
that needs to bridge. Wired panels and RF panels then coexist on one system.

**Recommendation: wired.** At 2–6 nodes in one hall, with panels that want to be
on walls where sockets generally are not, the cable does double duty as power and
data and removes the battery from the system entirely. Revisit only if surveying
the building says cable cannot go where the panels must.

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

**4. One series Schottky is enough for polarity.**

Modular connectors are keyed, so the *plug* cannot go in wrong — the only real
exposure is a cable that was wired reversed, and if you crimp your own or buy
straight-through leads that is already unlikely. A full bridge rectifier is
over-engineering for that risk.

Keep **one series Schottky** on the panel's +V input. It costs ~₹2 and ~0.3 V,
lets you stay on 5 V distribution, and converts the one bad outcome — a
destroyed panel — into "this panel does not light up", which is diagnosable in
seconds. That is the whole point; the bridge only bought the ability to *work*
when reversed, which is not worth 0.6 V and four parts.

**On RJ45, pick pins 4/5 for data.** The blue pair is identical in T568A and
T568B, so a mixed-standard or crossover lead cannot swap it. (Pins 1/2 and 3/6
*do* swap between the standards.) The blue pair is also where an RJ11 6P4C
plug's centre pair lands in an RJ45 jack, so the two connector systems agree.

*Optional, ~10 lines of firmware:* an A/B swap merely inverts the received
signal, and the ATtiny can undo it in hardware via `PORT_INVEN_bm` in the RX and
TX `PINnCTRL`. With a fail-safe receiver a correct idle line sits HIGH and a
swapped one sits LOW, so "RX continuously low for >20 ms" is a reliable detector.
Cheap insurance; not a requirement given keyed connectors.

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

#### Use a generic RS-485 module, not a transceiver footprint

**Yes — put a 6-pin header on the PCB and drop a ready-made MAX485-class module
on top.** At 2–6 nodes this is the right call:

- No fine-pitch part to place, and no bus-facing circuitry to respin if you get
  it wrong.
- **The transceiver is the part that dies.** It is the only thing connected to a
  long cable leaving the enclosure, so it eats the surges. Making it a
  ₹40 socketed module rather than a soldered IC turns a scrapped board into a
  30-second swap.
- Available everywhere, and ideal for step 1 of §11.

**Recommended part: the HW-0519-class "RS485 to TTL, automatic flow control"
module** — MAX485 based, ~₹31, 0.8 mm PCB with castellated pads so it can sit
flat on the carrier board rather than on a header. Four properties make it a
better fit than the plain blue breakout:

| Feature | Why it matters here |
|---|---|
| **Automatic direction control** | No DE/RE pin at all. Frees a GPIO *and* deletes the single most common RS-485 firmware bug — holding DE a bit too long or releasing it a bit too early. |
| **Termination fitted but disabled** (short the `R0` jumper to enable) | Exactly the default we want. Leave `R0` open on every node and there is nothing to desolder. |
| **On-board TVS + earth pad** | The cable leaves the enclosure and runs across a hall; the transceiver is what eats surges. A bare MAX485 gives you none of this. The earth pad may be left unconnected for indoor runs. |
| **RXD / TXD indicator LEDs** | Bring-up diagnostics for free — you can see traffic without a scope. |

Pinout is `VCC · GND · TXD · RXD` on the logic side, `A+ · B− · earth` on the
bus side. Two pins per node instead of five.

**⚠ Pull TXD up with 10 kΩ on your PCB.** The auto-direction circuit works by
sniffing the TXD line, and the ATtiny's UART pin is **high-Z from power-on until
firmware configures it**. A floating TXD can make the module assert its driver
and **jam the entire bus** while one node boots. The pull-up holds TXD in its
idle-high state through that window. This is the same class of hazard as the
PT4115 DIM pulldowns.

**Verify on arrival — four quick checks:**

1. **Measure A–B with a multimeter.** Should read open, not ~120 Ω. If it reads
   120 Ω, `R0` is shorted from the factory: six terminated nodes is 20 Ω, well
   under the ~54 Ω an RS-485 driver is specified for, and the differential
   voltage collapses toward the 200 mV receiver threshold. The resulting flakiness
   looks exactly like a firmware bug.
2. **Idle RXD should sit steady HIGH** with the bus connected but silent. If it
   chatters, the module has no fail-safe bias — add one network on the driver
   node.
3. **Does it echo?** Transmit a byte and see whether it arrives back on RXD. Many
   auto-direction designs tie RE permanently active, so you hear yourself. That
   is *useful* — it is exactly the transmit read-back that §6.3 wants for
   collision detection. **Either way the firmware must discard any frame whose
   source ID is its own.**
4. **Confirm the supply rail.** A genuine MAX485 is a 5 V part (4.75–5.25 V). The
   "3.3 V compatible" claim most likely refers to the *TTL side* accepting 3.3 V
   logic. Assume **VCC = 5 V** unless the chip turns out to be a MAX3485.

**Knock-on: distribute 12 V, not 5 V.** If the module wants a solid 5 V, then 5 V
distribution minus the series Schottky and cable drop lands near 4.3 V — out of
spec. Send **12 V** and drop it with a **linear regulator** on the panel. At
~30 mA that is (12−5) × 0.03 ≈ **0.2 W**, comfortable in a SOT-89, and a linear
part keeps a switcher off the conductors beside the data pair. This is simpler
than the earlier 3.3 V plan: one rail, everything in spec, no level shifting.

**Cost of auto-direction:** turnaround timing is no longer yours to control. That
is fine for free-running broadcast (§6.3), which has no tight timing anywhere. If
you ever switch to a polled master, re-check that the module releases the bus
fast enough between poll and reply.

The listing's "Note: Only one in the whole network…" line is machine-translated
and does not parse; there is no evidence of a real constraint behind it. The
120 Ω jumper is the only genuine one-per-network item, and we disable it anyway.

**What a generic module costs you.** You no longer choose the transceiver, and
almost every module carries a plain **MAX485**: fast edges, no true fail-safe.
Re-deriving the two properties the plug-and-play design leaned on:

- **Termination — still not needed.** Fast ~10 ns edges *do* make the line
  electrically long at 15 m, so reflections genuinely occur. But they settle in
  ~500 ns against an 8.7 µs bit sampled at 4.3 µs, so they still cannot cause a
  bit error at 115200. You pay in radiated EMI, not in reliability. **Do not
  raise the baud rate** on fast-slew parts without revisiting this.
- **Bias — needed again**, since a plain MAX485 has no fail-safe threshold.
  Either use the module's own bias resistors (above), or fit one network on the
  driver node.

So a generic module works fine here. Buying a **slew-limited, true-failsafe**
part (MAX3082 / SN65HVD3082E / THVD1410) is still the better engineering — lower
EMI, no bias resistors, headroom to go faster — but it is an optimisation, not a
prerequisite. If a module gets you building this month, use the module.

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
| **RS-485 auto-flow module** (HW-0519 class) | every node | **~₹31** | MAX485 + TVS + jumper-selectable 120 Ω + auto-direction. Leave `R0` open. **Needs a 10 kΩ pull-up on TXD.** |
| **HC-12 footprint** | every node, populated as needed | ~₹300 | 433 MHz UART-transparent alternative. Fit the footprint even if unused — see *Fit both footprints*. |
| 3-pin jumper on RX | every node | — | selects which module drives the MCU's RX. **Not a power switch** — an unpowered module still loads the line. |
| 74LVC1G08 AND gate | bridge node only | ~₹8 | merges both modules onto one RX so a node can carry wired and RF at once |
| 470 µF bulk cap | HC-12 nodes | ~₹5 | rides out the ~100 mA transmit burst |
| *or* **MAX3082** / **SN65HVD3082E** / **THVD1410** | every node | ~₹17–40 | bare IC, **slew-limited + true fail-safe** — lower EMI, no bias resistors, but no TVS and you own the DE timing. An optimisation, not a prerequisite. |
| 120 Ω 1 % | **none** | — | termination is unnecessary at ≤115200 / ~100 m with slew-limited parts; if ever needed, fit it in a 6P4C *plug*, not on a board |
| Bias resistors | **none** | — | eliminated by choosing a true-failsafe receiver; otherwise one network on the driver node only |
| Schottky diode (series) | per panel | ~₹2 | reverse-polarity protection: a miswired cable means "does not power up", not a dead board |
| **EC11 encoder** + knob | per channel | ₹25 | 20 detents, with push |
| 1 kΩ + 10 nF | per encoder line | — | RC debounce |
| RJ45 jacks or 4-pin screw terminals | per node | — | CAT5 in / CAT5 out for daisy-chain |
| 12 V → 5 V linear regulator | per panel | ~₹10 | ~0.2 W at 30 mA; no switcher beside the data pair |
| TVS + series resistors on A/B | per node | — | the bus leaves the enclosure |

---

## 10. Open questions

| Question | Notes |
|---|---|
| **Do panels display level?** | If yes they need an indicator per encoder (a small LED, or an LED ring) and must listen to `LEVELS` beacons. If no, panels are write-only and the protocol gets simpler. **Decide this first — it changes the panel BOM and half the protocol.** |
| **Bus speed** | 115200 is safe on twisted pair. Drop to 19200–38400 on untwisted cable — nothing here is throughput-bound. |
| **Free-running vs polled** | Free-running, per the collision numbers in §6.3. Polled stays an easy retrofit (~9.5 ms cycle at 6 nodes) if you want determinism during bring-up. |
| **Isolation** | Only needed if panels sit on different mains circuits. Costs ~₹200/node. |
| **Module or bare IC?** | Settled: HW-0519-class auto-flow module, ~₹31. TVS included, termination jumper-disabled, one fewer GPIO. Move to a bare slew-limited fail-safe IC only if EMI or a higher baud rate ever justifies it. |
| **Wired or RF per node** | Deferred to build time by fitting both footprints. Decide per install; a bridge node lets the two coexist. |
| **ATtiny3216 or 3226?** | The 2-series **3226** has two USARTs and 3 KB SRAM in the same 20-pin package — it makes wired+wireless-at-once trivial and costs nothing extra. Check local price and stock, and confirm USART1's pins fit §8. Leaning 3226. |
| **Zone count** | 16 is almost certainly plenty for one hall. |
| **Does the driver keep IR?** | It is 1 pin and already written. Probably yes, as a per-node override. |
| **Panel power** | **12 V down the pair → 5 V linear regulator on the panel.** Keeps the MAX485 module in spec and puts no switcher near the data pair. ~0.2 W in the regulator. |

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
