// glim — a joystick-controlled 3-channel LED dimmer on an ATtiny814.
//
//   up / down     ramp the selected channel brighter / dimmer (speed follows
//                 how far you push)
//   left / right  select which of the 3 channels you're controlling; the newly
//                 selected channel blinks once, so the light itself says which
//                 one answered — no channel-coded indicator required
//   tap switch    toggle the selected channel on / off (fades)
//   hold switch   toggle all channels on / off (fades)
//   hold 3 s      enter IR learn mode — see irLearn()
//
// Signal flow per channel:
//   setpoint level  → slewed display level (soft transitions)
//                   → 16-bit gamma curve (perceptually linear)
//                   → TCA0 normal-mode single-slope PWM (WO0/WO1/WO2; see
//                     config.h for which pins those land on per board).
//
// The PWM is bare-metal TCA0 rather than analogWrite() because the core can't
// drive TCA0 in normal (16-bit) mode. 16 bits matters: the PT4115's floor is a
// ~2 µs minimum on-time, which at 305 Hz is 40 counts of 65536 — an 8-bit timer
// would floor us 6.4× higher than the driver can actually resolve.
// millis() lives on TCD0, so TCA0 is ours to take over.

#include <Arduino.h>
#include <EEPROM.h>
#include <avr/wdt.h>
#include <util/atomic.h>
#include "config.h"

#if GLIM_STATUS_PIXEL
#include <tinyNeoPixel_Static.h>
#endif

#if GLIM_DEBUG
#if GLIM_DEBUG_HW_SERIAL
// rev2: LED ch3 sits on WO2's alternate pin, so USART0's own pins are free.
#define dbg Serial
#else
// rev1: no hardware USART (its TXD is LED ch3), so bit-bang. This is what
// forces GLIM_IR off in rev1 debug builds — see config.h.
#include <SoftwareSerial.h>
static SoftwareSerial dbg(DEBUG_RX_PIN, DEBUG_TX_PIN);
#endif
#endif

#define HR_MAX    ((uint16_t)PWM_PER)               // full-scale duty
#define FADE_SLEW (((int32_t)255 << 8) / FADE_MS)   // display slew, level<<8 per ms

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

static uint16_t minHR = 40;             // driver's min on-time, in duty counts

static int16_t  centreX = 512;          // joystick centres, auto-measured at boot
static int16_t  centreY = 512;

static uint32_t lastTick = 0;
static bool     dirty = false;          // state changed since last EEPROM save
static uint32_t dirtyAt = 0;

#if GLIM_IR
static uint32_t irMap[IR_ACT_COUNT];    // learned NEC codes, 0 = unbound
static bool     irBound = false;
#endif

// EEPROM layout: magic + version so future firmware can migrate rather than
// wipe the saved scene. Version 2 adds the learned IR codes.
#define EE_MAGIC   0x676C               // "gl"
#define EE_VERSION 2
struct Persist {
  uint16_t magic;
  uint8_t  version;
  uint8_t  selected;
  uint8_t  level[NUM_CHANNELS];
  uint8_t  muted[NUM_CHANNELS];
#if GLIM_IR
  uint8_t  irBound;
  uint32_t ir[IR_ACT_COUNT];
#endif
};

// ---------------------------------------------------------------------------
// PWM — TCA0 normal mode, 16-bit single-slope
// ---------------------------------------------------------------------------

// Logical brightness → 16-bit duty. Square law (~gamma 2.0) so equal joystick
// travel gives roughly equal perceived change, lifted off zero by minHR so the
// dimmest lit step is one the driver can actually reproduce.
static uint16_t gammaHR(uint8_t lvl) {
  if (lvl == 0) return 0;
  uint16_t sq = (uint16_t)lvl * lvl;                  // ≤ 65025
  // (HR_MAX - minHR) × sq peaks at 65495 × 65025 ≈ 4.26e9, inside uint32.
  return minHR + (uint16_t)(((uint32_t)(HR_MAX - minHR) * sq) / 65025UL);
}

