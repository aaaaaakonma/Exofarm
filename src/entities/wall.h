#ifndef WALL_H
#define WALL_H

#include <cstdint>
#include "shapes.h"
#include <vector>

/**
 * Wall utility class for the simulation.
 * Manages static geometry and provides deterministic collision testing
 * against the wall registry.
 */
class Wall {
public:
    // Test a circle (entity) against a list of AABB walls
    // Returns a combined OverlapResult for resolution
    static OverlapResult test_circle(int64_t cx, int64_t cy, int64_t radius, const std::vector<SimAABB> &walls);
    
    // Test a circle against a single wall
    static OverlapResult test_circle_single(int64_t cx, int64_t cy, int64_t radius, const SimAABB &wall);

    // Check if a point is inside any wall
    static bool test_point(int64_t px, int64_t py, const std::vector<SimAABB> &walls);
};

#endif // WALL_H
