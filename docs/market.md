# Where gliim sits

Market notes for rev A. Written to answer one question honestly: *if someone
wants a knob per zone at every door in a hall, what are they doing today, and
what does it cost them?*

Prices are indicative and gathered mid-2026. Commercial figures are US list, in
USD, because that is where they are published; gliim's are Indian build cost in
₹. Treat the ratios as the signal, not the absolute numbers.

---

## 1. The alternatives, honestly

### Wall dimmer switches — Lutron, Havells, Anchor, Legrand

Cheap, everywhere, and genuinely good at what they do. A rotary or slide dimmer
in a standard box, cutting mains phase to a dimmable fixture.

**Where they fall short for this job:**

- **They dim the mains, not the LED.** That means TRIAC/leading-edge chopping of
  the supply, and how well it works depends entirely on the fixture's own driver.
  Cheap dimmable LEDs flicker, buzz or drop out in the bottom third — which is
  exactly the range a hall wants in the evening.
- **One dimmer, one place.** Multi-point control means 3-way/4-way wiring runs
  back to a second box, and that only ever gives you *two* points, not five.
- **One circuit per dimmer.** Three zones is three switch plates, three mains
  runs, and an electrician.

### Smart bulbs and app systems — Philips Hue, Wipro, Syska

Per-bulb intelligence, an app, usually a hub, often Wi-Fi.

- Works beautifully for a handful of bulbs in a living room.
- **Scales badly in a hall**: every fixture needs to be a smart fixture, so cost
  scales with fixture count rather than with zone count.
- **The phone is the interface.** Guests cannot use it. Anyone without the app
  cannot use it. When the router is down, nobody can use it.
- Physical control is retrofitted at best — a battery remote or a smart switch
  that has to be paired.

### 0-10 V — the commercial default

The workhorse of architectural lighting. A low-voltage analogue pair runs from a
dimmer back to each driver.