// CMPnBUF, not CMPn: the buffered registers update at the period boundary, so a
// duty change mid-period can't produce a runt pulse (datasheet §20.3.3.4).
static inline void setHR(uint8_t ch, uint16_t hr) {
  switch (ch) {
    case 0: TCA0.SINGLE.CMP0BUF = hr; break;   // WO0 — LED1_PIN
    case 1: TCA0.SINGLE.CMP1BUF = hr; break;   // WO1 — LED2_PIN
    case 2: TCA0.SINGLE.CMP2BUF = hr; break;   // WO2 — LED3_PIN
  }
}

static void pwmInit() {
  takeOverTCA0();                                  // core stops managing TCA0
  // Relocate any waveform outputs that don't sit at their default pin. rev1
  // writes 0 here; rev2 moves all three to PB3/PB4/PB5 so I²C gets PB0/PB1 and
  // USART0 gets PB2.
  PORTMUX.CTRLC = PWM_PORTMUX;
  PORTB.DIRSET = LED_DIR_bm;                       // WO0/WO1/WO2 as outputs

  TCA0.SINGLE.CTRLA = 0;                            // stop before reconfiguring
  TCA0.SINGLE.CTRLD = 0;                            // normal (not split) mode
  // Single-slope: output set at TOP, cleared on compare match → duty = CMP/PER,
  // higher = brighter. CMPnEN hands the pins to the timer (datasheet §20.3.3.4).
  TCA0.SINGLE.CTRLB = TCA_SINGLE_WGMODE_SINGLESLOPE_gc |
                      TCA_SINGLE_CMP0EN_bm |
                      TCA_SINGLE_CMP1EN_bm |
                      TCA_SINGLE_CMP2EN_bm;
  TCA0.SINGLE.PER  = PWM_PER;
  TCA0.SINGLE.CMP0 = 0;
  TCA0.SINGLE.CMP1 = 0;
  TCA0.SINGLE.CMP2 = 0;
  TCA0.SINGLE.CTRLA = PWM_CLKSEL_MID | TCA_SINGLE_ENABLE_bm;
  minHR = (uint16_t)(((uint32_t)DRIVER_MIN_ON_NS * (F_CPU / 1000000UL)) /
                     (1000UL * PWM_PRESCALE_MID));
}

// ---------------------------------------------------------------------------
// Rendering: setpoint → slewed display → duty
// ---------------------------------------------------------------------------

static void renderChannel(uint8_t ch) {
  setHR(ch, gammaHR((uint8_t)(disp[ch] >> 8)));
}

// One timer feeds all three channels, so there's a single PWM frequency at a
// time. Choose it from the brightest lit channel: high frequency when anything
// is bright (stroboscopic flicker is visible there), low when everything is dim
// (flicker barely reads when faint, and the min-on-time floor shrinks in counts,
// so the dim end genuinely gets deeper). Three tiers with hysteresis so it can't
// hunt at a boundary.
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

  uint8_t  clksel;
  uint16_t presc;
  if (tier == 0)      { clksel = PWM_CLKSEL_LO;  presc = PWM_PRESCALE_LO;  }
  else if (tier == 2) { clksel = PWM_CLKSEL_HI;  presc = PWM_PRESCALE_HI;  }
  else                { clksel = PWM_CLKSEL_MID; presc = PWM_PRESCALE_MID; }

  // Only the prescaler changes; PER and the duties are untouched, so the duty
  // *ratio* — and therefore brightness — is identical across the switch. The
  // floor in counts does change, hence the re-render below.
  TCA0.SINGLE.CTRLA = clksel | TCA_SINGLE_ENABLE_bm;
  minHR = (uint16_t)(((uint32_t)DRIVER_MIN_ON_NS * (F_CPU / 1000000UL)) /
                     (1000UL * presc));
  for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) renderChannel(ch);
}
#else
static void updatePwmFreq() {}
#endif

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
// IR — NEC decode
// ---------------------------------------------------------------------------
//
// NEC frames are self-clocking on their falling edges alone, which makes the
// decoder tiny: measure the gap between successive falling edges and the frame
// falls out. 13.5 ms header, then 1.125 ms per '0' and 2.25 ms per '1', 32 bits
// LSB-first. A held button sends an 11.25 ms repeat frame every ~108 ms.

