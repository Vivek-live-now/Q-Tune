# 007 Q-Tune

A portable, standalone music player built on the ESP32-S3 SuperMini board, inheriting proven pinout, power, and board layout assumptions from the Q-Watch architecture.

## Overview

Q-Tune provides reliable, low-latency audio playback from an SPI microSD card through an I²S DAC/amplifier (MAX98357A) driving a speaker, paired with an interactive 1.3" SPI OLED display, 3 debounced control buttons, battery voltage monitoring, WS2812 RGB status LED, and INMP441 MEMS microphone audio spectrum visualizers.

---

## Hardware Architecture & Pin Allocation Matrix

The GPIO map for Q-Tune preserves Q-Watch's pin research while reallocating unneeded watch peripherals (IR TX/RX, Buzzer) to audio, microphone, and SD card interfaces.

### 1. Shared SPI Bus (OLED Display + microSD Card)
| Peripheral | Signal | GPIO | Notes |
| :--- | :--- | :--- | :--- |
| OLED / SD | MOSI (DIN) | 5 | Preserved from Q-Watch (Header Pin) |
| OLED / SD | SCK (CLK) | 7 | Preserved from Q-Watch (Header Pin) |
| SD Card | MISO (DATA_OUT) | 6 | Reclaimed from Q-Watch Buzzer (Header Pin) |
| OLED | CS | 4 | Preserved from Q-Watch (Header Pin) |
| OLED | DC | 2 | Preserved from Q-Watch (Header Pin) |
| OLED | RST | 41 | Adapted from Q-Watch (Standard digital output) |
| microSD | CS | 8 | Clean outer header pin (freed by OLED_RST moving to 41) |

### 2. I²S Audio Output (MAX98357A) & INMP441 MEMS Microphone
| Peripheral | Signal | GPIO | Notes |
| :--- | :--- | :--- | :--- |
| Audio Output / Mic | BCLK (Continuous SCK) | 17 | Reclaimed from Q-Watch IR RX (Underside Pad) |
| Audio Output / Mic | LRCLK / WS (Word Select) | 18 | Reclaimed from Q-Watch IR TX (Underside Pad) |
| MAX98357A DAC | DOUT (Serial Data Output) | 10 | Clean outer header pin (replaces old GPIO 42 conflict) |
| INMP441 Mic | DIN (Serial Data Input) | 15 | Reallocated from freed I2C port (replaces GPIO 44) |

#### INMP441 Microphone Pin Connections
| INMP441 Pin | ESP32-S3 SuperMini Pin | Description & Critical Notes |
| :--- | :--- | :--- |
| **VDD** | 3.3V | **3.3V Power Only** (Do NOT connect to 5V; absolute maximum is 3.6V). |
| **GND** | GND | Ground reference. |
| **SD** | GPIO 15 | Serial Data Out from mic to ESP32 DIN (allotted from freed I2C port). |
| **SCK** | GPIO 17 | Continuous Bit Clock (shared with MAX98357A BCLK). |
| **WS** | GPIO 18 | Word Select / LRCLK (shared with MAX98357A LRCK). |
| **L/R** | GND (or 3.3V) | **Mandatory connection**: Tie to GND for Left channel or 3.3V for Right channel. Never leave floating. |

#### MAX98357A SD_MODE & MCLK Configuration
- **MCLK**: Not required by MAX98357A (uses internal PLL).
- **SD_MODE**: Connected in hardware with a 100kΩ pull-up resistor to VDD. This configures the DAC to compute a **(Left + Right) / 2 mono mix**, making it ideal for driving a single speaker from stereo or mono source files without consuming an extra GPIO pin.

### 3. Freed I2C Bus & Digital Expansion
- **GPIO 15:** Reallocated as `I2S_MIC_DIN` for the INMP441 microphone.
- **GPIO 16:** Clean digital expansion / reserve pin.
- **Status:** I2C port freed completely.

### 4. Navigation Inputs (Adapted from Q-Watch 4-Button Architecture)
| Button | GPIO | Logic | Notes |
| :--- | :--- | :--- | :--- |
| Button Up | 39 | INPUT_PULLUP (Active LOW) | K1 Directional UP (Preserved from Q-Watch) |
| Button OK / Select | 40 | INPUT_PULLUP (Active LOW) | K1 Directional OK / SELECT (Adapted from Q-Watch) |
| Button Down | 42 | INPUT_PULLUP (Active LOW) | K1 Directional DOWN (Adapted from Q-Watch) |
| Button Cancel / Back | 21 | INPUT_PULLUP (Active LOW) | Tactile CANCEL / RTC Deep Sleep Wake (Adapted from Q-Watch) |

### 5. Power & Diagnostics
| Function | GPIO | Hardware Circuit |
| :--- | :--- | :--- |
| Battery Monitor | 1 (ADC1_CH0) | 100kΩ / 100kΩ external divider from LiPo VBAT + 104 filter cap |
| Onboard RGB LED | 48 | Built-in WS2812 RGB LED |

