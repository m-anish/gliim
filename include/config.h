// config.h — glim hardware map and behaviour tunables.
//
// Everything you'd want to adjust for a given build lives here, for both board
// revisions. The pin choices are not arbitrary — they are forced by which pins
// can emit a TCA0 waveform and which can reach the ADC. See docs/hardware.md
// for the datasheet reasoning before moving any of them.

#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
// Board select
// ---------------------------------------------------------------------------
//
//   1 = rev1 — ATtiny814, 14-pin SOIC, hand-soldered board
//   2 = rev2 — ATtiny3216, 20-pin SOIC, PCB (see hardware/rev2/)
//
// Set by the build environment (`-DGLIM_BOARD=2`), so `pio run -e rev2` picks
// rev2 without editing this file. Everything below the pin map is shared.

#ifndef GLIM_BOARD
#define GLIM_BOARD 1
#endif

// ---------------------------------------------------------------------------
// Pin map
// ---------------------------------------------------------------------------
//
// Common to both boards: the LED channels are driven by TCA0's WO0/WO1/WO2 — the
// only waveform outputs that exist in *normal* (16-bit) mode. Split mode would
// offer six outputs but only 8 bits, which is 6.4× coarser than the PT4115 can
// resolve. See docs/hardware.md.
//
// *Which* pins those outputs land on differs: rev1 uses the default PB0/PB1/PB2,
// rev2 the alternates PB3/PB4/PB5 so that I²C and the UART can have PB0-PB2.
//
// Also common: PA1/PA2 are the ADC pins (AIN1/AIN2) and cannot do PWM, so the
// joystick lives there on both boards.

#if GLIM_BOARD == 1
// ── rev1: ATtiny814, 14-pin ────────────────────────────────────────────────
#define LED1_PIN   PIN_PB0   // TCA0 WO0 / CMP0
#define LED2_PIN   PIN_PB1   // TCA0 WO1 / CMP1
#define LED3_PIN   PIN_PB2   // TCA0 WO2 / CMP2
#define LED_DIR_bm (PIN0_bm | PIN1_bm | PIN2_bm)   // PORTB pins the timer drives
#define PWM_PORTMUX 0                              // all three at default position

// IR on PB3 so its edge interrupt lands on PORTB_PORT_vect. PB3 is
// interrupt-capable (synchronous only — fine, glim never sleeps while decoding).
#define IR_PIN       PIN_PB3
#define IR_PORT      PORTB
#define IR_PIN_bm    PIN3_bm
#define IR_PINCTRL   PORTB.PIN3CTRL
#define IR_PORT_vect PORTB_PORT_vect

// As built, the joystick's X and Y landed transposed vs. the schematic — swap
// these two if left/right and up/down come out the wrong way round.
#define JOY_X_PIN  PIN_PA2   // AIN2  (left/right → channel select)
#define JOY_Y_PIN  PIN_PA1   // AIN1  (up/down    → brightness)
#define JOY_SW_PIN PIN_PA7

// rev1 has a WS2812 physically fitted; it is now driven as a plain "system on"
// lamp in a single colour, the same meaning as rev2's discrete LED.
#define STATUS_PIXEL_PIN PIN_PA6
#define GLIM_STATUS_LED  0

// No hardware UART here: PB2 is USART0's TXD but it's LED ch3, and USART0's only
// alternate (PA1) is the joystick. Debug bit-bangs over SoftwareSerial instead.
#define GLIM_DEBUG_HW_SERIAL 0
#define GLIM_I2C 0                 // no free pins for TWI0 on the 14-pin part
#define DEBUG_TX_PIN PIN_PA4
#define DEBUG_RX_PIN PIN_PA5

#elif GLIM_BOARD == 2
// ── rev2: ATtiny3216, 20-pin ───────────────────────────────────────────────
// All three LED channels sit on TCA0's **alternate** output pins, PB3/PB4/PB5,
// rather than the default PB0/PB1/PB2. That is a deliberate reshuffle to fit
// two peripherals the 14-pin rev1 could never have:
//
//   PB0/PB1 → TWI0 SCL/SDA — the ONLY pins I²C can reach without taking
//             PA1/PA2 from the joystick (datasheet Table 5-1). This is what
//             makes the Qwiic connector possible.
//   PB2     → USART0 TXD — a real hardware UART for debug telemetry, so
//             SoftwareSerial (whose dispatcher collides with the IR ISR) is
//             never needed and IR stays live in debug builds.
//
// The relocation is three PORTMUX bits. DS40002205A §15.3.3 restricts only
// TCA03/04/05 to split mode; TCA00/01/02 work in the 16-bit normal mode we use.
//
// Price paid: USART0's RXD is PB3, now LED ch1 — so debug serial is
// **transmit-only**. Fine for telemetry; main.cpp clears RXEN so the receiver
// can't sit interrupting on the PWM waveform.
#define LED1_PIN   PIN_PB3   // TCA0 WO0 / CMP0  (alternate position)
#define LED2_PIN   PIN_PB4   // TCA0 WO1 / CMP1  (alternate position)
#define LED3_PIN   PIN_PB5   // TCA0 WO2 / CMP2  (alternate position)
#define LED_DIR_bm (PIN3_bm | PIN4_bm | PIN5_bm)
#define PWM_PORTMUX (PORTMUX_TCA00_bm | PORTMUX_TCA01_bm | PORTMUX_TCA02_bm)

