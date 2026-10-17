# Q-Tunes

A portable, audiophile-grade standalone music player and Wi-Fi/USB digital audio streamer built on the ESP32-S3 SuperMini board, inheriting proven pinout, power, and board layout standards from the Q-Watch architecture.

---

## Highlights & Features

- **Audiophile Dual-Output Architecture:**
  - **Onboard Speaker / Amp:** I²S output to MAX98357A Class-D amplifier with hardware `(L+R)/2` mono downmix.
  - **FiiO KA11 USB DAC (Cirrus Logic CS43131):** Full-Speed USB Host Audio Class (UAC 1.0/2.0) streaming up to 32-bit/384kHz, hardware volume/mute, safe eject, and hot-unplug auto-fallback.
- **Universal Multi-Format Audio Engine:**
  - **Lossless WAV & FLAC:** Hardware frame-aligned PCM streaming with custom stream callbacks.
  - **MP3:** Zero-stack heap scratch buffer architecture via `minimp3`.
  - **M4A / AAC:** High-efficiency fixed-point AAC decoding via `arduino-libhelix`.
- **Wi-Fi Audio Streaming & DLNA / UPnP MediaRenderer:**
  - Full UPnP `AVTransport:1` and `RenderingControl:1` compliance with SSDP discovery (`239.255.255.250`).
  - 384 KB PSRAM circular ring buffer with dedicated FreeRTOS Core 0 streaming task.
  - Integrated Web Player Remote and Wi-Fi manager accessible at `http://q-tunes.local`.
- **Audiophile Audio DSP Engine:**
  - **5-Band Parametric Equalizer:** Biquad IIR filters (60Hz low shelf, 250Hz peaking, 1kHz peaking, 4kHz peaking, 12kHz high shelf) with NVS persistent presets (`FLAT`, `BASS BOOST`, `VOCAL`, `CUSTOM`).
  - **Jan Meier Analog Crossfeed:** Realistic headphone stereo imaging (`SUBTLE`, `NATURAL`) to reduce listening fatigue.
  - **Pseudosymmetric Soft Limiter:** Saturation clipping prevents harsh digital overflows.
- **Multi-Page Player View & Real-Time Synchronized Lyrics:**
  - **Page 1 (Now Playing):** Track metadata, progress bar, volume overlay, and live mini-visualizer or album art.
  - **Page 2 (Technical Info):** Sample rate, bit depth, channels, bitrate (kbps), format badge, and file size.
  - **Page 3 (Synchronized Lyrics):** Real-time synced scrolling lyrics parsing `.lrc` files, embedded ID3v2 `SYLT` / `USLT`, and M4A `©lyr` atoms with millisecond precision and negative scan caching.
- **Embedded Album Art & Monochrome Dithering:**
  - Tiny JPEG decompressor (`tjpgd`) extracts embedded artwork from ID3v2 `APIC`, FLAC `METADATA_BLOCK_PICTURE`, M4A `covr`, or folder `cover.jpg`.
  - High-fidelity **Atkinson** and **Floyd-Steinberg** error diffusion dithering down to 1-bit monochrome OLED bitmaps.
- **Real-Time Visualizer & Audio Metering Suite:**
  - 8 distinct visualizer modes including vintage **Analog Needle VU Meter** (ANSI C16.5 300ms mechanical ballistics), **HoRNet-style Dual Horizontal Bar VU**, **Vinyl Turntable VU**, and **16-Band Octave Spectrometer**.
- **Music-Reactive RGB Lighting & Vocal Expression Engine:**
  - WS2812 NeoPixel (GPIO 48) driven at 50 FPS with 7 reactive modes (including *Vocal Lightning*, *Bass Pulse*, *Energy VU*, *Spectrum Hue*, *Disco Strobe*).
  - Advanced vocal formant energy tracking, vibrato shimmer detection, and note sustain tracking.
- **ESP32-S3 Hardware Acceleration:**
  - **SIMD PIE LX7:** 128-bit vector instructions for volume scaling, downmixing, and buffer clearing.
  - **Hardware GDMA:** Asynchronous display flushing with ping-pong double buffering.
  - **Hardware FPU:** Compiled with `-fno-math-errno` and `-ffast-math` using precomputed Hanning and FFT twiddle LUTs.
