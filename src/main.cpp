// glim — a joystick-controlled 3-channel LED dimmer on an ATtiny814.
//
//   up / down     ramp the selected channel brighter / dimmer (speed follows
//                 how far you push)
//   left / right  select which of the 3 channels you're controlling; the newly
//                 selected channel blinks once so you know which light answered
//   tap switch    toggle the selected channel on / off (fades)
//   hold switch   all channels off (fades)
//
// Signal flow per channel:
//   setpoint level  → slewed display level (soft transitions)
//                   → gamma curve (perceptually linear)
//                   → high-res duty → dithering ISR (sub-LSB resolution)
//                   → TCA0 split-mode PWM on PA3/PA4/PA5.
//
// The PWM is driven straight from TCA0 rather than analogWrite(), because the
// LED pins (PA3/PA4/PA5 = WO3/WO4/WO5) only exist as timer outputs in split
// mode. millis() lives on TCD0, so TCA0 is ours to take over.

#include <Arduino.h>
#include <EEPROM.h>
#include <avr/wdt.h>
#include <util/atomic.h>
#include "config.h"

#if GLIM_STATUS_PIXEL
#include <tinyNeoPixel_Static.h>
#endif

#define DITHER_STEPS (1u << DITHER_BITS)          // sub-frames per dither cycle
#define HR_MAX       ((uint16_t)255 << DITHER_BITS) // full-scale high-res duty
#define FADE_SLEW    (((int32_t)255 << 8) / FADE_MS) // display slew, level<<8 per ms

// The ramp accumulator carries 16 fractional bits. That much headroom matters:
// at slow rates a whole millisecond's worth of ramp is a tiny fraction of one
// brightness level, and anything coarser would truncate it to nothing (or, if
// you paper over that with a minimum step, make the rate depend on how fast
// loop() happens to spin rather than on elapsed time).
#define LVL_SHIFT 16
#define LVL_MAX   ((int32_t)255 << LVL_SHIFT)
#define RATE_FULL (LVL_MAX / RAMP_FULL_MS)        // accumulator units per ms, full push
#define JOY_SPAN  (512 - JOY_DEADZONE)            // usable deflection past the deadzone

// Don't let a blocking stretch (ack blink, EEPROM write) integrate as one big
// jump when the loop resumes.
#define DT_CLAMP_MS 50

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

static uint8_t  level[NUM_CHANNELS];    // setpoint brightness 0..255 (0 = dark)
static bool     muted[NUM_CHANNELS];    // toggled off, but remembers its level
static int32_t  levelAcc[NUM_CHANNELS]; // ramp accumulator, level << LVL_SHIFT
static int32_t  disp[NUM_CHANNELS];     // slewed display level, level << 8
static uint8_t  selected = 0;           // channel the joystick is steering

// Shared with the dither ISR — high-res duty target per channel (0..HR_MAX).
static volatile uint16_t dutyHR[NUM_CHANNELS];

static int16_t  centreX = 512;          // joystick centres, auto-measured at boot
static int16_t  centreY = 512;

static uint32_t lastTick = 0;
static bool     dirty = false;          // state changed since last EEPROM save
static uint32_t dirtyAt = 0;

// EEPROM layout: magic + version so future firmware can migrate rather than
// wipe the saved scene.
#define EE_MAGIC   0x676C               // "gl"
#define EE_VERSION 1
struct Persist {
  uint16_t magic;
  uint8_t  version;
  uint8_t  selected;
  uint8_t  level[NUM_CHANNELS];
  uint8_t  muted[NUM_CHANNELS];
};

// ---------------------------------------------------------------------------
// PWM (TCA0 split mode) + dithering
// ---------------------------------------------------------------------------

