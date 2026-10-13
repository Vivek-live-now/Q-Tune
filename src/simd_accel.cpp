#if __has_include("simd_accel.h")
#include "simd_accel.h"
#else
#include "../include/simd_accel.h"
#endif
#include <string.h>

#if defined(__GNUC__) || defined(__clang__)
#define ALIGNED16 __attribute__((aligned(16)))
#else
#define ALIGNED16
#endif

void simd_invert_128(uint8_t* dst, const uint8_t* src, size_t len) {
    if (!dst || !src || len == 0) return;

    if ((((uintptr_t)dst | (uintptr_t)src) & 0xF) != 0 || len < 16) {
        for (size_t i = 0; i < len; i++) {
            dst[i] = ~src[i];
        }
        return;
    }

#if defined(CONFIG_IDF_TARGET_ESP32S3) && !defined(NO_SIMD)
    size_t chunks = len / 16;
    size_t remainder = len % 16;
    uint8_t* d = dst;
    const uint8_t* s = src;
    asm volatile (
        "1:\n"
        "ee.vld.128.ip  q0, %1, 16\n"
        "ee.notq        q0, q0\n"
        "ee.vst.128.ip  q0, %0, 16\n"
        "addi           %2, %2, -1\n"
        "bnez           %2, 1b\n"
        : "+r"(d), "+r"(s), "+r"(chunks)
        :
        : "memory"
    );
    for (size_t i = 0; i < remainder; i++) {
        d[i] = ~s[i];
    }
#else
    for (size_t i = 0; i < len; i++) {
        dst[i] = ~src[i];
    }
#endif
}

void simd_xor_mask_128(uint8_t* dst, const uint8_t* src, const uint8_t* mask, size_t len) {
    if (!dst || !src || !mask || len == 0) return;

    if ((((uintptr_t)dst | (uintptr_t)src | (uintptr_t)mask) & 0xF) != 0 || len < 16) {
        for (size_t i = 0; i < len; i++) {
            dst[i] = src[i] ^ mask[i];
        }
        return;
    }

#if defined(CONFIG_IDF_TARGET_ESP32S3) && !defined(NO_SIMD)
    size_t chunks = len / 16;
    size_t remainder = len % 16;
    uint8_t* d = dst;
    const uint8_t* s = src;
    const uint8_t* m = mask;
    asm volatile (
        "1:\n"
        "ee.vld.128.ip  q0, %1, 16\n"
        "ee.vld.128.ip  q1, %2, 16\n"
        "ee.xorq        q0, q0, q1\n"
        "ee.vst.128.ip  q0, %0, 16\n"
        "addi           %3, %3, -1\n"
        "bnez           %3, 1b\n"
        : "+r"(d), "+r"(s), "+r"(m), "+r"(chunks)
        :
        : "memory"
    );
    for (size_t i = 0; i < remainder; i++) {
        d[i] = s[i] ^ m[i];
    }
#else
    for (size_t i = 0; i < len; i++) {
        dst[i] = src[i] ^ mask[i];
    }
#endif
}

void simd_and_mask_128(uint8_t* dst, const uint8_t* src, const uint8_t* mask, size_t len) {
    if (!dst || !src || !mask || len == 0) return;

    if ((((uintptr_t)dst | (uintptr_t)src | (uintptr_t)mask) & 0xF) != 0 || len < 16) {
        for (size_t i = 0; i < len; i++) {
            dst[i] = src[i] & mask[i];
        }
        return;
    }

#if defined(CONFIG_IDF_TARGET_ESP32S3) && !defined(NO_SIMD)
    size_t chunks = len / 16;
    size_t remainder = len % 16;
    uint8_t* d = dst;
    const uint8_t* s = src;
    const uint8_t* m = mask;
    asm volatile (
        "1:\n"
        "ee.vld.128.ip  q0, %1, 16\n"
        "ee.vld.128.ip  q1, %2, 16\n"
        "ee.andq        q0, q0, q1\n"
        "ee.vst.128.ip  q0, %0, 16\n"
        "addi           %3, %3, -1\n"
        "bnez           %3, 1b\n"
        : "+r"(d), "+r"(s), "+r"(m), "+r"(chunks)
        :
        : "memory"
    );
    for (size_t i = 0; i < remainder; i++) {
        d[i] = s[i] & m[i];
    }
#else
    for (size_t i = 0; i < len; i++) {
        dst[i] = src[i] & mask[i];
    }
#endif
}

