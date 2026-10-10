# PSO Handgun — RP2040 Prop-Maker

An Arduino prop project for the Adafruit Feather RP2040 Prop-Maker.

This prop combines NeoPixel lighting effects, trigger-controlled weapon sounds, charging behavior, mode controls, and accelerometer reporting.


The sketch uses the RP2040's two cores:

- **Core 0** handles buttons, NeoPixels, effects, and accelerometer reporting.
- **Core 1** services the PSOAudio queue and plays audio through I2S.

Sketch revision: **1.3.0**

## Features

- Power-on sound
- Eight-pixel breathing and color-cycle effect
- Single NeoPixel barrel/muzzle light
- Handgun, mech gun, and rifle weapon configurations
- Quick-trigger shot
- Hold-to-charge power-up followed by a shot
- Mode button with sound and color-mode changes
- Interruptible audio playback
- Audio-synchronized muzzle flashes
- LIS3DH accelerometer reporting over Serial (built into the Prop Maker - was using for test purposes)
- Shared audio and effects implemented in reusable libraries

> [!IMPORTANT]
> The RP2040 version requires both `setup1()` and `loop1()`. `loop1()` must continuously call `PSOAudio::serviceAudioQueue()`.

## Project structure

A suitable overall repository layout is:

```text
Project/
├── README.md
├── libraries/
│   ├── PSOAudio/
│   │   ├── README.md
│   │   ├── library.properties
│   │   └── src/
│   │       ├── PSOAudio.h
│   │       ├── PSOAudio.cpp
│   │       └── Sounds/
│   └── PSOEffects/
│       ├── README.md
│       ├── library.properties
│       └── src/
│           └── PSOEffects.h
└── props/
    └── propmaker_blaster_v1/
        ├── propmaker_blaster_v1.ino
        └── README.md
```

Custom libraries:

- [PSOAudio](../../libraries/PSOAudio/) — sound assets, queue handling, playback, and synchronization counters
- [PSOEffects](../../libraries/PSOEffects/) — NeoPixel breathing and color-cycle effects

## Hardware

### Required components

- Adafruit Feather RP2040 Prop-Maker
- Eight external NeoPixels on the main-strip connection
- One external NeoPixel for the barrel light
- Trigger button connected to the external button input
- Mode button
- 8Ω 2W speaker
- Prop-Maker wiring, power, and connections

### Pin assignments

| Function | Connection | Notes |
|---|---|---|
| Main NeoPixel strip | `PIN_EXTERNAL_NEOPIXELS` | Must contain eight pixels |
| Barrel NeoPixel | GPIO 10 | Defined by `PIN_BARREL_LED` |
| Mode button | GPIO 9 | Defined by `PIN_MODE_BUTTON`; active-low with internal pull-up |
| Trigger | `PIN_EXTERNAL_BUTTON` | Active-low with internal pull-up |
| External audio power | `PIN_EXTERNAL_POWER` | Board-defined pin; set active-high before playback |
| I2S bit clock | `PIN_I2S_BIT_CLOCK` | Defined by the selected board variant |
| I2S data | `PIN_I2S_DATA` | Defined by the selected board variant |
| Accelerometer | I2C | Address `0x18` |

`PIN_EXTERNAL_NEOPIXELS`, `PIN_EXTERNAL_BUTTON`, `PIN_EXTERNAL_POWER`, `PIN_I2S_BIT_CLOCK`, and `PIN_I2S_DATA` are supplied by the Prop-Maker board variant. Select the correct board in the Arduino IDE before compiling.

Buttons are configured as `INPUT_PULLUP`. Connect each button between its assigned input and ground. The input reads `LOW` while pressed.

All external devices must share a common ground. Provide sufficient external power for the NeoPixels and amplifier.

## Software requirements

Install the following libraries through the Arduino Library Manager:

- **Adafruit NeoPixel**
- **Adafruit LIS3DH**
- **Adafruit Unified Sensor**

The sketch also requires the project libraries:

- **PSOAudio**
- **PSOEffects**

The PSOAudio library must include all of its generated files under:

```text
PSOAudio/src/Sounds/
```

Do not install PSOAudio or PSOEffects with an extra nested directory. The required structure begins with:

```text
libraries/
├── PSOAudio/
│   ├── library.properties
│   └── src/
└── PSOEffects/
    ├── library.properties
    └── src/
```

## Installation

1. Install the Arduino IDE.
2. Install an RP2040 board package containing the **Adafruit Feather RP2040 Prop-Maker** board variant.
3. Install Adafruit NeoPixel, Adafruit LIS3DH, and Adafruit Unified Sensor through Library Manager.
4. Copy the complete `PSOAudio` and `PSOEffects` folders into the Arduino sketchbook libraries directory.
5. Open `propmaker_blaster_v1.ino`.
6. Select **Adafruit Feather RP2040 Prop-Maker** as the board.
7. Select the RP2040's USB serial port.
8. Compile and upload the sketch.
9. Open Serial Monitor at **115200 baud**.

