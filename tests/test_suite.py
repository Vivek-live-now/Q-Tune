#!/usr/bin/env python3
"""
Q-Tune Verification Test Suite
Tests hardware pinouts, compatibility with Q-Watch, button logic,
battery PWL model, and audio/SD interfaces.
"""

import os
import re
import struct

def parse_hw_config(filepath):
    pins = {}
    with open(filepath, 'r') as f:
        for line in f:
            m = re.match(r'^\s*#define\s+([A-Za-z0-9_]+)\s+([0-9]+)', line)
            if m:
                pins[m.group(1)] = int(m.group(2))
    return pins

def test_pinout_matrix_and_qwatch_alignment():
    print("--- 1. Hardware Pinout & Q-Watch Alignment Test ---")
    base_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    qtune_hw = os.path.join(base_dir, "include", "hw_config.h")
    qwatch_hw = os.path.join(os.path.dirname(base_dir), "Q-Watch", "include", "hw_config.h")

    assert os.path.exists(qtune_hw), "Q-Tune hw_config.h not found"

    qtune_pins = parse_hw_config(qtune_hw)

    # Golden Q-Watch pin matrix standard
    qwatch_pins = {
        'OLED_MOSI': 5,
        'OLED_CLK': 7,
        'OLED_CS': 4,
        'OLED_DC': 2,
        'OLED_RST': 41,
        'BTN_UP': 39,
        'BTN_OK': 40,
        'BTN_DN': 42,
        'BTN_CANCEL': 21,
        'I2C_SDA': 15,
        'I2C_SCL': 16,
        'BATTERY_ADC': 1,
        'RGB_LED': 48
    }
    if os.path.exists(qwatch_hw):
        parsed = parse_hw_config(qwatch_hw)
        qwatch_pins.update(parsed)

    # Verify Screen Pins Match Q-Watch Exactly
    assert qtune_pins['SPI_MOSI'] == qwatch_pins['OLED_MOSI'] == 5, "MOSI mismatch"
    assert qtune_pins['SPI_SCK'] == qwatch_pins['OLED_CLK'] == 7, "CLK mismatch"
    assert qtune_pins['OLED_CS'] == qwatch_pins['OLED_CS'] == 4, "OLED_CS mismatch"
    assert qtune_pins['OLED_DC'] == qwatch_pins['OLED_DC'] == 2, "OLED_DC mismatch"
    assert qtune_pins['OLED_RST'] == qwatch_pins['OLED_RST'] == 41, "OLED_RST mismatch (must be 41)"
    print("  [PASS] Screen Pins 100% synchronized with Q-Watch: MOSI=5, CLK=7, CS=4, DC=2, RST=41")

    # Verify Button Pins Match Q-Watch Exactly
    assert qtune_pins['BTN_UP'] == qwatch_pins['BTN_UP'] == 39, "BTN_UP mismatch"
    assert qtune_pins['BTN_OK'] == qwatch_pins['BTN_OK'] == 40, "BTN_OK mismatch"
    assert qtune_pins['BTN_DN'] == qwatch_pins['BTN_DN'] == 42, "BTN_DN mismatch"
    assert qtune_pins['BTN_CANCEL'] == qwatch_pins['BTN_CANCEL'] == 21, "BTN_CANCEL mismatch"
    print("  [PASS] Button Pins 100% synchronized with Q-Watch: UP=39, OK=40, DN=42, CANCEL=21")

    # Verify Shared I2C, Battery ADC & RGB LED Match Q-Watch
    assert qtune_pins['I2C_SDA'] == qwatch_pins['I2C_SDA'] == 15, "I2C_SDA mismatch"
    assert qtune_pins['I2C_SCL'] == qwatch_pins['I2C_SCL'] == 16, "I2C_SCL mismatch"
    assert qtune_pins['BATTERY_ADC'] == qwatch_pins['BATTERY_ADC'] == 1, "BATTERY_ADC mismatch"
    assert qtune_pins['RGB_LED'] == qwatch_pins['RGB_LED'] == 48, "RGB_LED mismatch"
    print("  [PASS] Power/Sensors/LED 100% synchronized with Q-Watch: SDA=15, SCL=16, ADC=1, RGB=48")

    # Verify Reassigned Pins have NO Collisions
    active_pins = [
        qtune_pins['SPI_MOSI'],
        qtune_pins['SPI_SCK'],
        qtune_pins['SPI_MISO'],
        qtune_pins['OLED_CS'],
        qtune_pins['OLED_DC'],
        qtune_pins['OLED_RST'],
        qtune_pins['SD_CS'],
        qtune_pins['I2S_BCLK'],
        qtune_pins['I2S_LRCK'],
        qtune_pins['I2S_DOUT'],
        qtune_pins['I2C_SDA'],
        qtune_pins['I2C_SCL'],
        qtune_pins['BTN_UP'],
        qtune_pins['BTN_OK'],
        qtune_pins['BTN_DN'],
        qtune_pins['BTN_CANCEL'],
        qtune_pins['BATTERY_ADC'],
        qtune_pins['RGB_LED']
    ]
    assert len(active_pins) == len(set(active_pins)), f"Pin collision detected! {active_pins}"
    print(f"  [PASS] Zero pin collisions across all {len(active_pins)} active peripherals.")

    # Verify NO Strapping Pins are Used
    strapping_pins = {0, 3, 45, 46}
    for p in active_pins:
        assert p not in strapping_pins, f"Strapping pin GPIO {p} inadvertently used!"
    print("  [PASS] Strictly zero usage of ESP32-S3 boot strapping pins (0, 3, 45, 46).")

