# PSO Blaster — Adafruit Feather RP2040 Prop-Maker

Arduino firmware for a prop-maker blaster using the Adafruit Feather RP2040 Prop-Maker.

- **Author:** AW Labs
- **Firmware revision:** 1.2.1
- **Platform:** Adafruit Feather RP2040 Prop-Maker
- **Audio architecture:** RP2040 dual core
- **Supported profiles:** Handgun, Mechgun, and Rifle

> **Note:** The supplied sketch is not self-contained. Building it requires a custom `PSOEffects` library and six generated sound headers. See [Required project files](#required-project-files).

## Overview

The prop combines an eight-pixel NeoPixel strand, a single barrel NeoPixel, an I2S audio amplifier, two buttons, and a report-only LIS3DH accelerometer.

Core 0 handles buttons, LEDs, color modes, and gameplay state. Core 1 handles the I2S audio queue. A monotonic blast-start counter is used to synchronize the muzzle flash with the moment a shot sound actually begins playing.

## Features

- Power-on chime
- Eight-pixel breathing and color-cycle effect
- Single-pixel white muzzle flash
- Handgun, Mechgun, and Rifle sound profiles
- One- or three-flash muzzle bursts
- Tap-fire and charged-shot behavior
- Mode/swap button with audio playback
- Accent-color mode switching
- Immediate interruption and queue clearing on new input
- I2S audio synchronized with muzzle effects
- LIS3DH accelerometer reporting over the serial monitor
- Software debouncing for the mode button

## Weapon profiles

Select a profile by changing `WEAPON_TYPE` near the top of the sketch:

```cpp
#define WEAPON_TYPE WEAPON_HANDGUN
```

| Setting | Sound | Muzzle flashes |
|---|---|---:|
| `WEAPON_HANDGUN` | `blast.h` | 1 |
| `WEAPON_MECHGUN` | `mechBlast.h` | 3 |
| `WEAPON_RIFLE` | `rifleBlast.h` | 1 |

All profiles use `powerup.h` for a charged shot. The profile is selected at compile time; it is not changed at runtime.

## Hardware

### Required components

- Adafruit Feather RP2040 Prop-Maker
- 8-pixel GRB NeoPixel strand
- 1-pixel GRB NeoPixel for the muzzle
- LIS3DH accelerometer
- I2S-compatible audio amplifier connected to the Prop-Maker audio interface
- Trigger button connected to `PIN_EXTERNAL_BUTTON`
- Mode/swap button connected to GPIO 9
- Suitable regulated power for the NeoPixels
- Common ground between the Feather, LEDs, and audio hardware

### Connections

| Function | Connection |
|---|---|
| Main NeoPixel data | `PIN_EXTERNAL_NEOPIXELS` from the Prop-Maker board variant |
| Barrel NeoPixel data | GPIO 10 |
| Mode button | GPIO 9 and ground |
| Trigger | `PIN_EXTERNAL_BUTTON` and ground |
| External power rail | `PIN_EXTERNAL_POWER`; enabled by firmware |
| Audio clock/data | `PIN_I2S_BIT_CLOCK` and `PIN_I2S_DATA` |
| Accelerometer | Prop-Maker I2C bus, normally at address `0x18` |

Both buttons use `INPUT_PULLUP`, so an unpressed button reads `HIGH` and a pressed button reads `LOW`.

The firmware currently attempts to initialize the LIS3DH only at I2C address `0x18`. If the sensor is configured for `0x19`, change the address passed to `lis.begin()`.

## Required project files

A suggested repository layout is:

```text
.
├── README.md
└── firmware/
    └── propmaker_blaster_v1/
        ├── propmaker_blaster_v1.ino
        ├── PSOEffects.h
        └── Sounds/
            ├── power_sound.h
            ├── swap_sound.h
            ├── blast.h
            ├── powerup.h
            ├── mechBlast.h
            └── rifleBlast.h
```

The custom headers may instead be packaged as an Arduino library, but they must be available on the compiler's include path.

### Custom effects library

`PSOEffects.h` must provide:

- `psoUpdateBreathingColor(...)`
- `PSO_NUM_MODES`
- `PSO_MODE_NAMES`

The effect library owns the color palettes, breathing calculation, and mode names.

### Generated sound headers

The sketch expects each audio header to define the following symbols:

| Header | Required symbols |
|---|---|
| `power_sound.h` | `POWER_SOUND`, `POWER_SOUND_LENGTH`, `POWER_SOUND_SAMPLE_RATE` |
| `swap_sound.h` | `SWAP_SOUND`, `SWAP_SOUND_LENGTH`, `SWAP_SOUND_SAMPLE_RATE` |
| `blast.h` | `BLAST_SOUND`, `BLAST_SOUND_LENGTH`, `BLAST_SOUND_SAMPLE_RATE` |
| `powerup.h` | `POWERUP_SOUND`, `POWERUP_SOUND_LENGTH`, `POWERUP_SOUND_SAMPLE_RATE` |
| `mechBlast.h` | `MECHBLAST_SOUND`, `MECHBLAST_SOUND_LENGTH`, `MECHBLAST_SAMPLE_RATE` |
| `rifleBlast.h` | `RIFLEBLAST_SOUND`, `RIFLEBLAST_LENGTH`, `RIFLEBLAST_SAMPLE_RATE` |

If the WAV conversion script generates different names, update the include or `sound[]` entries in the sketch accordingly. Header names are case-sensitive on Linux and macOS build environments.

## Audio requirements

The playback code assumes that each generated audio array contains:

- 8-bit unsigned PCM
- Mono audio
- Unsigned silence centered at `128`
- A matching sample-rate constant
- A length measured in sample bytes

During playback, the firmware converts each unsigned byte to signed 16-bit I2S samples:

```cpp
int16_t centered = static_cast<int16_t>(data[i]) - 128;
```

Do not supply signed 8-bit audio without changing this conversion. Signed audio would subtract another `128`, introducing severe clipping or DC offset.

The same mono sample is written to both the left and right channels.

### Volume

```cpp
const int16_t AUDIO_GAIN = 25;
```

`25` is a conservative starting value. The theoretical maximum before clipping is approximately `256`. Increase the gain in small steps while monitoring the amplifier for distortion.

## Required Arduino support

Install:

1. Arduino IDE 2.x or an equivalent Arduino build environment
2. Adafruit's RP2040 board package
3. Adafruit NeoPixel
4. Adafruit LIS3DH
5. Adafruit Unified Sensor
6. The I2S library/API used by the selected RP2040 core and Prop-Maker reference sketch
7. The custom `PSOEffects` library (see PSOEffects repo)
8. All six generated sound headers

The selected board package must support the RP2040 `setup1()` and `loop1()` second-core functions and the Prop-Maker variant pin definitions.

## Build and upload

1. Connect and power the Prop-Maker.
2. Open `propmaker_blaster_v1.ino` in Arduino IDE.
3. Select the **Adafruit Feather RP2040 Prop-Maker** board.
4. Select the correct serial port.
5. Confirm that all custom headers and libraries are installed.
6. Set `WEAPON_TYPE` to the desired profile.
7. Compile the sketch.
8. Upload the firmware.
9. Open the serial monitor at **115200 baud**.

The board-specific macros below must be provided by the selected Prop-Maker board variant:

```cpp
PIN_EXTERNAL_NEOPIXELS
PIN_EXTERNAL_BUTTON
PIN_EXTERNAL_POWER
PIN_I2S_BIT_CLOCK
PIN_I2S_DATA
```

If these are undefined, the wrong board package or board variant is selected.

## Controls

### Power-on

After boot, the firmware:

1. Enables the external power rail
2. Starts the power-on chime
3. Initializes the LEDs and accelerometer
4. Begins the breathing color effect

### Trigger

A short trigger press:

- Interrupts the currently playing sound
- Clears all queued sounds
- Starts the selected weapon sound
- Flashes the barrel when the shot clip actually begins

Holding the trigger for `CHARGE_HOLD_MS` queues `powerup.h`, followed by the selected weapon sound. The charged shot starts at the charge threshold; it does not wait for release.

Pressing the trigger or mode button during playback immediately advances the playback generation, causing the current audio clip to stop.

### Mode button

Pressing the mode button:

- Interrupts the current sound
- Clears the audio queue
- Plays `swap_sound.h`
- Advances to the next color mode
- Prints the selected mode name to the serial monitor

The selected color mode resets when the device restarts.

## LED timing

Default muzzle-flash settings are:

```cpp
const uint16_t BARREL_FLASH_MS = 200;
const uint16_t BARREL_FLASH_GAP_MS = 50;
```

Each flash remains lit for 200 ms. Subsequent flashes begin after 50 ms of darkness. A three-flash Mechgun burst therefore lasts approximately 700 ms from the start of the first flash until the end of the final flash.

The main strip rendering is paused while the barrel pixel is lit. It can continue rendering during the dark gap between flashes.

## Audio and core architecture

### Core 0: controls and effects

Core 0 is responsible for:

- Reading the trigger and mode button
- Updating the color mode
- Rendering the main NeoPixel strip
- Scheduling muzzle flashes
- Monitoring the accelerometer
- Producing audio queue events

### Core 1: I2S playback

Core 1 is responsible for:

- Consuming sound IDs from the circular queue
- Starting the I2S interface
- Converting unsigned 8-bit samples to signed 16-bit samples
- Writing mono audio to both channels
- Stopping playback when a new input interrupts it

The queue contains eight slots, with one slot reserved to distinguish full from empty, allowing seven queued entries.

### Shot synchronization

`blastStartCount` is incremented by core 1 immediately before a new shot clip begins. Core 0 watches this counter and starts the muzzle-flash sequence only when the sound genuinely starts. This prevents the LED from firing for a sound that was queued and then interrupted before playback.

### Playback interruption

`playGeneration` is incremented whenever the trigger or mode button requests an interruption. Core 1 checks this value while processing samples and stops the current clip as soon as it changes.

## Configuration reference

| Setting | Default | Purpose |
|---|---:|---|
| `WEAPON_TYPE` | `WEAPON_HANDGUN` | Selects the weapon profile |
| `PIN_MODE_BUTTON` | `9` | Mode/swap button GPIO |
| `PIN_BARREL_LED` | `10` | Barrel NeoPixel GPIO |
| `MAIN_PIXEL_COUNT` | `8` | Main strip pixel count |
| `AUDIO_GAIN` | `25` | I2S sample gain |
| `BARREL_FLASH_MS` | `200` | Duration of each flash |
| `BARREL_FLASH_GAP_MS` | `50` | Dark gap between flashes |
| `CHARGE_HOLD_MS` | `500` | Hold time for a charged shot |
| `DEBOUNCE_MS` | `30` | Mode-button debounce interval |

Pixel order and timing are configured as:

```cpp
NEO_GRB + NEO_KHZ800
```

If a strip uses a different pixel order or protocol, update both `Adafruit_NeoPixel` constructors.

## Serial output

The serial monitor runs at 115200 baud. Example output includes:

```text
LIS3DH online
Color mode -> <mode name>
Accel X: 0.12  Y: 9.74  Z: -0.35 m/s^2
```

Acceleration values are reported in `m/s²`. The accelerometer is diagnostic only and does not currently alter gameplay or LED behavior.

## Troubleshooting

### Compilation fails on missing board pins

If `PIN_EXTERNAL_NEOPIXELS`, `PIN_EXTERNAL_BUTTON`, or the I2S pins are undefined, select the Adafruit Feather RP2040 Prop-Maker board and install the matching board package.

### Missing header or sound symbol errors

Confirm that:

- `PSOEffects.h` is visible to the compiler
- All files under `Sounds/` are present
- The generated symbol names match the `sound[]` table
- Header capitalization matches the filenames

### LEDs are dim or unstable

- Use a power supply sized for the NeoPixels
- Confirm that the data pin reaches the first pixel's DIN connection
- Verify common ground
- Check the 800 kHz and GRB settings
- Do not power a larger strip through an unspecified GPIO

### Barrel LED flashes but there is no sound

Check:

- I2S BCLK and DATA wiring
- Amplifier power and mute/enable pin
- Common ground
- Audio sample format
- `AUDIO_GAIN`
- Whether the audio header contains unsigned 8-bit PCM

### Sound plays but the barrel does not flash

Confirm that:

- The barrel pixel is connected to GPIO 10
- The selected sound is compiled as `SHOT_SOUND`
- The audio clip is long enough for core 0 to observe `blastStartCount`
- The LED library configuration matches the pixel

### Accelerometer is not detected

The current code only checks address `0x18`. Change the `lis.begin()` argument to `0x19` if the board is configured for the alternate address.

### One trigger press produces multiple shots

The trigger uses edge detection but does not have software debouncing. Check for contact bounce, a noisy connection, or an unsuitable mechanical switch.

## Revision history

### 1.2.1 — 2026-09-13

- Combined LED and sound effects into `PSOEffects`

### 1.2.0 — 2026-09-12

- Added Rifle weapon profile and `rifleBlast.h`

### 1.1.0 — 2026-09-11

- Added weapon-profile selection
- Added Mechgun profile
- Corrected muzzle-flash timing race
- Added playback interruption
- Disabled stacked sound playback

### 1.0.0 — 2026-09-07

- Initial handgun-prop release

## Credits

Firmware by AW Labs, based on Adafruit's Prop-Maker demo sketch for the board wiring and dual-core audio reference.

## License

No license was included with the supplied source. Add a `LICENSE` file before public distribution.
