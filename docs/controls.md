# Controls

glim is meant to be operable in the dark, by feel, by someone who has never seen
the code. One joystick, four gestures.

## The gestures

| Gesture | Effect |
|---------|--------|
| **Push up / down** | Ramp the *selected* channel brighter / dimmer. Push a little for fine control, push all the way for a fast sweep. |
| **Flick left / right** | Move to the previous / next channel (wraps around all 3). The channel you land on **blinks once** so you can see which light you're now controlling. |
| **Tap the stick (SW)** | Toggle **this** channel on / off. Turning it off remembers the level; turning a never-used channel on gives it a default brightness. |
| **Hold the stick (SW)** | Toggle **all** channels on / off. Anything lit → everything off ("goodnight"); nothing lit → everything back on at its remembered level. |

At power-on the selected channel blinks once, so you always know where you are.

**Maintenance gesture — factory reset:** power on with the button held. All three
channels swell up together as you keep holding; once they reach full and flash,
saved brightness/selection is wiped back to defaults. Let go before the flash to
cancel and boot normally.

## The remote

glim accepts any **NEC-protocol** IR remote — the near-universal cheap kind,
including a 44-key LED-strip remote or a spare TV remote. Nothing is hardcoded to
a particular model: you teach it which buttons you want.

### Teaching it (learn mode)

**Hold the joystick button for about 3 seconds.** You'll feel the all-off toggle
fire at ~0.7 s on the way — keep holding; glim undoes it when learn mode starts,
so the room ends up as it was.

The lights go out and the status pixel starts blinking. It walks six actions in
order, blinking **n+1 times** to say which one it's asking for:

| Blinks | Press the remote button you want for… |
|---|---|
| 1 | brighter |
| 2 | dimmer |
| 3 | next channel |
| 4 | previous channel |
| 5 | toggle this channel |
| 6 | toggle all channels |

Green flash = accepted. Red flash = you pressed a button already used for
something else; try a different one. Stop pressing for 10 seconds and it saves
whatever you've bound so far and exits — so binding just the first two is fine if
that's all you want. Three blue flashes mean it's saved.

Bindings live in EEPROM and survive power cycles and reflashing. Run learn mode
again any time to rebind; a factory reset clears them.

### Using it

The remote drives exactly the same actions as the stick — **hold** brighter or
dimmer to ramp (it tracks the remote's repeat frames, so it ramps for as long as
you hold), and the channel/toggle buttons are single-shot. The status pixel and
acknowledge-blink behave identically, so it's obvious which channel you're
steering from across the room.

> With no receiver fitted, the input pin idles high on its pull-up and nothing
> ever decodes — leaving `GLIM_IR` enabled costs nothing.

## How it feels, and why

- **Two toggles, same shape.** Tap = this light, hold = all lights. Both are
  toggles, both remember levels — there's no "off" gesture that lacks an "on".
- **Proportional ramp.** Brightness speed follows how far you push the stick, so
  you get both fine trimming (nudge) and quick changes (full push) from one axis
  without menus or modes.
- **Slower where it matters.** The ramp eases down toward the bottom of the range
  (4× slower at 0 than at full), because picking a *dim* level needs finer
  control than picking a bright one. At full push that's ~20 levels/sec at the
  bottom and ~80/sec at the top; a full sweep takes ~6 s.
- **Perceptually linear.** Duty is gamma-corrected (square law), so the ramp
  looks even top-to-bottom instead of doing everything in the last 10%.
- **Edge-triggered selection.** A flick changes the channel *once*; you have to
  let the stick return most of the way to centre before it will change again. No
  runaway scrolling.
- **The blink is the display.** With no screen, the acknowledge-blink is how the
  device tells you which channel it thinks you mean. Only the selected channel
  blinks; the others hold steady.
- **The pixel is the other half.** A WS2812 by the joystick holds the selected
  channel's colour (amber / green / blue), so "which light am I steering?" is
  answerable at a glance, not just at the moment you flick. When every channel is
  off it fades to a dim glow — a locator in a dark room.
- **It remembers.** State is written to EEPROM a few seconds after you stop, and
  restored on boot — so a wall switch (or a power blip) brings the room back the
  way you left it, not black or blazing.
- **It never boots dark.** Giving glim power is itself a request for light, so if
  the saved scene was entirely off, it comes up lit at its remembered levels
  rather than restoring the darkness. Per-channel choices still survive; only a
  wholly-dark scene is overridden. (Otherwise flipping the wall switch on and
  getting nothing just looks broken.)
- **Nothing snaps.** On/off toggles, all-off, and the boot restore all *fade*
  over a couple hundred ms rather than jumping. The fade is fast enough that it
  never lags the live joystick ramp.
- **Smooth at the bottom.** The 8-bit PWM is temporally dithered (sigma-delta),
  buying extra effective resolution so deep dimming glides instead of
  stair-stepping through visible steps.

## Tuning

Everything lives in [`include/config.h`](../include/config.h). The knobs you're
most likely to touch:

| Setting | What it does |
|---------|--------------|
| `JOY_Y_INVERT`, `JOY_X_INVERT` | Flip an axis if up-is-dimmer or right-is-wrong on your build. Cheap joysticks vary in orientation — expect to set at least one of these. |
| `RAMP_FULL_MS` | Ramp speed at the top of the range. **Lower = snappier** — this is the first knob to touch if the ramp feels wrong. |
| `RAMP_LOW_FACTOR` | How much slower the ramp is when dim vs. bright (4 = 4× slower at the bottom). 1 gives a constant rate. |
| `JOY_DEADZONE` | How far you can wobble around centre before anything happens. Raise it if a channel drifts on its own. |
| `JOY_X_THRESH` / `JOY_X_REARM` | How hard you must flick to change channel, and how far back to centre before the next flick counts. |
| `DEFAULT_LEVEL` | Brightness a channel comes up at on first boot / first tap-on. |
| `PWM_MIN_DUTY` | The dimmest lit step. Raise it if low settings flicker or drop out on the PT4115. |
| `FADE_MS` | Fade time for on/off toggles and the boot restore. Lower = snappier. |
| `DITHER_BITS` | Extra dimming resolution (0 disables). Higher is smoother but the dither pattern slows — keep ≤3 at ~1 kHz PWM to stay flicker-free. |
| `SW_LONGPRESS_MS` | How long "hold" is before it means all-off. |
| `FACTORY_HOLD_MS` | How long the button must be held *at power-on* to wipe to defaults. |

### Calibration note

The stick's centre is measured automatically at power-on, so **leave the joystick
released while it boots**. If your resting readings drift, set `GLIM_DEBUG 1` in
`config.h`, reflash, and open `pio device monitor` (115200) to watch live X/Y
values while you find good deadzone/threshold numbers.