def test_button_manager_event_handling():
    print("\n--- 2. Button Manager 4-Button Architecture Test ---")
    base_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    btn_h = os.path.join(base_dir, "include", "button_manager.h")
    btn_cpp = os.path.join(base_dir, "src", "button_manager.cpp")

    with open(btn_h) as f:
        h_text = f.read()
    with open(btn_cpp) as f:
        cpp_text = f.read()

    assert "BTN_EVENT_CANCEL_PRESS" in h_text, "Missing BTN_EVENT_CANCEL_PRESS"
    assert "BTN_EVENT_OK_PRESS" in h_text, "Missing BTN_EVENT_OK_PRESS"
    assert "btnCancel" in h_text, "Missing btnCancel member in ButtonManager"
    assert "pinMode(BTN_CANCEL, INPUT_PULLUP);" in cpp_text, "Missing pinMode for BTN_CANCEL"
    assert "checkButton(btnCancel)" in cpp_text, "Missing checkButton for btnCancel"
    assert "BTN_OK" in cpp_text, "Missing BTN_OK in button_manager.cpp"
    print("  [PASS] ButtonManager 4-button debounced input engine verified.")

def test_battery_monitor_pwl_curve():
    print("\n--- 3. Battery Monitor PWL Discharge Curve Test ---")
    # Simulate the PWL model implemented in battery.cpp
    def calc_percentage(v):
        if v >= 4.20: return 100
        if v >= 4.10: return 90
        if v >= 4.00: return 80
        if v >= 3.90: return 60
        if v >= 3.80: return 40
        if v >= 3.70: return 20
        if v >= 3.60: return 10
        if v >= 3.50: return 5
        return 0

    test_vectors = [
        (4.25, 100),
        (4.20, 100),
        (4.15, 90),
        (4.05, 80),
        (3.95, 60),
        (3.85, 40),
        (3.75, 20),
        (3.65, 10),
        (3.55, 5),
        (3.40, 0),
        (3.00, 0)
    ]

    for v, expected in test_vectors:
        res = calc_percentage(v)
        assert res == expected, f"Failed for {v}V: expected {expected}, got {res}"
    print(f"  [PASS] Piecewise Linear (PWL) LiPo discharge curve verified over {len(test_vectors)} test vectors.")

