#include "collision.h"
#include "cordic.h"

namespace Collision {

// ===== BOOLEAN TESTS =====

bool circle_vs_circle(int64_t x1, int64_t y1, int64_t r1,
                      int64_t x2, int64_t y2, int64_t r2) {
    int64_t dx = x2 - x1, dy = y2 - y1;
    int64_t radii = r1 + r2;
    return mul_fixed(dx,dx) + mul_fixed(dy,dy) <= mul_fixed(radii,radii);
}

bool aabb_vs_aabb(int64_t x1, int64_t y1, int64_t w1, int64_t h1,
                  int64_t x2, int64_t y2, int64_t w2, int64_t h2) {
    return (x1 < x2+w2) && (x1+w1 > x2) && (y1 < y2+h2) && (y1+h1 > y2);
}

bool circle_vs_aabb(int64_t cx, int64_t cy, int64_t r,
                    int64_t bx, int64_t by, int64_t bw, int64_t bh) {
    // OPT: cache right/bottom edges once; avoid recomputing bx+bw, by+bh twice
    int64_t bx2 = bx + bw, by2 = by + bh;
    int64_t dx = cx - max_fixed(bx, min_fixed(cx, bx2));
    int64_t dy = cy - max_fixed(by, min_fixed(cy, by2));
    return mul_fixed(dx,dx) + mul_fixed(dy,dy) <= mul_fixed(r,r);
}

bool point_in_circle(int64_t px, int64_t py, int64_t cx, int64_t cy, int64_t r) {
    int64_t dx = px-cx, dy = py-cy;
    return mul_fixed(dx,dx) + mul_fixed(dy,dy) <= mul_fixed(r,r);
}

bool point_in_aabb(int64_t px, int64_t py,
                   int64_t bx, int64_t by, int64_t bw, int64_t bh) {
    return px >= bx && px <= bx+bw && py >= by && py <= by+bh;
}

// ===== OVERLAP RESOLUTION =====

OverlapResult circle_vs_circle_overlap(int64_t x1, int64_t y1, int64_t r1,
                                        int64_t x2, int64_t y2, int64_t r2) {
    OverlapResult result;
    int64_t dx = x1 - x2;
    int64_t dy = y1 - y2;

    // OPT: squared early-exit before the expensive Cordic::magnitude call
    int64_t radii_sum    = r1 + r2;
    int64_t radii_sum_sq = mul_fixed(radii_sum, radii_sum);
    int64_t dist_sq      = mul_fixed(dx,dx) + mul_fixed(dy,dy);
    if (dist_sq >= radii_sum_sq) return result;

    int64_t dist = sqrt_fixed(dist_sq);
    if (dist >= radii_sum) return result; // guard for fixed-point rounding

    result.hit         = true;
    result.penetration = radii_sum - dist;

    if (dist < 10) {
        result.normal_x = 0;
        result.normal_y = ONE;
    } else {
        result.normal_x = div_fixed(dx, dist);
        result.normal_y = div_fixed(dy, dist);
    }
    return result;
}

OverlapResult circle_vs_aabb_overlap(int64_t cx, int64_t cy, int64_t r,
                                      int64_t bx, int64_t by, int64_t bw, int64_t bh) {
    OverlapResult result;

    // OPT: cache edges once — used in multiple expressions below
    int64_t bx2 = bx + bw;
    int64_t by2 = by + bh;

    // OPT: fast inside check avoids closest-point math and magnitude entirely
    bool inside = (cx >= bx && cx <= bx2 && cy >= by && cy <= by2);
    if (inside) {
        result.hit = true;
        int64_t d_left   = cx - bx;
        int64_t d_right  = bx2 - cx;
        int64_t d_top    = cy - by;
        int64_t d_bottom = by2 - cy;
        int64_t min_d    = min_fixed(min_fixed(d_left, d_right), min_fixed(d_top, d_bottom));

        if      (min_d == d_left)   { result.normal_x = -ONE; result.normal_y =    0; result.penetration = r + d_left;   }
        else if (min_d == d_right)  { result.normal_x =  ONE; result.normal_y =    0; result.penetration = r + d_right;  }
        else if (min_d == d_top)    { result.normal_x =    0; result.normal_y = -ONE; result.penetration = r + d_top;    }
        else                        { result.normal_x =    0; result.normal_y =  ONE; result.penetration = r + d_bottom; }
        return result;
    }

    int64_t closest_x = max_fixed(bx, min_fixed(cx, bx2));
    int64_t closest_y = max_fixed(by, min_fixed(cy, by2));
    int64_t dx        = cx - closest_x;
    int64_t dy        = cy - closest_y;

    // OPT: squared early-exit before calling Cordic::magnitude
    int64_t r_sq    = mul_fixed(r, r);
    int64_t dist_sq = mul_fixed(dx,dx) + mul_fixed(dy,dy);
    if (dist_sq > r_sq) return result;

    int64_t dist = sqrt_fixed(dist_sq);
    if (dist > r) return result; // guard for fixed-point rounding

    result.hit         = true;
    if (dist < 10) {
        // Fallback normal if penetration is microscopic to avoid divide-by-zero
        result.normal_x = 0;
        result.normal_y = ONE;
    } else {
        result.normal_x = div_fixed(dx, dist);
        result.normal_y = div_fixed(dy, dist);
    }
    result.penetration = r - dist;
    return result;
}

OverlapResult circle_vs_polygon_overlap(int64_t cx, int64_t cy, int64_t r, const SimPolygon& poly) {
    OverlapResult result;
    if (poly.vx.size() < 3) return result;

    // Adjust cx, cy to be relative to polygon origin so we can work in local space (or add poly.x/y to vertices)
    int64_t lcx = cx - poly.x;
    int64_t lcy = cy - poly.y;

    int64_t min_overlap = 0x7FFFFFFFFFFFFFFF;
    int64_t best_nx = 0;
    int64_t best_ny = 0;

    int num_vertices = (int)poly.vx.size();

    // 1. Test all edge normals
    for (int i = 0; i < num_vertices; ++i) {
        int j = (i + 1) % num_vertices;
        int64_t ex = poly.vx[j] - poly.vx[i];
        int64_t ey = poly.vy[j] - poly.vy[i];

        // Normal (outward assuming CCW vertices in Y-down Godot means normal is (ey, -ex) or (-ey, ex))
        // We'll just test the axis and make sure the polygon is projected correctly.
        int64_t nx = ey;
        int64_t ny = -ex;

        // Normalize
        int64_t len_sq = mul_fixed(nx, nx) + mul_fixed(ny, ny);
        if (len_sq == 0) continue;
        int64_t len = sqrt_fixed(len_sq);
        if (len == 0) continue;
        nx = div_fixed(nx, len);
        ny = div_fixed(ny, len);

        // Project polygon
        int64_t poly_min = 0x7FFFFFFFFFFFFFFF;
        int64_t poly_max = -0x7FFFFFFFFFFFFFFF;
        for (int k = 0; k < num_vertices; ++k) {
            int64_t proj = mul_fixed(poly.vx[k], nx) + mul_fixed(poly.vy[k], ny);
            if (proj < poly_min) poly_min = proj;
            if (proj > poly_max) poly_max = proj;
        }

        // Project circle
        int64_t circ_center_proj = mul_fixed(lcx, nx) + mul_fixed(lcy, ny);
        int64_t circ_min = circ_center_proj - r;
        int64_t circ_max = circ_center_proj + r;

        // Check overlap
        if (circ_max <= poly_min || circ_min >= poly_max) {
            return result; // Separating axis found
        }

        // Overlap amounts
        int64_t overlap1 = poly_max - circ_min;
        int64_t overlap2 = circ_max - poly_min;
        int64_t overlap = min_fixed(overlap1, overlap2);

        if (overlap < min_overlap) {
            min_overlap = overlap;
            // The normal should point from polygon to circle
            if (circ_center_proj > (poly_min + poly_max) / 2) {
                best_nx = nx;
                best_ny = ny;
            } else {
                best_nx = -nx;
                best_ny = -ny;
            }
        }
    }

    // 2. Test axis from closest vertex to circle center
    int64_t closest_dist_sq = 0x7FFFFFFFFFFFFFFF;
    int closest_idx = 0;
    for (int i = 0; i < num_vertices; ++i) {
        int64_t dx = lcx - poly.vx[i];
        int64_t dy = lcy - poly.vy[i];
        int64_t d_sq = mul_fixed(dx, dx) + mul_fixed(dy, dy);
        if (d_sq < closest_dist_sq) {
            closest_dist_sq = d_sq;
            closest_idx = i;
        }
    }

    int64_t axis_x = lcx - poly.vx[closest_idx];
    int64_t axis_y = lcy - poly.vy[closest_idx];
    int64_t axis_len_sq = mul_fixed(axis_x, axis_x) + mul_fixed(axis_y, axis_y);
    if (axis_len_sq > 0) {
        int64_t axis_len = sqrt_fixed(axis_len_sq);
        if (axis_len > 0) {
            int64_t nx = div_fixed(axis_x, axis_len);
            int64_t ny = div_fixed(axis_y, axis_len);

            // Project polygon
            int64_t poly_min = 0x7FFFFFFFFFFFFFFF;
            int64_t poly_max = -0x7FFFFFFFFFFFFFFF;
            for (int k = 0; k < num_vertices; ++k) {
                int64_t proj = mul_fixed(poly.vx[k], nx) + mul_fixed(poly.vy[k], ny);
                if (proj < poly_min) poly_min = proj;
                if (proj > poly_max) poly_max = proj;
            }

            // Project circle
            int64_t circ_center_proj = mul_fixed(lcx, nx) + mul_fixed(lcy, ny);
            int64_t circ_min = circ_center_proj - r;
            int64_t circ_max = circ_center_proj + r;

            if (circ_max <= poly_min || circ_min >= poly_max) {
                return result; // Separating axis found
            }

            int64_t overlap1 = poly_max - circ_min;
            int64_t overlap2 = circ_max - poly_min;
            int64_t overlap = min_fixed(overlap1, overlap2);

            if (overlap < min_overlap) {
                min_overlap = overlap;
                if (circ_center_proj > (poly_min + poly_max) / 2) {
                    best_nx = nx;
                    best_ny = ny;
                } else {
                    best_nx = -nx;
                    best_ny = -ny;
                }
            }
        }
    }

    result.hit = true;
    result.penetration = min_overlap;
    result.normal_x = best_nx;
    result.normal_y = best_ny;
    return result;
}

} // namespace Collision
