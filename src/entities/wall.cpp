#include "wall.h"
#include "collision.h"

OverlapResult Wall::test_circle_single(int64_t cx, int64_t cy, int64_t radius, const SimAABB &wall) {
    return Collision::circle_vs_aabb_overlap(
        cx, cy, radius,
        wall.x, wall.y, wall.width, wall.height
    );
}


OverlapResult Wall::test_circle(int64_t cx, int64_t cy, int64_t radius, const std::vector<SimAABB> &walls) {
    OverlapResult combined;
    
    for (const auto &wall : walls) {
        OverlapResult res = Collision::circle_vs_aabb_overlap(
            cx, cy, radius,
            wall.x, wall.y, wall.width, wall.height
        );
        
        if (res.hit) {
            // For simple combined resolution, we take the one with largest penetration
            // or we could accumulate them. Here we just return the first hit for simplicity
            // as SimWorld usually handles multiple iterations.
            if (!combined.hit || res.penetration > combined.penetration) {
                combined = res;
            }
        }
    }
    
    return combined;
}

bool Wall::test_point(int64_t px, int64_t py, const std::vector<SimAABB> &walls) {
    for (const auto &wall : walls) {
        if (Collision::point_in_aabb(px, py, wall.x, wall.y, wall.width, wall.height)) {
            return true;
        }
    }
    return false;
}
