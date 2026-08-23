#include "cordic.h"

namespace Cordic {

constexpr int32_t ANGLES[ITERATIONS] = {
    29491, 17409, 9198, 4669, 2343, 1172, 586, 293,
    146,   73
};

// ---------------------------------------------------------------------------
// Compile-time loop unrolling via template recursion.
// MSVC has no #pragma GCC unroll equivalent, so we force-inline each step.
// ---------------------------------------------------------------------------

template<int I>
struct VectoringStep {
    static __forceinline void apply(int64_t &x, int64_t &y, int64_t &angle) {
        int64_t mask = y >> 63;
        int64_t dx   = ((y >> I) ^ mask) - mask;
        int64_t dy   = ((x >> I) ^ mask) - mask;
        int64_t da   = (ANGLES[I] ^ mask) - mask;
        x     += dx;
        y     -= dy;
        angle += da;
        VectoringStep<I + 1>::apply(x, y, angle);
    }
};

template<>
struct VectoringStep<ITERATIONS> {
    static __forceinline void apply(int64_t &, int64_t &, int64_t &) {}
};

template<int I>
struct RotationStep {
    static __forceinline void apply(int64_t &x, int64_t &y, int64_t &angle) {
        int64_t mask = angle >> 63;
        int64_t dx   = ((y >> I) ^ mask) - mask;
        int64_t dy   = ((x >> I) ^ mask) - mask;
        int64_t da   = (ANGLES[I] ^ mask) - mask;
        x     -= dx;
        y     += dy;
        angle -= da;
        RotationStep<I + 1>::apply(x, y, angle);
    }
};

template<>
struct RotationStep<ITERATIONS> {
    static __forceinline void apply(int64_t &, int64_t &, int64_t &) {}
};

// ---------------------------------------------------------------------------

void vectoring(int64_t x, int64_t y, int64_t &out_angle, int64_t &out_magnitude) {
    int64_t mask_x = x >> 63;
    int64_t mask_y = y >> 63;

    int64_t angle = mask_x & ((PI_FIXED ^ mask_y) - mask_y);

    x = (x ^ mask_x) - mask_x;
    y = (y ^ mask_x) - mask_x;

    VectoringStep<0>::apply(x, y, angle);

    out_angle     = normalize_angle(angle);
    out_magnitude = mul_fixed(x, K_FACTOR);
}

void rotation(int64_t &x, int64_t &y, int64_t angle) {
    RotationStep<0>::apply(x, y, angle);
}

int64_t atan2(int64_t y, int64_t x) {
    if (x == 0 && y == 0) return 0;
    int64_t angle, mag;
    vectoring(x, y, angle, mag);
    return angle;
}

int64_t magnitude(int64_t x, int64_t y) {
    int64_t angle, mag;
    vectoring(x, y, angle, mag);
    return mag;
}

int64_t cos(int64_t angle) {
    Vec2 v = from_angle(angle);
    return v.x;
}

int64_t sin(int64_t angle) {
    Vec2 v = from_angle(angle);
    return v.y;
}

Polar vectorize(int64_t x, int64_t y) {
    Polar p;
    vectoring(x, y, p.angle, p.magnitude);
    return p;
}

Vec2 from_angle(int64_t angle) {
    int64_t ang = normalize_angle(angle);
    int64_t x   = K_FACTOR;
    int64_t y   = 0;

    // Branchless quadrant adjust.
    // hi_mask = -1 if ang >  HALF_PI, else 0
    // lo_mask = -1 if ang < -HALF_PI, else 0
    int64_t hi_mask   = (HALF_PI - ang)  >> 63;
    int64_t lo_mask   = (ang + HALF_PI)  >> 63;
    int64_t quad_mask = hi_mask | lo_mask;

    // Subtract PI if in upper quadrant, add PI if in lower, else 0
    int64_t adjust      = (hi_mask & -PI_FIXED) | (lo_mask & PI_FIXED);
    int64_t final_angle = ang + adjust;

    rotation(x, y, final_angle);

    // Branchless negate: (v ^ mask) - mask  gives -v when mask=-1, v when mask=0
    return { (x ^ quad_mask) - quad_mask, (y ^ quad_mask) - quad_mask };
}

} // namespace Cordic