- **~$40–70 per zone** for the dimmer, per
  [Jarvis Lighting](https://www.jarvislighting.com/blogs/jarvis-lighting-insights/led-dimming-protocols-0-10v-dali-guide)
  — the most common commercial protocol precisely because it is simple and needs
  no commissioning.
- Requires **0-10 V-capable drivers** on every fixture, which is a cost on top.
- Multi-point control is not free — you are back to wiring several dimmers into
  one control loop, with the interactions that implies.
- Nothing about it is *bad*. It is simply priced and shaped for a building, not
  a room.

### DALI — the proper digital system

Addressable, two-way, per-fixture control. The right answer for a real building.

- **$200+ per controller**, and it needs **commissioning with specialist
  software** to assign addresses and configure groups and scenes — normally a
  trained integrator ([Jarvis](https://www.jarvislighting.com/blogs/jarvis-lighting-insights/led-dimming-protocols-0-10v-dali-guide),
  [Ottima](https://www.ottima-tech.com/blog-details/0-10v-vs-dali-dimming-which-suitable-commercial.html)).
- The industry's own rule of thumb: **worth it at ~2,000 fixtures, not at 20.**
- gliim borrows DALI's *shape* — a low-voltage bus, addressable nodes — and
  deliberately discards its commissioning step.

### Maker controllers — WLED, ESPHome, Shelly

The honest peer group, since gliim is also a self-built board.

- [Rotary encoders can be wired to a Shelly Dimmer](https://www.instructables.com/Shelly-Dimmer-Wall-Switch-With-Rotary-Knob-and-Hom/),
  and it keeps working when Wi-Fi is down — so the idea is not unique.
- But these are **Wi-Fi-first designs with physical control bolted on.** The
  encoder is a mod, not the interface.
- **WLED is about effects on addressable strip**, not constant-current dimming of
  white fixtures. Different problem.
- **Shelly dims mains**, so it inherits the flicker floor of whatever driver is
  in the fixture.
- Combining WLED + ESPHome + Shelly + physical control is custom integration
  work every time. There is no product here, only a recipe.

---

## 2. What it costs

A hall with three zones and three control points:

| | Cost | Notes |
|---|---:|---|
| **gliim** — 1 driver node + 3 wired panels + cable | **~₹2,950** (~$35) | Every panel controls every zone. Cable is ~₹7/m. |
| 0-10 V — 3 dimmers | ~$120–210 | *Dimmers only.* Add 0-10 V drivers per fixture. One control point per zone. |
| DALI — 1 controller | $200+ | Plus commissioning by someone who owns the software. |
| Smart bulbs | scales with fixture count | Plus a hub, an app, and a working router. |

**Roughly 4–6× cheaper per zone than the commercial floor**, and multi-point is
free rather than an extra — a fourth panel is ~₹450 and needs no reconfiguration
anywhere.

Per-board build cost is in [`../hardware/bom-mainboard.md`](../hardware/bom-mainboard.md).

---

## 3. The actual gap

Every option above is good at something. Laid side by side, one column is empty:

| | Physical knob is the *primary* interface | Multi-point without rewiring | No commissioning | Deep flicker-free dimming | Works with the router off |
|---|:-:|:-:|:-:|:-:|:-:|
| Wall dimmer | ✓ | ✗ | ✓ | ✗ | ✓ |
| Smart bulbs | ✗ | ✓ | ✗ | ~ | ✗ |
| 0-10 V | ✓ | ~ | ✓ | ✓ | ✓ |
| DALI | ~ | ✓ | ✗ | ✓ | ✓ |
| Maker (Shelly/WLED) | ~ | ~ | ✗ | ✗ | ~ |
| **gliim** | **✓** | **✓** | **✓** | **✓** | **✓** |

The row is not a boast — it is a description of a **narrow niche nobody is
serving**, because the niche is too small to be a product. Commercial vendors
price for buildings; consumer vendors sell phones-as-interface. A room-sized
system with knobs as the point falls between the two.

That is the correct reason for this to exist as an open design rather than a
business.

---

## 4. Who it is for

**A good fit:** halls, community rooms, studios, workshops, dining rooms and
libraries — spaces with several distinct lighting zones, several entrances, and
users who will not be briefed. Anywhere a phone-based system would be a barrier
rather than a feature.

**A bad fit:** a single room with one light (a ₹300 wall dimmer wins); anything
needing scheduling, occupancy sensing or energy reporting (that is
[lokki](https://github.com/m-anish/lokki)); any installation that needs a
warranty, a certification mark, or somebody to call.

---

## 5. What it is not

Worth stating plainly, because the comparison table flatters:

- **No certification.** No CE, no BIS, no UL. It is a self-built board.
- **No warranty and no support.** If it fails at 2 a.m. you are the support.
- **Not mains-rated.** Everything runs from a USB-C PD supply at 15 V — which is
  a deliberate design choice, and also means it cannot replace a wall switch on a
  mains circuit.
- **Untested at scale.** rev A has not been through a season in a real hall.

None of this disqualifies it for its intended use — a friend's home, a community
hall, a workshop. All of it disqualifies it from being sold.

---

## 6. Sources

- [Jarvis Lighting — LED dimming protocols: 0-10V vs DALI](https://www.jarvislighting.com/blogs/jarvis-lighting-insights/led-dimming-protocols-0-10v-dali-guide)
- [Ottima — 0-10V vs DALI for commercial lighting](https://www.ottima-tech.com/blog-details/0-10v-vs-dali-dimming-which-suitable-commercial.html)
- [Lummi Light — DALI control system guide](https://lummilight.com/dali-lighting-control-system/)
- [Instructables — Shelly Dimmer wall switch with rotary knob](https://www.instructables.com/Shelly-Dimmer-Wall-Switch-With-Rotary-Knob-and-Hom/)
- [ESPHome — Shelly Dimmer component](https://esphome.io/components/light/shelly_dimmer/)