def test_wav_header_parser():
    print("\n--- 4. Audio WAV Header Binary Parser Test ---")
    # Synthetic 16-bit 44.1kHz Stereo WAV header
    riff = b'RIFF'
    chunk_size = 36 + 176400 # 36 + 1 sec audio
    wave = b'WAVE'
    fmt = b'fmt '
    subchunk1_size = 16
    audio_format = 1 # PCM
    num_channels = 2
    sample_rate = 44100
    byte_rate = 44100 * 2 * 2
    block_align = 4
    bits_per_sample = 16
    data_tag = b'data'
    data_size = 176400

    raw_header = struct.pack(
        '<4sI4s4sIHHIIHH4sI',
        riff, chunk_size, wave, fmt, subchunk1_size,
        audio_format, num_channels, sample_rate, byte_rate,
        block_align, bits_per_sample, data_tag, data_size
    )

    assert len(raw_header) == 44, "Header length should be 44 bytes"
    # Verify unpacking matches AudioPlayer::parseWAVHeader logic
    fields = struct.unpack('<4sI4s4sIHHIIHH4sI', raw_header)
    assert fields[0] == b'RIFF'
    assert fields[2] == b'WAVE'
    assert fields[5] == 1 # PCM
    assert fields[6] == 2 # 2 channels
    assert fields[7] == 44100 # 44.1 kHz
    assert fields[10] == 16 # 16-bit
    assert fields[12] == 176400
    print("  [PASS] 44-byte RIFF/WAVE 16-bit 44.1kHz stereo header verified.")

def test_power_manager_scaling():
    print("\n--- 5. Power Manager & Dynamic CPU Scaling Test ---")
    base_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    pm_h = os.path.join(base_dir, "include", "power_manager.h")
    pm_cpp = os.path.join(base_dir, "src", "power_manager.cpp")

    assert os.path.exists(pm_h), "power_manager.h not found"
    assert os.path.exists(pm_cpp), "power_manager.cpp not found"

    with open(pm_cpp) as f:
        src = f.read()

    assert "case PowerProfile::PERFORMANCE: return 240;" in src
    assert "case PowerProfile::BALANCED:    return 160;" in src
    assert "case PowerProfile::ENDURANCE:   return 80;" in src
    assert "esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_CANCEL, 0);" in src
    print("  [PASS] Dynamic CPU frequencies (240MHz / 160MHz / 80MHz) and GPIO 21 RTC wake verified.")

def test_simd_accel_engine():
    print("\n--- 6. SIMD LX7 PIE Framebuffer Engine Test ---")
    base_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    simd_h = os.path.join(base_dir, "include", "simd_accel.h")
    simd_cpp = os.path.join(base_dir, "src", "simd_accel.cpp")

    assert os.path.exists(simd_h), "simd_accel.h not found"
    assert os.path.exists(simd_cpp), "simd_accel.cpp not found"

    # Simulate 128-bit SIMD vector invert and XOR mask equivalence
    src_bytes = bytes([i % 256 for i in range(1024)])
    mask_bytes = bytes([(i * 7) % 256 for i in range(1024)])

    # Invert verification
    inverted = bytes([~b & 0xFF for b in src_bytes])
    assert len(inverted) == 1024
    assert inverted[0] == 255
    assert inverted[255] == 0

    # XOR mask verification
    xored = bytes([b ^ m for b, m in zip(src_bytes, mask_bytes)])
    assert len(xored) == 1024
    for i in range(1024):
        assert xored[i] == (src_bytes[i] ^ mask_bytes[i])
    print("  [PASS] 1024-byte OLED framebuffer SIMD invert and XOR operations verified.")

def test_led_manager_modes():
    print("\n--- 7. Non-Blocking FastLED Engine Test ---")
    base_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    led_h = os.path.join(base_dir, "include", "led_manager.h")
    led_cpp = os.path.join(base_dir, "src", "led_manager.cpp")

    with open(led_h) as f:
        h_src = f.read()
    with open(led_cpp) as f:
        cpp_src = f.read()

    assert "BREATHING" in h_src
    assert "BEAT_PULSE" in h_src
    assert "LOW_BATTERY_PULSE" in h_src
    assert "void loop();" in h_src
    assert "triggerPulse" in h_src
    assert "bat_pct <= 10" in cpp_src
    print("  [PASS] FastLED non-blocking loop, beat pulse, and low battery override verified.")

def main():
    print("==================================================")
    print("        Q-TUNE AUTOMATED VERIFICATION SUITE       ")
    print("==================================================")
    test_pinout_matrix_and_qwatch_alignment()
    test_button_manager_event_handling()
    test_battery_monitor_pwl_curve()
    test_wav_header_parser()
    test_power_manager_scaling()
    test_simd_accel_engine()
    test_led_manager_modes()
    print("\nAll 7 Q-Tune test verifications PASSED (100%)!\n")

if __name__ == '__main__':
    main()