- **microSD Anti-Corruption & Safety Architecture:**
  - Deep-sleep `SD_CS` hardware pin hold latching, brownout auto-shutdown sentry, safe eject menu toggle, and hot-unplug I/O watchdog.
  - Supports SDSC, SDHC, and SDXC (64GB+, 128GB, 256GB+) with direct SPI exFAT detection.

---

## Hardware Architecture & Pin Allocation Matrix

The GPIO map preserves the proven Q-Watch pin matrix while reallocating unneeded watch peripherals (IR TX/RX, Buzzer) to audio, microphone, USB host, and microSD interfaces with **zero pin collisions**:

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
| MAX98357A DAC | DOUT (Serial Data Output) | 10 | Clean outer header pin |
| INMP441 Mic | DIN (Serial Data Input) | 15 | Reallocated from freed I2C port |

#### INMP441 Microphone Pin Connections
| INMP441 Pin | ESP32-S3 SuperMini Pin | Description & Critical Notes |
| :--- | :--- | :--- |
| **VDD** | 3.3V | **3.3V Power Only** (Do NOT connect to 5V; absolute maximum is 3.6V). |
| **GND** | GND | Ground reference. |
| **SD** | GPIO 15 | Serial Data Out from mic to ESP32 DIN. |
| **SCK** | GPIO 17 | Continuous Bit Clock (shared with MAX98357A BCLK). |
| **WS** | GPIO 18 | Word Select / LRCLK (shared with MAX98357A LRCK). |
| **L/R** | GND (or 3.3V) | **Mandatory connection**: Tie to GND for Left channel or 3.3V for Right channel. Never leave floating. |

#### MAX98357A SD_MODE & MCLK Configuration
- **MCLK**: Not required by MAX98357A (uses internal PLL clock recovery).
- **SD_MODE**: Connected in hardware with a 100kΩ pull-up resistor to VDD. This configures the DAC to compute a **(Left + Right) / 2 mono mix**, making it ideal for driving a single speaker from stereo or mono source files without consuming an extra GPIO pin.

### 3. FiiO KA11 USB Host Subsystem (Native USB-OTG)
| Signal | GPIO | Description & Bus Protocol |
| :--- | :--- | :--- |
| **USB_HOST_DM** | 19 | USB Full-Speed D- (Native differential data minus) |
| **USB_HOST_DP** | 20 | USB Full-Speed D+ (Native differential data plus) |

> [!IMPORTANT]
> **Boot Pin Preservation:** GPIO 19 and 20 are NOT configured as USB Host pull-downs at boot time in `USBManager::begin()`, preserving native USB CDC serial programming and debugging. Host termination (~15kΩ pull-downs) is engaged only upon explicit mounting or hotplug detection.
> **Safe Bus Parking:** Calling `safeEject()` or disconnecting tristates GPIO 19/20 (`INPUT` mode), protecting the Cirrus Logic CS43131 DAC against inductive voltage spikes and pop noises.

### 4. Navigation Inputs (4-Button Architecture)
| Button | GPIO | Logic | Notes |
| :--- | :--- | :--- | :--- |
| Button Up | 39 | INPUT_PULLUP (Active LOW) | K1 Directional UP / Auto-repeating volume hold |
| Button OK / Select | 40 | INPUT_PULLUP (Active LOW) | Directional OK / Play-Pause / Single-shot page cycle hold |
| Button Down | 42 | INPUT_PULLUP (Active LOW) | K1 Directional DOWN / Auto-repeating volume hold |
| Button Cancel / Back | 21 | INPUT_PULLUP (Active LOW) | Tactile CANCEL / Global Back / RTC Deep Sleep Wake |

