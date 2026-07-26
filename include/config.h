// config.h — glim hardware map and behaviour tunables.
//
// Everything you'd want to adjust for a given build lives here. The pin
// choices are not arbitrary: on the ATtiny814 only PA3/PA4/PA5 can emit a
// TCA0 waveform (PWM), and only PA1/PA2 among the free pins can read the ADC.
// See docs/hardware.md for the datasheet reasoning.

#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
// Pin map (ATtiny814, 14-pin SOIC)
// ---------------------------------------------------------------------------

// LED channels → PT4115 PWM/DIM inputs. These MUST stay on PA3/PA4/PA5:
// they are TCA0 split-mode outputs WO3/WO4/WO5 (HCMP0/HCMP1/HCMP2).
#define LED1_PIN   PIN_PA3   // TCA0 WO3 / HCMP0
#define LED2_PIN   PIN_PA4   // TCA0 WO4 / HCMP1
#define LED3_PIN   PIN_PA5   // TCA0 WO5 / HCMP2

// Joystick. X/Y must be ADC-capable pins; PA1=AIN1, PA2=AIN2. Which axis lands
// on which pin is down to how the module is wired — swap these two if left/right
// and up/down come out transposed.
#define JOY_X_PIN  PIN_PA2   // ADC AIN2  (left/right → channel select)
#define JOY_Y_PIN  PIN_PA1   // ADC AIN1  (up/down    → brightness)
#define JOY_SW_PIN PIN_PA7   // digital, active-low with internal pull-up

// WS2812 status pixel — plain GPIO, any free pin. (PB0/PB1/PB2/PB3 remain free;
// the IR receiver goes on PB0.)
#define STATUS_PIXEL_PIN PIN_PA6

#define NUM_CHANNELS 3

// ---------------------------------------------------------------------------
// PWM
// ---------------------------------------------------------------------------

// TCA0 split clock divider. At F_CPU=20 MHz, DIV256 → 20e6/256/256 ≈ 305 Hz,
// which makes one duty count 12.8 µs — long enough for the PT4115 to actually
// reach regulation, so PWM_MIN_DUTY can sit at 1.
//
// The tradeoff here is arithmetic, not preference:
//     (on-time per count) × (PWM frequency) = 1/256,  always.
// Raising the frequency shortens the pulse the driver must reproduce, which
// *raises* the lowest honest duty; lowering it risks stroboscopic shimmer.
// F_CPU doesn't move that curve, it only changes which points you can land on.
// DIV64 here would give 1221 Hz / 3.2 µs.
#define PWM_CLKSEL TCA_SPLIT_CLKSEL_DIV256_gc

// Smallest non-zero duty (out of 255) → the dimmest lit step, here 1/256 =
// 0.39% (~8% perceived). At 305 Hz one count is 12.8 µs, which the PT4115
// should reach regulation within. If the bottom couple of levels flicker, drop
// out, or stop being monotonic, the driver isn't managing that pulse — raise
// this back toward 3.
#define PWM_MIN_DUTY 1

// Variable PWM frequency. All three channels share one timer, so there's one
// frequency at a time; it's chosen from the *brightest lit* channel. High when
// bright (kills stroboscopic flicker where the eye catches it), low when dim
// (long, clean pulses the driver reproduces reliably at the bottom, and flicker
// is barely visible when faint). Note: this does NOT lower the 0.39% floor —
// that's the 8-bit resolution limit — it only makes the lowest steps clean and
// cuts flicker up top. Set to 0 to hold PWM_CLKSEL fixed.
#define GLIM_VARIABLE_PWM_FREQ 1

// The three tiers (at 20 MHz: DIV64≈1221 Hz, DIV256≈305 Hz, DIV1024≈76 Hz) and
// the brightest-channel logical level (0..255) at which we step between them.
// HYST is the hysteresis band that stops it hunting at a boundary.
#define PWM_FREQ_HI_CLKSEL  TCA_SPLIT_CLKSEL_DIV64_gc     // bright
#define PWM_FREQ_MID_CLKSEL TCA_SPLIT_CLKSEL_DIV256_gc    // mid  (== PWM_CLKSEL)
#define PWM_FREQ_LO_CLKSEL  TCA_SPLIT_CLKSEL_DIV1024_gc   // deep dim
#define PWM_FREQ_HI_LEVEL   128    // brightest channel above this → HI tier
#define PWM_FREQ_LO_LEVEL   24     // brightest channel below this → LO tier
#define PWM_FREQ_HYST       8

// ---------------------------------------------------------------------------
// Brightness model
// ---------------------------------------------------------------------------

// Logical brightness is 0..255 per channel and is gamma-corrected to PWM duty
// so the joystick feels linear to the eye. DEFAULT_LEVEL is what an untouched
// channel comes up at on first-ever boot, and what a tap gives a channel that
// has never been set.
#define DEFAULT_LEVEL 110

// Time (ms) to sweep a channel across its full range at full stick deflection,
// measured at the *top* of the range. Smaller = snappier, larger = more gentle.
// Partial deflection is proportional, so a gentle push is much slower than this.
#define RAMP_FULL_MS 3125