// Logical brightness → high-res duty. Square law (~gamma 2.0) so equal joystick
// travel gives roughly equal perceived change, lifted off zero by PWM_MIN_DUTY
// so the lowest lit step is actually visible on the PT4115.
static uint16_t gammaHR(uint8_t lvl) {
  if (lvl == 0) return 0;
  const uint16_t minHR = (uint16_t)PWM_MIN_DUTY << DITHER_BITS;
  const uint16_t maxHR = HR_MAX;
  return minHR + (uint16_t)(((uint32_t)(maxHR - minHR) * lvl * lvl) / (255UL * 255UL));
}

static inline void writeHCMP(uint8_t ch, uint8_t v) {
  switch (ch) {
    case 0: TCA0.SPLIT.HCMP0 = v; break; // PA3 / WO3
    case 1: TCA0.SPLIT.HCMP1 = v; break; // PA4 / WO4
    case 2: TCA0.SPLIT.HCMP2 = v; break; // PA5 / WO5
  }
}

static inline void setHR(uint8_t ch, uint16_t hr) {
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { dutyHR[ch] = hr; }
}

// Fires at the high-byte timer's BOTTOM, once per PWM period (~976 Hz). Renders
// each channel's high-res duty to the 8-bit compare register, spreading the
// fractional part over time via first-order sigma-delta.
ISR(TCA0_HUNF_vect) {
  static uint8_t ditherAcc[NUM_CHANNELS];
  for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) {
    uint16_t hr   = dutyHR[ch];
    uint8_t  base = hr >> DITHER_BITS;
    uint8_t  frac = hr & (DITHER_STEPS - 1);
    uint8_t  a    = ditherAcc[ch] + frac;
    uint8_t  carry = 0;
    if (a >= DITHER_STEPS) { a -= DITHER_STEPS; carry = 1; }
    ditherAcc[ch] = a;
    uint16_t out = (uint16_t)base + carry;
    if (out > 255) out = 255;
    writeHCMP(ch, (uint8_t)out);
  }
  TCA0.SPLIT.INTFLAGS = TCA_SPLIT_HUNF_bm;
}

static void pwmInit() {
  takeOverTCA0();                                  // core stops managing TCA0
  PORTA.DIRSET = PIN3_bm | PIN4_bm | PIN5_bm;      // WO3/WO4/WO5 as outputs

  TCA0.SPLIT.CTRLA = 0;                             // stop before reconfiguring
  TCA0.SPLIT.CTRLD = TCA_SPLIT_SPLITM_bm;          // two 8-bit timers
  TCA0.SPLIT.CTRLB = TCA_SPLIT_HCMP0EN_bm |        // enable WO3/WO4/WO5, which
                     TCA_SPLIT_HCMP1EN_bm |        // takes the pins away from
                     TCA_SPLIT_HCMP2EN_bm;         // the PORT output register
  TCA0.SPLIT.HPER = 255;
  TCA0.SPLIT.LPER = 255;
  TCA0.SPLIT.HCMP0 = 0;
  TCA0.SPLIT.HCMP1 = 0;
  TCA0.SPLIT.HCMP2 = 0;
  TCA0.SPLIT.INTFLAGS = TCA_SPLIT_HUNF_bm;         // clear stale flag
  TCA0.SPLIT.INTCTRL = TCA_SPLIT_HUNF_bm;          // dither ISR each period
  TCA0.SPLIT.CTRLA = PWM_CLKSEL | TCA_SPLIT_ENABLE_bm;
}

