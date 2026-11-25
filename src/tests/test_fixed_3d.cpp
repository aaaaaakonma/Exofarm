#include "math/fixed_3d.h"
#include "math/noise.h"
#include <iostream>
#include <cmath>
#include <cassert>
#include <cstdlib>

using namespace std;

// Convert double to 16.16 fixed
int64_t d2f(double d) {
    return (int64_t)(d * 65536.0);
}

// Convert 16.16 fixed to double
double f2d(int64_t f) {
    return (double)f / 65536.0;
}

void test_trigonometry() {
    cout << "Testing Bhaskara I Sine Approximation..." << endl;
    for (double deg = -720; deg <= 720; deg += 15.0) {
        double rad = deg * 3.14159265358979323846 / 180.0;
        int64_t rad_fixed = d2f(rad);
        
        int64_t s_fixed = sin_fixed(rad_fixed);
        int64_t c_fixed = cos_fixed(rad_fixed);
        
        double s_actual = sin(rad);
        double c_actual = cos(rad);
        
        double s_diff = abs(s_actual - f2d(s_fixed));
        double c_diff = abs(c_actual - f2d(c_fixed));
        
        assert(s_diff < 0.003 && "Sin error exceeded 0.003 tolerance");
        assert(c_diff < 0.003 && "Cos error exceeded 0.003 tolerance");
    }
    cout << "  [PASS] Trigonometry & multi-revolution angle normalization verified." << endl;
}

void test_quaternion() {
    cout << "\nTesting Quaternion Rotation..." << endl;
    Vec3Fixed axis(0, ONE, 0); // Y axis
    QuatFixed q = QuatFixed::from_axis_angle(axis, HALF_PI); // Rotate 90 degrees (pi/2)
    
    Vec3Fixed v(ONE, 0, 0); // Point at (1, 0, 0)
    Vec3Fixed rotated = q.rotate_vector(v);
    
    cout << "Original Vector: (1, 0, 0)" << endl;
    cout << "Rotated 90 deg around Y: (" << f2d(rotated.x) << ", " << f2d(rotated.y) << ", " << f2d(rotated.z) << ")" << endl;
    
    assert(abs(f2d(rotated.x)) < 0.001);
    assert(abs(f2d(rotated.y)) < 0.001);
    assert(abs(f2d(rotated.z) - (-1.0)) < 0.001);
    cout << "  [PASS] Quaternion 3D rotation verified." << endl;
}

