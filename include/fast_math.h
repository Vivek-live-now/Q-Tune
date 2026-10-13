#ifndef FAST_MATH_H
#define FAST_MATH_H

#include <Arduino.h>
#include <math.h>

#ifdef TWO_PI
#undef TWO_PI
#endif

#ifdef HALF_PI
#undef HALF_PI
#endif

#ifdef PI
#undef PI
#endif

namespace FastMath {
    constexpr float TWO_PI = 6.28318530717958647692f;
    constexpr float PI_VAL = 3.14159265358979323846f;
    constexpr float HALF_PI = 1.57079632679489661923f;

    inline float fast_sin(float x) {
        return sinf(x);
    }
    inline float fast_cos(float x) {
        return cosf(x);
    }
    inline float fast_inv_sqrt(float x) {
        return 1.0f / sqrtf(x);
    }
    inline float fast_log10(float x) {
        return log10f(x);
    }
    inline float fast_db20(float linear) {
        return (linear > 0.00001f) ? (20.0f * log10f(linear)) : -100.0f;
    }
}

#endif // FAST_MATH_H