// One timer feeds all three channels, so there's a single PWM frequency at a
// time. Choose it from the brightest lit channel: high frequency when anything
// is bright (stroboscopic flicker is visible there), low when everything is dim
// (the driver gets long clean pulses at the bottom, and flicker barely reads
// when faint). Hysteresis via three tiers keeps it from hunting at a boundary.
// This does not move the 0.39% floor — see config.h.
#if GLIM_VARIABLE_PWM_FREQ
static void updatePwmFreq() {
  uint8_t maxlvl = 0;
  for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) {
    uint8_t l = (uint8_t)(disp[ch] >> 8);   // slewed level; 0 when off/faded out
    if (l > maxlvl) maxlvl = l;
  }

  static uint8_t tier = 1;                   // 0=lo, 1=mid, 2=hi (matches pwmInit)
  uint8_t next = tier;
  switch (tier) {
    case 0: if (maxlvl > PWM_FREQ_LO_LEVEL + PWM_FREQ_HYST) next = 1; break;
    case 1: if (maxlvl > PWM_FREQ_HI_LEVEL + PWM_FREQ_HYST) next = 2;
            else if (maxlvl < PWM_FREQ_LO_LEVEL - PWM_FREQ_HYST) next = 0; break;
    case 2: if (maxlvl < PWM_FREQ_HI_LEVEL - PWM_FREQ_HYST) next = 1; break;
  }
  if (next == tier) return;
  tier = next;

  uint8_t clksel = (tier == 0) ? PWM_FREQ_LO_CLKSEL
                 : (tier == 2) ? PWM_FREQ_HI_CLKSEL
                               : PWM_FREQ_MID_CLKSEL;
  // Changing CLKSEL only alters the prescaler; PER and the HCMPn duties are
  // unchanged, so brightness-per-count is identical and no rescale is needed.
  // Hysteresis makes this rare, so a one-period seam at the switch is invisible.
  TCA0.SPLIT.CTRLA = clksel | TCA_SPLIT_ENABLE_bm;
}
#else
static void updatePwmFreq() {}
#endif

// ---------------------------------------------------------------------------
// Rendering: setpoint → slewed display → high-res duty (ISR does the rest)
// ---------------------------------------------------------------------------

static void renderChannel(uint8_t ch) {
  setHR(ch, gammaHR((uint8_t)(disp[ch] >> 8)));
}

// Glide each channel's display level toward its target (0 when muted). Fast
// enough to track the live joystick ramp without lag, slow enough that a
// toggle or boot restore reads as a gentle fade.
static void slewAndRender(uint32_t dtMs) {
  // No floor on the step: with dt == 0 this correctly does nothing rather than
  // creeping once per loop iteration.
  int32_t step = FADE_SLEW * (int32_t)dtMs;
  for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) {
    int32_t target = (int32_t)(muted[ch] ? 0 : level[ch]) << 8;
    int32_t d = target - disp[ch];
    if (d > step)       disp[ch] += step;
    else if (d < -step) disp[ch] -= step;
    else                disp[ch]  = target;
    renderChannel(ch);
  }
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

static void markDirty() { dirty = true; dirtyAt = millis(); }

static void loadState() {
  Persist p;
  EEPROM.get(0, p);
  if (p.magic == EE_MAGIC && p.version == EE_VERSION && p.selected < NUM_CHANNELS) {
    selected = p.selected;
    for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
      level[i] = p.level[i];
      muted[i] = p.muted[i];
    }
  } else {
    // Blank/unrecognised EEPROM: come up with a gentle default so a freshly
    // installed light does something the moment it gets power.
    selected = 0;
    for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
      level[i] = DEFAULT_LEVEL;
      muted[i] = false;
    }
  }
  // Never boot into darkness. The supply is typically a wall switch, so giving
  // the thing power is itself a request for light — restoring a saved "all off"
  // scene would just look broken. Per-channel choices survive; a wholly dark
  // scene doesn't.
  bool anyOn = false;
  for (uint8_t i = 0; i < NUM_CHANNELS; i++)
    if (!muted[i] && level[i] > 0) { anyOn = true; break; }
  if (!anyOn) {
    for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
      muted[i] = false;
      if (level[i] == 0) level[i] = DEFAULT_LEVEL;
    }
  }

  for (uint8_t i = 0; i < NUM_CHANNELS; i++) levelAcc[i] = (int32_t)level[i] << LVL_SHIFT;
}

static void saveState() {
  Persist p;
  p.magic = EE_MAGIC;
  p.version = EE_VERSION;
  p.selected = selected;
  for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
    p.level[i] = level[i];
    p.muted[i] = muted[i];
  }
  EEPROM.put(0, p);   // .put uses .update internally → only changed bytes wear
  dirty = false;
}

