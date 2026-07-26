# glim

![status](https://img.shields.io/badge/status-firmware%20v1-6e5494)
![mcu](https://img.shields.io/badge/MCU-ATtiny814-323330)
![driver](https://img.shields.io/badge/LED%20driver-PT4115-fbb034)
![core](https://img.shields.io/badge/core-megaTinyCore-00979d)
![license](https://img.shields.io/badge/license-MIT-blue)

> A *glim* is an old word for a small light — a candle, a lantern, the thing you
> carry into a dark room. This one is the size of a thumbnail and takes its
> orders from a joystick.

A tiny hand-controlled LED dimmer. One cheap thumb joystick, three channels of
warm dimmable light, and an ATtiny814 in the middle deciding how bright the room
should be. Built to put good, easily-adjustable lighting in a friend's house —
no app, no wall of switches, just push up for brighter and flick sideways to
pick a light.

The unshowy little sibling of [lokki](https://github.com/m-anish/lokki), which
does the same job at campus scale. Part of the
[starstucklab](https://github.com/m-anish/starstucklab) family.

---

## What it does

- Drives **3 independent LED channels** through PT4115 constant-current drivers,
  dimmed by hardware PWM (~305 Hz, tuned for how low the drivers can honestly go).
- **One joystick** does everything:
  - **up / down** — the selected channel gets brighter / dimmer, at a speed that
    follows how far you push.
  - **left / right** — pick which channel you're steering. The one you land on
    blinks once so you know it heard you.
  - **tap the stick** — toggle that channel on / off (it remembers its level).
  - **hold the stick** — everything off. Goodnight.
- **Remembers the room.** Levels are saved a few seconds after you stop fiddling,
  so flipping the wall switch brings the lights back exactly as you left them.
- **Feels linear.** Brightness is gamma-corrected, so equal joystick travel is
  equal-looking change instead of "nothing… nothing… BLINDING."

## Hardware

| Part | Role |
|------|------|
| ATtiny814 | brains — 3× PWM, 2× ADC for the joystick, 1 button input |
| 3× PT4115 | buck LED drivers, one per channel (up to ~5 LEDs each) |
| Joystick module | cheap 5-pin analog thumbstick (GND/5V/X/Y/SW) |
| Buck module | steps the 6–30 V supply down to 5 V for the logic |
| PSU | 19 V / 3 A here, but anything 6–30 V works |

The DC supply feeds all three drivers directly; a small buck converter taps off
it for the 5 V logic rail. Full wiring, the power tree, and **the reason the pin
map looks the way it does** are in [docs/hardware.md](docs/hardware.md); the
parts list (core + indicator LEDs + IR remote) is in
[hardware/BOM.md](hardware/BOM.md).

> **Note on the pin map:** the LEDs live on PA3/PA4/PA5 and the joystick on
> PA1/PA2/PA7 — not the other way around. On the ATtiny814 only PA3/PA4/PA5 can
> emit PWM, and PA1/PA2 are the ADC pins; the roles are forced by the silicon.
> See [docs/hardware.md](docs/hardware.md) if you're wiring a board.

## Controls

See [docs/controls.md](docs/controls.md) for the full feel — deadzones, ramp
speed, tap vs. hold, and everything you can tune.

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
override, `--slow` if an upload is flaky. Programmer wiring is in
[docs/hardware.md](docs/hardware.md).

## Layout

```
glim/
├── src/main.cpp        firmware
├── include/config.h    pins + every tunable in one place
├── platformio.ini      build / upload config
├── utils/
│   ├── flash.sh         build/flash/monitor wrapper (start here)
│   └── find-port.sh     locate the USB-serial programmer
├── hardware/
│   ├── BOM.md           rev1 parts list + build plan
│   └── rev2/            next board: spec, PT4115 circuit, input circuits
├── ROADMAP.md          where it goes next
└── docs/
    ├── hardware.md      wiring, power, pin-map rationale, programmer
    └── controls.md      the joystick UX and how to tune it
```

## Status

Firmware v1: barebones but complete — 3 channels, full joystick control,
persistence. Hardware is a hand-soldered board. The whole design is written to
survive a move: pins and behaviour are all in `config.h`.

**rev2 is specified** — ATtiny1616, 16-bit PWM (~6× deeper dimming), USB-C PD
power, on-board IR remote and status pixel:
[`hardware/rev2/`](hardware/rev2/README.md).

Where it goes next — indicator LEDs, IR remote, up to 6 channels on the same
chip, and the line where it hands off to lokki — is in
[ROADMAP.md](ROADMAP.md).

## License

MIT — see [LICENSE](LICENSE).
