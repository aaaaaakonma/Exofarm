#pragma once
#include "fixed.h"
#include <cstdint>

// Pure CORDIC functions — no singletons, no Godot, no state
// All values in 16.16 fixed point

namespace Cordic {

static constexpr int ITERATIONS = 10;

extern const int32_t ANGLES[ITERATIONS];

// Vectoring mode — single pass, returns both angle and magnitude
// This is the core operation everything else derives from
void    vectoring(int64_t x, int64_t y, int64_t &out_angle, int64_t &out_magnitude);

// Rotation mode — rotates (K_FACTOR, 0) by angle, returns cos in x, sin in y
void    rotation(int64_t &x, int64_t &y, int64_t angle);

// Public derived functions — thin wrappers around the two above
// NOTE: atan2 and magnitude both run vectoring() independently. If you need both, use vectorize().
int64_t atan2(int64_t y, int64_t x);
int64_t magnitude(int64_t x, int64_t y);

// NOTE: cos and sin both run the full 16-iteration CORDIC via from_angle(). If you need both, use from_angle() directly.
int64_t cos(int64_t angle);
int64_t sin(int64_t angle);

struct Vec2 { int64_t x; int64_t y; };
struct Polar { int64_t angle; int64_t magnitude; };

Polar   vectorize(int64_t x, int64_t y);   // one pass, angle + magnitude
Vec2    from_angle(int64_t angle);          // one pass, cos + sin

} // namespace Cordic