// ---------------------------------------------------------------------------
// Status pixel — colour says which channel the joystick is steering.
// ---------------------------------------------------------------------------

#if GLIM_STATUS_PIXEL
static uint8_t pixelBuf[3];
static tinyNeoPixel statusPixel = tinyNeoPixel(1, STATUS_PIXEL_PIN, NEO_GRB, pixelBuf);

// Only pushed when it actually changes: the WS2812 bit-bang runs with
// interrupts off for ~30 µs, and there's no reason to nudge the dither ISR more
// often than necessary.
static void updateStatusPixel(bool force) {
  static uint8_t lastSel = 0xFF;
  static bool    lastIdle = false;

  bool idle = true;
  for (uint8_t c = 0; c < NUM_CHANNELS; c++)
    if (!muted[c] && level[c] > 0) { idle = false; break; }

  if (!force && selected == lastSel && idle == lastIdle) return;
  lastSel = selected;
  lastIdle = idle;

  static const uint32_t colours[NUM_CHANNELS] = {
    STATUS_COLOR_CH1, STATUS_COLOR_CH2, STATUS_COLOR_CH3
  };
  uint32_t c = colours[selected];
  uint16_t scale = idle ? STATUS_BRIGHT_IDLE : STATUS_BRIGHT;
  statusPixel.setPixelColor(0,
    (uint8_t)(((uint16_t)((c >> 16) & 0xFF) * scale) / 255),
    (uint8_t)(((uint16_t)((c >>  8) & 0xFF) * scale) / 255),
    (uint8_t)(((uint16_t)( c        & 0xFF) * scale) / 255));
  statusPixel.show();
}
#else
static void updateStatusPixel(bool) {}
#endif

// If the joystick button is held at power-on, wipe saved state back to
// defaults. All channels swell up together as a "charging" cue while held; a
// flash confirms the wipe. Released early → cancelled, normal boot. Runs after
// pwmInit() (so it can drive the LEDs) and before loadState() (so the wipe
// takes effect). The watchdog isn't running yet, so the long hold is safe.
static void setAllHR(uint16_t hr) {
  for (uint8_t c = 0; c < NUM_CHANNELS; c++) setHR(c, hr);
}

static void factoryResetCheck() {
  if (digitalRead(JOY_SW_PIN) != LOW) return;      // not held → normal boot

  uint32_t t0 = millis();
  while (digitalRead(JOY_SW_PIN) == LOW) {
    uint32_t held = millis() - t0;
    if (held >= FACTORY_HOLD_MS) {
      Persist blank;                                // invalidate saved state
      blank.magic = 0xFFFF;
      EEPROM.put(0, blank);
      for (uint8_t f = 0; f < 6; f++) {             // confirm: flash all channels
        setAllHR((f & 1) ? 0 : gammaHR(DEFAULT_LEVEL));
        delay(110);
      }
      setAllHR(0);
      return;                                       // loadState() → defaults
    }
    setAllHR(gammaHR((uint8_t)((held * 255) / FACTORY_HOLD_MS)));  // swell up
    delay(5);
  }
  setAllHR(0);                                      // released early → cancel
}

// ---------------------------------------------------------------------------
// Feedback: blink the selected channel so the user sees which one they picked.
// Only the selected channel is disturbed; the others hold their dithered PWM.
// ---------------------------------------------------------------------------

static void ackBlink(uint8_t ch) {
  bool lit = !muted[ch] && level[ch] > 0;
  for (uint8_t i = 0; i < 2; i++) {
    // If the light is on, dip it; if it's off, pulse it up — either way it
    // visibly "answers".
    setHR(ch, lit ? 0 : gammaHR(DEFAULT_LEVEL));
    delay(80);
    setHR(ch, gammaHR((uint8_t)(disp[ch] >> 8)));   // back to current display
    delay(90);
  }
}

// ---------------------------------------------------------------------------
// Switch: debounced, distinguishes tap from hold.
// ---------------------------------------------------------------------------