// Extra fine control when dim. The ramp runs this many times slower at the
// bottom of the range than at the top, easing smoothly in between — so you can
// trim a night-light by tiny amounts without the top end feeling glued. Set to
// 1 to disable and get a constant rate.
//
// With the defaults, a full 0→100% sweep at full push takes roughly 6 s; the
// bottom of the range moves gently, the top moves briskly. If the whole thing
// feels wrong, drop/raise RAMP_FULL_MS first.
#define RAMP_LOW_FACTOR 4

// Soft transitions: on/off toggles, all-off/all-on, and the boot restore glide
// over this many ms instead of snapping.
//
// This is a slew-rate limit, not a filter, so as long as it's comfortably faster
// than the joystick ramp (~82 levels/s ≈ 21 disp-units/ms at full push, vs ~87
// here) the display still tracks live dimming exactly — the fade only shows on
// step changes. Don't push it much past ~1000 ms or the ramp will start to lag.
//
// Being a *rate*, a toggle to a dim level lands proportionally sooner than one
// to full — which is what you want for a toggle (it stays responsive), but not
// for the power-up fade. That one gets its own fixed duration below.
#define FADE_MS 750

// Power-up fade-in, as a fixed duration: the boot fade takes this long whether
// it's rising to 10% or 100%. That's what makes it read as a deliberate "waking
// up" rather than a flick — at a plain rate limit, restoring a dim scene would
// be over in a few tens of ms and look like a snap.
#define BOOT_FADE_MS 1200

// Temporal dithering: buys brightness resolution below the 8-bit PWM floor by
// nudging duty ±1 across frames (first-order sigma-delta), so deep dimming
// stops stair-stepping. DITHER_BITS extra bits, 0 disables. The dither pattern
// repeats at PWM_freq / 2^DITHER_BITS, so keep it high enough to stay invisible:
// at 305 Hz, 1 bit → 152 Hz (safe), 2 → 76 Hz, 3 → 38 Hz (would flicker).
// Dropping to 1 bit is the price of the lower PWM frequency — dither can only
// smooth the range *above* the floor, it can't lower the floor itself.
#define DITHER_BITS 1

// ---------------------------------------------------------------------------
// Status pixel (WS2812)
// ---------------------------------------------------------------------------

// A single addressable LED showing which channel the joystick is steering —
// the one thing the per-channel indicator LEDs can't tell you, since they mirror
// level. When every channel is off it drops to a dim glow, so the stick is still
// findable in a dark room.
#define GLIM_STATUS_PIXEL 1

// Colour per channel, 0xRRGGBB.
#define STATUS_COLOR_CH1 0xFF2000   // amber
#define STATUS_COLOR_CH2 0x00FF30   // green
#define STATUS_COLOR_CH3 0x0040FF   // blue

// Pixel brightness (0..255): normal, and when all channels are off.
#define STATUS_BRIGHT      40
#define STATUS_BRIGHT_IDLE 4

// ---------------------------------------------------------------------------
// Joystick feel
// ---------------------------------------------------------------------------

// Readings are 10-bit (0..1023); centre is auto-measured at boot. These are
// deflections away from that measured centre.
#define JOY_DEADZONE   110   // ignore small wobble around centre
#define JOY_X_THRESH   320   // deflection that commits a channel change
#define JOY_X_REARM    90    // must fall back inside this before the next change

// If an axis feels backwards on your build, flip the matching invert. "Up =
// brighter" and "right = next channel" are the intended directions.
#define JOY_Y_INVERT   0
#define JOY_X_INVERT   0

// Switch timing.
#define SW_DEBOUNCE_MS   25
#define SW_LONGPRESS_MS  700   // hold this long → all channels off

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

// Levels/mute/selection are saved to EEPROM this long after the last change,
// so a wall-switch power cycle restores the previous scene without hammering
// the EEPROM on every tick.
#define EEPROM_SAVE_DELAY_MS 3000

// ---------------------------------------------------------------------------
// Reliability
// ---------------------------------------------------------------------------

// Watchdog: auto-reset if loop() ever wedges. It's an installed, unattended
// light — cheap insurance. Timeout is ~2 s; loop() and the boot sequence both
// finish well inside that.
#define GLIM_WATCHDOG 1

// Factory reset: power on with the joystick button held. All channels swell up
// together as you hold; once they hit full and flash, saved state is wiped back
// to defaults. Release before then to cancel. FACTORY_HOLD_MS is the hold time.
#define GLIM_FACTORY_RESET 1
#define FACTORY_HOLD_MS 2000

// ---------------------------------------------------------------------------
// Debug
// ---------------------------------------------------------------------------

// Set to 1 to stream joystick/level telemetry on USART0 (PB2=TX, PB3=RX) at
// 115200. Handy for calibration; leave at 0 for production.
//
// Guarded so it can be overridden at build time without editing this file —
// `utils/flash.sh --debug` does exactly that.
#ifndef GLIM_DEBUG
#define GLIM_DEBUG 0
#endif