### 5. Power, Diagnostics & Reserved Pins
| Function | GPIO | Hardware Circuit / Description |
| :--- | :--- | :--- |
| Battery Monitor | 1 (ADC1_CH0) | 100kΩ / 100kΩ divider from LiPo VBAT + 100nF filter capacitor |
| Onboard RGB LED | 48 | Built-in WS2812 NeoPixel RGB LED |
| Clean Expansion | 16 | Free general-purpose digital I/O |
| Dedicated UART0 | 43 (TX), 44 (RX) | **Completely freed** for unhindered hardware flashing and high-speed serial logging |
| Strapping Pins (AVOIDED) | 0, 3, 45, 46 | Strictly zero peripheral allocation to prevent bootloader locking |

---

## Operating Modes & Application Shell

On startup, Q-Tune launches into the **Unified Home Menu** styled with the Q-Watch OS design language:

```
+-----------------------------------+
| Q-TUNES                     [88%] |
+-----------------------------------+
| > 1. Music Player                 |
|   2. Spectrum Visualizer          |
|   3. Wi-Fi Audio Stream           |
|   4. Equalizer (DSP)              |
+-----------------------------------+
```

1. **Music Player**:
   - Categorized library navigation (All Tracks, Artists, Albums, Playlists, Recent Tracks, Folder Browser).
   - Multi-page Now Playing view (Player HUD ↔ Technical Metadata ↔ Synced LRC Lyrics).
   - Seamless output badge (`SPKR` vs `KA11`).
2. **Spectrum Visualizer**:
   - Full-screen real-time audio visualization with 5-second auto-hiding status bar.
   - 8 presets: `ANALOG VU`, `HORIZ VU`, `VU+SPEC`, `VINYL VU`, `BARS`, `WAVE`, `PLASMA`, `STAR`.
3. **Wi-Fi Audio Stream**:
   - Standalone DLNA / UPnP MediaRenderer receiver with live mini visualizer and IP display.
   - Remote web interface at `http://q-tunes.local`.
4. **Equalizer (DSP)**:
   - 5-band vertical slider HUD (`60Hz`, `250Hz`, `1kHz`, `4kHz`, `12kHz`).
   - Quick preset switcher (`FLAT`, `BASS BOOST`, `VOCAL`, `ACOUSTIC`, `CLUB`, `CUSTOM`).
   - Jan Meier headphone crossfeed selector (`OFF`, `SUBTLE`, `NATURAL`).
5. **RGB Light Effects**:
   - 7 music-reactive modes + 4 ambient modes.
   - Real-time adjustment of brightness, color palette, and audio sensitivity.
6. **Hardware Diagnostics**:
   - Interactive 12-point hardware test suite (including Dual Speaker Left/Right/Both test, FiiO KA11 USB DAC test, and Vocal Reactive Check).
7. **System Info**:
   - Battery voltage and percentage, CPU frequency governor, SD card filesystem usage, and manual Safe Eject SD toggle.

---

## FiiO KA11 USB DAC & Real UAC Host Subsystem