static bool     swPressed = false;
static uint32_t swChangedAt = 0;
static bool     swLongFired = false;

static void handleSwitch() {
  bool raw = (digitalRead(JOY_SW_PIN) == LOW);   // active-low
  uint32_t now = millis();

  if (raw != swPressed) {
    if (now - swChangedAt < SW_DEBOUNCE_MS) return; // bounce
    swPressed = raw;
    swChangedAt = now;
    if (swPressed) {
      swLongFired = false;                         // new press begins
    } else if (!swLongFired) {
      // Released before the long-press threshold → tap: toggle selected.
      bool lit = !muted[selected] && level[selected] > 0;
      if (lit) {
        muted[selected] = true;                    // off, remembering level
      } else {
        muted[selected] = false;                   // on
        if (level[selected] == 0) {
          level[selected] = DEFAULT_LEVEL;
          levelAcc[selected] = (int32_t)DEFAULT_LEVEL << LVL_SHIFT;
        }
      }
      markDirty();
    }
    return;
  }

  // Held past the threshold → toggle *everything*, mirroring what a tap does to
  // one channel. Anything lit → all off; nothing lit → all back on.
  if (swPressed && !swLongFired && now - swChangedAt >= SW_LONGPRESS_MS) {
    swLongFired = true;
    bool anyOn = false;
    for (uint8_t i = 0; i < NUM_CHANNELS; i++)
      if (!muted[i] && level[i] > 0) { anyOn = true; break; }

    for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
      if (anyOn) {
        muted[i] = true;                     // off, remembering levels
      } else {
        muted[i] = false;                    // on
        if (level[i] == 0) {                 // never set → give it something
          level[i] = DEFAULT_LEVEL;
          levelAcc[i] = (int32_t)DEFAULT_LEVEL << LVL_SHIFT;
        }
      }
    }
    markDirty();
  }
}

// ---------------------------------------------------------------------------
// Joystick axes
// ---------------------------------------------------------------------------

static int16_t deflection(uint8_t pin, int16_t centre, bool invert) {
  int16_t d = (int16_t)analogRead(pin) - centre;
  return invert ? (int16_t)-d : d;
}

// Up/down: ramp the selected channel, speed proportional to deflection.
static void handleBrightness(uint32_t dtMs) {
  if (dtMs == 0) return;                // no time passed → nothing to integrate

  int16_t y = deflection(JOY_Y_PIN, centreY, JOY_Y_INVERT);
  int16_t mag = (y < 0 ? -y : y) - JOY_DEADZONE;
  if (mag <= 0) return;
  if (mag > JOY_SPAN) mag = JOY_SPAN;

  int32_t &acc = levelAcc[selected];

  // Ease the rate down toward the bottom of the range: at level 0 the ramp runs
  // RAMP_LOW_FACTOR times slower than at full brightness, scaling linearly in
  // between. Dim settings get fine trimming; the top end still moves.
  //   factor(lvl) = (255 + (F-1)·lvl) / (255·F)   →  1/F at lvl 0, 1 at lvl 255
  uint8_t lvl = (uint8_t)(acc >> LVL_SHIFT);
  int32_t rate = ((int32_t)RATE_FULL *
                  (255 + (int32_t)(RAMP_LOW_FACTOR - 1) * lvl)) /
                 (255L * RAMP_LOW_FACTOR);

  int32_t step = (rate * (int32_t)mag * (int32_t)dtMs) / JOY_SPAN;

  if (y > 0) { acc += step; if (acc > LVL_MAX) acc = LVL_MAX; }
  else       { acc -= step; if (acc < 0) acc = 0; }

  level[selected] = (uint8_t)(acc >> LVL_SHIFT);
  muted[selected] = false;              // actively adjusting un-mutes
  markDirty();
}

// Left/right: edge-triggered channel select with re-arming, plus ack blink.
static bool xArmed = true;