#if GLIM_IR
static volatile uint32_t irCode = 0;      // last complete 32-bit frame
static volatile bool     irReady = false;
static volatile uint32_t irActivityMs = 0; // last header/repeat — "still held"

ISR(IR_PORT_vect) {
  if (!(IR_PORT.INTFLAGS & IR_PIN_bm)) return;
  IR_PORT.INTFLAGS = IR_PIN_bm;              // write-1-to-clear

  static uint32_t last = 0;
  static uint32_t acc = 0;
  static uint8_t  bits = 0xFF;             // 0xFF = not in a frame

  uint32_t now = micros();
  uint32_t d = now - last;
  last = now;

  if (d > 12500 && d < 15500) {            // header → new frame
    bits = 0; acc = 0; irActivityMs = millis();
  } else if (d > 9500 && d <= 12500) {     // repeat frame → button still held
    irActivityMs = millis(); bits = 0xFF;
  } else if (bits < 32) {
    if (d > 900 && d < 1400) {             // '0'
      acc >>= 1;
    } else if (d > 1900 && d < 2700) {     // '1'
      acc = (acc >> 1) | 0x80000000UL;
    } else {                               // out of spec → abandon the frame
      bits = 0xFF; return;
    }
    if (++bits == 32) {
      irCode = acc; irReady = true; irActivityMs = millis(); bits = 0xFF;
    }
  }
}

static void irInit() {
  pinMode(IR_PIN, INPUT_PULLUP);
  // Falling-edge interrupt. With no receiver fitted the pull-up holds the line
  // high, so no edges arrive and nothing decodes.
  IR_PINCTRL = PORT_ISC_FALLING_gc | PORT_PULLUPEN_bm;
}

// Take a completed frame, if any.
static bool irTake(uint32_t *out) {
  bool got = false;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    if (irReady) { *out = irCode; irReady = false; got = true; }
  }
  return got;
}

static bool irHeld() {
  uint32_t t;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { t = irActivityMs; }
  return (millis() - t) < IR_HOLD_MS;
}
#endif  // GLIM_IR

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
#if GLIM_IR
    irBound = p.irBound;
    for (uint8_t i = 0; i < IR_ACT_COUNT; i++) irMap[i] = p.ir[i];
#endif
  } else {
    // Blank/unrecognised EEPROM: come up with a gentle default so a freshly
    // installed light does something the moment it gets power.
    selected = 0;
    for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
      level[i] = DEFAULT_LEVEL;
      muted[i] = false;
    }
#if GLIM_IR
    irBound = false;
    for (uint8_t i = 0; i < IR_ACT_COUNT; i++) irMap[i] = 0;
#endif
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
#if GLIM_IR
  p.irBound = irBound;
  for (uint8_t i = 0; i < IR_ACT_COUNT; i++) p.ir[i] = irMap[i];
#endif
  EEPROM.put(0, p);   // .put uses .update internally → only changed bytes wear
  dirty = false;
}

// ---------------------------------------------------------------------------
// Status indicator — one meaning: "the system is on".
// ---------------------------------------------------------------------------
//
// Deliberately *not* channel-coded. The channels announce themselves better than
// a shared indicator can: selecting one blinks that light (ackBlink), and a small
// indicator LED hung on each driver's DIM line mirrors that channel's brightness
// for free — no extra pins, since it just sits on the PWM output. So this is only
// a lamp, rendered on whatever the board has fitted.

#if GLIM_STATUS_PIXEL
static uint8_t pixelBuf[3];
static tinyNeoPixel statusPixel = tinyNeoPixel(1, STATUS_PIXEL_PIN, NEO_GRB, pixelBuf);

