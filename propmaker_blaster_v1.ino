// PSO Blaster (Adafruit Feather RP2040 Prop-Maker)
// Author: AW Labs
// Revision: 1.2.1
//
// Based on Adafruit's Prop-Maker demo sketch for wiring/pin reference
// (dual-core setup: core0 = game logic/LEDs/inputs, core1 = I2S audio).
//
// Revision History
//  1.2.1 - 2026-09-13
//      Combined sound and LED effects into PSOEffects library
//
//  1.2.0 - 2026-09-12
//      Added WEAPON_RIFLE and rifleBlast.h
//
//  1.1.0 - 2026-09-11
//      Added WEAPON_TYPE toggle
//      Added WEAPON_MECHGUN type
//      Corrected race condition for muzzle flash sequence 
//      Added ability to interrupt sound already playing and 
//        disable stacking queue
//
//  1.0.0 - 2026-09-07
//      Initial Release (Handgun prop only)
//
// Behavior:
//  1. Power-on chime from power_sound.h
//  2. 8x NeoPixel breathing + color-cycle on PIN_EXTERNAL_NEOPIXELS
//     (same palette/breathing math as the dagger's 30-LED strand)
//  3. 1x barrel NeoPixel on pin 10, flashes white to simulate a shot
//     (Handgun: single flash. Mechgun: 3-flash burst - see WEAPON_TYPE below)
//  4. Mode/swap button on pin 9 -> swap_sound.h + cycles main strip color mode
//  5. PIN_EXTERNAL_BUTTON = trigger -> shot sound + muzzle flash burst
//  6. Trigger held > CHARGE_HOLD_MS -> powerup.h then the shot sound (charged shot)
//  7. LIS3DH accelerometer: read + print to Serial only, no gameplay tie-in
//  8. Trigger/mode presses interrupt whatever's currently playing and clear
//     the queue, so rapid presses retrigger instantly instead of stacking up

// ---------------- Weapon type toggle ----------------
// Handgun and Mechgun are separate physical props but share this sketch.
// Flip this one line and reflash for whichever build you're doing.
#define WEAPON_HANDGUN 0
#define WEAPON_MECHGUN 1
#define WEAPON_RIFLE   2
#define WEAPON_TYPE WEAPON_HANDGUN   // <-- change to WEAPON_MECHGUN for the mechgun build

#include <Adafruit_NeoPixel.h>
#include <Adafruit_LIS3DH.h>
#include <Adafruit_Sensor.h>
#include <I2S.h>

#include <PSOEffects.h> // shared color/breathing scheme - lives in your Arduino libraries folder
// NOTE: confirm these array/variable names match what your WAV->header
// script actually generated (Adafruit's own converter uses the pattern
// <basename>AudioData / <basename>SampleRate, as used below).
#include <Sounds/power_sound.h>
#include <Sounds/swap_sound.h>
#include <Sounds/blast.h>
#include <Sounds/powerup.h>
#include <Sounds/mechBlast.h>
#include <Sounds/rifleBlast.h>

// ---------------- Pin assignments ----------------
#define PIN_MODE_BUTTON   9    // external mode/swap switch
#define PIN_BARREL_LED    10   // single NeoPixel, muzzle flash

// PIN_EXTERNAL_NEOPIXELS, PIN_EXTERNAL_BUTTON, PIN_EXTERNAL_POWER,
// PIN_I2S_BIT_CLOCK, PIN_I2S_DATA all come from the Prop-Maker board
// variant definitions (same as the Adafruit demo).

// ---------------- NeoPixel strips ----------------
#define MAIN_PIXEL_COUNT  8

