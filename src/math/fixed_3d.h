#pragma once
#include "fixed.h"

// ===== Fast Trigonometry =====

// Fast sine approximation using Bhaskara I's formula
// sin(x) = 16*x*(PI - x) / (5*PI^2 - 4*x*(PI - x))
FORCE_INLINE int64_t sin_fixed_positive(int64_t x) {
    if (x < 0) x = 0;
    if (x > PI_FIXED) x = PI_FIXED;
    int64_t pix = PI_FIXED - x;
    int64_t x_pix = mul_fixed(x, pix);
    int64_t num = x_pix << 4; // 16 * x * (pi - x)
    // 5 * PI^2 in 16.16 fixed point:
    // PI = 205887. PI^2 = 646824. 5 * PI^2 = 3234120.
    int64_t pi_sq = mul_fixed(PI_FIXED, PI_FIXED);
    int64_t den = (5 * pi_sq) - (x_pix << 2);
    if (den == 0) return 0;
    return div_fixed(num, den);
}

FORCE_INLINE int64_t sin_fixed(int64_t x) {
    x = normalize_angle(x);
    if (x < 0) return -sin_fixed_positive(-x);
    return sin_fixed_positive(x);
}

FORCE_INLINE int64_t cos_fixed(int64_t x) {
    return sin_fixed(x + HALF_PI);
}

// ===== 3D Vector =====

struct Vec3Fixed {
    int64_t x = 0, y = 0, z = 0;

    Vec3Fixed() = default;
    Vec3Fixed(int64_t _x, int64_t _y, int64_t _z) : x(_x), y(_y), z(_z) {}

    Vec3Fixed operator+(const Vec3Fixed& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3Fixed operator-(const Vec3Fixed& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3Fixed operator*(int64_t scalar) const { return {mul_fixed(x, scalar), mul_fixed(y, scalar), mul_fixed(z, scalar)}; }
    Vec3Fixed operator/(int64_t scalar) const { return {div_fixed(x, scalar), div_fixed(y, scalar), div_fixed(z, scalar)}; }

    int64_t dot(const Vec3Fixed& o) const {
        return mul_fixed(x, o.x) + mul_fixed(y, o.y) + mul_fixed(z, o.z);
    }

    Vec3Fixed cross(const Vec3Fixed& o) const {
        return {
            mul_fixed(y, o.z) - mul_fixed(z, o.y),
            mul_fixed(z, o.x) - mul_fixed(x, o.z),
            mul_fixed(x, o.y) - mul_fixed(y, o.x)
        };
    }

    int64_t length_sq() const {
        return dot(*this);
    }

    int64_t length() const {
        return sqrt_fixed(length_sq());
    }

    Vec3Fixed normalized() const {
        int64_t len_sq = length_sq();
        if (len_sq == 0) return {0, 0, 0};
        int64_t inv_len = inv_sqrt_fixed(len_sq);
        return (*this) * inv_len;
    }
};

// ===== Quaternion =====

struct QuatFixed {
    int64_t x = 0, y = 0, z = 0, w = ONE;

    QuatFixed() = default;
    QuatFixed(int64_t _x, int64_t _y, int64_t _z, int64_t _w) : x(_x), y(_y), z(_z), w(_w) {}

    static QuatFixed from_axis_angle(const Vec3Fixed& axis, int64_t angle) {
        Vec3Fixed n = axis.normalized();
        int64_t half_angle = angle >> 1;
        int64_t s = sin_fixed(half_angle);
        int64_t c = cos_fixed(half_angle);
        QuatFixed q(
            mul_fixed(n.x, s),
            mul_fixed(n.y, s),
            mul_fixed(n.z, s),
            c
        );
        return q.normalized();
    }

    QuatFixed operator*(const QuatFixed& o) const {
        return {
            mul_fixed(w, o.x) + mul_fixed(x, o.w) + mul_fixed(y, o.z) - mul_fixed(z, o.y),
            mul_fixed(w, o.y) - mul_fixed(x, o.z) + mul_fixed(y, o.w) + mul_fixed(z, o.x),
            mul_fixed(w, o.z) + mul_fixed(x, o.y) - mul_fixed(y, o.x) + mul_fixed(z, o.w),
            mul_fixed(w, o.w) - mul_fixed(x, o.x) - mul_fixed(y, o.y) - mul_fixed(z, o.z)
        };
    }

    QuatFixed conjugate() const {
        return {-x, -y, -z, w};
    }

    int64_t length_sq() const {
        return mul_fixed(x, x) + mul_fixed(y, y) + mul_fixed(z, z) + mul_fixed(w, w);
    }

    QuatFixed normalized() const {
        int64_t len_sq = length_sq();
        if (len_sq == 0) return {0, 0, 0, ONE};
        int64_t inv_len = inv_sqrt_fixed(len_sq);
        return {
            mul_fixed(x, inv_len),
            mul_fixed(y, inv_len),
            mul_fixed(z, inv_len),
            mul_fixed(w, inv_len)
        };
    }

    Vec3Fixed rotate_vector(const Vec3Fixed& v) const {
        QuatFixed q_vec(v.x, v.y, v.z, 0);
        QuatFixed res = (*this) * q_vec * this->conjugate();
        return {res.x, res.y, res.z};
    }
};