static void statusInit() { pinMode(STATUS_PIXEL_PIN, OUTPUT); }

// The WS2812 bit-bang runs with interrupts off for ~30 µs, so only write when
// the state actually changes — the IR decoder times edges to a few µs and would
// rather not be interrupted more often than necessary.
static void statusSet(bool on) {
  static int8_t last = -1;
  if ((int8_t)on == last) return;
  last = on;
  statusPixel.setPixelColor(0, on ? (uint8_t)((STATUS_COLOR_ON >> 16) & 0xFF) : 0,
                               on ? (uint8_t)((STATUS_COLOR_ON >>  8) & 0xFF) : 0,
                               on ? (uint8_t)( STATUS_COLOR_ON        & 0xFF) : 0);
  statusPixel.show();
}

#elif GLIM_STATUS_LED
static inline void ledWrite(bool on) {
#if STATUS_LED_ACTIVE_LOW
  digitalWrite(STATUS_LED_PIN, on ? LOW : HIGH);
#else
  digitalWrite(STATUS_LED_PIN, on ? HIGH : LOW);
#endif
}
static void statusInit() { pinMode(STATUS_LED_PIN, OUTPUT); ledWrite(false); }

// Software PWM: lit for the first millisecond of every STATUS_LED_DIV. Costs one
// modulo per loop and consumes no timer.
static void statusSet(bool on) {
  ledWrite(on && ((millis() % STATUS_LED_DIV) == 0));
}

#else
static void statusInit() {}
static void statusSet(bool) {}
#endif

// Called every loop. Lit whenever firmware is running — optionally pulsing, so a
// live board is distinguishable from one the watchdog keeps resetting.
static void statusRender() {
#if GLIM_STATUS_HEARTBEAT
  statusSet((millis() % STATUS_HEARTBEAT_MS) < (STATUS_HEARTBEAT_MS / 8));
#else
  statusSet(true);
#endif
}

// Blocking feedback for the IR learn walk — the one place the indicator must say
// more than "on". Uses blink *counts* rather than colours, so it reads identically
// on a single LED and on the pixel.
static void statusBlink(uint8_t n, uint16_t onMs, uint16_t offMs) {
  for (uint8_t i = 0; i < n; i++) {
    uint32_t t = millis();
    while (millis() - t < onMs)  { statusSet(true);  wdt_reset(); }
    t = millis();
    while (millis() - t < offMs) { statusSet(false); wdt_reset(); }
  }
}

// ---------------------------------------------------------------------------
// Actions — shared by the joystick and the remote
// ---------------------------------------------------------------------------

static void ackBlink(uint8_t ch);   // fwd

static void actToggleChannel(uint8_t ch) {
  bool lit = !muted[ch] && level[ch] > 0;
  if (lit) {
    muted[ch] = true;                          // off, remembering level
  } else {
    muted[ch] = false;                         // on
    if (level[ch] == 0) {
      level[ch] = DEFAULT_LEVEL;
      levelAcc[ch] = (int32_t)DEFAULT_LEVEL << LVL_SHIFT;
    }
  }
  markDirty();
}

// Toggle everything, mirroring what a tap does to one channel: anything lit →
// all off; nothing lit → all back on at remembered levels.
static void actToggleAll() {
  bool anyOn = false;
  for (uint8_t i = 0; i < NUM_CHANNELS; i++)
    if (!muted[i] && level[i] > 0) { anyOn = true; break; }

  for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
    if (anyOn) {
      muted[i] = true;
    } else {
      muted[i] = false;
      if (level[i] == 0) {
        level[i] = DEFAULT_LEVEL;
        levelAcc[i] = (int32_t)DEFAULT_LEVEL << LVL_SHIFT;
      }
    }
  }
  markDirty();
}

static void actSelect(int8_t dir) {
  selected = (uint8_t)((selected + NUM_CHANNELS + dir) % NUM_CHANNELS);
  ackBlink(selected);
  markDirty();
}

