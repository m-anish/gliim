# glim

![status](https://img.shields.io/badge/status-rev2%20planning-orange)
![mcu](https://img.shields.io/badge/MCU-ATtiny3216-323330)
![driver](https://img.shields.io/badge/LED%20driver-PT4115-fbb034)
![bus](https://img.shields.io/badge/bus-RS--485-6e5494)
![core](https://img.shields.io/badge/core-megaTinyCore-00979d)
![license](https://img.shields.io/badge/license-MIT-blue)

> A *glim* is an old word for a small light — a candle, a lantern, the thing you
> carry into a dark room.

Dimmable light you control by **turning a knob**. One EC11 rotary encoder per
channel, PT4115 constant-current drivers, and an ATtiny3216 doing 16-bit PWM.
Built to put good, easily-adjustable lighting in a friend's house — no app, no
pairing, no wall of identical switches. Grab the knob for the light you want and
turn it.

Control panels talk to driver nodes over **RS-485 on CAT5**, so a panel can sit
15 m from the lights it runs and a hall can have as many of each as it needs.

The unshowy little sibling of [lokki](https://github.com/m-anish/lokki), which
does the same job at campus scale. Part of the
[starstucklab](https://github.com/m-anish/starstucklab) family.

---

## Status

**rev2 is in planning.** The architecture is written up; the firmware for it is
not. See **[docs/architecture.md](docs/architecture.md)** — topology, the
transport comparison, the protocol design, and pin budgets for both node types.

What exists today: a working 3-channel dimmer firmware (5986 B / 32 KB) whose PWM
engine, gamma curve, ramps, EEPROM persistence and IR all carry forward — but
with the **pre-pivot joystick UI and pin map**. That part gets rewritten for
encoders and the bus.

**rev1** — the ATtiny814 joystick version — is finished, worked, and is retired.
It lives in [`deprecated/`](deprecated/README.md) with its firmware, docs and BOM
intact, plus the six hard-won findings worth carrying forward.

## What it does

- Drives **3 independent LED channels** through PT4115 constant-current drivers,
  dimmed by **16-bit** hardware PWM — deep enough to reach the drivers' own
  ~1600:1 limit, so the bottom of the range is smooth rather than steppy.
- **One knob per channel.** Turn for brighter/dimmer, press to toggle. No channel
  selection, no mode, nothing to remember.
- **Panels anywhere on the bus.** Any panel can run any zone; several panels can
  run the same zone without fighting.
- **Remembers the room.** Levels are saved a few seconds after you stop fiddling,
  so flipping the wall switch brings the lights back exactly as you left them.
- **Feels linear.** Brightness is gamma-corrected, so equal knob travel is
  equal-looking change instead of "nothing… nothing… BLINDING."

## Hardware

| Part | Role |
|------|------|
| ATtiny3216 | driver node — 3× 16-bit PWM, RS-485, IR, I²C, status LED |
| ATtiny1616 | control panel — 3× EC11, RS-485 |
| 3× PT4115 | buck LED drivers, one per channel (up to ~5 LEDs each) |
| MAX483 / MAX3483 | slew-limited RS-485 transceiver, one per node (~₹17) |
| EC11 encoder | one per channel, with push switch |
| USB-C PD trigger | 12–20 V for the LED rail, 5 V for logic |
| 2-pair CAT5 + RJ11 (~₹7/m) | one cable carries the bus **and** panel power |

Full board spec and pin map: [hardware/board.md](hardware/board.md). The LED
channel itself — topology, sense resistor, inductor, layout, thermals — is in
[hardware/led-driver.md](hardware/led-driver.md).

> **The pin map is load-bearing.** The LED channels must sit on TCA0's WO0–WO2,
> the only waveform outputs that exist in 16-bit normal mode. Using WO3–WO5
> instead forces split mode and an 8-bit floor — 6.4× coarser than the PT4115 can
> resolve. Read [docs/architecture.md](docs/architecture.md) §8 before moving
> anything.

## Build & flash

Firmware is C++ on [megaTinyCore](https://github.com/SpenceKonde/megaTinyCore),
built with PlatformIO and flashed over UPDI with a serial adapter.
`utils/flash.sh` finds the adapter for you — no port to hardcode:

```bash
utils/flash.sh                    # build + flash
utils/flash.sh --fuses            # write clock/BOD/EESAVE fuses (once, fresh chip)
utils/flash.sh --debug --monitor  # flash a telemetry build, then watch it
utils/flash.sh --list             # which serial ports can I see?
utils/flash.sh --help             # all the flags
```

Port detection lives in `utils/find-port.sh` (it deliberately ignores the
Bluetooth serial ports that trip up naive auto-detect). Pass `--port` to
override, `--slow` if an upload is flaky.

## Layout

```
glim/
├── docs/architecture.md   ← the system: topology, RS-485, protocol, pin budgets
├── src/main.cpp           firmware (pre-pivot; rewrite pending)
├── include/config.h       pins + every tunable in one place
├── platformio.ini         build / upload config
├── utils/
│   ├── flash.sh            build/flash/monitor wrapper (start here)
│   └── find-port.sh        locate the USB-serial programmer
├── hardware/
│   ├── board.md            the driver node: MCU, pin map, power, subsystems
│   ├── led-driver.md       the LED channel: PT4115, boost options, CV strips
│   └── input.md            input circuits (partly superseded by the bus)
├── ROADMAP.md             where it goes next
└── deprecated/            rev1: firmware, docs, BOM, and what it taught us
```

## License

MIT — see [LICENSE](LICENSE).