// I²C / Qwiic. TWI0 at its default pins, free because the LEDs moved.
//   PB0 = SCL, PB1 = SDA
// **Qwiic is a 3.3 V standard and this board runs at 5 V** — the connector needs
// a 3.3 V LDO and a level shifter, not a direct connection. See
// hardware/rev2/README.md §5 before wiring one.
#define GLIM_I2C 1

// IR on PC0 — PORTC is the space the 20-pin part adds.
#define IR_PIN       PIN_PC0
#define IR_PORT      PORTC
#define IR_PIN_bm    PIN0_bm
#define IR_PINCTRL   PORTC.PIN0CTRL
#define IR_PORT_vect PORTC_PORT_vect

// New board, so the schematic defines the axes rather than the other way round.
#define JOY_X_PIN  PIN_PA1   // AIN1  (left/right → channel select, or ladder node)
#define JOY_Y_PIN  PIN_PA2   // AIN2  (up/down    → brightness; unused with the ladder)
#define JOY_SW_PIN PIN_PA7

// Status indicator: **one plain LED, meaning "system on"**. Nothing more.
//
// It does not encode the selected channel, because the channels can say that
// themselves and say it better: selecting one makes *that light* blink
// (`ackBlink`), and a small indicator LED on each driver's DIM line mirrors that
// channel's brightness for free — no pins, since it just hangs off the PWM
// output. See hardware/rev2/README.md §7.
//
// One GPIO, one resistor, no library, no bit-bang, any supply rail.
#define GLIM_STATUS_LED       1
#define STATUS_LED_PIN        PIN_PC1
#define STATUS_LED_ACTIVE_LOW 0     // 1 if you wire it to VDD instead of GND

// Hardware USART0, TX only (see above). Wire the adapter's RX to PB2.
#define GLIM_DEBUG_HW_SERIAL 1

// PA3/PA4/PA5 are TCA0 WO3/WO4/WO5 — LED channels 4-6 if you populate the
// expansion drivers, but only in split mode, which costs the 16-bit resolution.
// Genuinely free: PA6 (DAC-capable) and PC3.

#else
#error "GLIM_BOARD must be 1 (rev1 / ATtiny814) or 2 (rev2 / ATtiny3216)"
#endif

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
// One status concept, two renderings: rev1 lights its fitted WS2812, rev2 a
// plain LED. Both mean the same thing — the system is powered and running.
#if GLIM_STATUS_LED
#define GLIM_STATUS_PIXEL 0
#else
#define GLIM_STATUS_PIXEL 1
#endif

// Colour per channel, 0xRRGGBB.
// WS2812 builds (rev1): the colour and brightness of the "system on" lamp.
#define STATUS_COLOR_ON  0x20180A   // warm, dim — it's an indicator, not a light

// Plain-LED builds (rev2): brightness is software PWM, expressed as "lit for
// 1 ms in every N". N=2 is 50 % at 500 Hz, N=8 is 12.5 % at 125 Hz — both far
// above flicker fusion for a small indicator, and no timer is consumed.
// Raise N if the LED is distracting at night.
#define STATUS_LED_DIV 8

// Set to 1 for a slow pulse instead of a steady lamp. Steady says "powered";
// pulsing says "firmware is running", which distinguishes a live board from one
// the watchdog is resetting. Handy on the bench, busier in a bedroom.
#define GLIM_STATUS_HEARTBEAT 0
#define STATUS_HEARTBEAT_MS 2000


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

// Debug output can't use the hardware USART on either board (see the pin map):
// it bit-bangs over SoftwareSerial on DEBUG_TX_PIN. Wire the USB-serial
// adapter's RX to that pin — a different pin from the UPDI node.

// SoftwareSerial routes through the core's attachInterrupt dispatcher, which
// defines every PORT interrupt vector — so it cannot coexist with the IR
// decoder's own raw ISR, on any port. That forces the two apart on **rev1**,
// which is tolerable: debug builds exist for joystick calibration, where the
// remote isn't needed.
//
// **rev2 has no such conflict** — it uses the real USART0, so IR stays enabled
// even in debug builds.
#if GLIM_DEBUG && !GLIM_DEBUG_HW_SERIAL && GLIM_IR
#undef GLIM_IR
#define GLIM_IR 0
#endif
