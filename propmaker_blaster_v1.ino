// PSO Blaster (Adafruit Feather RP2040 Prop-Maker)
// Author: AW Labs
// Revision: 1.3.0
//
// Based on Adafruit's Prop-Maker demo sketch for wiring/pin reference
// (dual-core setup: core0 = game logic/LEDs/inputs, core1 = I2S audio).
//
// Revision History
//  1.3.0 - Moved sound assets, queue, and I2S playback into PSOAudio.
//
//  1.2.1 - Combined sound and LED effects into PSOEffects library.
//
//  1.2.0 - Added WEAPON_RIFLE and rifleBlast.h.
//
//  1.1.0 - Added WEAPON_TYPE toggle, corrected the muzzle-flash race,
//           added interruptible audio, and disabled sound stacking.
//
//  1.0.0 - Initial release.
//
// Behavior:
//  1. Power-on chime from power_sound.h
//  2. 8x NeoPixel breathing + color-cycle on PIN_EXTERNAL_NEOPIXELS
//  3. Barrel NeoPixel flashes for handgun/mechgun/rifle
//  4. Mode button cycles the main-strip color mode
//  5. Quick trigger press plays a shot
//  6. Holding the trigger queues powerup followed by the shot
//  7. LIS3DH accelerometer: read and print to Serial only
//  8. New trigger/mode events interrupt current audio and clear its queue

// ---------------- Weapon type toggle ----------------
#define WEAPON_HANDGUN 0
#define WEAPON_MECHGUN 1
#define WEAPON_RIFLE   2
#define WEAPON_TYPE WEAPON_HANDGUN

#include <Adafruit_NeoPixel.h>
#include <Adafruit_LIS3DH.h>
#include <Adafruit_Sensor.h>

#include <PSOEffects.h>
#include <PSOAudio.h>

// ---------------- Pin assignments ----------------
#define PIN_MODE_BUTTON   9
#define PIN_BARREL_LED    10

// PIN_EXTERNAL_NEOPIXELS, PIN_EXTERNAL_BUTTON, PIN_EXTERNAL_POWER,
// PIN_I2S_BIT_CLOCK, and PIN_I2S_DATA come from the Prop-Maker board
// variant definitions.

// ---------------- NeoPixel strips ----------------
#define MAIN_PIXEL_COUNT  8

Adafruit_NeoPixel mainStrip(
  MAIN_PIXEL_COUNT,
  PIN_EXTERNAL_NEOPIXELS,
  NEO_GRB + NEO_KHZ800
);

Adafruit_NeoPixel barrelLED(
  1,
  PIN_BARREL_LED,
  NEO_GRB + NEO_KHZ800
);

// ---------------- Accelerometer (report-only) ----------------
Adafruit_LIS3DH lis = Adafruit_LIS3DH();

// ---------------- Color modes ----------------
uint8_t currentMode = 0;

// ---------------- Barrel flash state ----------------
bool     barrelFlashActive     = false;
uint32_t barrelFlashStartMs    = 0;
const uint16_t BARREL_FLASH_MS = 200;

// A shot consists of one or more flashes.
uint8_t  flashesRemaining      = 0;
uint32_t nextFlashAllowedMs    = 0;
const uint16_t BARREL_FLASH_GAP_MS = 50;

// ---------------- Trigger state ----------------
bool     triggerPrevState    = HIGH;
bool     triggerHeld         = false;
bool     triggerCharged      = false;
uint32_t triggerPressStartMs = 0;
const uint16_t CHARGE_HOLD_MS = 500;

// ---------------- Mode/swap button state ----------------
bool     modePrevState      = HIGH;
bool     modeLatchedState   = HIGH;
uint32_t modeLastDebounceMs = 0;
const uint16_t DEBOUNCE_MS  = 30;

// Which sound plays for a shot, and how many muzzle flashes accompany it.
#if WEAPON_TYPE == WEAPON_MECHGUN
  #define SHOT_SOUND       PSOAudio::SND_MECH_BLAST
  #define SHOT_FLASH_COUNT 3
#elif WEAPON_TYPE == WEAPON_RIFLE
  #define SHOT_SOUND       PSOAudio::SND_RIFLE
  #define SHOT_FLASH_COUNT 1
#else
  #define SHOT_SOUND       PSOAudio::SND_BLAST
  #define SHOT_FLASH_COUNT 1
#endif

// ================= Audio (runs entirely on core1) =================

void setup1(void) {
  // Enable the external audio circuit from core1 before starting the
  // boot clip. setup() also sets this pin, so repeating the operation
  // there is harmless.
  pinMode(PIN_EXTERNAL_POWER, OUTPUT);
  digitalWrite(PIN_EXTERNAL_POWER, HIGH);

  PSOAudio::Pins audioPins =
    PSOAudio::makeI2SPins(
      PIN_I2S_BIT_CLOCK,
      PIN_I2S_DATA
    );

  if (!PSOAudio::begin(audioPins)) {
    return;
  }

  // Allow the amplifier and its filtering circuitry to settle before
  // beginning the power-on clip.
  delay(100);

  PSOAudio::startClip(PSOAudio::SND_POWER_ON);
}


