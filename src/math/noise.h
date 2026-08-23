#pragma once
#include "fixed.h"
#include "rng.h"
#include <cstdint>
#include <algorithm>

namespace SimMath {

// High-performance 2D integer hash
inline uint32_t hash2d(int32_t x, int32_t y, uint64_t seed) {
    uint64_t h = seed ^ (static_cast<uint64_t>(static_cast<uint32_t>(x)) * 0x1f1f1f1f1f1f1f1fULL) ^ 
                        (static_cast<uint64_t>(static_cast<uint32_t>(y)) * 0x517cc1b727220a95ULL);
    h = (h ^ (h >> 30)) * 0xbf58476d1ce4e5b9ULL;
    h = (h ^ (h >> 27)) * 0x94d049bb133111ebULL;
    return static_cast<uint32_t>(h ^ (h >> 31));
}

// 5th-order Quintic smooth interpolation polynomial: 6t^5 - 15t^4 + 10t^3
// Input t is [0, ONE], output is [0, ONE]
inline int64_t quintic_smooth(int64_t t) {
    if (t <= 0) return 0;
    if (t >= ONE) return ONE;
    int64_t t2 = (t * t) >> 16;
    int64_t t3 = (t2 * t) >> 16;
    int64_t inner = (t * 6 - (15 * ONE)); // 16.16
    inner = (t * inner) >> 16;            // t * (t * 6 - 15)
    inner = inner + (10 * ONE);           // t * (t * 6 - 15) + 10
    return (t3 * inner) >> 16;
}

// Cubic Hermite smoothstep: 3t^2 - 2t^3
inline int64_t hermite_smooth(int64_t t) {
    if (t <= 0) return 0;
    if (t >= ONE) return ONE;
    int64_t t2 = (t * t) >> 16;
    int64_t three_minus_2t = (3 * ONE) - (2 * t);
    return (t2 * three_minus_2t) >> 16;
}

// Fixed-point 16.16 2D Gradient Noise (returns [-ONE, ONE])
inline int64_t gradient_noise2d(int64_t x_fixed, int64_t y_fixed, uint64_t seed) {
    int32_t x0 = static_cast<int32_t>(x_fixed >> 16);
    int32_t y0 = static_cast<int32_t>(y_fixed >> 16);
    if (x_fixed < 0 && (x_fixed & 0xFFFF) != 0) x0--;
    if (y_fixed < 0 && (y_fixed & 0xFFFF) != 0) y0--;

    int32_t x1 = x0 + 1;
    int32_t y1 = y0 + 1;

    int64_t fx0 = x_fixed - (static_cast<int64_t>(x0) << 16);
    int64_t fy0 = y_fixed - (static_cast<int64_t>(y0) << 16);
    int64_t fx1 = fx0 - ONE;
    int64_t fy1 = fy0 - ONE;

    static const int32_t GRAD_X[8] = { 65536, -65536,      0,       0,  46341, -46341,  46341, -46341 };
    static const int32_t GRAD_Y[8] = {     0,      0,  65536, -65536,  46341,  46341, -46341, -46341 };

    auto dot_grad = [&](int32_t ix, int32_t iy, int64_t dx, int64_t dy) -> int64_t {
        uint32_t h = hash2d(ix, iy, seed);
        int g = h & 7;
        return (dx * GRAD_X[g] + dy * GRAD_Y[g]) >> 16;
    };

    int64_t n00 = dot_grad(x0, y0, fx0, fy0);
    int64_t n10 = dot_grad(x1, y0, fx1, fy0);
    int64_t n01 = dot_grad(x0, y1, fx0, fy1);
    int64_t n11 = dot_grad(x1, y1, fx1, fy1);

    int64_t wx = quintic_smooth(fx0);
    int64_t wy = quintic_smooth(fy0);

    int64_t nx0 = n00 + mul_fixed(wx, n10 - n00);
    int64_t nx1 = n01 + mul_fixed(wx, n11 - n01);

    return nx0 + mul_fixed(wy, nx1 - nx0);
}

// Fractal Brownian Motion (fBm) in 16.16 fixed-point (returns approx [-ONE, ONE])
inline int64_t fbm2d(int64_t x_fixed, int64_t y_fixed, uint64_t seed, int octaves = 4, int64_t persistence = 32768, int64_t lacunarity = 131072) {
    int64_t total = 0;
    int64_t amp = ONE;
    int64_t max_amp = 0;
    int64_t cur_x = x_fixed;
    int64_t cur_y = y_fixed;
    uint64_t cur_seed = seed;

    for (int i = 0; i < octaves; ++i) {
        int64_t n = gradient_noise2d(cur_x, cur_y, cur_seed);
        total += mul_fixed(n, amp);
        max_amp += amp;

        amp = mul_fixed(amp, persistence);
        cur_x = mul_fixed(cur_x, lacunarity);
        cur_y = mul_fixed(cur_y, lacunarity);
        cur_seed = cur_seed * 6364136223846793005ULL + 1442695040888963407ULL;
    }

    if (max_amp > 0) {
        return div_fixed(total, max_amp);
    }
    return total;
}

// Ridge Noise for sharp mountain ridges: (ONE - abs(noise))^2
inline int64_t ridge_noise2d(int64_t x_fixed, int64_t y_fixed, uint64_t seed, int octaves = 4) {
    int64_t total = 0;
    int64_t amp = ONE;
    int64_t max_amp = 0;
    int64_t cur_x = x_fixed;
    int64_t cur_y = y_fixed;
    uint64_t cur_seed = seed;
    int64_t weight = ONE;

    for (int i = 0; i < octaves; ++i) {
        int64_t n = gradient_noise2d(cur_x, cur_y, cur_seed);
        int64_t signal = ONE - abs_fixed(n);
        signal = mul_fixed(signal, signal);
        signal = mul_fixed(signal, weight);
        weight = std::clamp(signal * 2, 0LL, ONE);

        total += mul_fixed(signal, amp);
        max_amp += amp;

        amp = mul_fixed(amp, 32768); // 0.5
        cur_x = mul_fixed(cur_x, 131072); // 2.0
        cur_y = mul_fixed(cur_y, 131072);
        cur_seed = cur_seed * 6364136223846793005ULL + 1442695040888963407ULL;
    }

    if (max_amp > 0) {
        return div_fixed(total, max_amp);
    }
    return total;
}

// Domain Warped fBm: fbm(p + fbm(p + warp_offset))
inline int64_t domain_warped_fbm2d(int64_t x_fixed, int64_t y_fixed, uint64_t seed, int64_t warp_scale = 131072) {
    int64_t qx = fbm2d(x_fixed + 345 * ONE, y_fixed + 123 * ONE, seed + 101, 3);
    int64_t qy = fbm2d(x_fixed + 789 * ONE, y_fixed + 987 * ONE, seed + 202, 3);

    int64_t rx = x_fixed + mul_fixed(qx, warp_scale);
    int64_t ry = y_fixed + mul_fixed(qy, warp_scale);

    return fbm2d(rx, ry, seed, 4);
}

// Fixed-point 2D Cellular / Voronoi Noise
struct VoronoiResult {
    int64_t dist_sq; // in 16.16 fixed-point
    int32_t cell_x;
    int32_t cell_y;
    uint32_t cell_id;
};

inline VoronoiResult voronoi2d(int64_t x_fixed, int64_t y_fixed, uint64_t seed) {
    int32_t x0 = static_cast<int32_t>(x_fixed >> 16);
    int32_t y0 = static_cast<int32_t>(y_fixed >> 16);
    if (x_fixed < 0 && (x_fixed & 0xFFFF) != 0) x0--;
    if (y_fixed < 0 && (y_fixed & 0xFFFF) != 0) y0--;

    int64_t min_dist_sq = 0x7FFFFFFFFFFFFFFF;
    int32_t best_cx = x0;
    int32_t best_cy = y0;
    uint32_t best_id = 0;

    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            int32_t cx = x0 + dx;
            int32_t cy = y0 + dy;
            uint32_t h = hash2d(cx, cy, seed);

            int64_t px = (static_cast<int64_t>(cx) << 16) + (mul_fixed((h & 0xFFFF), 52428) + 6553);
            int64_t py = (static_cast<int64_t>(cy) << 16) + (mul_fixed(((h >> 16) & 0xFFFF), 52428) + 6553);

            int64_t diff_x = x_fixed - px;
            int64_t diff_y = y_fixed - py;
            int64_t d2 = mul_fixed(diff_x, diff_x) + mul_fixed(diff_y, diff_y);

            if (d2 < min_dist_sq) {
                min_dist_sq = d2;
                best_cx = cx;
                best_cy = cy;
                best_id = h;
            }
        }
    }

    return { min_dist_sq, best_cx, best_cy, best_id };
}

} // namespace SimMath