void test_vector_normalization() {
    cout << "\nTesting Vector Normalization across scales (1 to 40,000 units)..." << endl;
    // Regression check for inv_sqrt_fixed Newton-Raphson truncation blowup
    // Prior to fix, any vector with magnitude > 256 units blew up by +125% (magnitude 2.25)
    const int64_t standard_magnitudes[] = {1, 5, 10, 50, 100, 256, 500, 1000};
    
    for (int64_t mag : standard_magnitudes) {
        int64_t mag_fixed = mag * ONE;
        Vec2Fixed v2(mag_fixed, 0);
        Vec2Fixed n2 = v2.normalized();
        double d_len2 = f2d(n2.length());
        
        Vec3Fixed v3(0, mag_fixed, 0);
        Vec3Fixed n3 = v3.normalized();
        double d_len3 = f2d(n3.length());
        
        assert(abs(d_len2 - 1.0) < 0.02 && "Vec2Fixed::normalized magnitude deviated from 1.0!");
        assert(abs(d_len3 - 1.0) < 0.02 && "Vec3Fixed::normalized magnitude deviated from 1.0!");
        
        // Diagonal vectors
        Vec2Fixed v2_diag(mag_fixed, mag_fixed);
        Vec2Fixed n2_diag = v2_diag.normalized();
        assert(abs(f2d(n2_diag.length()) - 1.0) < 0.03);
        
        Vec3Fixed v3_diag(mag_fixed, mag_fixed, mag_fixed);
        Vec3Fixed n3_diag = v3_diag.normalized();
        assert(abs(f2d(n3_diag.length()) - 1.0) < 0.03);
    }
    cout << "  [PASS] Vec2Fixed/Vec3Fixed normalized() unit length verified for standard scales." << endl;

    // Test large magnitudes up to 40,000 units (verifying no 2.25x blowup and bounded behavior)
    const int64_t large_magnitudes[] = {2500, 5000, 10000, 20000, 40000};
    for (int64_t mag : large_magnitudes) {
        int64_t mag_fixed = mag * ONE;
        Vec2Fixed v2(mag_fixed, 0);
        Vec2Fixed n2 = v2.normalized();
        double d_len2 = f2d(n2.length());
        assert(d_len2 >= 0.5 && d_len2 <= 1.01 && "Large vector normalized exceeded 1.0 (2.25x blowup regression)!");

        Vec3Fixed v3(0, mag_fixed, 0);
        Vec3Fixed n3 = v3.normalized();
        double d_len3 = f2d(n3.length());
        assert(d_len3 >= 0.5 && d_len3 <= 1.01 && "Large vector normalized exceeded 1.0 (2.25x blowup regression)!");
    }
    cout << "  [PASS] Large vector normalization verified without 2.25x blowup regression." << endl;

    // Small-vector clamp check (len_sq <= 64 returns zero vector, not 100x explosion)
    Vec2Fixed v2_small(4, 4); // len_sq = 32 <= 64
    Vec2Fixed n2_small = v2_small.normalized();
    assert(n2_small.x == 0 && n2_small.y == 0);

    Vec3Fixed v3_small(3, 3, 3); // len_sq = 27 <= 64
    Vec3Fixed n3_small = v3_small.normalized();
    assert(n3_small.x == 0 && n3_small.y == 0 && n3_small.z == 0);
    cout << "  [PASS] Near-zero vector clamp verified (no 100x explosion)." << endl;

    // Division by zero check
    Vec2Fixed v2_div = Vec2Fixed(100 * ONE, 200 * ONE) / 0;
    assert(v2_div.x == 0 && v2_div.y == 0);

    Vec3Fixed v3_div = Vec3Fixed(100 * ONE, 200 * ONE, 300 * ONE) / 0;
    assert(v3_div.x == 0 && v3_div.y == 0 && v3_div.z == 0);
    cout << "  [PASS] Vector division-by-zero guards verified." << endl;
}

void test_noise_negative_coordinates() {
    cout << "\nTesting Gradient Noise & Voronoi Negative Coordinate Continuity..." << endl;
    uint64_t seed = 1337;
    
    // Sample gradient noise around x=0 boundary to verify continuity and non-flatline
    int64_t prev_n = SimMath::gradient_noise2d(-2 * ONE, 0, seed);
    bool non_constant = false;
    
    for (int64_t x = -2 * ONE; x <= 2 * ONE; x += ONE / 16) {
        int64_t n = SimMath::gradient_noise2d(x, 0, seed);
        assert(n >= -ONE && n <= ONE && "Gradient noise out of [-ONE, ONE] bounds!");
        if (n != prev_n) non_constant = true;
        // Verify continuity (no jumps > 0.5 within 1/16 step)
        int64_t step_delta = abs_fixed(n - prev_n);
        assert(step_delta < (ONE / 2) && "Gradient noise discontinuous jump detected!");
        prev_n = n;
    }
    assert(non_constant && "Gradient noise along negative coordinates was erroneously constant!");
    cout << "  [PASS] Gradient noise negative coordinate continuity verified." << endl;

    // Sample Voronoi across negative coordinates
    for (int64_t x = -3 * ONE; x <= 3 * ONE; x += ONE / 4) {
        for (int64_t y = -3 * ONE; y <= 3 * ONE; y += ONE / 4) {
            SimMath::VoronoiResult v = SimMath::voronoi2d(x, y, seed);
            assert(v.dist_sq >= 0 && "Voronoi distance squared negative!");
            assert(v.cell_id != 0 && "Voronoi cell id invalid!");
        }
    }
    cout << "  [PASS] Voronoi negative coordinate sampling verified." << endl;
}

int main() {
    test_trigonometry();
    test_quaternion();
    test_vector_normalization();
    test_noise_negative_coordinates();

    cout << "\n==========================================" << endl;
    cout << "All Math Suite Verification Tests PASSED!" << endl;
    cout << "==========================================" << endl;
    return 0;
}