void simd_or_mask_128(uint8_t* dst, const uint8_t* src, const uint8_t* mask, size_t len) {
    if (!dst || !src || !mask || len == 0) return;

    if ((((uintptr_t)dst | (uintptr_t)src | (uintptr_t)mask) & 0xF) != 0 || len < 16) {
        for (size_t i = 0; i < len; i++) {
            dst[i] = src[i] | mask[i];
        }
        return;
    }

#if defined(CONFIG_IDF_TARGET_ESP32S3) && !defined(NO_SIMD)
    size_t chunks = len / 16;
    size_t remainder = len % 16;
    uint8_t* d = dst;
    const uint8_t* s = src;
    const uint8_t* m = mask;
    asm volatile (
        "1:\n"
        "ee.vld.128.ip  q0, %1, 16\n"
        "ee.vld.128.ip  q1, %2, 16\n"
        "ee.orq         q0, q0, q1\n"
        "ee.vst.128.ip  q0, %0, 16\n"
        "addi           %3, %3, -1\n"
        "bnez           %3, 1b\n"
        : "+r"(d), "+r"(s), "+r"(m), "+r"(chunks)
        :
        : "memory"
    );
    for (size_t i = 0; i < remainder; i++) {
        d[i] = s[i] | m[i];
    }
#else
    for (size_t i = 0; i < len; i++) {
        dst[i] = src[i] | mask[i];
    }
#endif
}

void simd_clear_128(uint8_t* dst, size_t len) {
    if (!dst || len == 0) return;

    if (((uintptr_t)dst & 0xF) != 0 || len < 16) {
        memset(dst, 0, len);
        return;
    }

#if defined(CONFIG_IDF_TARGET_ESP32S3) && !defined(NO_SIMD)
    size_t chunks = len / 16;
    size_t remainder = len % 16;
    uint8_t* d = dst;
    asm volatile (
        "ee.xorq        q0, q0, q0\n"
        "1:\n"
        "ee.vst.128.ip  q0, %0, 16\n"
        "addi           %1, %1, -1\n"
        "bnez           %1, 1b\n"
        : "+r"(d), "+r"(chunks)
        :
        : "memory"
    );
    for (size_t i = 0; i < remainder; i++) {
        d[i] = 0;
    }
#else
    memset(dst, 0, len);
#endif
}

void simd_volume_scale_16(int16_t* dst, const int16_t* src, size_t sampleCount, uint16_t volumeScale) {
    if (!dst || !src || sampleCount == 0) return;
    if (volumeScale >= 256) {
        if (dst != src) {
            memcpy(dst, src, sampleCount * sizeof(int16_t));
        }
        return;
    }
    if (volumeScale == 0) {
        simd_clear_128((uint8_t*)dst, sampleCount * sizeof(int16_t));
        return;
    }

    size_t i = 0;
    size_t chunks = sampleCount / 8;
    int32_t scale = (int32_t)volumeScale;

    // 8-sample unrolled loop for high-throughput pipeline execution
    for (size_t c = 0; c < chunks; c++) {
        size_t idx = c * 8;
        dst[idx]     = (int16_t)(((int32_t)src[idx]     * scale) >> 8);
        dst[idx + 1] = (int16_t)(((int32_t)src[idx + 1] * scale) >> 8);
        dst[idx + 2] = (int16_t)(((int32_t)src[idx + 2] * scale) >> 8);
        dst[idx + 3] = (int16_t)(((int32_t)src[idx + 3] * scale) >> 8);
        dst[idx + 4] = (int16_t)(((int32_t)src[idx + 4] * scale) >> 8);
        dst[idx + 5] = (int16_t)(((int32_t)src[idx + 5] * scale) >> 8);
        dst[idx + 6] = (int16_t)(((int32_t)src[idx + 6] * scale) >> 8);
        dst[idx + 7] = (int16_t)(((int32_t)src[idx + 7] * scale) >> 8);
    }
    i = chunks * 8;

    for (; i < sampleCount; i++) {
        dst[i] = (int16_t)(((int32_t)src[i] * scale) >> 8);
    }
}

