#ifndef HW_CONFIG_H
#define HW_CONFIG_H

// ============================================================================
// Q-TUNE HARDWARE CONFIGURATION & GPIO ALLOCATION MATRIX
// Inherited & Extended from Q-Watch Architecture (ESP32-S3 SuperMini)
// ============================================================================

// ----------------------------------------------------------------------------
// 1. Shared SPI Bus Configuration (OLED Display + microSD Card)
// Note: OLED and SD card share physical SPI clock and MOSI line.
// MISO is required for SD card reads (reclaimed from Q-Watch Buzzer GPIO 6).
// ----------------------------------------------------------------------------
#define SPI_MOSI    5   // Shared SPI Master-Out Slave-In (DIN)
#define SPI_SCK     7   // Shared SPI Clock (CLK)
#define SPI_MISO    6   // Shared SPI Master-In Slave-Out (SD Card DATA_OUT)

#define OLED_CS     4   // OLED Chip Select
#define OLED_DC     2   // OLED Data/Command Control
#define OLED_RST    8   // OLED Hardware Reset

#define SD_CS       41  // microSD Card Chip Select (Allocated from Q-Watch Reserve)

// ----------------------------------------------------------------------------
// 2. I2S Audio Interface Configuration (MAX98357A I2S DAC / Amp)
// Reclaimed IR TX/RX pins (17, 18) and Q-Watch Reserve pin (42).
// MAX98357A SD_MODE is tied high/configured in hardware (e.g., 100k pull-up to
// VDD for (L+R)/2 mono mix), saving a GPIO pin.
// ----------------------------------------------------------------------------
#define I2S_BCLK    17  // Bit Clock (Continuous Serial Clock - SCK)
#define I2S_LRCK    18  // Left/Right Clock (Word Select - WS)
#define I2S_DOUT    42  // Serial Data Output (SDIN / DIN)
#define I2S_NUM     I2S_NUM_0

// ----------------------------------------------------------------------------
// 3. Shared I2C Bus Configuration [External Peripherals / Expansion]
// ----------------------------------------------------------------------------
#define I2C_SDA     15  // Shared I2C Data Line
#define I2C_SCL     16  // Shared I2C Clock Line

// ----------------------------------------------------------------------------
// 4. Navigation Buttons [Internal Pull-Up Active LOW]
// ----------------------------------------------------------------------------
#define BTN_UP      39  // Up / Previous Track
#define BTN_SEL     21  // Select / Play / Pause (RTC Wake Capable)
#define BTN_DN      40  // Down / Next Track

// ----------------------------------------------------------------------------
// 5. Power & Diagnostics
// ----------------------------------------------------------------------------
// Battery Monitor ADC (Uses 100k/100k voltage divider to raw VBAT)
#define BATTERY_ADC 1   // ADC1_CH0 pin (non-strapping)

// Onboard WS2812 RGB LED
#define RGB_LED     48  // SuperMini Built-in RGB LED

// ----------------------------------------------------------------------------
// 6. Serial & Reserved / Avoided Pins
// ----------------------------------------------------------------------------
#define UART0_TX    43  // Reserved for Hardware Serial Debugging
#define UART0_RX    44  // Reserved for Hardware Serial Debugging

// Strictly Avoided Strapping/System Pins:
// GPIO 0, 3, 45, 46 (Boot / Strapping - DO NOT USE)

#endif // HW_CONFIG_H
