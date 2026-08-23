#pragma once
#include <cstdint>

#ifdef _MSC_VER
#define FORCE_INLINE __forceinline
#else
#define FORCE_INLINE inline __attribute__((always_inline))
#endif

// 16.16 fixed point
static constexpr int64_t ONE          = 65536;
static constexpr int64_t PI_FIXED     = 205887;   // PI * 65536
static constexpr int64_t HALF_PI      = 102944;
static constexpr int64_t TWO_PI       = 411775;
static constexpr int64_t K_FACTOR     = 39797;    // CORDIC gain correction * 65536

inline int64_t mul_fixed(int64_t a, int64_t b) { return (a * b) >> 16; }
inline int64_t div_fixed(int64_t a, int64_t b) { return (a << 16) / b; }
inline int64_t abs_fixed(int64_t a)            { return a < 0 ? -a : a; }
inline int64_t min_fixed(int64_t a, int64_t b) { return a < b ? a : b; }
inline int64_t max_fixed(int64_t a, int64_t b) { return a > b ? a : b; }

// Pure-integer square root for 16.16 fixed point.
// Uses binary search: finds r such that r*r == x in 16.16 space.
// We want sqrt(x) where x is 16.16, result is 16.16.
// Mathematically: result = sqrt(x_raw) << 8 (since sqrt(2^16) = 2^8).
// We compute isqrt(x_raw) via binary search, then shift up by 8.
// Two Newton-Raphson iterations refine the result to sub-ULP precision.
inline int64_t sqrt_fixed(int64_t x) {
    if (x <= 0) return 0;

    // Integer square root of the raw value via binary search.
    // Find largest r such that r*r <= x_raw.
    uint64_t val = (uint64_t)x;
    uint64_t r = 0;
    // Highest bit position to start: for 16.16 values, max raw is ~2^47
    // so sqrt is at most ~2^24. Start bit at 2^23.
    uint64_t bit = 1ULL << 23;
    while (bit > 0) {
        uint64_t candidate = r | bit;
        if (candidate * candidate <= val) {
            r = candidate;
        }
        bit >>= 1;
    }

    // r is now isqrt(x_raw). Convert to 16.16:
    // sqrt(x_fixed) = sqrt(x_raw / 2^16) = sqrt(x_raw) / 2^8
    // In 16.16 output: result_raw = sqrt(x_raw) * 2^16 / 2^8 = sqrt(x_raw) * 2^8
    int64_t res = (int64_t)(r << 8);

    // Two Newton-Raphson iterations in fixed-point to refine sub-bit precision.
    // N-R for sqrt: x_{n+1} = (x_n + S / x_n) / 2
    if (res > 0) {
        res = (res + div_fixed(x, res)) >> 1;
        res = (res + div_fixed(x, res)) >> 1;
    }
    return res;
}

// Pure-integer inverse square root for 16.16 fixed point.
// Returns 1/sqrt(x) in 16.16 format.
// Uses sqrt_fixed then divides: inv_sqrt(x) = 1 / sqrt(x) = ONE / sqrt(x).
// Two Newton-Raphson iterations refine the result.
inline int64_t inv_sqrt_fixed(int64_t x) {
    if (x <= 64) return 65536 * 100; // Cap for very small values

    int64_t s = sqrt_fixed(x);
    if (s <= 0) return 65536 * 100;
    int64_t res = div_fixed(ONE, s);

    // Two Newton-Raphson iterations for inv_sqrt:
    // x_{n+1} = x_n * (3 - val * x_n^2) / 2
    for (int i = 0; i < 2; ++i) {
        int64_t x0_sq = (res * res) >> 16;
        int64_t x_x0_sq = (x * x0_sq) >> 16;
        int64_t three_minus = (3LL << 16) - x_x0_sq;
        res = (res * three_minus) >> 17;
    }

    return res;
}

inline int64_t normalize_angle(int64_t angle) {
    while (angle > PI_FIXED) angle -= TWO_PI;
    while (angle < -PI_FIXED) angle += TWO_PI;
    return angle;
}

// Distance from point (px, py) to line segment (x1, y1)-(x2, y2) in 16.16 fixed point
inline int64_t point_to_segment_dist_sq(int64_t px, int64_t py, int64_t x1, int64_t y1, int64_t x2, int64_t y2) {
    int64_t dx = x2 - x1;
    int64_t dy = y2 - y1;
    int64_t len_sq = mul_fixed(dx, dx) + mul_fixed(dy, dy);
    if (len_sq <= 16) {
        int64_t dpx = px - x1;
        int64_t dpy = py - y1;
        return mul_fixed(dpx, dpx) + mul_fixed(dpy, dpy);
    }

    int64_t t = div_fixed(mul_fixed(px - x1, dx) + mul_fixed(py - y1, dy), len_sq);
    t = (t < 0) ? 0 : (t > ONE ? ONE : t);

    int64_t proj_x = x1 + mul_fixed(dx, t);
    int64_t proj_y = y1 + mul_fixed(dy, t);

    int64_t ex = px - proj_x;
    int64_t ey = py - proj_y;
    return mul_fixed(ex, ex) + mul_fixed(ey, ey);
}