void loop1(void) {
  PSOAudio::serviceAudioQueue();
}

// ================= Game logic / LEDs / inputs (core0) =================

void setup() {
  Serial.begin(115200);

  pinMode(PIN_EXTERNAL_POWER, OUTPUT);
  digitalWrite(PIN_EXTERNAL_POWER, HIGH);

  pinMode(PIN_EXTERNAL_BUTTON, INPUT_PULLUP);
  pinMode(PIN_MODE_BUTTON, INPUT_PULLUP);

  mainStrip.begin();
  mainStrip.show();

  barrelLED.begin();
  barrelLED.show();

  if (!lis.begin(0x18)) {
    Serial.println(
      "Couldn't start LIS3DH - check wiring/address"
    );
  } else {
    lis.setRange(LIS3DH_RANGE_2_G);
    Serial.println("LIS3DH online");
  }
}

void loop() {
  uint32_t now = millis();

  // While the barrel LED is flashing, skip main-strip rendering.
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

void updateBreathingColor(uint32_t now) {
  psoUpdateBreathingColor(
    mainStrip,
    now,
    currentMode
  );
}

// ---------------- Barrel LED: muzzle flash ----------------

void flashBarrel(uint32_t now) {
  barrelFlashActive  = true;
  barrelFlashStartMs = now;

  barrelLED.setPixelColor(
    0,
    barrelLED.Color(255, 255, 255)
  );

  barrelLED.show();
}

void updateBarrelFlash(uint32_t now) {
  if (barrelFlashActive &&
      (now - barrelFlashStartMs >= BARREL_FLASH_MS)) {
    barrelFlashActive = false;

    barrelLED.setPixelColor(0, 0);
    barrelLED.show();
  }
}

// Starts a fresh flash burst when the shot clip actually starts on core1.
// Merely queueing the sound does not start a flash.
void monitorBlastFlash(uint32_t now) {
  static uint16_t lastHandledShotStart = 0;

  uint16_t currentShotStart =
    PSOAudio::startCount(SHOT_SOUND);

  while (lastHandledShotStart != currentShotStart) {
    flashesRemaining   = SHOT_FLASH_COUNT;
    nextFlashAllowedMs = now;

    lastHandledShotStart++;
  }
}

// Fires the next flash once the previous flash has turned off and the
// configured gap has elapsed.
void updateFlashBurst(uint32_t now) {
  if (flashesRemaining > 0 &&
      !barrelFlashActive &&
      now >= nextFlashAllowedMs) {

    flashBarrel(now);
    flashesRemaining--;

    nextFlashAllowedMs =
      now + BARREL_FLASH_MS + BARREL_FLASH_GAP_MS;
  }
}

// ---------------- Trigger ----------------

void handleTrigger(uint32_t now) {
  bool state = digitalRead(PIN_EXTERNAL_BUTTON);

  // Press edge.
  if (triggerPrevState == HIGH && state == LOW) {
    triggerHeld         = true;
    triggerCharged      = false;
    triggerPressStartMs = now;
  }

  // Charge threshold crossed while still held.
  if (triggerHeld &&
      !triggerCharged &&
      (now - triggerPressStartMs >= CHARGE_HOLD_MS)) {

    triggerCharged = true;

    // Preserve the original order:
    // powerup clip -> shot clip.
    PSOAudio::interruptAndClearQueue();
    PSOAudio::queueSound(PSOAudio::SND_POWERUP);
    PSOAudio::queueSound(SHOT_SOUND);
  }

  // Release edge.
  if (triggerPrevState == LOW && state == HIGH) {
    if (triggerHeld && !triggerCharged) {
      // Quick tap.
      PSOAudio::startClip(SHOT_SOUND);
    }

    triggerHeld    = false;
    triggerCharged = false;
  }

  triggerPrevState = state;
}

// ---------------- Mode/swap button ----------------

void handleModeButton(uint32_t now) {
  bool raw = digitalRead(PIN_MODE_BUTTON);

  if (raw != modePrevState) {
    modeLastDebounceMs = now;
  }

  modePrevState = raw;

  if ((now - modeLastDebounceMs) > DEBOUNCE_MS) {
    if (raw != modeLatchedState) {
      modeLatchedState = raw;

      if (modeLatchedState == LOW) {
        PSOAudio::startClip(PSOAudio::SND_SWAP);

        currentMode =
          (currentMode + 1) % PSO_NUM_MODES;

        Serial.print("Color mode -> ");
        Serial.println(PSO_MODE_NAMES[currentMode]);
      }
    }
  }
}

// ---------------- Gyro/accelerometer: report-only ----------------

void reportAccelerometer(uint32_t now) {
  static uint32_t lastReportMs = 0;
  const uint16_t REPORT_INTERVAL_MS = 200;

  if (now - lastReportMs < REPORT_INTERVAL_MS) {
    return;
  }

  lastReportMs = now;

  sensors_event_t event;
  lis.getEvent(&event);

  Serial.print("Accel X: ");
  Serial.print(event.acceleration.x);

  Serial.print("  Y: ");
  Serial.print(event.acceleration.y);

  Serial.print("  Z: ");
  Serial.print(event.acceleration.z);

  Serial.println(" m/s^2");
}