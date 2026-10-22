# Q-Tunes

[![Platform](https://img.shields.io/badge/Platform-ESP32--S3-blue.svg)](https://www.espressif.com/)
[![Architecture](https://img.shields.io/badge/SoC-Dual--Core%20Xtensa%20LX7%20240MHz-orange.svg)]()
[![Verification](https://img.shields.io/badge/Verification%20Suite-33%2F33%20PASSED%20(100%25)-success.svg)](tests/test_suite.py)
[![Audio Engine](https://img.shields.io/badge/Audio-WAV%20%7C%20FLAC%20%7C%20MP3%20%7C%20M4A%2FAAC-red.svg)]()
[![USB Host](https://img.shields.io/badge/USB%20DAC-FiiO%20KA11%20UAC%201.0%2F2.0-blueviolet.svg)]()
[![AI Companion](https://img.shields.io/badge/AI%20Companion-Gemini%203.5%20%7C%20DJ%20Neko%20%26%20Mochi-brightgreen.svg)]()
[![License](https://img.shields.io/badge/License-MIT-green.svg)]()

A portable, audiophile-grade standalone music player, high-resolution USB DAC streamer, Wi-Fi DLNA/UPnP network renderer, and intelligent dual-personality AI companion built on the **ESP32-S3 SuperMini** platform. Q-Tunes inherits proven electrical pinout, power, and layout standards from the **Q-Watch** architecture while delivering uncompromised hardware-accelerated digital audio playback.

---

## Table of Contents

- [Key Capabilities](#key-capabilities)
- [System Architecture](#system-architecture)
  - [Hardware Signal Pipeline](#hardware-signal-pipeline)
  - [4-Tier Layered AI Architecture](#4-tier-layered-ai-architecture)
- [Hardware Specifications & Pinout Matrix](#hardware-specifications--pinout-matrix)
  - [1. Shared SPI Bus (OLED Display + microSD)](#1-shared-spi-bus-oled-display--microsd)
  - [2. I²S Audio & MEMS Microphone](#2-i2s-audio--mems-microphone)
  - [3. Native USB Host Subsystem (FiiO KA11)](#3-native-usb-host-subsystem-fiio-ka11)
  - [4. Navigation Input Matrix](#4-navigation-input-matrix)
  - [5. Power, Battery ADC & Telemetry](#5-power-battery-adc--telemetry)
- [Wiring & Electrical Connection Guide](#wiring--electrical-connection-guide)
- [Operating Modes & User Interface](#operating-modes--user-interface)
- [Navigation & Control Matrix](#navigation--control-matrix)
- [Audiophile DSP Engine](#audiophile-dsp-engine)
- [Multi-Page Player & Synced Lyrics](#multi-page-player--synced-lyrics)
- [AI Companion Subsystem (DJ Neko & Mochi)](#ai-companion-subsystem-dj-neko--mochi)
- [Wi-Fi Streaming & DLNA MediaRenderer](#wi-fi-streaming--dlna-mediarenderer)
- [MicroSD Safety & Anti-Corruption Protocol](#microsd-safety--anti-corruption-protocol)
- [Project Directory Layout](#project-directory-layout)
- [Getting Started & Build Instructions](#getting-started--build-instructions)
- [Hardware Diagnostics & Verification Suite](#hardware-diagnostics--verification-suite)
- [License & Acknowledgments](#license--acknowledgments)

---

## Key Capabilities

- **Dual-Output Audiophile Architecture:**
  - **Onboard Speaker / Amp:** I²S output to MAX98357A Class-D amplifier with hardware `(L+R)/2` mono downmix.
  - **FiiO KA11 USB DAC (Cirrus Logic CS43131):** Full-Speed USB Host Audio Class (UAC 1.0/2.0) streaming up to 32-bit/384kHz, hardware volume/mute via USB control transfers, hearing protection clamping (10% initial safe volume), safe eject, and hot-unplug fallback.
- **Universal Multi-Format Audio Engine:**
  - **Lossless WAV & FLAC:** Hardware frame-aligned streaming via `dr_wav` and `dr_flac` with zero-allocation I/O callbacks.
  - **MP3:** Zero-stack heap scratch buffer architecture via `minimp3`.
  - **M4A / AAC:** High-efficiency fixed-point AAC decoding via `arduino-libhelix`.
- **Audiophile DSP Suite:**
  - **5-Band Parametric EQ:** Hardware FPU accelerated Biquad IIR filters (`60Hz`, `250Hz`, `1kHz`, `4kHz`, `12kHz`) with persistent NVS presets.
  - **Jan Meier Analog Crossfeed:** Realistic headphone stereo imaging to reduce hard-pan listening fatigue.
  - **Pseudosymmetric Soft Limiter:** Saturation clipping prevents digital overflow during dynamic peaks.
- **Dual AI Companion (DJ Neko & Mochi):**
  - **DJ Neko:** Audiophile cat persona with animated headphones, BPM head-bobbing, and frequency-reactive ears/whiskers.
  - **Classic Mochi:** The energetic Dasai companion pet from Q-Watch.
  - **65 Authentic `.anim` Faces:** Preloaded into PSRAM from microSD for glitch-free concurrent audio streaming.
  - **Google Gemini 3.5 API Integration:** Full device awareness (volume, EQ, tracks, battery) with ~4s low-latency cloud responses and zero-churn persistent worker tasks.
- **Wi-Fi Streaming & DLNA / UPnP MediaRenderer:**
  - UPnP `AVTransport:1` and `RenderingControl:1` compliance with SSDP discovery.
  - 384 KB PSRAM circular ring buffer with dedicated FreeRTOS Core 0 streaming task.
  - Mobile/Desktop Web Remote HUD at `http://q-tunes.local`.
- **Visuals, Lyrics & Reactive Lighting:**
  - **4-Page Player HUD:** Album art, technical metadata, synchronized LRC lyrics, and library explorer.
  - **Monochrome Dithering:** Tiny JPEG decompressor (`tjpgd`) with Atkinson and Floyd-Steinberg error diffusion for 1-bit OLEDs.
  - **8 Visualizer Modes:** Analog needle VU meter (ANSI C16.5 ballistics), dual horizontal bar VU, turntable VU, spectrometer, waveform, and plasma.
  - **Music-Reactive WS2812 RGB LED (50 FPS):** Vocal Lightning, Bass Pulse, Spectrum Hue, and vocal formant tracking.
- **Hardware Acceleration (ESP32-S3):**
  - **SIMD PIE LX7:** 128-bit vector instructions for volume scaling, stereo downmix, and buffer clears.
  - **GDMA Pipeline:** Asynchronous display flushing with ping-pong double buffering.
  - **Hardware FPU:** Compiled with `-fno-math-errno` and `-ffast-math`.

---

## System Architecture

### Hardware Signal Pipeline

```
                                      ┌────────────────────────────────────────────────────────┐
                                      │                      Audio Sources                     │
                                      │  microSD Card (FAT32)  │  Wi-Fi Stream (DLNA / UPnP)   │
                                      └───────────────────┬────────────────────────────────────┘
                                                          │
                                                          ▼
                                      ┌────────────────────────────────────────────────────────┐
                                      │                Multi-Format Decoder Engine             │
                                      │      dr_wav  │  dr_flac  │  minimp3  │  Helix AAC      │
                                      └───────────────────┬────────────────────────────────────┘
                                                          │
                                                          ▼
                                      ┌────────────────────────────────────────────────────────┐
                                      │                 Audiophile DSP Pipeline                │
                                      │  5-Band Parametric EQ ──► Jan Meier Crossfeed ──► Soft │
                                      └───────────────────┬────────────────────────────────────┘
                                                          │
                                                          ▼
                                      ┌────────────────────────────────────────────────────────┐
                                      │                 Output Stream Router                   │
                                      └─────────────┬────────────────────────────┬─────────────┘
                                                    │                            │
                                                    ▼                            ▼
                                      ┌──────────────────────────┐ ┌──────────────────────────┐
                                      │      MAX98357A I²S       │ │     FiiO KA11 USB DAC    │
                                      │  (L+R)/2 Mono Downmix    │ │  Cirrus Logic CS43131    │
                                      │  Built-in 3W Speaker Amp │ │  Full-Speed UAC 1.0/2.0  │
                                      └──────────────────────────┘ └──────────────────────────┘
```

### 4-Tier Layered AI Architecture

Q-Tunes implements a clean, decoupled 4-tier layered architecture for AI interactions, inspired by the persistent, zero-churn multitasking of Q-Watch:

```
┌────────────────────────────────────────────────────────────────────────────────────────┐
│ Layer 1: Presentation & Persona Layer                                                  │
│   • UI Player & Status Toasts   • AI Companion: DJ Neko & Mochi State Machines         │
│   • 65 .anim Face Catalog       • Earcons: playMeow(), playPurr(), playChirp()         │
│   • Dedicated Diagnostics Menu  • Web Remote REST Endpoints (/api/ai/*)                │
└───────────────────────────────────────────┬────────────────────────────────────────────┘
                                            │
                                            ▼
┌────────────────────────────────────────────────────────────────────────────────────────┐
│ Layer 2: Orchestration & State Machine Layer (GeminiClient)                            │
│   • Telemetry Serialization: buildSystemStateJson() (track, volume, EQ, format, batt)  │
│   • Dynamic Prompt Generator: buildPrompt() (persona guidelines, action syntax)        │
│   • Action Dispatcher: executeAction() (EQ, track select, volume, animations)          │
│   • Non-Blocking Mailbox Interface: askAsync(), pollAsyncResult(), cancelAsync()       │
└───────────────────────────────────────────┬────────────────────────────────────────────┘
                                            │
                                            ▼
┌────────────────────────────────────────────────────────────────────────────────────────┐
│ Layer 3: Persistent Static Worker Layer (Zero Task Churn)                              │
│   • Statically Pre-allocated BSS Stack (10,240 Bytes) & StaticTask_t TCB               │
│   • Single Persistent Worker Daemon (gemini_worker) Pinned to Core 0                   │
│   • FreeRTOS Direct-to-Task Notifications (xTaskNotifyGive / ulTaskNotifyTake)         │
│   • 0% Heap Churn: Immune to FreeRTOS heap fragmentation on ESP32-S3                   │
└───────────────────────────────────────────┬────────────────────────────────────────────┘
                                            │
                                            ▼
┌────────────────────────────────────────────────────────────────────────────────────────┐
│ Layer 4: Network & Transport Engine Layer                                              │
│   • Constrained mbedTLS Buffers: WiFiClientSecure setBufferSizes(2048, 1024) [~9KB]    │
│   • Public DNS Fallback: Quad8 (8.8.8.8) + Cloudflare (1.1.1.1) alongside DHCP         │
│   • Latency Optimization: thinkingLevel: MINIMAL cuts API delay to ~4.3s               │
│   • Auto-Fallback & Quota Preservation: gemini-3.5-flash ──► gemini-3.5-flash-lite     │
└────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## Hardware Specifications & Pinout Matrix

Q-Tunes is based on the **ESP32-S3 SuperMini** (Dual-Core LX7 @ 240MHz, 512KB SRAM, 8MB PSRAM, 8MB Flash). The pin assignment preserves the Q-Watch layout while expanding interfaces with **zero pin collisions** and **strictly avoiding ESP32-S3 strapping pins (0, 3, 45, 46)**:

### 1. Shared SPI Bus (OLED Display + microSD)
| Peripheral | Signal | GPIO | Pin Type | Notes |
| :--- | :--- | :--- | :--- | :--- |
| OLED / SD | **MOSI (DIN)** | **5** | Header Pin | Shared SPI Master Out Slave In |
| OLED / SD | **SCK (CLK)** | **7** | Header Pin | Shared SPI Serial Clock |
| microSD | **MISO (DOUT)**| **6** | Header Pin | Reclaimed from Buzzer |
| OLED | **CS** | **4** | Header Pin | Display Chip Select (Active LOW) |
| OLED | **DC** | **2** | Header Pin | Display Data / Command Select |
| OLED | **RST** | **41** | Header Pin | Display Hardware Reset |
| microSD | **CS** | **8** | Header Pin | microSD Chip Select (Active LOW, Latch Hold in Deep Sleep) |

### 2. I²S Audio & MEMS Microphone
| Peripheral | Signal | GPIO | Pin Type | Notes |
| :--- | :--- | :--- | :--- | :--- |
| Audio Out / Mic | **BCLK (SCK)** | **17** | Underside Pad | Shared Continuous Bit Clock |
| Audio Out / Mic | **LRCLK (WS)** | **18** | Underside Pad | Shared Word Select / Frame Sync |
| MAX98357A Amp | **DOUT (DATA)** | **10** | Header Pin | Serial Audio Data Output from ESP32 to Amp |
| INMP441 Mic | **DIN (DATA)** | **15** | Header Pin | Serial Audio Data Input from Mic to ESP32 |

### 3. Native USB Host Subsystem (FiiO KA11)
| Signal | GPIO | Bus Protocol | Notes |
| :--- | :--- | :--- | :--- |
| **USB_HOST_DM** | **19** | USB Full-Speed D- | Native differential D- with safe bus parking |
| **USB_HOST_DP** | **20** | USB Full-Speed D+ | Native differential D+ with safe bus parking |

> [!IMPORTANT]
> **Boot Pin Safety:** GPIO 19 and 20 are NOT pulled down at boot time, preserving native USB CDC serial flashing and monitoring. Host pull-downs (~15kΩ) engage only when mounting the DAC. Calling `safeEject()` tristates both pins to protect against inductive spikes.

### 4. Navigation Input Matrix
| Button | GPIO | Logic | Behavior |
| :--- | :--- | :--- | :--- |
| **Button UP** | **39** | Active LOW (Pull-up) | Menu Up / Volume Up (Hold auto-repeat) |
| **Button OK / SEL** | **40** | Active LOW (Pull-up) | Menu Select / Play-Pause / Cycle Page (Hold >650ms) |
| **Button DOWN** | **42** | Active LOW (Pull-up) | Menu Down / Volume Down (Hold auto-repeat) |
| **Button CANCEL** | **21** | Active LOW (Pull-up) | Back / Exit / RTC Wakeup from Deep Sleep |

### 5. Power, Battery ADC & Telemetry
| Peripheral | GPIO | Circuit / Logic | Notes |
| :--- | :--- | :--- | :--- |
| **Battery ADC** | **1 (ADC1_CH0)** | 100kΩ / 100kΩ Divider | LiPo VBAT monitor with PWL discharge curve |
| **Onboard RGB** | **48** | Built-in WS2812 | Non-blocking FastLED @ 50 FPS |
| **Dedicated UART0** | **43 (TX), 44 (RX)**| Asynchronous Serial | **Completely freed** for hardware logging |
| **Expansion GPIO** | **16** | General-purpose I/O | Spare digital pin |

---

## Wiring & Electrical Connection Guide

### 1. INMP441 MEMS Microphone
| INMP441 Pin | ESP32-S3 SuperMini | Electrical Specification & Wiring Notes |
| :--- | :--- | :--- |
| **VDD** | **3.3V** | **3.3V ONLY**. Never connect to 5V (absolute maximum is 3.6V). |
| **GND** | **GND** | Common ground reference. |
| **SD** | **GPIO 15** | Serial Data output from microphone. |
| **SCK** | **GPIO 17** | Continuous I²S Bit Clock (shared with MAX98357A BCLK). |
| **WS** | **GPIO 18** | Word Select / LRCLK (shared with MAX98357A LRCK). |
| **L/R** | **GND** | **Mandatory:** Tie to GND for Left channel audio. Never leave floating. |

### 2. MAX98357A I²S Mono Amplifier
| MAX98357A Pin | ESP32-S3 SuperMini | Electrical Specification & Wiring Notes |
| :--- | :--- | :--- |
| **VIN** | **5V (or 3.3V)** | Connect to 5V (USB VBUS / LiPo boost) for full 3.2W output power. |
| **GND** | **GND** | Common ground reference. |
| **BCLK** | **GPIO 17** | Continuous Bit Clock. |
| **LRC** | **GPIO 18** | Word Select / Frame Sync. |
| **DIN** | **GPIO 10** | Serial Audio Data input. |
| **SD_MODE** | **100kΩ to VDD** | Configures DAC hardware to mix `(L+R)/2` mono into a single speaker. |
| **GAIN** | **GND** | Configures default 9 dB gain stage. |

---

## Operating Modes & User Interface

Q-Tunes boots into a 4-item scroll window styled in the Q-Watch OS design language:

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

1. **Music Player:** Categorized library browsing (All Tracks, Artists, Albums, Playlists, Folder Browser), 4-page Now Playing HUD, and synchronized lyrics.
2. **Spectrum Visualizer:** Full-screen 60 FPS real-time audio visualization with 5-second auto-hiding top HUD.
3. **Wi-Fi Audio Stream:** Standalone DLNA / UPnP MediaRenderer receiver with live IP address display and Web Remote HUD.
4. **Equalizer (DSP):** 5-band vertical slider HUD, instant preset switcher (`FLAT`, `BASS BOOST`, `VOCAL`, `CLUB`), and Jan Meier headphone crossfeed.
5. **AI Companion (DJ Neko & Mochi):** Live interactive pet with 65 authentic `.anim` expressions, audio reactivity, voice commands, and Gemini 3.5 cloud reasoning.
6. **RGB Light Effects:** 7 music-reactive modes (including Vocal Lightning and Bass Pulse) with adjustable brightness and sensitivity.
7. **Hardware Diagnostics:** Interactive 13-point diagnostic suite including AI & Gemini Check, Dual Speaker Channel Isolation, and FiiO KA11 DAC test.
8. **System Info:** Battery voltage / PWL percentage curve, dynamic CPU scaling, microSD usage, and manual Safe Eject toggle.

---

## Navigation & Control Matrix

| Context | Button | Action |
| :--- | :--- | :--- |
| **Main Menu** | UP / DOWN (Short) | Scroll 4-item menu window |
| **Main Menu** | OK / SEL (Short) | Launch selected application |
| **Main Menu** | CANCEL (Hold >650ms) | Enter low-power deep sleep (RTC GPIO 21 wake) |
| **Library Browser**| UP / DOWN (Short) | Browse files, folders, or categories |
| **Library Browser**| OK / SEL (Short) | Play selected track / open folder |
| **Library Browser**| CANCEL (Short) | Return to parent directory / Main Menu |
| **Now Playing** | OK / SEL (Short) | Toggle Play / Pause |
| **Now Playing** | OK / SEL (Hold >650ms) | Cycle Page (`[1/4] Player` → `[2/4] Info` → `[3/4] Lyrics` → `[4/4] Queue`) |
| **Now Playing** | UP / DOWN (Short) | Previous / Next track |
| **Now Playing** | UP / DOWN (Hold >650ms) | Digital volume adjust (with pop-up volume bar) |
| **Now Playing** | CANCEL (Short) | Return to library browser |
| **Visualizer** | UP / DOWN (Short) | Cycle 8 visualizer presets |
| **Visualizer** | OK / SEL (Hold >650ms) | Cycle sensitivity (`LOW` → `NORM` → `HIGH`) |
| **Visualizer** | Any Button | Reveal top HUD (auto-hides after 5 seconds) |
| **Equalizer (DSP)**| UP / DOWN (Short) | Adjust active band gain (-12 dB to +12 dB) |
| **Equalizer (DSP)**| OK / SEL (Short) | Select next frequency band |
| **Equalizer (DSP)**| OK / SEL (Hold >650ms) | Cycle EQ preset (`FLAT`, `BASS BOOST`, `VOCAL`, `CUSTOM`) |
| **AI Companion** | OK / SEL (Short) | Activate INMP441 voice query / listen mode |
| **AI Companion** | UP (Short) | Toggle active companion: **DJ Neko ⇄ Mochi** |
| **AI Companion** | DOWN (Short) | Cycle visual rendering mode (`HYBRID` → `REACTIVE` → `ANIM`) |
| **Any Screen** | CANCEL (Hold >650ms) | Global escape back to Main Menu |

---

## Audiophile DSP Engine

The signal processing engine operates on decoded 16-bit or 24-bit PCM samples before dispatching them to the active output interface:

```
[Decoded PCM] ──► [5-Band Parametric EQ] ──► [Jan Meier Crossfeed] ──► [Soft Limiter] ──► [Output Router]
                                                                                               │
                                                               ┌───────────────────────────────┴──────────┐
                                                               ▼                                          ▼
                                                       [MAX98357A I²S]                            [FiiO KA11 USB]
```

1. **5-Band Parametric EQ:**
   - Pre-warped Bilinear Transform Biquad IIR filters with hardware FPU vectorization.
   - Frequency Bands: `60 Hz` (Low Shelf), `250 Hz` (Peaking), `1 kHz` (Peaking), `4 kHz` (Peaking), `12 kHz` (High Shelf).
   - Gain Range: -12 dB to +12 dB per band with 1 dB resolution.
2. **Jan Meier Analog Crossfeed:**
   - Simulates natural speaker acoustic room blending on headphones by feeding low-pass filtered, time-delayed signals from the opposite channel.
   - Eliminates listening fatigue on hard-panned stereo recordings (e.g. 1960s stereo mixes).
3. **Pseudosymmetric Soft Limiter:**
   - Hyperbolic saturation curve prevents harsh digital clipping when boosting EQ bands near 0 dBFS.

---

## Multi-Page Player & Synced Lyrics

Hold **OK / SEL (>650ms)** on the Now Playing screen to cycle across 4 dedicated views:

- **Page 1 [1/4] Player HUD & Artwork:** Track title, artist, elapsed/total time bar, volume overlay, 8-band mini spectrum analyzer, and 1-bit dithered album cover.
- **Page 2 [2/4] Technical Metadata:** Decoder engine badge (`WAV`, `FLAC`, `MP3`, `M4A`), sample rate (`44.1 kHz`, `48 kHz`, `96 kHz`), bit depth (`16-bit` / `24-bit`), channel layout (`Stereo` / `Mono`), bitrate (`kbps`), file size, and active output (`SPKR` vs `KA11`).
- **Page 3 [3/4] Synchronized LRC Lyrics:**
  - Auto-loads `.lrc` files matching track names or parses embedded ID3v2 `SYLT`, `USLT`, and M4A `©lyr` atoms.
  - Sub-second millisecond synchronization with active line highlighting.
  - Negative scan caching prevents unnecessary SD card read operations on instrumental tracks.
- **Page 4 [4/4] Track Queue & Info:** Displays playlist sequence index, track path, and upcoming queue preview.

---

## AI Companion Subsystem (DJ Neko & Mochi)

### Personas
- **DJ Neko (Cat):** An audiophile feline companion wearing over-ear studio headphones. Features bass-reactive whiskers, tempo-synced head-bobbing, and twitching ears. Responds with playful charm, purrs, and meows.
- **Classic Mochi:** The iconic Dasai companion pet from Q-Watch. Loyal, energetic, and full of character.

### Google Gemini API Integration
- **Context Serialization:** Real-time system state (volume, playback state, track name, EQ preset, battery level) is serialized and provided to Gemini in every prompt.
- **Action Dispatcher:** Gemini can directly execute device actions:
  - `PLAY_TRACK:<name>`, `PAUSE`, `RESUME`, `NEXT`, `PREV`, `SET_VOLUME:<0-100>`
  - `SET_EQ:<preset>`, `SET_CROSSFEED:<mode>`, `SWITCH_OUTPUT:<SPEAKER|KA11>`
  - `SWITCH_COMPANION:<CAT|MOCHI>`, `PLAY_SOUND:<MEOW|PURR|CHIRP>`, `PLAY_ANIM:<name>`
- **Low-Latency Engine:** Uses `"thinkingConfig": {"thinkingLevel": "MINIMAL"}` to bypass prolonged cloud reasoning delays, delivering concise answers in ~4.3 seconds.
- **Zero-Churn Persistent Worker:** Avoids runtime task allocation failures by utilizing a persistent, statically allocated FreeRTOS task on Core 0 signaled via direct-to-task notifications.

### Configuration
Create a text file on the microSD card named `/gemini.txt` (or configure via Web Remote at `http://q-tunes.local/ai`):
```ini
api_key = AIzaSyYourGeminiApiKeyHere
model = gemini-3.5-flash
companion = CAT
```

---

## Wi-Fi Streaming & DLNA MediaRenderer

- **DLNA / UPnP MediaRenderer v1:** Fully compliant with UPnP UDA 1.0, `AVTransport:1`, and `RenderingControl:1`. Compatible with BubbleUPnP, mconnect, AirMusic, and Windows Media Player.
- **384 KB PSRAM Buffer:** Dedicated FreeRTOS Core 0 streaming task buffers network audio to eliminate dropouts.
- **Web Player Remote:** Connect to `http://q-tunes.local` on your smartphone or PC browser:
  - Real-time playback status, track metadata, and progress scrubber.
  - Volume control and output selector.
  - Interactive AI Companion chat and device control interface.
  - Wi-Fi network scanner and credential manager.

---

## MicroSD Safety & Anti-Corruption Protocol

1. **Deep Sleep CS Hold:** `SD_CS` (GPIO 8) is held `HIGH` using `gpio_hold_en((gpio_num_t)SD_CS)` during deep sleep, preventing floating lines from corrupting flash memory controllers.
2. **Brownout Auto-Shutdown:** Hardware ADC monitoring terminates audio playback and unmounts the filesystem before supply voltage drops below `3.35V`.
3. **Manual Safe Eject:** Accessible under **System Info** (`OK / Select` toggles Eject / Mount) to safely flush file caches.
4. **Hot-Unplug Watchdog:** Detects consecutive read timeouts (`>= 15`) and immediately stops audio DMA.
5. **Formatting Recommendations:**
   - Format cards to **FAT32** with 32KB clusters using **GUIFormat** (Windows), `diskutil` (macOS), or `mkfs.vfat` (Linux).
   - Supports SDSC, SDHC, and SDXC (64GB, 128GB, 256GB+).

---

## Project Directory Layout

```
Q-Tune/
├── include/                 # Header declarations
│   ├── ai_companion.h       # DJ Neko & Mochi state machines & 65-face catalog
│   ├── anim_engine.h        # QANM binary animation decoder & PSRAM loader
│   ├── audio_decoder.h      # WAV, FLAC, MP3, and AAC unified decoder classes
│   ├── audio_dsp.h          # 5-Band parametric EQ, crossfeed & soft limiter
│   ├── audio_player.h       # Central audio pipeline & FreeRTOS player task
│   ├── battery.h            # LiPo ADC monitor & PWL discharge curve
│   ├── button_manager.h     # 4-button debounced input engine & hold timing
│   ├── diagnostics.h        # 13-point interactive hardware test suite
│   ├── display.h            # U8g2 OLED graphics engine & menu viewport
│   ├── gemini_client.h      # Layered AI client, persistent worker & parser
│   ├── hw_config.h          # Hardware pinout definitions & GPIO matrix
│   ├── led_manager.h        # FastLED music-reactive lighting engine
│   ├── metadata_parser.h    # ID3v2/v1, Vorbis, M4A ilst tag extractor
│   ├── player_config.h      # NVS persistent configuration storage
│   ├── sd_manager.h         # MicroSD card anti-corruption & safe eject
│   ├── simd_accel.h         # ESP32-S3 PIE LX7 128-bit SIMD acceleration
│   ├── spectrum_analyzer.h  # FFT engine & 8 visualizer presets
│   ├── ui_player.h          # 4-Page Now Playing view & synced LRC lyrics
│   ├── usb_manager.h        # FiiO KA11 UAC 1.0/2.0 USB Host driver
│   └── wifi_streamer.h      # DLNA MediaRenderer, SSDP & Web Remote
├── src/                     # C++ Implementation files
│   ├── main.cpp             # System setup, home menu & application shell
│   └── ...                  # Matching implementation sources
├── sdcard/                  # Recommended microSD root contents
│   ├── anim/                # 65 consolidated .anim companion faces
│   ├── music/               # Music library (.wav, .flac, .mp3, .m4a, .lrc)
│   └── gemini.txt           # Gemini API key and configuration
├── tests/                   # Verification suite & native unit tests
│   ├── test_suite.py        # Automated 33-point verification test runner
│   ├── test_ai_companion_host.cpp # Native C++ host verification binary
│   └── fixtures/            # Test audio files and metadata tags
└── platformio.ini           # PlatformIO configuration & build flags
```

---

## Getting Started & Build Instructions

### Hardware Requirements (BOM)
1. **ESP32-S3 SuperMini** (Dual-Core LX7, 8MB PSRAM, 8MB Flash).
2. **0.96" or 1.3" 128x64 SPI OLED** (SH1106 or SSD1306).
3. **MicroSD Card SPI Module** (with 3.3V LDO or direct 3.3V).
4. **MAX98357A I²S DAC / Amplifier Module** + 4Ω to 8Ω speaker.
5. **INMP441 MEMS I²S Microphone Module**.
6. **FiiO KA11 USB Type-C DAC** (or equivalent UAC 1.0/2.0 DAC dongle).
7. **4x Tactile Push Buttons** (UP, OK, DOWN, CANCEL).
8. **3.7V LiPo Battery** (500mAh - 2000mAh) + 100kΩ/100kΩ voltage divider.

### MicroSD Card Preparation
1. Format your microSD card to **FAT32** with 32KB clusters.
2. Copy the [`sdcard/anim/`](sdcard/anim/) folder from this repository directly to the root of the microSD card.
3. Create a `/music/` folder on the card and copy your audio files (`.flac`, `.mp3`, `.wav`, `.m4a`, `.lrc`) into it.
4. (Optional) Create `/gemini.txt` at the root with your Gemini API key:
   ```ini
   api_key = AIzaSyYourApiKeyHere
   model = gemini-3.5-flash
   companion = CAT
   ```

### Firmware Compilation & Flashing
Install [PlatformIO Core](https://platformio.org/) or the VS Code PlatformIO extension:

```bash
# 1. Clone the repository
git clone https://github.com/your-username/Q-Tune.git
cd Q-Tune

# 2. Build the firmware
pio run

# 3. Connect ESP32-S3 via USB and upload firmware
pio run --target upload

# 4. Open serial monitor (115200 baud)
pio device monitor -b 115200
```

---

## Hardware Diagnostics & Verification Suite

Q-Tunes includes an automated verification test suite verifying all 33 subsystem layers, including native C++ compilation of audio DSP, UAC USB Host, GDMA, and Gemini AI:

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
--- 31. Dual Speaker Diagnostics & Channel Isolation Suite --- [PASS]
--- 32. AI Companion (DJ Neko & Mochi), Gemini API & AnimEngine Suite --- [PASS]
--- 33. Multi-Format Metadata Tag Engine (MP3/FLAC/M4A/WAV) & 4-Page Player Test --- [PASS]

All 33 Q-Tune static/source verification checks PASSED (100%)!
```

---

## License & Acknowledgments

- **License:** Released under the [MIT License](LICENSE).
- **Core Decoders:** `dr_wav` & `dr_flac` by David Reid, `minimp3` by Lieven van der Heide, and `arduino-libhelix` by RealNetworks.
- **Graphics & Audio:** `U8g2` by olikraus, `FastLED` by Daniel Garcia, and `tjpgd` by ChaN.
- **Hardware Heritage:** Dedicated to the **Q-Watch** community for open-source pinout and embedded OS design patterns.
