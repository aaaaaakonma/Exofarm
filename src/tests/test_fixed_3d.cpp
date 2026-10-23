#include "math/fixed_3d.h"
#include <iostream>
#include <cmath>
#include <cassert>

using namespace std;

// Convert double to 16.16 fixed
int64_t d2f(double d) {
    return (int64_t)(d * 65536.0);
}

// Convert 16.16 fixed to double
double f2d(int64_t f) {
    return (double)f / 65536.0;
}

int main() {
    cout << "Testing Bhaskara I Sine Approximation..." << endl;
    for (double deg = 0; deg <= 360; deg += 15.0) {
        double rad = deg * 3.14159265358979323846 / 180.0;
        int64_t rad_fixed = d2f(rad);
        
        int64_t s_fixed = sin_fixed(rad_fixed);
        int64_t c_fixed = cos_fixed(rad_fixed);
        
        double s_actual = sin(rad);
        double c_actual = cos(rad);
        
        cout << "Angle: " << deg << " deg" << endl;
        cout << "  Sin: Expected=" << s_actual << " Got=" << f2d(s_fixed) << " Diff=" << abs(s_actual - f2d(s_fixed)) << endl;
        cout << "  Cos: Expected=" << c_actual << " Got=" << f2d(c_fixed) << " Diff=" << abs(c_actual - f2d(c_fixed)) << endl;
    }

    cout << "\nTesting Quaternion Rotation..." << endl;
    Vec3Fixed axis(0, ONE, 0); // Y axis
    QuatFixed q = QuatFixed::from_axis_angle(axis, HALF_PI); // Rotate 90 degrees (pi/2)
    
    Vec3Fixed v(ONE, 0, 0); // Point at (1, 0, 0)
    Vec3Fixed rotated = q.rotate_vector(v);
    
    cout << "Original Vector: (1, 0, 0)" << endl;
    cout << "Rotated 90 deg around Y: (" << f2d(rotated.x) << ", " << f2d(rotated.y) << ", " << f2d(rotated.z) << ")" << endl;
    
    // It should be (0, 0, -1) in right-handed, or depending on sign.
    // Fixed point sine of 90 is 1.0, cos is 0.0.
    
    return 0;
}