// Move the selected channel's setpoint by a signed accumulator delta, easing the
// rate down toward the bottom of the range so dim settings trim finely.
static void actRamp(int8_t sign, uint32_t dtMs, int16_t mag) {
  int32_t &acc = levelAcc[selected];
  uint8_t lvl = (uint8_t)(acc >> LVL_SHIFT);
  //   factor(lvl) = (255 + (F-1)·lvl) / (255·F)   →  1/F at lvl 0, 1 at lvl 255
  int32_t rate = ((int32_t)RATE_FULL *
                  (255 + (int32_t)(RAMP_LOW_FACTOR - 1) * lvl)) /
                 (255L * RAMP_LOW_FACTOR);
  int32_t step = (rate * (int32_t)mag * (int32_t)dtMs) / JOY_SPAN;

  if (sign > 0) { acc += step; if (acc > LVL_MAX) acc = LVL_MAX; }
  else          { acc -= step; if (acc < 0) acc = 0; }

  level[selected] = (uint8_t)(acc >> LVL_SHIFT);
  muted[selected] = false;              // actively adjusting un-mutes
  markDirty();
}

// ---------------------------------------------------------------------------
// Feedback: blink the selected channel so the user sees which one they picked.
// Only the selected channel is disturbed; the others hold their PWM.
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
// IR learn mode
// ---------------------------------------------------------------------------
//
// Walks the six actions in order. For each: blink the status LED (n+1) times,
// wait for a remote press, store it, confirm with a green flash. A press that
// duplicates an already-bound code is rejected with a red flash and re-asked.
// Ten seconds of silence ends the walk and saves what's bound so far.

#if GLIM_IR
static void irLearn() {
  uint32_t learned[IR_ACT_COUNT];
  uint8_t  n = 0;
  uint32_t dummy;

  for (uint8_t i = 0; i < NUM_CHANNELS; i++) setHR(i, 0);   // lights out, focus
  irTake(&dummy);                                            // drop any stale frame

  for (n = 0; n < IR_ACT_COUNT; n++) {
    statusBlink(n + 1, 120, 180);                            // n+1 blinks = step n

    uint32_t t0 = millis();
    for (;;) {
      wdt_reset();
      if (millis() - t0 > IR_LEARN_TIMEOUT_MS) goto done;    // give up, save what we have

      uint32_t code;
      if (!irTake(&code)) continue;
      if (code == 0) continue;

      bool dup = false;
      for (uint8_t i = 0; i < n; i++) if (learned[i] == code) dup = true;
      if (dup) {                                             // same button twice
        statusBlink(4, 70, 70);                              // rejected
        t0 = millis();
        continue;
      }
      learned[n] = code;
      statusBlink(1, 400, 150);                              // accepted
      break;
    }
  }

done:
  for (uint8_t i = 0; i < n; i++) irMap[i] = learned[i];
  for (uint8_t i = n; i < IR_ACT_COUNT; i++) irMap[i] = 0;   // unbound
  irBound = (n > 0);
  saveState();

  statusBlink(3, 250, 250);                                  // saved
  statusRender();
  for (uint8_t i = 0; i < NUM_CHANNELS; i++) renderChannel(i);
}

// Dispatch a completed frame. Ramping is handled by irHeld() in loop(), so the
// two ramp actions only need to record which way to go.
static int8_t irRampDir = 0;

static void irDispatch(uint32_t code) {
  if (!irBound) return;
  irRampDir = 0;
  for (uint8_t a = 0; a < IR_ACT_COUNT; a++) {
    if (irMap[a] == 0 || irMap[a] != code) continue;
    switch (a) {
      case IR_ACT_UP:     irRampDir =  1; break;
      case IR_ACT_DOWN:   irRampDir = -1; break;
      case IR_ACT_NEXT:   actSelect(+1);  break;
      case IR_ACT_PREV:   actSelect(-1);  break;
      case IR_ACT_TOGGLE: actToggleChannel(selected); break;
      case IR_ACT_ALL:    actToggleAll();  break;
    }
    return;
  }
}
#endif  // GLIM_IR

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
// Switch: debounced, distinguishes tap from hold from very-long hold.
// ---------------------------------------------------------------------------

