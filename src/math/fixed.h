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
static constexpr int64_t TWO_PI       = 411774;
static constexpr int64_t K_FACTOR     = 39797;    // CORDIC gain correction * 65536

inline int64_t mul_fixed(int64_t a, int64_t b) { return (a * b) >> 16; }
inline int64_t div_fixed(int64_t a, int64_t b) { if (b == 0) return 0; return (static_cast<int64_t>(static_cast<uint64_t>(a) << 16)) / b; }
inline int64_t abs_fixed(int64_t a)            { return a < 0 ? -a : a; }
inline int64_t min_fixed(int64_t a, int64_t b) { return a < b ? a : b; }
inline int64_t max_fixed(int64_t a, int64_t b) { return a > b ? a : b; }

inline float   fixed_to_float(int64_t x)       { return (float)x / 65536.0f; }
inline double  fixed_to_double(int64_t x)      { return (double)x / 65536.0; }
inline int64_t float_to_fixed(float x)         { return (int64_t)(x * 65536.0f); }
inline int64_t double_to_fixed(double x)       { return (int64_t)(x * 65536.0); }

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
// Note: div_fixed(ONE, s) provides exact 1-LSB precision since sqrt_fixed has already converged.
inline int64_t inv_sqrt_fixed(int64_t x) {
    if (x <= 64) return 65536 * 100; // Cap for very small values

    int64_t s = sqrt_fixed(x);
    if (s <= 0) return 65536 * 100;
    return div_fixed(ONE, s);
}

// Normalize an angle (16.16 radians) into [-PI, PI] in O(1) using integer modular arithmetic.
// Q3 FIX: the previous while-loop was O(|angle| / TWO_PI), which could iterate thousands of
// times for large accumulated angles (e.g. from velocity integration). This version is O(1)
// and deterministic regardless of input magnitude.
inline int64_t normalize_angle(int64_t angle) {
    // Reduce modulo TWO_PI into [0, TWO_PI)
    angle = angle % TWO_PI;
    // Shift into [-PI, PI]
    if (angle > PI_FIXED)  angle -= TWO_PI;
    if (angle < -PI_FIXED) angle += TWO_PI;
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
