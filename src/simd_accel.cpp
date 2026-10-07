#include "simd_accel.h"
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