static bool     swPressed = false;
static uint32_t swChangedAt = 0;
static bool     swLongFired = false;
static bool     swLearnFired = false;

static void handleSwitch() {
  bool raw = (digitalRead(JOY_SW_PIN) == LOW);   // active-low
  uint32_t now = millis();

  if (raw != swPressed) {
    if (now - swChangedAt < SW_DEBOUNCE_MS) return; // bounce
    swPressed = raw;
    swChangedAt = now;
    if (swPressed) {
      swLongFired = false;                         // new press begins
      swLearnFired = false;
    } else if (!swLongFired) {
      actToggleChannel(selected);                  // tap
    }
    return;
  }

  if (!swPressed) return;

  // Held past the short threshold → toggle everything.
  if (!swLongFired && now - swChangedAt >= SW_LONGPRESS_MS) {
    swLongFired = true;
    actToggleAll();
  }

#if GLIM_IR
  // Still holding well past that → IR learn mode. Undo the all-toggle first, so
  // the gesture doesn't leave the room in a state you didn't ask for.
  if (swLongFired && !swLearnFired && now - swChangedAt >= IR_LEARN_HOLD_MS) {
    swLearnFired = true;
    actToggleAll();                                // revert the 700 ms toggle
    while (digitalRead(JOY_SW_PIN) == LOW) wdt_reset();   // wait for release
    swPressed = false;
    swChangedAt = millis();
    irLearn();
    lastTick = millis();
  }
#endif
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

  actRamp(y > 0 ? 1 : -1, dtMs, mag);
}

// Left/right: edge-triggered channel select with re-arming.
static bool xArmed = true;

static void handleSelect() {
  int16_t x = deflection(JOY_X_PIN, centreX, JOY_X_INVERT);
  int16_t ax = x < 0 ? -x : x;

  if (xArmed && ax > JOY_X_THRESH) {
    actSelect(x > 0 ? +1 : -1);
    xArmed = false;
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
  statusInit();
  pwmInit();

#if GLIM_DEBUG
  dbg.begin(115200);
#if GLIM_DEBUG_HW_SERIAL
  // Transmit only: USART0's RXD pin is an LED output on this board, so leaving
  // the receiver enabled would have it interrupting on the PWM waveform.
  USART0.CTRLB &= ~USART_RXEN_bm;
#endif
  dbg.println(F("glim boot"));
#endif

#if GLIM_FACTORY_RESET
  factoryResetCheck();
#endif
  calibrateCentre();
  loadState();
#if GLIM_IR
  irInit();
#endif
  statusRender();

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

#if GLIM_IR
  uint32_t code;
  if (irTake(&code)) irDispatch(code);
  // A held remote button repeats every ~108 ms; keep ramping while they last.
  if (irRampDir != 0) {
    if (irHeld()) actRamp(irRampDir, dt, JOY_SPAN / 2);   // half-deflection feel
    else          irRampDir = 0;
  }
#endif

  slewAndRender(dt);
  updatePwmFreq();
  statusRender();

  if (dirty && (now - dirtyAt) >= EEPROM_SAVE_DELAY_MS) saveState();

#if GLIM_WATCHDOG
  wdt_reset();
#endif

#if GLIM_DEBUG
  static uint32_t dbgLast = 0;
  if (now - dbgLast > 250) {
    dbgLast = now;
    dbg.print(F("ch=")); dbg.print(selected);
    dbg.print(F(" lvl=")); dbg.print(level[selected]);
    dbg.print(F(" mute=")); dbg.print(muted[selected]);
    dbg.print(F(" x=")); dbg.print(analogRead(JOY_X_PIN));
    dbg.print(F(" y=")); dbg.println(analogRead(JOY_Y_PIN));
  }
#endif
}
