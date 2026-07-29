# Deprecated — rev1, and the pre-pivot design

Everything here is **retired**. It is kept because it worked, it was measured on
real hardware, and several of its findings still hold — not because it is a path
worth continuing down.

The live design is [`../hardware/board.md`](../hardware/board.md) and
[`../docs/architecture.md`](../docs/architecture.md).

---

## What rev1 was

A joystick-controlled 3-channel LED dimmer on an **ATtiny814** (14-pin SOIC),
hand-soldered. One analog thumb joystick drove everything: up/down ramped the
*selected* channel, left/right selected which channel, tap toggled it, hold
toggled all. Three PT4115 constant-current drivers, 16-bit PWM, an IR remote with
a learn mode, and a status LED.

It was finished, flashed and working.

## Why it is deprecated

Two decisions changed the shape of the product, and neither is a repair to rev1:

1. **One rotary encoder per channel**, instead of one joystick multiplexed across
   channels. Selecting a channel before adjusting it is a mode, and modes are
   what a screenless device should avoid. A knob per channel has no mode.
2. **Control panels distributed across a hall**, tens of metres from the drivers,
   on a proper bus. rev1's single-box, single-control assumption is exactly what
   that breaks.

Together these make the ATtiny814's 12 I/O untenable and the joystick UX
obsolete, so rev2 is a new board and new firmware rather than an increment.

## What is in here

| File | What it is |
|---|---|
| `firmware/` | The last working firmware — `main.cpp`, `config.h`, `platformio.ini`. Built for both ATtiny814 (rev1) and ATtiny3216 (rev2, pre-pivot). |
| `rev1-hardware.md` | rev1 pin map, PWM engine, power tree, serialUPDI programming |
| `rev1-controls.md` | The joystick UX: gestures, ramp feel, IR learn mode, tunables |
| `rev1-BOM.md` | rev1 parts list and build plan |
| `ATtiny214-414-814-DS40001912A.pdf` | ATtiny814 datasheet |

The firmware snapshot builds clean as committed: **5824 B / 8192 (rev1)**,
**5986 B / 32768 (rev2 target)**.

## Regenerating rev1 firmware, if you ever need it

The snapshot in `firmware/` is complete and self-contained. To resurrect it:

```bash
cp deprecated/firmware/main.cpp     src/
cp deprecated/firmware/config.h     include/
cp deprecated/firmware/platformio.ini .
utils/flash.sh --fuses    # once per fresh chip — clock/BOD/EESAVE
utils/flash.sh            # rev1 = ATtiny814, the default target
```

`GLIM_BOARD` in `config.h` picks the board (1 = ATtiny814, 2 = ATtiny3216);
`utils/flash.sh --rev2` sets it via `-DGLIM_BOARD=2`.

If you would rather have an AI rebuild it from scratch, the three things it must
be told — because each was discovered the hard way and none is obvious from the
code — are in the next section.

## Findings that survive the pivot

These were expensive to learn and remain true. Carry them forward.

1. **The LED channels must sit on TCA0's WO0/WO1/WO2.** Those are the only
   waveform outputs that exist in *normal* (16-bit) mode. Split mode offers six
   outputs but only 8 bits, and 8-bit is **6.4× coarser than the PT4115 can
   actually resolve**. See point 2.
2. **The PT4115's dimming floor is a ~2 µs minimum on-time, not a duty.** Its
   datasheet limits at 100 Hz (0.02 %) and 20 kHz (4 %) both reduce to 2 µs. So
   the floor in *counts* falls as PWM frequency drops, and resolution — not the
   driver — was the binding constraint all along. Express the floor as a time
   (`DRIVER_MIN_ON_NS`) and convert per prescaler.
3. **A floating DIM pin means full brightness.** The PT4115 pulls DIM up
   internally through 200 kΩ to a 5 V regulator, and the MCU's pins are high-Z
   from power-on until firmware runs. Without a **10 kΩ pulldown** per channel,
   every light blasts at 100 % on every power cycle, before any code executes.
4. **megaTinyCore's Arduino pin numbers are not port bit positions.** `PIN_PA0`
   is 11 while `PIN_PA3` is 10, so deriving a bitmask from a pin number silently
   produces nonsense. Spell out mask, `PINnCTRL` and vector separately.
5. **Ramp rates must integrate elapsed time, not loop iterations.** `millis()`
   dt is 0 on most passes, so a "minimum step of 1" makes the rate depend on how
   fast `loop()` spins. Carry enough fractional bits that a millisecond of slow
   ramp is representable.
6. **Never boot into darkness.** Restoring a saved all-off scene on a
   wall-switched fixture reads as broken — applying power is itself a request for
   light.

`rev1-hardware.md` has the full datasheet reasoning behind 1–3.