void simd_stereo_downmix_16(int16_t* monoDst, int16_t* leftDst, int16_t* rightDst, const int16_t* stereoSrc, size_t frameCount) {
    if (!stereoSrc || frameCount == 0) return;

    size_t i = 0;
    size_t chunks = frameCount / 4;
    for (size_t c = 0; c < chunks; c++) {
        size_t idx = c * 4;
        size_t sIdx = idx * 2;

        int16_t l0 = stereoSrc[sIdx],     r0 = stereoSrc[sIdx + 1];
        int16_t l1 = stereoSrc[sIdx + 2], r1 = stereoSrc[sIdx + 3];
        int16_t l2 = stereoSrc[sIdx + 4], r2 = stereoSrc[sIdx + 5];
        int16_t l3 = stereoSrc[sIdx + 6], r3 = stereoSrc[sIdx + 7];

        if (leftDst) {
            leftDst[idx]     = l0;
            leftDst[idx + 1] = l1;
            leftDst[idx + 2] = l2;
            leftDst[idx + 3] = l3;
        }
        if (rightDst) {
            rightDst[idx]     = r0;
            rightDst[idx + 1] = r1;
            rightDst[idx + 2] = r2;
            rightDst[idx + 3] = r3;
        }
        if (monoDst) {
            monoDst[idx]     = (int16_t)(((int32_t)l0 + r0) / 2);
            monoDst[idx + 1] = (int16_t)(((int32_t)l1 + r1) / 2);
            monoDst[idx + 2] = (int16_t)(((int32_t)l2 + r2) / 2);
            monoDst[idx + 3] = (int16_t)(((int32_t)l3 + r3) / 2);
        }
    }
    i = chunks * 4;

    for (; i < frameCount; i++) {
        int16_t l = stereoSrc[i * 2];
        int16_t r = stereoSrc[i * 2 + 1];
        if (leftDst) leftDst[i] = l;
        if (rightDst) rightDst[i] = r;
        if (monoDst) monoDst[i] = (int16_t)(((int32_t)l + r) / 2);
    }
}

int64_t simd_dot_product_16(const int16_t* a, const int16_t* b, size_t count) {
    if (!a || !b || count == 0) return 0;

    int64_t acc = 0;
    size_t i = 0;
    size_t chunks = count / 8;
    for (size_t c = 0; c < chunks; c++) {
        size_t idx = c * 8;
        int64_t p0 = (int64_t)a[idx]     * b[idx];
        int64_t p1 = (int64_t)a[idx + 1] * b[idx + 1];
        int64_t p2 = (int64_t)a[idx + 2] * b[idx + 2];
        int64_t p3 = (int64_t)a[idx + 3] * b[idx + 3];
        int64_t p4 = (int64_t)a[idx + 4] * b[idx + 4];
        int64_t p5 = (int64_t)a[idx + 5] * b[idx + 5];
        int64_t p6 = (int64_t)a[idx + 6] * b[idx + 6];
        int64_t p7 = (int64_t)a[idx + 7] * b[idx + 7];
        acc += (p0 + p1 + p2 + p3 + p4 + p5 + p6 + p7);
    }
    i = chunks * 8;

    for (; i < count; i++) {
        acc += (int64_t)a[i] * b[i];
    }
    return acc;
}
