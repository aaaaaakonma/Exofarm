#pragma once
#include "fixed.h"
#include <algorithm>
#include <vector>
#include <cstdint>

namespace SimMath {

// 1D Catmull-Rom Cubic Spline Interpolation in 16.16 fixed-point
// Evaluates cubic polynomial through 4 control points p0, p1, p2, p3 at fractional offset t in [0, ONE].
// C^1 continuous first derivative.
inline int64_t cubic_catmull_rom(int64_t p0, int64_t p1, int64_t p2, int64_t p3, int64_t t) {
    if (t <= 0) return p1;
    if (t >= ONE) return p2;

    int64_t c3 = -p0 + (3 * p1) - (3 * p2) + p3;
    int64_t c2 = (2 * p0) - (5 * p1) + (4 * p2) - p3;
    int64_t c1 = -p0 + p2;
    int64_t c0 = 2 * p1;

    // Horner's evaluation: (((c3 * t + c2) * t + c1) * t + c0) / 2
    int64_t v = mul_fixed(c3, t) + c2;
    v = mul_fixed(v, t) + c1;
    v = mul_fixed(v, t) + c0;
    return v >> 1;
}

// 1D Catmull-Rom First Derivative (df/dt) in 16.16 fixed-point
inline int64_t cubic_derivative(int64_t p0, int64_t p1, int64_t p2, int64_t p3, int64_t t) {
    t = std::clamp(t, 0LL, ONE);

    int64_t c3 = -p0 + (3 * p1) - (3 * p2) + p3;
    int64_t c2 = (2 * p0) - (5 * p1) + (4 * p2) - p3;
    int64_t c1 = -p0 + p2;

    // Derivative: (3 * c3 * t^2 + 2 * c2 * t + c1) / 2
    int64_t v = mul_fixed(3 * c3, t) + (2 * c2);
    v = mul_fixed(v, t) + c1;
    return v >> 1;
}

// 2D Bicubic Grid Sampling Helper
// grid: array of size grid_w * grid_h
// x_fixed, y_fixed: world coordinates in 16.16
// step_fixed: world distance between adjacent control nodes (e.g. 2 * ONE)
inline int64_t bicubic_sample_2d(const int64_t* grid, int32_t grid_w, int32_t grid_h,
                                 int64_t x_fixed, int64_t y_fixed, int64_t step_fixed) {
    if (grid_w <= 0 || grid_h <= 0 || step_fixed <= 0) return 0;
    if (grid_w == 1 && grid_h == 1) return grid[0];

    int64_t max_x_fixed = static_cast<int64_t>(grid_w - 1) * step_fixed;
    int64_t max_y_fixed = static_cast<int64_t>(grid_h - 1) * step_fixed;

    int64_t clamped_x = std::clamp(x_fixed, 0LL, max_x_fixed);
    int64_t clamped_y = std::clamp(y_fixed, 0LL, max_y_fixed);

    int32_t gx = static_cast<int32_t>(clamped_x / step_fixed);
    int32_t gy = static_cast<int32_t>(clamped_y / step_fixed);

    if (gx >= grid_w - 1) gx = grid_w - 2;
    if (gy >= grid_h - 1) gy = grid_h - 2;
    if (gx < 0) gx = 0;
    if (gy < 0) gy = 0;

    int64_t rem_x = clamped_x - (static_cast<int64_t>(gx) * step_fixed);
    int64_t rem_y = clamped_y - (static_cast<int64_t>(gy) * step_fixed);

    int64_t tx = div_fixed(rem_x, step_fixed);
    int64_t ty = div_fixed(rem_y, step_fixed);

    auto get_val = [&](int32_t ix, int32_t iy) -> int64_t {
        ix = std::clamp(ix, 0, grid_w - 1);
        iy = std::clamp(iy, 0, grid_h - 1);
        return grid[static_cast<size_t>(iy) * grid_w + ix];
    };

    int64_t col[4];
    for (int j = -1; j <= 2; ++j) {
        int32_t iy = gy + j;
        int64_t p0 = get_val(gx - 1, iy);
        int64_t p1 = get_val(gx + 0, iy);
        int64_t p2 = get_val(gx + 1, iy);
        int64_t p3 = get_val(gx + 2, iy);
        col[j + 1] = cubic_catmull_rom(p0, p1, p2, p3, tx);
    }

    return cubic_catmull_rom(col[0], col[1], col[2], col[3], ty);
}

// 2D Bicubic Gradient Evaluation (dH/dx, dH/dy) in 16.16 fixed-point (elevation delta per tile unit)
inline void bicubic_gradient_2d(const int64_t* grid, int32_t grid_w, int32_t grid_h,
                                int64_t x_fixed, int64_t y_fixed, int64_t step_fixed,
                                int64_t& out_gx, int64_t& out_gy) {
    if (grid_w <= 0 || grid_h <= 0 || step_fixed <= 0) {
        out_gx = 0;
        out_gy = 0;
        return;
    }

    int64_t max_x_fixed = static_cast<int64_t>(grid_w - 1) * step_fixed;
    int64_t max_y_fixed = static_cast<int64_t>(grid_h - 1) * step_fixed;

    int64_t clamped_x = std::clamp(x_fixed, 0LL, max_x_fixed);
    int64_t clamped_y = std::clamp(y_fixed, 0LL, max_y_fixed);

    int32_t gx = static_cast<int32_t>(clamped_x / step_fixed);
    int32_t gy = static_cast<int32_t>(clamped_y / step_fixed);

    if (gx >= grid_w - 1) gx = grid_w - 2;
    if (gy >= grid_h - 1) gy = grid_h - 2;
    if (gx < 0) gx = 0;
    if (gy < 0) gy = 0;

    int64_t rem_x = clamped_x - (static_cast<int64_t>(gx) * step_fixed);
    int64_t rem_y = clamped_y - (static_cast<int64_t>(gy) * step_fixed);

    int64_t tx = div_fixed(rem_x, step_fixed);
    int64_t ty = div_fixed(rem_y, step_fixed);

    auto get_val = [&](int32_t ix, int32_t iy) -> int64_t {
        ix = std::clamp(ix, 0, grid_w - 1);
        iy = std::clamp(iy, 0, grid_h - 1);
        return grid[static_cast<size_t>(iy) * grid_w + ix];
    };

    int64_t col_val[4];
    int64_t col_dx[4];

    for (int j = -1; j <= 2; ++j) {
        int32_t iy = gy + j;
        int64_t p0 = get_val(gx - 1, iy);
        int64_t p1 = get_val(gx + 0, iy);
        int64_t p2 = get_val(gx + 1, iy);
        int64_t p3 = get_val(gx + 2, iy);

        col_val[j + 1] = cubic_catmull_rom(p0, p1, p2, p3, tx);
        int64_t d_raw = cubic_derivative(p0, p1, p2, p3, tx);
        // df/dx = (df/dt) * (dt/dx) = (df/dt) * (ONE / step_fixed)
        col_dx[j + 1] = div_fixed(d_raw, step_fixed);
    }

    // dH/dx is vertical blend of horizontal derivatives
    out_gx = cubic_catmull_rom(col_dx[0], col_dx[1], col_dx[2], col_dx[3], ty);

    // dH/dy is vertical derivative of horizontal blended values
    int64_t dy_raw = cubic_derivative(col_val[0], col_val[1], col_val[2], col_val[3], ty);
    out_gy = div_fixed(dy_raw, step_fixed);
}

// 2D Bicubic Slope Magnitude: sqrt(gx^2 + gy^2)
inline int64_t bicubic_slope_2d(const int64_t* grid, int32_t grid_w, int32_t grid_h,
                                int64_t x_fixed, int64_t y_fixed, int64_t step_fixed) {
    int64_t gx = 0, gy = 0;
    bicubic_gradient_2d(grid, grid_w, grid_h, x_fixed, y_fixed, step_fixed, gx, gy);
    int64_t g_sq = mul_fixed(gx, gx) + mul_fixed(gy, gy);
    return sqrt_fixed(g_sq);
}

} // namespace SimMath
