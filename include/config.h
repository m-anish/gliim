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

// LED channels → PT4115 PWM/DIM inputs. These MUST stay on PB0/PB1/PB2: they
// are TCA0's WO0/WO1/WO2, the only outputs that exist in *normal* (16-bit) mode.
// Moving them back to PA3/PA4/PA5 would force split mode and cost 8 bits of
// dimming resolution — see docs/hardware.md before touching this.
#define LED1_PIN   PIN_PB0   // TCA0 WO0 / CMP0
#define LED2_PIN   PIN_PB1   // TCA0 WO1 / CMP1
#define LED3_PIN   PIN_PB2   // TCA0 WO2 / CMP2

// IR receiver (TSOP38238) demodulated output, active-low, idles high.
//
// On PB3 rather than a spare PORTA pin so its edge interrupt lands on
// PORTB_PORT_vect, leaving PORTA_PORT_vect free for SoftwareSerial's receiver in
// debug builds — two ISRs can't share a vector. PB3 is interrupt-capable
// (synchronous only, which is fine: glim never sleeps while decoding).
//
// The four defines must agree. megaTinyCore's Arduino pin numbers are NOT port
// bit positions (PIN_PA0 is 11, PIN_PA3 is 10), so none of these can be derived
// from IR_PIN — spell them out.
#define IR_PIN       PIN_PB3
#define IR_PORT      PORTB
#define IR_PIN_bm    PIN3_bm         // PB3's bit within PORTB
#define IR_PINCTRL   PORTB.PIN3CTRL
#define IR_PORT_vect PORTB_PORT_vect

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

// TCA0 runs in NORMAL mode as one 16-bit single-slope PWM with three compare
// channels. PER = 65535 gives the full 16 bits of resolution:
//     f_PWM = F_CPU / (N × (PER+1))     → 20 MHz / 65536 = 305 Hz at DIV1
#define PWM_PER 65535

// The PT4115's real floor is a *minimum on-time*, not a duty: its datasheet
// dimming limits at 100 Hz (0.02%) and 20 kHz (4%) both reduce to ~2 µs. Below
// that pulse width the driver stops regulating and output goes non-monotonic.
//
// Expressed as time, the floor in *counts* falls automatically as the PWM
// frequency drops — which is why the low-frequency tier dims genuinely deeper:
//     min_counts = DRIVER_MIN_ON_NS × F_CPU / (1e9 × prescaler)
//     at 20 MHz → 40 counts at DIV1, 20 at DIV2, 10 at DIV4
// Raise this if the bottom couple of levels flicker or stop being monotonic.
#define DRIVER_MIN_ON_NS 2000

// Variable PWM frequency. All three channels share one timer, so there is one
// frequency at a time; it follows the *brightest lit* channel. High when bright
// (stroboscopic flicker is visible there), low when dim (flicker barely reads
// when faint, and the min-on-time floor shrinks in counts, so dim gets deeper).
// Set to 0 to hold PWM_CLKSEL_MID fixed.
#define GLIM_VARIABLE_PWM_FREQ 1

// At 20 MHz with PER=65535, DIV1 gives 305 Hz — and that is the *ceiling*, since
// full 16-bit resolution needs the whole period. So unlike the old 8-bit setup
// (which could run 1221 Hz), the tiers only ever step downward from 305 Hz:
//
//   DIV1  305 Hz   floor 40 counts (0.061%, ~3.5% perceived)  1638:1
//   DIV2  152 Hz   floor 20 counts (0.031%, ~2.5% perceived)  3277:1
//   DIV4   76 Hz   floor 10 counts (0.015%, ~1.8% perceived)  6554:1
//
// 305 Hz is therefore the default for bright *and* mid — the top two tiers
// coincide, and that's deliberate: it's the frequency the board has run happily
// at, and even there 16-bit already beats the old 8-bit floor by 6.4×. Only the
// deep-dim tier trades frequency away, where flicker is least visible.
// Set PWM_CLKSEL_LO to DIV4 if you want the last 2× of depth and can live with
// 76 Hz; that's the deepest this hardware goes.
#define PWM_CLKSEL_HI   TCA_SINGLE_CLKSEL_DIV1_gc    // bright   — 305 Hz
#define PWM_CLKSEL_MID  TCA_SINGLE_CLKSEL_DIV1_gc    // mid      — 305 Hz
#define PWM_CLKSEL_LO   TCA_SINGLE_CLKSEL_DIV2_gc    // deep dim — 152 Hz
#define PWM_PRESCALE_HI  1
#define PWM_PRESCALE_MID 1
#define PWM_PRESCALE_LO  2
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

// (Temporal dithering was removed with the move to 16-bit PWM. It existed to
// fake ~10 bits out of an 8-bit timer; the hardware now gives 16, which is far
// past what dithering bought and costs no flicker margin.)

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
// IR remote (NEC protocol, 38 kHz)
// ---------------------------------------------------------------------------

// Safe to leave enabled with no receiver fitted: the pin idles high on its
// internal pull-up, so no edges arrive and nothing ever decodes.
#define GLIM_IR 1

// Six actions are learnable, in this order. Learn mode walks them one at a time.
#define IR_ACT_UP     0   // brighter (hold to keep ramping)
#define IR_ACT_DOWN   1   // dimmer   (hold to keep ramping)
#define IR_ACT_NEXT   2   // next channel
#define IR_ACT_PREV   3   // previous channel
#define IR_ACT_TOGGLE 4   // toggle the selected channel
#define IR_ACT_ALL    5   // toggle every channel
#define IR_ACT_COUNT  6

// Hold the joystick button this long to enter learn mode. It is deliberately
// past SW_LONGPRESS_MS: you feel the all-toggle fire at 700 ms, and if you keep
// holding you land in learn mode (which undoes that toggle).
#define IR_LEARN_HOLD_MS 3000

// Give up on a learn step after this long with no remote press, and save
// whatever has been bound so far.
#define IR_LEARN_TIMEOUT_MS 10000

// A held remote button repeats every ~108 ms. If this long passes with no IR
// activity, treat the button as released and stop ramping.
#define IR_HOLD_MS 200

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

// Set to 1 to stream joystick/level telemetry at 115200. Handy for joystick
// calibration; leave at 0 for production.
//
// Guarded so it can be overridden at build time without editing this file —
// `utils/flash.sh --debug` does exactly that.
#ifndef GLIM_DEBUG
#define GLIM_DEBUG 0
#endif

// Debug output can't use the hardware USART any more: USART0's TXD is PB2, which
// is now LED channel 3, and its only alternate (PA1) is the joystick. So debug
// builds bit-bang over SoftwareSerial on the pins freed by moving the LEDs off
// PORTA. Wire the USB-serial adapter's **RX to PA4** — note this is a different
// pin from the UPDI node.
#define DEBUG_TX_PIN PIN_PA4
#define DEBUG_RX_PIN PIN_PA5   // unused in practice; SoftwareSerial wants a pin

// SoftwareSerial routes through the core's attachInterrupt dispatcher, which
// defines every PORT interrupt vector — so it cannot coexist with the IR
// decoder's own raw ISR, on any port. They're mutually exclusive by nature, and
// that's fine: debug builds exist for joystick calibration, where you don't need
// the remote. Production builds (GLIM_DEBUG=0) keep IR.
#if GLIM_DEBUG && GLIM_IR
#undef GLIM_IR
#define GLIM_IR 0
#endif