Q-Tune implements a fully featured, bare-metal USB Audio Class (UAC 1.0/2.0) Host driver ([`esp-usb/host/class/uac`](https://github.com/espressif/esp-usb/tree/master/host/class/uac)) operating directly over the ESP32-S3 USB-OTG peripheral for the **FiiO KA11 High-Res DAC** (Cirrus Logic CS43131):

- **Real Hardware USB Host Controller:**
  - `uac_host_install()` configures the internal USB-OTG PHY, launches the host event task, and registers the driver client (`usb_host_client_register`).
  - `uac_host_device_open()` opens the physical USB device handle from the USB host controller and parses real standard device (`usb_device_desc_t`) and configuration (`usb_config_desc_t`) descriptors.
  - `uac_host_handle_events()` pumps asynchronous USB events directly from `usb_host_client_handle_events()`.
- **Dynamic Format Negotiation:** Probes audio streaming interfaces and claims matching alternate settings for 44.1 kHz, 48 kHz, 88.2 kHz, 96 kHz, 176.4 kHz, or 192 kHz at 16-bit or 24-bit stereo PCM.
- **Isochronous Audio Transfer Subsystem:** Allocates native USB isochronous transfer request blocks (`usb_host_transfer_alloc`) and dispatches audio frames directly into the USB hardware pipe via `usb_host_transfer_submit()`.
- **Cirrus Logic CS43131 Hardware Volume & Mute:**
  - Sends real USB Class-Specific `SET_CUR` control transfers (`usb_host_transfer_submit_control`) targeting the Audio Control Feature Unit (`0x21` request type, `UAC_SET_CUR`, `UAC_VOLUME_CONTROL`, `UAC_MUTE_CONTROL`).
  - Directly alters internal DAC analog gain registers in 1/256 dB steps without digital bit reduction or dynamic range degradation.
- **Hearing Protection & Ear Safety (10% Initial Volume):**
  - Because high-performance headphone dongles like the FiiO KA11 can deliver up to 200mW into low-impedance IEMs, connecting the DAC automatically clamps and initializes the output volume to **10%** (`-23.0 dB`) to protect the listener against acoustic shock and hearing damage.
- **Hot-Unplug Protection & Instant I²S Fallback:** Interrupt-driven disconnect handling instantly releases endpoints and falls back to `OUTPUT_MODE_SPEAKER_I2S` so background playback loops never stall or panic.
- **Safe Eject & Bus Parking:** Gracefully stops active isochronous streams, releases interfaces, closes USB client handles, and tristates GPIO 19/20 (`INPUT` mode) before physical disconnection to eliminate inductive pops.
- **Non-Volatile Settings Persistence (NVS):**
  - Uses ESP32 NVS (`Preferences` namespace `qtune_cfg`) to remember user preferences across power cycles:
    - Independent **Speaker Volume** (`vol_spk`, default 80%) and **FiiO DAC Volume** (`vol_fiio`, default 10% safe level).
    - **Active Output Mode** (`out_mode`: Speaker I²S vs FiiO KA11 USB DAC).
    - **Playback Mode** (`play_mode`: Repeat All, Repeat One, Shuffle, Single).
    - **5-Band Parametric EQ & Headphone Crossfeed** presets and custom band gains.

---

## Wi-Fi Audio Streaming & DLNA MediaRenderer

Q-Tune transforms into a high-fidelity wireless network streamer:

- **DLNA / UPnP MediaRenderer v1:** Implements standard `AVTransport:1` and `RenderingControl:1` SOAP actions (`SetAVTransportURI`, `Play`, `Pause`, `Stop`, `Seek`, `SetVolume`, `SetMute`).
- **Zero-Glitch PSRAM Buffering:** Core 0 runs `QWiFiStreamTask` with a 384 KB circular ring buffer allocated in SPIRAM to absorb Wi-Fi network jitter and packet retransmissions.
- **Web Player Remote & REST API:**
  - Access `http://q-tunes.local` (or IP address) from any phone or PC browser.
  - Interactive web HUD displaying real-time playback state, track info, progress slider, volume controls, and Wi-Fi credential scanner.
- **Output Flexibility:** Streamed Wi-Fi audio routes transparently through the active output mode (onboard MAX98357A speaker or FiiO KA11 USB DAC) and feeds the live spectrum visualizer.

---

## Audiophile DSP Engine

A signal processing pipeline engineered specifically for portable listening:

```
[Decoded PCM] ──► [5-Band Parametric EQ] ──► [Jan Meier Crossfeed] ──► [Soft Limiter] ──► [Output Router]
                                                                                              │
                                                               ┌──────────────────────────────┴──────────┐
                                                               ▼                                         ▼
                                                       [MAX98357A I²S]                           [FiiO KA11 USB]
```

1. **5-Band Parametric EQ:**
   - Pre-warped Bilinear Transform Biquad IIR filters with hardware FPU acceleration.
   - Frequency bands: `60 Hz` (Low Shelf), `250 Hz` (Peaking), `1 kHz` (Peaking), `4 kHz` (Peaking), `12 kHz` (High Shelf).
   - Adjustable gain: -12 dB to +12 dB per band.
2. **Jan Meier Analog Crossfeed:**
   - Blends subtle low-pass filtered cross-channel signals with frequency-dependent time delays to simulate natural speaker stereo imaging on headphones.
   - Eliminates extreme hard-panned stereo fatigue on early stereo recordings (e.g., 1960s jazz and rock).
3. **Pseudosymmetric Soft Limiter:**
   - Smooth hyperbolic saturation curve prevents harsh digital clipping when boosting EQ bands near full scale (0 dBFS).

---

## Multi-Page Player & Synchronized Lyrics

The Now Playing screen supports multi-page navigation by pressing and holding **OK / Select (>650ms)**:

- **Page 1: Player HUD & Album Art:** Displays track title, artist, elapsed time, total duration, 8-band mini spectrum analyzer, and optional 1-bit dithered album cover.
- **Page 2: Technical Metadata:** Detailed audio format readouts including audio decoder engine, sample rate (e.g., `44100 Hz`), bit depth (`16-bit` / `24-bit`), channel layout (`Stereo` / `Mono`), bitrate (`320 kbps`, `1411 kbps`), total file size, and active output device.
- **Page 3: Synchronized LRC Lyrics:**
  - Automatically loads `.lrc` files matching the track name, or extracts embedded lyrics from MP3 ID3v2 `SYLT` (Synchronized Lyrics), `USLT` (Unsynchronized Lyrics), and M4A `©lyr` atoms.
  - Smooth 3-line vertical display with active line highlighting and sub-second millisecond synchronization.
  - Negative scan caching prevents unnecessary SD card file access on tracks without lyrics.

---

## Navigation & Controls Matrix

| Context | Button | Action |
| :--- | :--- | :--- |
| **Main Menu** | UP / DOWN (Short) | Move cursor through 4-item viewport |
| **Main Menu** | OK / SEL (Short) | Launch selected application |
| **Main Menu** | CANCEL (Long >650ms) | Enter low-power deep sleep / screen off |
| **Track List** | UP / DOWN (Short) | Browse files / categories |
| **Track List** | OK / SEL (Short) | Play selected track |
| **Track List** | CANCEL (Short) | Return to Main Menu |
| **Now Playing** | OK / SEL (Short) | Toggle Play / Pause |
| **Now Playing** | OK / SEL (Hold >650ms) | Cycle Page (Page 1: Player → Page 2: Info → Page 3: Lyrics) |
| **Now Playing** | UP / DOWN (Short) | Previous / Next track |
| **Now Playing** | UP / DOWN (Hold >650ms) | Digital Volume Up / Down (with on-screen overlay) |
| **Now Playing** | CANCEL (Short) | Return to Track List |
| **Visualizer** | Any Button | Reveal Top Status Bar (auto-hides after 5s) |
| **Visualizer** | UP / DOWN (Short) | Previous / Next visualizer preset |
| **Visualizer** | OK / SEL (Hold >650ms) | Cycle Visualizer Sensitivity (`LOW` → `NORM` → `HIGH`) |
| **Equalizer** | UP / DOWN (Short) | Adjust selected band gain (-12 dB to +12 dB) |
| **Equalizer** | OK / SEL (Short) | Advance to next frequency band |
| **Equalizer** | OK / SEL (Hold >650ms) | Cycle Preset (`FLAT` → `BASS BOOST` → `VOCAL` → `CUSTOM`) |
| **Any Screen** | CANCEL (Hold >650ms) | Global escape to Main Menu |

---

## microSD Safety & Anti-Corruption Protocol

1. **Hardware CS Hold in Deep Sleep:** `SD_CS` (GPIO 8) is held `HIGH` using `gpio_hold_en((gpio_num_t)SD_CS)` during deep sleep, preventing floating SPI lines from causing flash memory controller corruption.
2. **Critical Low-Battery Auto-Shutdown:** Hardware ADC monitoring terminates audio playback and safely unmounts filesystems before brownout voltage levels (`<= 3.35V`) are reached.
3. **Manual Safe Eject:** Accessible in **System Info** (`OK / Select` toggles Eject / Mount) to flush file caches before removing the card.
4. **Hot-Unplug Watchdog:** Consecutive I/O timeouts (`>= 15`) immediately halt audio DMA and close file handles.
5. **Formatting Guidelines for 64GB+ SDXC Cards:**
   - Format cards to **FAT32** with 32KB cluster size using **GUIFormat** (Windows), `diskutil` (macOS), or `mkfs.vfat` (Linux).

---

## Automated Verification & Test Suite

The project includes an automated verification test suite verifying all 30 subsystem layers, including native C++ compilation and execution of audio DSP, UAC USB Host, GDMA, and AAC decoders:

```bash
python3 tests/test_suite.py
```

```
==================================================
        Q-TUNE AUTOMATED VERIFICATION SUITE       
==================================================
--- 1. Hardware Pinout & Q-Watch Alignment Test --- [PASS]
--- 2. Button Manager 4-Button Architecture Test --- [PASS]
--- 3. Battery Monitor PWL Discharge Curve Test --- [PASS]
--- 4. Audio WAV Header Binary Parser Test --- [PASS]
--- 5. Power Manager & Dynamic CPU Scaling Test --- [PASS]
--- 6. SIMD LX7 PIE Vector Acceleration Engine Test --- [PASS]
--- 7. Non-Blocking FastLED & Music-Reactive Lighting Engine Test --- [PASS]
--- 8. INMP441 MEMS Microphone & Spectrum Analyzer Pipeline Test --- [PASS]
--- 9. Digital Volume Control & Quadratic Perceptual Scaling Test --- [PASS]
--- 10. FreeRTOS Audio Task & Thread-Safe SPI Mutex Test --- [PASS]
--- 11. Playback Modes & Auto-Advance Engine Test --- [PASS]
--- 12. Unified App Shell & Mode Switching Test --- [PASS]
--- 13. Live WAV Stream Decoding Visualizer Engine Test --- [PASS]
--- 14. Q-Watch Aligned Menu System Architecture Test --- [PASS]
--- 15. FLAC Decoder Engine & Clean Q-Tunes Architecture Test --- [PASS]
--- 16. SD Card Anti-Corruption & Safety Mechanisms Test --- [PASS]
--- 17. FiiO KA11 High-Res USB DAC & Safe Eject Subsystem Test --- [PASS]
--- 18. Wi-Fi Audio Streaming & DLNA/UPnP MediaRenderer Subsystem Test --- [PASS]
--- 19. MP3 & M4A/AAC Multi-Format Audio Decoding Test --- [PASS]
--- 20. Library Indexing, Categories, Now Playing Protection & Multi-Page LRC Lyrics Test --- [PASS]
--- 21. Album Art Extraction, Monochrome Dithering & Player Customization Test --- [PASS]
--- 22. MP3 Zero-Stack Heap Scratch, Helix AAC & SDXC 64GB exFAT Test --- [PASS]
--- 23. Interactive Vocal Reactive Check Tool & Expression Engine Test --- [PASS]
--- 24. VU Analog Meter & Spectrometer Suite Test --- [PASS]
--- 25. Fixture-Based Lyrics Parser & Index Validation Test --- [PASS]
--- 26. Hardware FPU Math & Fast DSP Subsystem Test --- [PASS]
--- 27. Native LEDManager::cycleColor() Test Suite --- [PASS]
--- 28. Native AAC File Format Header Parsing & Bitrate Calculation Test --- [PASS]
--- 29. ESP32-S3 GDMA Subsystem & Shared-SPI Display Pipeline --- [PASS]
--- 30. Audiophile Audio DSP Engine Test (5-Band Parametric EQ, Crossfeed & Soft Limiter) --- [PASS]

All 30 Q-Tune static/source verification checks PASSED (100%)!
```

---

## Getting Started & Building

1. **Prerequisites:** Install [PlatformIO Core](https://platformio.org/) or the VS Code PlatformIO extension.
2. **Build and Flash:**
   ```bash
   # Build firmware
   pio run

   # Flash to ESP32-S3 SuperMini
   pio run --target upload
   ```
3. **microSD Preparation:**
   - Format card as FAT32.
   - Create a `/music/` directory at the root.
   - Copy `.wav`, `.flac`, `.mp3`, `.m4a`, or `.aac` files (and corresponding `.lrc` files if desired) into `/music/`.
4. **Power On:** Insert card and boot Q-Tune. Connect headphones to the FiiO KA11 USB DAC or listen through the onboard MAX98357A speaker.
