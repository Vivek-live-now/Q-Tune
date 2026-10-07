#ifndef SIMD_ACCEL_H
#define SIMD_ACCEL_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// OLED 128-bit SIMD Vector Engine
// Inverts len bytes of buffer (128-bit / 16-byte vector chunks with scalar tail fallback)
void simd_invert_128(uint8_t* dst, const uint8_t* src, size_t len);

// Bitwise XOR mask (useful for transparent HUD overlays, cursor masks, visualizers)
void simd_xor_mask_128(uint8_t* dst, const uint8_t* src, const uint8_t* mask, size_t len);

// Bitwise AND mask
void simd_and_mask_128(uint8_t* dst, const uint8_t* src, const uint8_t* mask, size_t len);

// Bitwise OR mask
void simd_or_mask_128(uint8_t* dst, const uint8_t* src, const uint8_t* mask, size_t len);

#ifdef __cplusplus
}
#endif

#endif // SIMD_ACCEL_H
