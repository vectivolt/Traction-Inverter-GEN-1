/* ti_math.h — small single-precision helpers (header-only, no loops without a bound). */
#ifndef TI_MATH_H
#define TI_MATH_H

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#define TI_PI 3.14159265358979f
#define TI_2PI 6.28318530717959f
#define TI_SQRT3 1.73205080756888f
#define TI_SQRT2 1.41421356237310f
#define TI_RPM_PER_RAD_S 9.54929658551372f /* 60 / (2*pi) */

static inline bool ti_finite(float x) { return isfinite(x) != 0; }

static inline float ti_clampf(float x, float lo, float hi)
{
    return (x < lo) ? lo : ((x > hi) ? hi : x);
}

static inline float ti_absf(float x) { return (x < 0.0f) ? -x : x; }

static inline float ti_maxf(float a, float b) { return (a > b) ? a : b; }

static inline float ti_minf(float a, float b) { return (a < b) ? a : b; }

static inline float ti_signf(float x) { return (x > 0.0f) ? 1.0f : ((x < 0.0f) ? -1.0f : 0.0f); }

/* Wrap to [0, 2*pi). Input bounded to a few turns in normal use; fmodf handles any finite value. */
static inline float ti_wrap_2pi(float a)
{
    float r = fmodf(a, TI_2PI);
    if (r < 0.0f) {
        r += TI_2PI;
    }
    if (r >= TI_2PI) {
        r = 0.0f;
    }
    return r;
}

/* Wrap to [-pi, pi). */
static inline float ti_wrap_pi(float a)
{
    return ti_wrap_2pi(a + TI_PI) - TI_PI;
}

/* Move x toward target by at most step (step >= 0). */
static inline float ti_ramp(float x, float target, float step)
{
    if (target > x + step) {
        return x + step;
    }
    if (target < x - step) {
        return x - step;
    }
    return target;
}

#endif /* TI_MATH_H */