A typical Windows sketchbook library path is:

```text
C:\Users\<username>\Documents\Arduino\libraries
```

## Controls and behavior

### Boot sequence

1. Core 0 configures the buttons, NeoPixels, and LIS3DH.
2. Core 1 enables `PIN_EXTERNAL_POWER`.
3. Core 1 initializes PSOAudio using the Prop-Maker I2S pins.
4. The sketch waits 100 ms for the amplifier and filtering circuitry to settle.
5. `SND_POWER_ON` begins on core 1.

Keep the amplifier-enable operation and settling delay in `setup1()` before starting the boot clip.

### Trigger behavior

The trigger is active-low.

#### Quick tap

A press shorter than 500 ms is recognized when the button is released. The selected shot sound starts, and the corresponding muzzle-flash burst begins when the audio clip actually starts.

#### Hold to charge

Holding the trigger for at least 500 ms:

1. Stops current audio and clears its pending queue.
2. Queues the power-up sound.
3. Queues the selected weapon shot.
4. Plays both clips sequentially.

The shot flash begins only after the queued shot clip starts.

### Mode button

Pressing the mode button:

1. Stops current audio and clears the pending queue.
2. Starts the swap sound.
3. Advances to the next NeoPixel color mode.
4. Prints the new mode name to Serial Monitor.

The mode button uses a 30 ms debounce interval.

### Main NeoPixel strip

The eight-pixel main strip runs a breathing color-cycle effect provided by PSOEffects. Main-strip rendering pauses briefly while the barrel flash is active.

### Barrel NeoPixel

The barrel pixel flashes white for 200 ms per shot. Consecutive flashes have a 50 ms gap after the previous flash turns off.

The flash is synchronized to `PSOAudio::startCount()`, not merely to the moment a sound is placed in the queue. This prevents the light from starting if a queued clip is interrupted or never begins.

### Accelerometer

The LIS3DH is report-only in this revision. Its acceleration values are printed every 200 ms but do not control any effects.

## Weapon configuration

Select the weapon at compile time near the top of the sketch:

```cpp
#define WEAPON_HANDGUN 0
#define WEAPON_MECHGUN 1
#define WEAPON_RIFLE   2

#define WEAPON_TYPE WEAPON_HANDGUN
```

Available configurations:

| Configuration | Shot sound | Muzzle flashes |
|---|---|---:|
| `WEAPON_HANDGUN` | `PSOAudio::SND_BLAST` | 1 |
| `WEAPON_MECHGUN` | `PSOAudio::SND_MECH_BLAST` | 3 |
| `WEAPON_RIFLE` | `PSOAudio::SND_RIFLE` | 1 |

Changing `WEAPON_TYPE` and recompiling selects the shot sound and number of flashes.

The weapon setting is not changed at runtime.

## Dual-core architecture

```text
Core 0                              Core 1
------                              ------
setup()                             setup1()
  Serial                              External audio power
  Buttons                             PSOAudio::begin()
  NeoPixels                           Power-on clip
  LIS3DH

loop()                               loop1()
  Trigger                             PSOAudio::serviceAudioQueue()
  Mode button                           Pop queued sound
  Main-strip effects                    Play through I2S
  Muzzle flashes
  Accelerometer report
          \                           /
           \--- PSOAudio queue ------/
                    |
              Pico SDK mutex
```

Core 0 may call the PSOAudio queue API while core 1 is playing audio. PSOAudio protects its shared queue with a Pico SDK mutex.

Do not:

- Call `PSOAudio::serviceAudioQueue()` from both cores
- Remove or block `loop1()`
- Call PSOAudio queue functions from an interrupt handler
- Replace the PSOAudio mutex with unsupported `__atomic_*`, `LDREX`/`STREX`, or `spin_lock_unsafe*` calls
- Place long delays in `loop1()`

`setup()` and `setup1()` may run concurrently, so both functions set `PIN_EXTERNAL_POWER` to its active level.

## Audio synchronization

PSOAudio increments a playback-start counter only after a clip has genuinely started. The sketch monitors the selected shot's counter:

```cpp
uint16_t currentShotStart =
  PSOAudio::startCount(SHOT_SOUND);
```

When this value changes, the sketch arms a new muzzle-flash burst.

This approach ensures that:

- Queueing a shot does not immediately start its flash
- A power-up clip can finish before the shot flash begins
- An interrupted stale clip does not trigger a flash
- Visual and audio events remain synchronized

## Customization

### LED count

The main strip is configured for eight pixels:

```cpp
#define MAIN_PIXEL_COUNT 8
```

Change this value if using a different number of pixels.

### Flash timing

```cpp
const uint16_t BARREL_FLASH_MS = 200;
const uint16_t BARREL_FLASH_GAP_MS = 50;
```

