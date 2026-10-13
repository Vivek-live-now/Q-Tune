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

// 128-bit Vector Zeroing
void simd_clear_128(uint8_t* dst, size_t len);

// Audio Stream Vector Operations
// 1. Vector 16-bit PCM volume attenuation: (sample * volumeScale) >> 8
void simd_volume_scale_16(int16_t* dst, const int16_t* src, size_t sampleCount, uint16_t volumeScale);

// 2. Vector stereo-to-mono downmixing & de-interleaving:
//    Separates interleaved [L0, R0, L1, R1, ...] into mono (L+R)/2, left, and right buffers.
void simd_stereo_downmix_16(int16_t* monoDst, int16_t* leftDst, int16_t* rightDst, const int16_t* stereoSrc, size_t frameCount);

// 3. Vector 16-bit integer dot product with 64-bit accumulation:
int64_t simd_dot_product_16(const int16_t* a, const int16_t* b, size_t count);

#ifdef __cplusplus
}
#endif

#endif // SIMD_ACCEL_H
