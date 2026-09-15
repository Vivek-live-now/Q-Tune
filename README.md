# 007 Q-Tune (Audiophile Edition)

A portable, standalone music player built on the ESP32-S3 SuperMini board, inheriting proven pinout, power, and board layout assumptions from the Q-Watch architecture, with FiiO KA11 USB DAC High-Res audio support.

## Overview

Q-Tune provides reliable, low-latency audio playback from an SPI microSD card through either an I²S DAC/amplifier (MAX98357A) driving an onboard speaker or an external **FiiO KA11 (CS43131)** High-Res USB DAC via USB OTG Host mode. It includes an interactive 1.3" SPI OLED display, 3 debounced control buttons, battery voltage monitoring, WS2812 RGB status LED, and INMP441 MEMS microphone audio spectrum visualizers.

---

## Hardware Architecture & Pin Allocation Matrix

The GPIO map for Q-Tune preserves Q-Watch's pin research while reallocating unneeded watch peripherals (IR TX/RX, Buzzer) to audio, microphone, USB host, and SD card interfaces.

### 1. Shared SPI Bus (OLED Display + microSD Card)
| Peripheral | Signal | GPIO | Notes |
| :--- | :--- | :--- | :--- |
| OLED / SD | MOSI (DIN) | 5 | Preserved from Q-Watch |
| OLED / SD | SCK (CLK) | 7 | Preserved from Q-Watch |
| SD Card | MISO (DATA_OUT) | 6 | Reclaimed from Q-Watch Buzzer |
| OLED | CS | 4 | Preserved from Q-Watch |
| OLED | DC | 2 | Preserved from Q-Watch |
| OLED | RST | 8 | Preserved from Q-Watch |
| microSD | CS | 41 | Allocated from Q-Watch Reserve pin |

### 2. I²S Audio Output (MAX98357A) & INMP441 MEMS Microphone
| Peripheral | Signal | GPIO | Notes |
| :--- | :--- | :--- | :--- |
| Audio Output / Mic | BCLK (Continuous SCK) | 17 | Reclaimed from Q-Watch IR RX |
| Audio Output / Mic | LRCLK / WS (Word Select) | 18 | Reclaimed from Q-Watch IR TX |
| MAX98357A DAC | DOUT (Serial Data Output) | 42 | Allocated from Q-Watch Reserve pin |
| INMP441 Mic | DIN (Serial Data Input) | 44 | Reclaimed UART0 RX / Expansion |

### 3. FiiO KA11 Audiophile USB DAC Interface (USB OTG Host)
| Peripheral | Signal | GPIO | Hardware Circuit |
| :--- | :--- | :--- | :--- |
| FiiO KA11 DAC | USB D- | 19 | ESP32-S3 Native USB OTG Host |
| FiiO KA11 DAC | USB D+ | 20 | ESP32-S3 Native USB OTG Host |
| FiiO KA11 VBUS | 5V Power | -- | Require 3.7V->5V Boost Converter (150mA supply) |

#### MAX98357A SD_MODE & MCLK Configuration
- **MCLK**: Not required by MAX98357A (uses internal PLL).
- **SD_MODE**: Connected in hardware with a 100kΩ pull-up resistor to VDD. This configures the DAC to compute a **(Left + Right) / 2 mono mix** for speaker playback without consuming an extra GPIO pin.

### 4. Shared I²C Bus (Expansion Peripherals)
| Signal | GPIO | Notes |
| :--- | :--- | :--- |
| SDA | 15 | Preserved from Q-Watch |
| SCL | 16 | Preserved from Q-Watch |

### 5. Navigation Inputs
| Button | GPIO | Logic | Notes |
| :--- | :--- | :--- | :--- |
| Button Up | 39 | INPUT_PULLUP (Active LOW) | Reclaims JTAG MTCK |
| Button Select | 21 | INPUT_PULLUP (Active LOW) | RTC Wake Capable |
| Button Down | 40 | INPUT_PULLUP (Active LOW) | |

### 6. Power & Diagnostics
| Function | GPIO | Hardware Circuit |
| :--- | :--- | :--- |
| Battery Monitor | 1 (ADC1_CH0) | 100kΩ / 100kΩ external divider from LiPo VBAT + 104 filter cap |
| Onboard RGB LED | 48 | Built-in WS2812 RGB LED |

---

## Interactive Diagnostic Menu

On startup, Q-Tune launches an interactive hardware diagnostic menu allowing verification of:
1. **OLED Display** - Border & text rendering
2. **SD Card Detect** - Bus communication check
3. **SD Filesystem** - Total & used capacity report
4. **WAV Discovery** - `/music/` folder listing
5. **Button Inputs** - Debounced button state check
6. **I2C Bus Scan** - Connected slave address scan
7. **Battery ADC** - Live voltage & percentage display
8. **RGB LED Test** - WS2812 color cycle
9. **I2S Audio Test** - Synthesized 1kHz test tone through MAX98357A
10. **INMP441 Mic Visualizer** - Audio spectrum and MilkDrop-style visualizer presets
11. **FiiO KA11 USB DAC** - USB Host connection test

---

## Getting Started

1. Open the project in PlatformIO.
2. Build and flash the project to an ESP32-S3 SuperMini:
   ```bash
   pio run --target upload
   ```
3. Format a microSD card (FAT32), create a `/music/` folder, and copy 16-bit 44.1kHz WAV, MP3, or FLAC files into it.
4. Connect your FiiO KA11 DAC to the USB OTG Type-C female port powered by 5V.
5. Insert the SD card into Q-Tune and power on.