`BARREL_FLASH_MS` controls how long each flash remains on. `BARREL_FLASH_GAP_MS` controls the delay after a flash turns off before the next flash may begin.

### Trigger threshold

```cpp
const uint16_t CHARGE_HOLD_MS = 500;
```

Change this to alter how long the trigger must be held before the charge sequence starts.

### Button debounce

```cpp
const uint16_t DEBOUNCE_MS = 30;
```

This value applies to the mode button.

### Initial color mode

```cpp
uint8_t currentMode = 0;
```

Mode zero is selected on boot. Additional modes are defined by PSOEffects through `PSO_NUM_MODES` and `PSO_MODE_NAMES`.

### Accelerometer reporting

```cpp
const uint16_t REPORT_INTERVAL_MS = 200;
```

This controls how frequently acceleration values are printed to Serial Monitor.

## Serial Monitor

Open Serial Monitor at 115200 baud.

Expected output includes:

```text
LIS3DH online
Color mode -> <mode name>
Accel X: ...  Y: ...  Z: ... m/s^2
```

If the accelerometer does not initialize:

```text
Couldn't start LIS3DH - check wiring/address
```

The example expects the LIS3DH I2C address to be `0x18`.

## Testing checklist

After uploading, verify the following:

1. The power-on sound plays after startup.
2. The eight-pixel main strip displays a breathing color effect.
3. A short trigger press produces one shot sound and muzzle flash.
4. Holding the trigger for 500 ms plays the power-up and then the shot.
5. The mech gun configuration produces three flashes.
6. The rifle configuration uses the rifle sound.
7. The mode button plays the swap sound and changes the main-strip mode.
8. Serial Monitor displays acceleration values every 200 ms.
9. Pressing the mode button or starting another charge action interrupts current audio correctly.

## Troubleshooting

### The sketch compiles but freezes

Confirm that:

- `setup1()` and `loop1()` are present
- `loop1()` continuously calls `PSOAudio::serviceAudioQueue()`
- PSOAudio uses the Pico SDK mutex
- The current PSOAudio source no longer contains the failed `__atomic_*`, `LDREX`/`STREX`, or `spin_lock_unsafe*` implementation
- Only one copy of PSOAudio is installed
- No PSOAudio queue API is called from an ISR

The RP2040 lock implementation should use:

```cpp
#include <pico/mutex.h>

mutex_enter_blocking(&queueMutex);
mutex_exit(&queueMutex);
```

### No sound on boot

Check the following:

1. The correct Prop-Maker board variant is selected.
2. `PSOAudio::begin()` is called in `setup1()`.
3. `PIN_EXTERNAL_POWER` is set `HIGH` before playback.
4. The 100 ms amplifier settling delay remains in `setup1()`.
5. `loop1()` continuously services the audio queue.
6. The selected sound header has valid sample data and metadata.

A useful diagnostic is:

```cpp
Serial.println(
  PSOAudio::startCount(PSOAudio::SND_POWER_ON)
);
```

If the counter does not increase, the boot clip has not started. If it increases but no sound is heard, check the amplifier, I2S wiring, power, grounding, and volume.

### Trigger freezes but boot sound works

Make sure the installed `PSOAudio.cpp` is the version using the Pico SDK mutex. Reinstall the complete PSOAudio library, restart the Arduino IDE, and compile again.

### Barrel LED does not flash

The flash is tied to the audio playback-start counter. Confirm that the selected shot sound is actually beginning.

Also verify:

- The barrel pixel is connected to GPIO 10
- The pixel has adequate power
- The pixel and board share a common ground
- `SHOT_FLASH_COUNT` is greater than zero

### No main-strip animation

Check that:

- `MAIN_PIXEL_COUNT` matches the installed strip
- The strip is connected to `PIN_EXTERNAL_NEOPIXELS`
- The external NeoPixels have adequate power
- PSOEffects is installed and included

### Mode button does nothing

The mode button uses an internal pull-up and must connect GPIO 9 to ground. Check the debounce setting and confirm that GPIO 9 is not being used by another peripheral.

### Arduino continues compiling old library code

After updating a library:

1. Save all source files.
2. Close the Arduino IDE.
3. Reopen the IDE.
4. Compile again.
5. Delete the generated build-cache directory shown in the compiler output if necessary.

Also check for duplicate PSOAudio installations in the active sketchbook libraries directory.

## Revision history

| Revision | Changes |
|---|---|
| 1.3.0 | Moved sound assets, audio queue, and I2S playback into PSOAudio |
| 1.2.1 | Combined sound and LED effects into PSOEffects |
| 1.2.0 | Added rifle weapon support and `rifleBlast.h` |
| 1.1.0 | Added `WEAPON_TYPE`, corrected the muzzle-flash race, added interruptible audio, and disabled automatic sound stacking |
| 1.0.0 | Initial release |

## License

MIT
