#ifndef HW_CONFIG_H
#define HW_CONFIG_H

// ============================================================================
// Q-TUNE HARDWARE CONFIGURATION & GPIO ALLOCATION MATRIX
// Inherited & Extended from Q-Watch Architecture (ESP32-S3 SuperMini)
// Audiophile Edition: MAX98357A Mono Speaker + FiiO KA11 USB DAC (CS43131)
// ============================================================================

// ----------------------------------------------------------------------------
// 1. Shared SPI Bus Configuration (OLED Display + microSD Card)
// OLED and SD card share physical SPI clock (7) and MOSI line (5).
// MISO (6) is required for SD card reads (reclaimed from Q-Watch Buzzer GPIO 6).
// ----------------------------------------------------------------------------
#define SPI_MOSI    5   // Shared SPI Master-Out Slave-In (DIN)
#define SPI_SCK     7   // Shared SPI Clock (CLK)
#define SPI_MISO    6   // Shared SPI Master-In Slave-Out (SD Card DATA_OUT)

#define OLED_CS     4   // OLED Chip Select
#define OLED_DC     2   // OLED Data/Command Control
#define OLED_RST    8   // OLED Hardware Reset

#define SD_CS       41  // microSD Card Chip Select (Allocated from Q-Watch Reserve)

// ----------------------------------------------------------------------------
// 2. I2S Audio Output (MAX98357A) & INMP441 MEMS Microphone
// Reclaimed IR TX/RX pins (17, 18) for shared I2S BCLK and LRCK.
// MAX98357A DOUT uses GPIO 42 (Allocated from Q-Watch Reserve).
// INMP441 Microphone DIN uses GPIO 44 (Reclaimed UART0 RX / Expansion).
// ----------------------------------------------------------------------------
#define I2S_BCLK    17  // Bit Clock (Shared continuous SCK for DAC and Mic)
#define I2S_LRCK    18  // Left/Right Clock (Shared WS for DAC and Mic)
#define I2S_DOUT    42  // Serial Data Output to MAX98357A DAC/Amp
#define MIC_DIN     44  // Serial Data Input from INMP441 MEMS Microphone
#define I2S_NUM     I2S_NUM_0

// ----------------------------------------------------------------------------
// 3. FiiO KA11 Audiophile USB Audio Host Interface (CS43131 High-Res DAC)
// Uses ESP32-S3 native USB OTG controller pins in USB Host Mode.
// Connects to a USB Type-C female port powered by 5V (via 3.7V->5V Boost Conv).
// ----------------------------------------------------------------------------
#define USB_HOST_DM 19  // USB Native D-
#define USB_HOST_DP 20  // USB Native D+

// ----------------------------------------------------------------------------
// 4. Shared I2C Bus Configuration [External Peripherals / Expansion]
// ----------------------------------------------------------------------------
#define I2C_SDA     15  // Shared I2C Data Line
#define I2C_SCL     16  // Shared I2C Clock Line

// ----------------------------------------------------------------------------
// 5. Navigation Buttons [Internal Pull-Up Active LOW]
// ----------------------------------------------------------------------------
#define BTN_UP      39  // Up / Previous Track
#define BTN_SEL     21  // Select / Play / Pause (RTC Wake Capable)
#define BTN_DN      40  // Down / Next Track

// ----------------------------------------------------------------------------
// 6. Power & Diagnostics
// ----------------------------------------------------------------------------
#define BATTERY_ADC 1   // ADC1_CH0 pin (non-strapping)
#define RGB_LED     48  // SuperMini Built-in WS2812 RGB LED

// ----------------------------------------------------------------------------
// 7. Serial & Reserved / Avoided Pins
// ----------------------------------------------------------------------------
#define UART0_TX    43  // Reserved for Hardware Serial Debugging

// Strictly Avoided Strapping/System Pins:
// GPIO 0, 3, 45, 46 (Boot / Strapping - DO NOT USE)

#endif // HW_CONFIG_H