Adafruit_NeoPixel mainStrip(MAIN_PIXEL_COUNT, PIN_EXTERNAL_NEOPIXELS, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel barrelLED(1, PIN_BARREL_LED, NEO_GRB + NEO_KHZ800);

// ---------------- Accelerometer (report-only) ----------------
Adafruit_LIS3DH lis = Adafruit_LIS3DH();

// ---------------- Color modes (mode button cycles these) ----------------
// Palette, breathing math, and mode names all come from PSOEffects.h now -
// see PSO_COLORS / PSO_NUM_MODES / PSO_MODE_NAMES there. Just need to
// track which mode we're currently on.
uint8_t currentMode = 0;


// AUDIO_GAIN tops out around 256 before you start clipping on full-scale samples (127 * 256 = 32512).
const int16_t AUDIO_GAIN = 25; // start here, raise toward ~256 if you want it louder still

// ---------------- Barrel flash state ----------------
bool     barrelFlashActive     = false;
uint32_t barrelFlashStartMs    = 0;
const uint16_t BARREL_FLASH_MS = 200;

// A "shot" is 1 or more flashes in a row (SHOT_FLASH_COUNT), spaced by
// BARREL_FLASH_GAP_MS. Handgun: 1 flash, indistinguishable from before.
// Mechgun: 3 flashes in quick succession per shot.
uint8_t  flashesRemaining      = 0;
uint32_t nextFlashAllowedMs    = 0;
const uint16_t BARREL_FLASH_GAP_MS = 50;

// ---------------- Trigger (PIN_EXTERNAL_BUTTON) state ----------------
bool     triggerPrevState    = HIGH; // INPUT_PULLUP idle-high
bool     triggerHeld         = false;
bool     triggerCharged      = false; // charged shot already fired this press
uint32_t triggerPressStartMs = 0;
const uint16_t CHARGE_HOLD_MS = 500;

// ---------------- Mode/swap button (pin 9) state ----------------
bool     modePrevState      = HIGH;
bool     modeLatchedState   = HIGH;
uint32_t modeLastDebounceMs = 0;
const uint16_t DEBOUNCE_MS  = 30;

// ================= Audio (runs entirely on core1) =================
// Same "table of clips" pattern as the Adafruit demo, but instead of
// auto-cycling we drive it from a small queue that core0 (game logic)
// pushes into. blastStartCount lets core0 know exactly when a blast clip
// actually starts, so the barrel flash can sync to the blast sound
// instead of to when it was merely queued.

struct SoundClip {
  const uint8_t *data;
  uint32_t       len;
  uint32_t       rate;
};

enum SoundId : uint8_t {
  SND_POWER_ON = 0,
  SND_SWAP,
  SND_BLAST,
  SND_POWERUP,
  SND_MECH_BLAST,
  SND_RIFLE,
  N_SOUNDS
};

SoundClip sound[N_SOUNDS] = {
  { POWER_SOUND,    POWER_SOUND_LENGTH,    POWER_SOUND_SAMPLE_RATE    }, // SND_POWER_ON
  { SWAP_SOUND,     SWAP_SOUND_LENGTH,     SWAP_SOUND_SAMPLE_RATE     }, // SND_SWAP
  { BLAST_SOUND,    BLAST_SOUND_LENGTH,    BLAST_SOUND_SAMPLE_RATE    }, // SND_BLAST
  { POWERUP_SOUND,  POWERUP_SOUND_LENGTH,  POWERUP_SOUND_SAMPLE_RATE  }, // SND_POWERUP
  { MECHBLAST_SOUND, MECHBLAST_SOUND_LENGTH, MECHBLAST_SOUND_SAMPLE_RATE }, // SND_MECH_BLAST
  { RIFLEBLAST_SOUND, RIFLEBLAST_SOUND_LENGTH, RIFLEBLAST_SOUND_SAMPLE_RATE },    // SND_RIFLE
};

// Which sound plays on trigger pull, and how many muzzle flashes go with
// it, both follow WEAPON_TYPE - this is the only place that needs to know
// about the difference between the two props.
#if WEAPON_TYPE == WEAPON_MECHGUN
  #define SHOT_SOUND       SND_MECH_BLAST
  #define SHOT_FLASH_COUNT 3
#elif WEAPON_TYPE == WEAPON_RIFLE
  #define SHOT_SOUND      SND_RIFLE
  #define SHOT_FLASH_COUNT 1
#else
  #define SHOT_SOUND      SND_BLAST
  #define SHOT_FLASH_COUNT 1
#endif

// small circular queue: core0 = producer, core1 = consumer
volatile uint8_t soundQueue[8];
volatile uint8_t queueHead = 0; // next slot to write
volatile uint8_t queueTail = 0; // next slot to read

// Bumped by core1 every time it actually begins playing a NEW blast clip
// (not merely queuing one). core0 just watches for this to advance past
// whatever it's already handled - a monotonic counter can't be matched
// against a stale/already-in-progress blast the way a shared "currently
// playing" level or a one-shot pending flag can (see write-up in chat).
volatile uint16_t blastStartCount = 0;

// Bumped whenever a new trigger/mode press should cut off whatever's
// currently playing rather than wait in line behind it - play_i2s() checks
// this every sample and bails out as soon as it changes.
volatile uint16_t playGeneration = 0;

I2S i2s(OUTPUT);

void queueSound(SoundId id) {
  uint8_t next = (queueHead + 1) % 8;
  if (next == queueTail) return; // queue full; drop rather than block gameplay
  soundQueue[queueHead] = (uint8_t)id;
  queueHead = next;
}

// Interrupts whatever's playing right now and drops anything still queued
// behind it, so a fresh trigger/mode press always feels instant instead of
// waiting for the previous clip (and its queue) to finish first.
void interruptAndClearQueue() {
  playGeneration++;
  queueHead = queueTail;
}

void setup1(void) {
  i2s.setBCLK(PIN_I2S_BIT_CLOCK);
  i2s.setDATA(PIN_I2S_DATA);
  i2s.setBitsPerSample(16);

  queueSound(SND_POWER_ON); // item #1
}

void loop1() {
  if (queueTail != queueHead) {
    uint8_t id = soundQueue[queueTail];
    queueTail = (queueTail + 1) % 8;

    if (id == SHOT_SOUND) {
      blastStartCount++; // signals "a shot genuinely started" to core0
    }
    play_i2s(sound[id].data, sound[id].len, sound[id].rate);
  }
}

// 8-bit unsigned PCM is centered at 128 (silence). Re-center to signed
// before scaling so we actually use the I2S DAC's full swing instead of
// sitting on a DC offset - this alone is a big, "free" volume increase
// over just multiplying the raw byte. AUDIO_GAIN tops out around 256
// before you start clipping on full-scale samples (127 * 256 = 32512).

void play_i2s(const uint8_t *data, uint32_t len, uint32_t rate) {
  uint16_t myGen = playGeneration;
  if (!i2s.begin(rate)) {
    Serial.println("Failed to initialize I2S!");
    delay(500);
    i2s.end();
    return;
  }
  for (uint32_t i = 0; i < len; i++) {
    if (playGeneration != myGen) break; // a newer press cut this clip off
    int16_t centered = (int16_t)data[i] - 128;
    int16_t sample = centered * AUDIO_GAIN;
    i2s.write(sample); // left
    i2s.write(sample); // right
  }
  i2s.end();
}

// ================= Game logic / LEDs / inputs (core0) =================

void setup() {
  Serial.begin(115200);

  pinMode(PIN_EXTERNAL_POWER, OUTPUT);
  digitalWrite(PIN_EXTERNAL_POWER, HIGH); // external rail stays on

  pinMode(PIN_EXTERNAL_BUTTON, INPUT_PULLUP); // trigger
  pinMode(PIN_MODE_BUTTON, INPUT_PULLUP);     // mode/swap

  mainStrip.begin();
  mainStrip.show();

  barrelLED.begin();
  barrelLED.show();

  if (!lis.begin(0x18)) {  // try 0x19 if your board is wired to the alt address
    Serial.println("Couldn't start LIS3DH - check wiring/address");
    // Not fatal: gyro is report-only in this build, so don't block the prop.
  } else {
    lis.setRange(LIS3DH_RANGE_2_G);
    Serial.println("LIS3DH online");
  }
}

void loop() {
  uint32_t now = millis();

  // While the barrel LED is flashing, skip the main strip's render entirely
  // (freezes its last color rather than calling show() on it) - this rules
  // out any contention between the two strips' show() calls as a cause of
  // the dim/off-white flash.
  if (!barrelFlashActive) {
    updateBreathingColor(now);
  }
  handleTrigger(now);
  handleModeButton(now);
  monitorBlastFlash(now);
  updateBarrelFlash(now);
  updateFlashBurst(now);
  reportAccelerometer(now);
}

// ---------------- Main strip: breathing + color cycle ----------------
// All the actual math now lives in PSOEffects.h - this just hands it
// this prop's strip, the clock, and which mode we're on.
void updateBreathingColor(uint32_t now) {
  psoUpdateBreathingColor(mainStrip, now, currentMode);
}

// ---------------- Barrel LED: muzzle flash ----------------
// Takes `now` from the caller rather than calling millis() itself - using
// one consistent timestamp per loop() iteration avoids a race where this
// reads a later millis() tick than updateBarrelFlash() checks against
// later in the same pass (which could underflow the elapsed-time math and
// immediately snuff the flash it just lit).
void flashBarrel(uint32_t now) {
  barrelFlashActive  = true;
  barrelFlashStartMs = now;
  barrelLED.setPixelColor(0, barrelLED.Color(255, 255, 255));
  barrelLED.show();
}

void updateBarrelFlash(uint32_t now) {
  if (barrelFlashActive && (now - barrelFlashStartMs >= BARREL_FLASH_MS)) {
    barrelFlashActive = false;
    barrelLED.setPixelColor(0, 0);
    barrelLED.show();
  }
}

// Starts a fresh flash burst every time a shot actually starts playing on
// core1. Chasing a monotonic counter (rather than matching a pending flag
// against a shared "currently playing" level) means it can't fire early
// against a stale, already-in-progress shot from earlier, and it can't
// miss a transition even if core0 happens to poll past it - it just
// catches up.
void monitorBlastFlash(uint32_t now) {
  static uint16_t lastHandledBlastCount = 0;
  while (lastHandledBlastCount != blastStartCount) {
    flashesRemaining   = SHOT_FLASH_COUNT; // (re)start the burst for this shot
    nextFlashAllowedMs = now;              // first flash of the burst fires right away
    lastHandledBlastCount++;
  }
}

// Steps through the current burst one flash at a time: fires the next
// flash once the previous one has fully turned off and the gap has
// elapsed. For Handgun (SHOT_FLASH_COUNT=1) this fires once and stops,
// identical to the old single-flash behavior.
void updateFlashBurst(uint32_t now) {
  if (flashesRemaining > 0 && !barrelFlashActive && now >= nextFlashAllowedMs) {
    flashBarrel(now);
    flashesRemaining--;
    nextFlashAllowedMs = now + BARREL_FLASH_MS + BARREL_FLASH_GAP_MS;
  }
}

// ---------------- Trigger: PIN_EXTERNAL_BUTTON ----------------
void handleTrigger(uint32_t now) {
  bool state = digitalRead(PIN_EXTERNAL_BUTTON); // LOW = pressed (INPUT_PULLUP)

  // Press edge
  if (triggerPrevState == HIGH && state == LOW) {
    triggerHeld         = true;
    triggerCharged       = false;
    triggerPressStartMs = now;
  }

  // Charge threshold crossed while still held - item #6
  if (triggerHeld && !triggerCharged &&
      (now - triggerPressStartMs >= CHARGE_HOLD_MS)) {
    triggerCharged = true;
    interruptAndClearQueue();
    queueSound(SND_POWERUP);
    queueSound(SHOT_SOUND);
  }

  // Release edge
  if (triggerPrevState == LOW && state == HIGH) {
    if (triggerHeld && !triggerCharged) {
      // Quick tap, released before the charge threshold - item #5
      interruptAndClearQueue();
      queueSound(SHOT_SOUND);
    }
    triggerHeld    = false;
    triggerCharged = false;
  }

  triggerPrevState = state;
}

// ---------------- Mode/swap button: pin 9 ----------------
void handleModeButton(uint32_t now) {
  bool raw = digitalRead(PIN_MODE_BUTTON);

  if (raw != modePrevState) {
    modeLastDebounceMs = now;
  }
  modePrevState = raw;

  if ((now - modeLastDebounceMs) > DEBOUNCE_MS) {
    if (raw != modeLatchedState) {
      modeLatchedState = raw;
      if (modeLatchedState == LOW) { // pressed
        interruptAndClearQueue();
        queueSound(SND_SWAP);
        currentMode = (currentMode + 1) % PSO_NUM_MODES;
        Serial.print("Color mode -> ");
        Serial.println(PSO_MODE_NAMES[currentMode]);
      }
    }
  }
}

// ---------------- Gyro/accelerometer: report-only ----------------
void reportAccelerometer(uint32_t now) {
  static uint32_t lastReportMs = 0;
  const uint16_t REPORT_INTERVAL_MS = 200; // 5x/sec, plenty for the serial monitor

  if (now - lastReportMs < REPORT_INTERVAL_MS) return;
  lastReportMs = now;

  sensors_event_t event;
  lis.getEvent(&event);

  // acceleration.{x,y,z} are in m/s^2. At rest, whichever axis is pointed
  // "down" will read ~9.8 due to gravity - that's the easiest way to see
  // how the sensor is oriented on the board. Tip/shake it while watching
  // this to get a feel for how fast values move and how noisy they are.
  Serial.print("Accel X: "); Serial.print(event.acceleration.x);
  Serial.print("  Y: ");     Serial.print(event.acceleration.y);
  Serial.print("  Z: ");     Serial.print(event.acceleration.z);
  Serial.println(" m/s^2");
}
