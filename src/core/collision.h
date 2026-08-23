#pragma once
#include "shapes.h"
#include <cstdint>

// Pure collision functions — no singletons, no Godot, no state
// Caller is responsible for canonical ordering of input arrays

namespace Collision {

// ===== BOOLEAN TESTS =====
bool circle_vs_circle(int64_t x1, int64_t y1, int64_t r1,
                      int64_t x2, int64_t y2, int64_t r2);

bool aabb_vs_aabb(int64_t x1, int64_t y1, int64_t w1, int64_t h1,
                  int64_t x2, int64_t y2, int64_t w2, int64_t h2);

bool circle_vs_aabb(int64_t cx, int64_t cy, int64_t r,
                    int64_t bx, int64_t by, int64_t bw, int64_t bh);

bool point_in_circle(int64_t px, int64_t py,
                     int64_t cx, int64_t cy, int64_t r);

bool point_in_aabb(int64_t px, int64_t py,
                   int64_t bx, int64_t by, int64_t bw, int64_t bh);

// ===== OVERLAP RESOLUTION =====
OverlapResult circle_vs_circle_overlap(int64_t x1, int64_t y1, int64_t r1,
                                        int64_t x2, int64_t y2, int64_t r2);
OverlapResult circle_vs_aabb_overlap(int64_t cx, int64_t cy, int64_t r,
                                      int64_t bx, int64_t by, int64_t bw, int64_t bh);

OverlapResult circle_vs_polygon_overlap(int64_t cx, int64_t cy, int64_t r, const SimPolygon& poly);

} // namespace Collision