static void handleSelect() {
  int16_t x = deflection(JOY_X_PIN, centreX, JOY_X_INVERT);
  int16_t ax = x < 0 ? -x : x;

  if (xArmed && ax > JOY_X_THRESH) {
    if (x > 0) selected = (selected + 1) % NUM_CHANNELS;
    else       selected = (selected + NUM_CHANNELS - 1) % NUM_CHANNELS;
    xArmed = false;
    ackBlink(selected);
    markDirty();
  } else if (!xArmed && ax < JOY_X_REARM) {
    xArmed = true;
  }
}

// ---------------------------------------------------------------------------
// Boot / loop
// ---------------------------------------------------------------------------

static void calibrateCentre() {
  // Assumes the stick is released at power-on. Average a few reads.
  int32_t sx = 0, sy = 0;
  const uint8_t n = 16;
  for (uint8_t i = 0; i < n; i++) {
    sx += analogRead(JOY_X_PIN);
    sy += analogRead(JOY_Y_PIN);
    delay(4);
  }
  centreX = sx / n;
  centreY = sy / n;
}

void setup() {
  pinMode(JOY_SW_PIN, INPUT_PULLUP);
#if GLIM_STATUS_PIXEL
  pinMode(STATUS_PIXEL_PIN, OUTPUT);   // required by the _Static variant
#endif
  pwmInit();

#if GLIM_DEBUG
  Serial.begin(115200);
  Serial.println(F("glim boot"));
#endif

#if GLIM_FACTORY_RESET
  factoryResetCheck();
#endif
  calibrateCentre();
  loadState();
  updateStatusPixel(true);

  // Soft-start: interpolate every display from 0 up to the restored scene over a
  // fixed BOOT_FADE_MS. Deliberately not the FADE_MS slew limiter — that's a
  // rate, so restoring a dim scene would finish in tens of ms and read as a snap
  // no matter how gentle the rate. Here dim and bright scenes take equally long.
  for (uint8_t i = 0; i < NUM_CHANNELS; i++) disp[i] = 0;
  uint32_t t0 = millis();
  for (;;) {
    uint32_t e = millis() - t0;
    if (e >= (uint32_t)BOOT_FADE_MS) break;
    uint16_t f = (uint16_t)((e * 256UL) / BOOT_FADE_MS);   // 0..255 of the way
    for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) {
      int32_t target = (int32_t)(muted[ch] ? 0 : level[ch]) << 8;
      disp[ch] = (target * (int32_t)f) >> 8;
      renderChannel(ch);
    }
    delay(2);
  }
  for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) {   // land exactly on target
    disp[ch] = (int32_t)(muted[ch] ? 0 : level[ch]) << 8;
    renderChannel(ch);
  }

  ackBlink(selected);          // show which channel is active at startup

#if GLIM_WATCHDOG
  _PROTECTED_WRITE(WDT.CTRLA, WDT_PERIOD_2KCLK_gc);   // ~2 s
#endif
  lastTick = millis();
}

void loop() {
  uint32_t now = millis();
  uint32_t dt = now - lastTick;
  lastTick = now;
  if (dt > DT_CLAMP_MS) dt = DT_CLAMP_MS;

  handleBrightness(dt);
  handleSelect();
  handleSwitch();
  slewAndRender(dt);
  updatePwmFreq();
  updateStatusPixel(false);

  if (dirty && (now - dirtyAt) >= EEPROM_SAVE_DELAY_MS) saveState();

#if GLIM_WATCHDOG
  wdt_reset();
#endif

#if GLIM_DEBUG
  static uint32_t dbg = 0;
  if (now - dbg > 250) {
    dbg = now;
    Serial.print(F("ch=")); Serial.print(selected);
    Serial.print(F(" lvl=")); Serial.print(level[selected]);
    Serial.print(F(" mute=")); Serial.print(muted[selected]);
    Serial.print(F(" x=")); Serial.print(analogRead(JOY_X_PIN));
    Serial.print(F(" y=")); Serial.println(analogRead(JOY_Y_PIN));
  }
#endif
}