### 6. Reserved & Avoided Pins
- **Hardware UART0 Debugging / Flashing:** GPIO 43 (TX), GPIO 44 (RX) — **Completely freed** from all peripherals for clean native serial debugging.
- **System / Boot Strapping Pins (STRICTLY AVOIDED):** GPIO 0, 3, 45, 46.

---

## Decoupling & Driver Strategy

- **MAX98357A Load:** Place a `100nF (104)` ceramic capacitor in parallel with a `100µF` bulk electrolytic capacitor directly across the power rails (`3.3V`/`5V` and `GND`) of the MAX98357A driver board to absorb current surges during peak audio output.
- **Battery Filter:** The battery voltage divider on GPIO 1 includes a 100nF ceramic capacitor across the lower 100kΩ resistor to filter out noise.

---

## File System & Audio Format Requirements

- **Directory:** `/music/`
- **Supported Formats:** Extensible `AudioDecoder` framework for WAV, MP3, and FLAC files.
- **Sample Track Path:** `/music/song.wav`

---

## Operating Modes & Application Shell

On startup, Q-Tune launches into the **Unified Home Menu**, providing instant access to all core applications:

1. **Music Player**: Full standalone playback engine with track listing, status, progress bar, digital volume, and repeat/shuffle modes.
2. **Spectrum Visualizer**: Real-time 16-band FFT audio visualizer (Bar Spectrum, Waveform, MilkDrop Plasma, Starfield) powered by INMP441 MEMS mic.
3. **Hardware Diagnostics**: 10-point interactive diagnostic suite with return-to-menu navigation.
4. **System Info**: Live VBAT voltage, battery %, CPU frequency profile, volume %, and SD mount status.

---

## Navigation & Controls Matrix

| Context | Button | Action |
| :--- | :--- | :--- |
| **Main Menu** | UP / DOWN (Short) | Move cursor |
| **Main Menu** | OK / SEL (Short) | Launch selected mode |
| **Main Menu** | CANCEL (Long) | Screen off / sleep |
| **Track List** | UP / DOWN (Short) | Browse files in `/music/` |
| **Track List** | OK / SEL (Short) | Play selected track |
| **Track List** | CANCEL (Short) | Return to Main Menu |
| **Now Playing** | OK / SEL (Short) | Toggle Play / Pause |
| **Now Playing** | OK / SEL (Hold >450ms) | Cycle Playback Mode (`[ALL]` → `[R-1]` → `[SHF]` → `[SGL]`) |
| **Now Playing** | UP / DOWN (Short) | Previous / Next track |
| **Now Playing** | UP / DOWN (Hold >450ms) | Digital Volume Up / Down (with on-screen overlay) |
| **Now Playing** | CANCEL (Short) | Return to Track List |
| **Any Screen** | CANCEL (Hold >450ms) | Global escape to Main Menu |

---

## FreeRTOS Dual-Core Audio Architecture

Q-Tune leverages the ESP32-S3 dual-core LX7 processor to guarantee stutter-free audio output:
- **Core 0 (Audio Engine Task)**: Dedicated FreeRTOS background task continuously streams audio samples to I2S DMA with zero starvation.
- **Core 1 (UI & Peripherals)**: Handles OLED rendering, button debouncing, LED animations, and battery monitoring.
- **Multi-Core SPI Mutex**: Thread-safe hardware arbitration (`spiBusMutex`) prevents bus contention between the SPI microSD card and SPI OLED display.
- **Digital Volume Scaling**: Non-linear quadratic perceptual volume curve (`(vol / 100)^2 * 256`) applied using fast fixed-point arithmetic.

---

## Interactive Diagnostic Menu

The diagnostics suite verifies all physical subsystems:
1. **OLED Display** - Border & text rendering
2. **SD Card Detect** - Bus communication check
3. **SD Filesystem** - Total & used capacity report
4. **WAV Discovery** - `/music/` folder listing
5. **Button Inputs** - Debounced button state check
6. **Pin Matrix Map** - Live hardware GPIO allocation and verification display
7. **Battery ADC** - Live voltage & percentage display
8. **RGB LED Test** - WS2812 color cycle
9. **I2S Audio Test** - Synthesized 1kHz test tone through MAX98357A
10. **INMP441 Mic Visualizer** - Audio spectrum and MilkDrop-style visualizer presets
11. **Return to Menu** - Clean exit to the unified home shell

---

## Getting Started

1. Open the project in PlatformIO.
2. Build and flash the project to an ESP32-S3 SuperMini:
   ```bash
   pio run --target upload
   ```
3. Format a microSD card (FAT32), create a `/music/` folder, and copy 16-bit 44.1kHz WAV, MP3, or FLAC files into it.
4. Insert the card into Q-Tune and power on.
