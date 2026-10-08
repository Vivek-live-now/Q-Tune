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
#define SPI_MOSI    5   // Shared SPI Master-Out Slave-In (DIN) [OLED_MOSI]
#define SPI_SCK     7   // Shared SPI Clock (CLK) [OLED_CLK]
#define SPI_MISO    6   // Shared SPI Master-In Slave-Out (SD Card DATA_OUT)

#define OLED_CS     4   // OLED Chip Select [LOCKED to Q-Watch]
#define OLED_DC     2   // OLED Data/Command Control [LOCKED to Q-Watch]
#define OLED_RST    41  // OLED Hardware Reset [ADAPTED FROM Q-WATCH: GPIO 41]

// SD Card Chip Select reassigned to GPIO 8 (outer header, freed by OLED_RST moving to 41)
#define SD_CS       8   // microSD Card Chip Select

// ----------------------------------------------------------------------------
// 2. I2S Audio Interface Configuration (MAX98357A I2S DAC / Amp)
// Reclaimed IR TX/RX pins (17, 18).
// DOUT moved to GPIO 10 (outer header pin, clean digital output).
// MAX98357A SD_MODE is tied high/configured in hardware (e.g., 100k pull-up to
// VDD for (L+R)/2 mono mix), saving a GPIO pin.
// ----------------------------------------------------------------------------
#define I2S_BCLK    17  // Bit Clock (Continuous Serial Clock - SCK) [reclaims Q-Watch IR_RX]
#define I2S_LRCK    18  // Left/Right Clock (Word Select - WS) [reclaims Q-Watch IR_TX]
#define I2S_DOUT    10  // Serial Data Output (SDIN / DIN) [Clean outer header GPIO 10]
#define I2S_NUM     I2S_NUM_0

// INMP441 MEMS Microphone Input (Allotted from freed I2C port)
#define I2S_MIC_DIN 15  // Microphone Serial Data In (Reassigned to GPIO 15, freeing UART0 RX 44)

// ----------------------------------------------------------------------------
// 3. Expansion / Reserve Pins (I2C Bus Freed)
// ----------------------------------------------------------------------------
#define GPIO_RESERVE_16 16 // Clean digital expansion / reserve (Freed former I2C_SCL)

// ----------------------------------------------------------------------------
// 4. Navigation Buttons [Internal Pull-Up Active LOW - ADAPTED FROM Q-WATCH]
// ----------------------------------------------------------------------------
#define BTN_UP      39  // K1 Directional UP [MATCHES Q-WATCH]
#define BTN_OK      40  // K1 Directional SELECT / OK [MATCHES Q-WATCH]
#define BTN_DN      42  // K1 Directional DOWN [MATCHES Q-WATCH]
#define BTN_CANCEL  21  // Tactile CANCEL / BACK button (RTC_GPIO16 - Deep Sleep Wake) [MATCHES Q-WATCH]

// Backwards-compatibility alias for 3-button code
#define BTN_SEL     BTN_OK

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
#define UART0_TX    43  // Dedicated Hardware Serial Debugging / Flashing
#define UART0_RX    44  // Dedicated Hardware Serial Debugging / Flashing (Completely Freed)

// Strictly Avoided Strapping/System Pins:
// GPIO 0, 3, 45, 46 (Boot / Strapping - DO NOT USE)

#endif // HW_CONFIG_H
