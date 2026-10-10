# Q-Tunes

A portable, standalone music player built on the ESP32-S3 SuperMini board, inheriting proven pinout, power, and board layout assumptions from the Q-Watch architecture.

## Overview

Q-Tunes provides reliable, low-latency audio playback (lossless WAV and FLAC) from an SPI microSD card through an I²S DAC/amplifier (MAX98357A) driving a speaker, paired with an interactive 1.3" SPI OLED display, debounced control buttons, battery voltage monitoring, WS2812 RGB status LED, and real-time audio spectrum visualizers (live audio stream decoding and INMP441 MEMS microphone).

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
- **Supported Formats:** Lossless WAV, FLAC, MP3 (powered by zero-stack heap scratch minimp3 engine), M4A, and AAC (powered by Helix fixed-point decoder).
- **SD Card Support:** Supports SDSC, SDHC, and SDXC cards (up to 64GB, 128GB, 256GB+). Strictly negotiates at <= 400kHz SPI clock per SDXC specification. Includes hardware-level direct SPI exFAT filesystem detection:
  - **Why FAT32 on ESP32:** The ESP32 hardware FatFs driver requires FAT32 for zero-copy DMA streaming and glitch-free audio playback. Modern SDXC cards (64GB+) are pre-formatted as exFAT by manufacturers.
  - **Formatting 64GB+ cards to FAT32:**
    1. **Windows:** Download free **GUIFormat** (`fat32format`) or **Rufus**. Select your SD card drive, choose **FAT32** with **32KB** (or 64KB) Allocation Unit Size, and perform a Quick Format. (Windows' built-in format utility limits FAT32 to 32GB; GUIFormat removes this restriction up to 2TB).
    2. **macOS:** Open Terminal: `diskutil eraseDisk FAT32 QTUNE MBRFormat /dev/diskX` (replace X with your SD card disk number).
    3. **Linux:** `sudo mkfs.vfat -F 32 -s 64 /dev/sdX1`
  - Create the `/music` folder at the root of the card and copy your tracks into it.
- **Sample Track Path:** `/music/song.wav`

---

## Operating Modes & Application Shell

On startup, Q-Tune launches into the **Unified Home Menu**, providing instant access to all core applications:

1. **Music Player**: Full standalone playback engine with track listing, status, live 8-band mini-equalizer HUD, progress bar, digital volume, and repeat/shuffle modes.
2. **Spectrum Visualizer**: Real-time live WAV stream visualizer (16-Band Spectrum with Peak-Hold caps, Oscilloscope Waveform, MilkDrop Plasma, Starfield) reacting directly to live decoded WAV audio playing from SD card.
3. **RGB Light Effects**: Dedicated music-reactive lighting engine with 6 real-time audio-driven modes, base color selection, brightness control, sensitivity tuning, and test pulse.
4. **Hardware Diagnostics**: 10-point interactive diagnostic suite (including dedicated INMP441 MEMS microphone hardware test) with return-to-menu navigation.
5. **System Info**: Live VBAT voltage, battery %, CPU frequency profile, volume %, and SD mount status.

---

## Music-Reactive RGB Lighting Engine

Q-Tune features an advanced audio-reactive lighting subsystem driving the onboard WS2812 NeoPixel (GPIO 48) at 50 FPS (~20ms), fed directly by the Core 0 lock-free PCM audio tap and 16-band FFT engine:

### 1. Music-Reactive Modes
- **BASS PULSE (`REACT_BASS_PULSE`)**: Taps sub-bass and kick drum energy (bands 0–2). Delivers instant transient attack on drum kicks followed by smooth, organic exponential decay. User-selected base color scales with beat energy.
- **ENERGY VU (`REACT_ENERGY_VU`)**: Real-time loudness VU meter mapping RMS audio power to a dynamic color gradient:
  - Quiet ($0\%-35\%$): Cyan $\to$ Forest Green
  - Moderate ($35\%-70\%$): Green $\to$ Amber / Yellow
  - Peak ($70\%-100\%$): Amber $\to$ Deep Crimson Red
- **SPECTRUM HUE (`REACT_SPECTRUM_HUE`)**: Real-time harmonic frequency centroid mapping:
  - Bass-dominant (0–250 Hz) $\to$ Warm Red / Orange
  - Mid-dominant (250–2000 Hz) $\to$ Emerald Green / Gold
  - Treble-dominant (2000+ Hz) $\to$ Cyan / Electric Blue / Violet
  - Smooth hue interpolation tracks melodic changes while overall brightness tracks audio power.
- **RAINBOW FLOW (`REACT_RAINBOW_FLOW`)**: Continuous 360° rainbow color wheel. High-frequency percussion and hi-hats dynamically accelerate wheel spin speed, while bass kicks pulse brightness.
- **FIRE FLAME (`REACT_FIRE`)**: Realistic campfire / ember simulation. Natural red/amber flame micro-flickering that surges into intense gold/white flares on audio transients.
- **DISCO STROBE (`REACT_DISCO_FLASH`)**: High-energy dance club strobe. Detects sudden bass transient deltas ($>7$ dB jumps) to trigger rapid flashes that shift to contrasting complementary hues via golden-ratio angle stepping ($+77^\circ$).

### 2. Resting Ambient & Playback Lifecycle
- When music is paused, stopped, or silent, the reactive modes smoothly transition to a dim, soothing resting breath ($14-46$ brightness, $0.24$ Hz) to conserve power while indicating the player is alive.
- User-selected lighting mode persists across track changes, pauses, and resumes via dedicated playback lifecycle hooks (`onPlaybackStart`, `onPlaybackPause`, `onPlaybackResume`, `onPlaybackStop`).

### 3. Non-Reactive Modes & Hardware Safety
- **BREATHING**: Soothing 0.5 Hz rhythmic pulsing of the chosen color.
- **RAINBOW WHEEL**: Smooth autonomous color wheel cycling.
- **SOLID COLOR**: Constant static illumination from the 8-color palette.
- **OFF**: Full LED sleep (0 current draw).
- **CRITICAL BATTERY OVERRIDE**: Battery sentry ($V_{\text{BAT}} \le 10\%$) immediately overrides any active lighting with a sharp Red warning strobe.

### 4. Interactive Configuration Menu
Accessible directly from Main Menu item `3. RGB Light Effects`:
- **Mode**: Cycle across all 6 reactive and 4 classic modes with live preview.
- **Color**: Cycle through 8 vibrant hues (`CYAN`, `BLUE`, `GREEN`, `YELLOW`, `ORANGE`, `RED`, `MAGENTA`, `WHITE`).
- **Brightness**: 5-step digital dimming (`10%`, `24%`, `47%`, `71%`, `100%`).
- **Sensitivity**: 3-stage gain multiplier (`LOW` $0.7\times$, `NORM` $1.0\times$, `HIGH` $1.45\times$) for quiet acoustic or compressed EDM tracks.
- **Test Pulse**: Trigger white verification double-flash.

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
| **Now Playing** | OK / SEL (Hold >650ms) | Cycle Page (Page 1: Player → Page 2: Track Info → Page 3: LRC Lyrics) |
| **Now Playing** | UP / DOWN (Short) | Previous / Next track |
| **Now Playing** | UP / DOWN (Hold >650ms) | Digital Volume Up / Down (with on-screen overlay) |
| **Now Playing** | CANCEL (Short) | Return to Track List |
| **Visualizer Mode** | Any Button | Reveal Top Status Bar (auto-hides after 5s for full-screen 128×64 visualizer) |
| **Visualizer Mode** | UP / DOWN (Short) | Previous / Next visualizer preset (`BARS` ↔ `WAVE` ↔ `PLASMA` ↔ `STAR`) |
| **Visualizer Mode** | OK / SEL (Short) | Toggle Play / Pause |
| **Visualizer Mode** | OK / SEL (Hold >650ms) | Cycle Visualizer Sensitivity (`LOW` → `NORMAL` → `HIGH`) |
| **Visualizer Mode** | UP / DOWN (Hold >650ms) | Digital Volume Up / Down |
| **Visualizer Mode** | CANCEL (Short / Hold) | Return to Main Menu (Playback continues in background) |
| **Any Screen** | CANCEL (Hold >650ms) | Global escape to Main Menu |

---

## FreeRTOS Dual-Core Audio Architecture

Q-Tune leverages the ESP32-S3 dual-core LX7 processor to guarantee stutter-free audio output:
- **Core 0 (Audio Engine Task)**: Dedicated FreeRTOS background task continuously streams audio samples to I2S DMA with zero starvation.
- **Core 1 (UI & Peripherals)**: Handles OLED rendering, button debouncing, LED animations, and battery monitoring.
- **Multi-Core SPI Mutex**: Thread-safe hardware arbitration (`spiBusMutex`) prevents bus contention between the SPI microSD card and SPI OLED display.
- **Lock-Free Visualizer Tap**: Real-time 512-sample circular ring buffer passes live decoded 16-bit PCM WAV audio from Core 0 directly to Core 1 without taking locks or interrupting DMA transmission.
- **Digital Volume Scaling**: Non-linear quadratic perceptual volume curve (`(vol / 100)^2 * 256`) applied using fast fixed-point arithmetic. Pre-attenuation audio taps ensure visualizers stay vibrant regardless of listening volume.

---

## Q-Watch Signature Menu System

Q-Tune implements the identical high-fidelity menu architecture from the **Q-Watch** OS:
- **Header Title Bar**: Micro font (`5x7`) header at `(2, 7)` with dividing rule line at `y=9` and right-aligned battery percentage or counter.
- **Inverted Selection Box**: High-contrast `118x11` inverted solid box (`u8g2.drawBox(2, y_pos - 9, 118, 11)`) with reverse monochrome text rendering (`drawColor 0`).
- **4-Item Viewport Window**: Fixed 4-item view window with automatic smooth window sliding (`offset = selection - 3` on down, `offset = selection` on up).
- **Proportional Scrollbar**: Crisp 3px wide right frame at `(123, 12, 3, 46)` with a 10px sliding thumb indicator computed proportionally over `(item_count - 4)`.
- **Right-Aligned Parameter Tags**: Standardized right-aligned values (`[PLAY]`, `[BARS]`, `100%`, etc.) styled with reverse video inside the selection box.
- **Unified Navigation Engine**: `Display::navigateMenu(selection, offset, count, direction, wrap)` powers the Main Menu, Music Player track browser, and Hardware Diagnostics.

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

## microSD Anti-Corruption & Hardware Safety Mechanisms

Portable audio players risk microSD filesystem corruption from sudden power cutoffs, brownouts, or hot card removal. Q-Tune implements a 5-layer safety architecture:

1. **Graceful Power-Down & Sleep Unmount Protocol**:
   - Calling `powerManager.safeShutdown()` halts active audio streams, closes file handles, takes the SPI bus mutex, and invokes `SD.end()`.
   - Forces `SD_CS` (GPIO 8) to `HIGH` (deselected) and locks it during ESP32-S3 deep sleep using `gpio_hold_en((gpio_num_t)SD_CS)` and `gpio_deep_sleep_hold_en()`. This prevents floating lines from causing spurious SPI clocking or card controller wear-leveling corruption while sleeping.
   - Waits for mechanical CANCEL button release before entering sleep to prevent instant reboot loops, and unlocks the hardware hold latch on boot via `gpio_hold_dis((gpio_num_t)SD_CS)` and `gpio_deep_sleep_hold_dis()` so the card remounts seamlessly upon wake-up.

2. **Critical Low-Battery Auto-Shutdown Sentry**:
   - High volume playback with the MAX98357A amplifier can trigger brownout voltage sag when the LiPo battery is nearly exhausted.
   - Continuous battery sampling in the main loop detects critical thresholds (`<= 3.35V`). The sentry immediately terminates audio playback (shedding high-current load), flashes an OLED shutdown alert, unmounts the SD card cleanly, and enters deep sleep.

3. **Software Safe Eject & Hot Remount**:
   - In **System Info**, pressing **OK / Select** toggles **Safe Eject SD** / **Mount SD**.
   - Safe Eject cleanly unmounts the card, pulls CS HIGH, and reports `SD: EJECTED (SAFE)` on screen before the card is physically removed.
   - Re-inserting the card and pressing Select remounts the filesystem and rescans the playlist without requiring an ESP32 reboot.

4. **Hot-Unplug & I/O Read Error Watchdog**:
   - If the microSD card is removed during active playback or suffers read timeouts, the audio engine detects consecutive read failures (`consecutiveReadErrors >= 15`), immediately aborts playback, releases the SPI mutex, closes handles, and triggers `sdManager.notifyCardRemoved()`.

5. **Mutual Bus Exclusion & Read-Only Guarantees**:
   - Both `OLED_CS` and `SD_CS` are mutually de-asserted (HIGH) before alternate bus transfers begin over the shared SPI bus (GPIO 5, 7).
   - Audio operations strictly use `FILE_READ`, preventing uncommitted write cache corruption.

---

## Getting Started

1. Open the project in PlatformIO.
2. Build and flash the project to an ESP32-S3 SuperMini:
   ```bash
   pio run --target upload
   ```
3. Format a microSD card (FAT32), create a `/music/` folder, and copy 16-bit 44.1kHz WAV, MP3, or FLAC files into it.
4. Insert the card into Q-Tune and power on.
