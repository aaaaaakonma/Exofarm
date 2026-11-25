#pragma once

#include "../math/fixed.h"
#include "../math/fixed_3d.h"
#include "../math/bicubic.h"
#include "dem_erosion.h"
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>

namespace Hydrology {

// ===== Continuous Parameterized River Reach =====
// 1D Parametric curve C(s) representing a non-chunked continuous river channel.
struct ParametricRiverReach {
    uint32_t id = 0;
    uint8_t  tier = 1;                    // 1 = Brook, 2 = River, 3 = Trunk River
    std::vector<Vec2Fixed> waypoints;     // Centerline coordinates (16.16 fixed-point)
    std::vector<int64_t>   arc_lengths;   // Cumulative length s along reach (16.16)
    std::vector<int64_t>   widths;        // Channel width W(s) along reach (16.16)
    std::vector<int64_t>   bed_elevations;// Centerline bed elevation Zb(s) (16.16)
    std::vector<int64_t>   water_elevations; // Monotonic downstream water surface elevation Zw(s) (16.16)
    
    int64_t  total_length = 0;            // Total reach length L (16.16)
    int64_t  base_width = 2 * ONE;        // Default width (16.16)
    int64_t  flow_rate = 0;               // Effective throughput (16.16)
    int64_t  max_capacity = 0;            // Flow capacity (16.16)
    int64_t  water_level = 100 * ONE;     // 100% full (16.16)
    int64_t  pump_rate = 0;               // Total active extractions (16.16)
    bool     is_dry = false;

    uint32_t upstream_reach_id = 0;
    uint32_t downstream_reach_id = 0;
    std::vector<uint32_t> upstream_reach_ids;

    // Evaluates continuous Catmull-Rom point and tangent at normalized parameter t in [0, 1]
    void evaluate_at(int64_t t_fixed, Vec2Fixed& out_pos, Vec2Fixed& out_tangent, int64_t& out_width, int64_t& out_bed) const {
        if (waypoints.empty()) {
            out_pos = {0, 0};
            out_tangent = {ONE, 0};
            out_width = base_width;
            out_bed = 0;
            return;
        }
        if (waypoints.size() == 1) {
            out_pos = waypoints[0];
            out_tangent = {ONE, 0};
            out_width = widths.empty() ? base_width : widths[0];
            out_bed = bed_elevations.empty() ? 0 : bed_elevations[0];
            return;
        }

        int32_t n = static_cast<int32_t>(waypoints.size());
        int64_t clamped_t = std::clamp(t_fixed, 0LL, ONE);
        int64_t scaled = clamped_t * (n - 1);
        int32_t idx = static_cast<int32_t>(scaled >> 16);
        if (idx >= n - 1) idx = n - 2;
        int64_t frac = scaled & 0xFFFF;

        int32_t i0 = std::max(0, idx - 1);
        int32_t i1 = idx;
        int32_t i2 = std::min(n - 1, idx + 1);
        int32_t i3 = std::min(n - 1, idx + 2);

        out_pos.x = SimMath::cubic_catmull_rom(waypoints[i0].x, waypoints[i1].x, waypoints[i2].x, waypoints[i3].x, frac);
        out_pos.y = SimMath::cubic_catmull_rom(waypoints[i0].y, waypoints[i1].y, waypoints[i2].y, waypoints[i3].y, frac);

        int64_t dx = SimMath::cubic_derivative(waypoints[i0].x, waypoints[i1].x, waypoints[i2].x, waypoints[i3].x, frac);
        int64_t dy = SimMath::cubic_derivative(waypoints[i0].y, waypoints[i1].y, waypoints[i2].y, waypoints[i3].y, frac);
        out_tangent = Vec2Fixed(dx, dy).normalized();

        if (!widths.empty() && widths.size() >= waypoints.size()) {
            out_width = SimMath::cubic_catmull_rom(widths[i0], widths[i1], widths[i2], widths[i3], frac);
        } else if (!widths.empty()) {
            out_width = widths[std::min<size_t>(idx, widths.size() - 1)];
        } else {
            out_width = base_width;
        }

        if (!bed_elevations.empty() && bed_elevations.size() >= waypoints.size()) {
            out_bed = SimMath::cubic_catmull_rom(bed_elevations[i0], bed_elevations[i1], bed_elevations[i2], bed_elevations[i3], frac);
        } else if (!bed_elevations.empty()) {
            out_bed = bed_elevations[std::min<size_t>(idx, bed_elevations.size() - 1)];
        } else {
            out_bed = 0;
        }
    }

    // Evaluates continuous Catmull-Rom point, tangent, width, bed elevation, and water surface elevation
    void evaluate_at(int64_t t_fixed, Vec2Fixed& out_pos, Vec2Fixed& out_tangent, int64_t& out_width, int64_t& out_bed, int64_t& out_water) const {
        evaluate_at(t_fixed, out_pos, out_tangent, out_width, out_bed);
        if (water_elevations.empty()) {
            out_water = out_bed + 22938LL; // +0.35m nominal
            return;
        }
        int32_t n = static_cast<int32_t>(water_elevations.size());
        if (n == 1) {
            out_water = water_elevations[0];
            return;
        }
        int64_t clamped_t = std::clamp(t_fixed, 0LL, ONE);
        int64_t scaled = clamped_t * (n - 1);
        int32_t idx = static_cast<int32_t>(scaled >> 16);
        if (idx >= n - 1) idx = n - 2;
        int64_t frac = scaled & 0xFFFF;

        int32_t i0 = std::max(0, idx - 1);
        int32_t i1 = idx;
        int32_t i2 = std::min(n - 1, idx + 1);
        int32_t i3 = std::min(n - 1, idx + 2);

        out_water = SimMath::cubic_catmull_rom(water_elevations[i0], water_elevations[i1], water_elevations[i2], water_elevations[i3], frac);
    }
};

// ===== Estuary Mouth Terminal =====
// Geometric and hydrodynamic transition boundary where an inland river reach discharges into the ocean.
struct EstuaryMouth {
    uint32_t reach_id = 0;
    Vec2Fixed position;          // Coordinates of mouth (16.16)
    Vec2Fixed outflow_direction; // Tangent unit vector pointing into the sea (16.16)
    int64_t width = 4 * ONE;     // Channel width at mouth (16.16)
    int64_t discharge_velocity = 2 * ONE; // Outflow velocity in m/s (16.16)
};

// ===== Non-Chunked Continuous River Network =====
class ParameterizedRiverNetwork {
public:
    std::vector<ParametricRiverReach> reaches;

    void clear() {
        reaches.clear();
    }

    void add_reach(const ParametricRiverReach& r) {
        reaches.push_back(r);
    }

    // Queries nearest river reach to point p.
    // Returns perpendicular distance d_perp, local channel width, flow vector, and bathymetric depth.
    bool sample_nearest_river(
        const Vec2Fixed& p,
        int64_t& out_dist_to_center,
        int64_t& out_width,
        Vec2Fixed& out_flow_vector,
        int64_t& out_depth,
        uint32_t& out_reach_id
    ) const {
        if (reaches.empty()) return false;

        int64_t min_dist_sq = 0x7FFFFFFFFFFFFFFF;
        bool found = false;

        for (const auto& r : reaches) {
            if (r.waypoints.size() < 2) continue;
            for (size_t i = 0; i + 1 < r.waypoints.size(); ++i) {
                Vec2Fixed a = r.waypoints[i];
                Vec2Fixed b = r.waypoints[i + 1];
                Vec2Fixed ab = b - a;
                int64_t ab_len_sq = ab.length_sq();
                if (ab_len_sq <= 16) continue;

                Vec2Fixed ap = p - a;
                int64_t proj = div_fixed(ap.dot(ab), ab_len_sq);
                int64_t t = std::clamp(proj, 0LL, ONE);

                Vec2Fixed closest = a + (ab * t);
                Vec2Fixed diff = p - closest;
                int64_t dist_sq = diff.length_sq();

                if (dist_sq < min_dist_sq) {
                    min_dist_sq = dist_sq;
                    found = true;
                    out_reach_id = r.id;
                    out_dist_to_center = sqrt_fixed(dist_sq);

                    int64_t w_a = i < r.widths.size() ? r.widths[i] : r.base_width;
                    int64_t w_b = (i + 1) < r.widths.size() ? r.widths[i + 1] : r.base_width;
                    out_width = w_a + mul_fixed(t, w_b - w_a);

                    Vec2Fixed tangent = ab.normalized();
                    int64_t speed = r.is_dry ? 0 : std::clamp(r.flow_rate / 20, ONE / 4, 3 * ONE);
                    out_flow_vector = tangent * speed;

                    // Parabolic depth: max ~1.5m to 3.5m depth at centerline
                    int64_t max_depth = 2 * ONE + mul_fixed(out_width, ONE / 8);
                    if (out_width > 0 && out_dist_to_center < out_width / 2) {
                        int64_t u = div_fixed(out_dist_to_center, out_width / 2);
                        int64_t u_sq = mul_fixed(u, u);
                        out_depth = mul_fixed(max_depth, ONE - u_sq);
                    } else {
                        out_depth = 0;
                    }
                }
            }
        }
        return found;
    }

    // Carves the continuous parabolic riverbed into the elevation field
    void carve_riverbeds(std::vector<int32_t>& elevation, int32_t w, int32_t h, int64_t cell_size_fixed, int64_t carve_depth_fixed = (9 * ONE) / 2) const {
        if (reaches.empty() || w <= 0 || h <= 0 || cell_size_fixed <= 0) return;
        size_t total = static_cast<size_t>(w) * h;
        if (elevation.size() < total) return;

        for (const auto& r : reaches) {
            if (r.waypoints.size() < 2) continue;
            for (size_t i = 0; i + 1 < r.waypoints.size(); ++i) {
                Vec2Fixed a = r.waypoints[i];
                Vec2Fixed b = r.waypoints[i + 1];
                int64_t w_a = i < r.widths.size() ? r.widths[i] : r.base_width;
                int64_t w_b = (i + 1) < r.widths.size() ? r.widths[i + 1] : r.base_width;
                int64_t bed_a = i < r.bed_elevations.size() ? r.bed_elevations[i] : 0;
                int64_t bed_b = (i + 1) < r.bed_elevations.size() ? r.bed_elevations[i + 1] : 0;

                int64_t max_w = std::max(w_a, w_b);
                int64_t aabb_min_x = (std::min(a.x, b.x) - max_w) / cell_size_fixed;
                int64_t aabb_max_x = (std::max(a.x, b.x) + max_w) / cell_size_fixed + 1;
                int64_t aabb_min_y = (std::min(a.y, b.y) - max_w) / cell_size_fixed;
                int64_t aabb_max_y = (std::max(a.y, b.y) + max_w) / cell_size_fixed + 1;

                int32_t x0 = std::clamp(static_cast<int32_t>(aabb_min_x), 0, w - 1);
                int32_t x1 = std::clamp(static_cast<int32_t>(aabb_max_x), 0, w - 1);
                int32_t y0 = std::clamp(static_cast<int32_t>(aabb_min_y), 0, h - 1);
                int32_t y1 = std::clamp(static_cast<int32_t>(aabb_max_y), 0, h - 1);

                Vec2Fixed ab = b - a;
                int64_t ab_len_sq = ab.length_sq();
                if (ab_len_sq <= 16) continue;

                for (int32_t gy = y0; gy <= y1; ++gy) {
                    int64_t py = static_cast<int64_t>(gy) * cell_size_fixed;
                    for (int32_t gx = x0; gx <= x1; ++gx) {
                        int64_t px = static_cast<int64_t>(gx) * cell_size_fixed;
                        Vec2Fixed p(px, py);

                        Vec2Fixed ap = p - a;
                        int64_t t = std::clamp(div_fixed(ap.dot(ab), ab_len_sq), 0LL, ONE);
                        Vec2Fixed closest = a + (ab * t);
                        int64_t dist = (p - closest).length();

                        int64_t local_w = w_a + mul_fixed(t, w_b - w_a);
                        int64_t radius = local_w / 2;
                        if (dist < radius && radius > 0) {
                            int64_t local_bed = bed_a + mul_fixed(t, bed_b - bed_a);

                            // Depth incision: smoothly taper headwater spring origin (i == 0) to avoid abrupt cuts
                            int64_t seg_max_depth = carve_depth_fixed;
                            if (i == 0) {
                                // Taper from 20% incision at t=0 to 100% incision at t=1
                                int64_t taper = 13107LL + mul_fixed(t, 52429LL); // 0.20 + t * 0.80
                                seg_max_depth = mul_fixed(carve_depth_fixed, taper);
                            }

                            int64_t u = div_fixed(dist, radius);
                            int64_t depth = mul_fixed(seg_max_depth, ONE - mul_fixed(u, u));
                            int64_t target_elev = local_bed - depth;

                            size_t idx = static_cast<size_t>(gy) * w + gx;
                            if (target_elev < elevation[idx]) {
                                elevation[idx] = static_cast<int32_t>(target_elev);
                            }
                        }
                    }
                }
            }
        }
    }

    // Burns river presence and flow directions into the 2D grid buffers across the widened channel
    void burn_to_grid(
        std::vector<uint8_t>& out_rivers,
        std::vector<int32_t>& out_flow_targets,
        int32_t w, int32_t h,
        int64_t cell_size_fixed
    ) const {
        if (w <= 0 || h <= 0 || cell_size_fixed <= 0) return;
        size_t total = static_cast<size_t>(w) * h;
        out_rivers.assign(total, 0);
        if (out_flow_targets.size() < total) {
            out_flow_targets.assign(total, -1);
        }
        if (reaches.empty()) return;

        for (const auto& r : reaches) {
            if (r.waypoints.empty()) continue;

            // Mark centerline waypoints first
            for (size_t i = 0; i < r.waypoints.size(); ++i) {
                int32_t gx = static_cast<int32_t>(r.waypoints[i].x / cell_size_fixed);
                int32_t gy = static_cast<int32_t>(r.waypoints[i].y / cell_size_fixed);
                if (gx >= 0 && gx < w && gy >= 0 && gy < h) {
                    size_t idx = static_cast<size_t>(gy) * w + gx;
                    if (idx < total) {
                        out_rivers[idx] = std::max(out_rivers[idx], r.tier);
                    }

                    if (i + 1 < r.waypoints.size()) {
                        int32_t nx = static_cast<int32_t>(r.waypoints[i + 1].x / cell_size_fixed);
                        int32_t ny = static_cast<int32_t>(r.waypoints[i + 1].y / cell_size_fixed);
                        if (nx >= 0 && nx < w && ny >= 0 && ny < h) {
                            size_t n_idx = static_cast<size_t>(ny) * w + nx;
                            if (idx < out_flow_targets.size() && n_idx < total) {
                                out_flow_targets[idx] = static_cast<int32_t>(n_idx);
                            }
                        }
                    }
                }
            }

            // Burn widened channel across reach segments
            for (size_t i = 0; i + 1 < r.waypoints.size(); ++i) {
                Vec2Fixed a = r.waypoints[i];
                Vec2Fixed b = r.waypoints[i + 1];
                int64_t w_a = i < r.widths.size() ? r.widths[i] : r.base_width;
                int64_t w_b = (i + 1) < r.widths.size() ? r.widths[i + 1] : r.base_width;
                int64_t max_w = std::max(w_a, w_b);

                int64_t min_x = (std::min(a.x, b.x) - max_w / 2) / cell_size_fixed;
                int64_t max_x = (std::max(a.x, b.x) + max_w / 2) / cell_size_fixed + 1;
                int64_t min_y = (std::min(a.y, b.y) - max_w / 2) / cell_size_fixed;
                int64_t max_y = (std::max(a.y, b.y) + max_w / 2) / cell_size_fixed + 1;

                int32_t x0 = std::clamp(static_cast<int32_t>(min_x), 0, w - 1);
                int32_t x1 = std::clamp(static_cast<int32_t>(max_x), 0, w - 1);
                int32_t y0 = std::clamp(static_cast<int32_t>(min_y), 0, h - 1);
                int32_t y1 = std::clamp(static_cast<int32_t>(max_y), 0, h - 1);

                Vec2Fixed ab = b - a;
                int64_t ab_len_sq = ab.length_sq();
                if (ab_len_sq <= 16) continue;

                int32_t tgt_gx = std::clamp(static_cast<int32_t>(b.x / cell_size_fixed), 0, w - 1);
                int32_t tgt_gy = std::clamp(static_cast<int32_t>(b.y / cell_size_fixed), 0, h - 1);
                int32_t seg_target = tgt_gy * w + tgt_gx;

                for (int32_t gy = y0; gy <= y1; ++gy) {
                    int64_t py = static_cast<int64_t>(gy) * cell_size_fixed;
                    for (int32_t gx = x0; gx <= x1; ++gx) {
                        int64_t px = static_cast<int64_t>(gx) * cell_size_fixed;
                        Vec2Fixed p(px, py);
                        Vec2Fixed ap = p - a;
                        int64_t t = std::clamp(div_fixed(ap.dot(ab), ab_len_sq), 0LL, ONE);
                        Vec2Fixed closest = a + (ab * t);
                        int64_t dist = (p - closest).length();

                        int64_t local_w = w_a + mul_fixed(t, w_b - w_a);
                        if (dist <= local_w / 2) {
                            size_t idx = static_cast<size_t>(gy) * w + gx;
                            out_rivers[idx] = std::max(out_rivers[idx], r.tier);
                            if (out_flow_targets[idx] == -1 && static_cast<int32_t>(idx) != seg_target) {
                                out_flow_targets[idx] = seg_target;
                            }
                        }
                    }
                }
            }
        }
    }

    // Extracts continuous sub-cell river centerlines using MFD-field sub-grid (S=4) droplet advection,
    // hysteresis thresholding, Zhang-Suen skeletonization, spur pruning, Voronoi cross-sectional Q,
    // and Eulerian infill/spill surface hydraulics along the resulting centerline splines.
    void extract_from_subgrid_droplets(
        const std::vector<int32_t>& elevation,
        int32_t w, int32_t h,
        int64_t cell_size_fixed,
        int32_t min_area = 100,
        int64_t width_multiplier = ONE,
        uint64_t seed = 1337
    ) {
        clear();
        if (w <= 2 || h <= 2 || cell_size_fixed <= 0) return;
        size_t total = static_cast<size_t>(w) * h;
        if (elevation.size() < total) return;

        // 1. Sub-grid (S=4) MFD-field (p=2) droplet advection
        Erosion::SubgridDropletField field;
        Erosion::compute_subgrid_mfd_droplets(elevation, w, h, field, seed);
        constexpr int32_t S = Erosion::SubgridDropletField::SUBGRID_SCALE;
        const int32_t sw = field.sub_w;
        const int32_t sh = field.sub_h;
        const size_t sub_total = static_cast<size_t>(sw) * sh;
        if (sub_total == 0) return;

        // 2. Smooth occupancy (3x3 binomial, sigma ~ 0.5) & hysteresis threshold
        std::vector<int32_t> occ_smooth(sub_total, 0); // 16.16 fixed-point
        for (int32_t y = 0; y < sh; ++y) {
            int32_t ym = std::max(0, y - 1);
            int32_t yp = std::min(sh - 1, y + 1);
            for (int32_t x = 0; x < sw; ++x) {
                int32_t xm = std::max(0, x - 1);
                int32_t xp = std::min(sw - 1, x + 1);
                int64_t sum =
                    field.occ[static_cast<size_t>(ym) * sw + xm] +
                    2 * field.occ[static_cast<size_t>(ym) * sw + x] +
                    field.occ[static_cast<size_t>(ym) * sw + xp] +
                    2 * field.occ[static_cast<size_t>(y) * sw + xm] +
                    4 * field.occ[static_cast<size_t>(y) * sw + x] +
                    2 * field.occ[static_cast<size_t>(y) * sw + xp] +
                    field.occ[static_cast<size_t>(yp) * sw + xm] +
                    2 * field.occ[static_cast<size_t>(yp) * sw + x] +
                    field.occ[static_cast<size_t>(yp) * sw + xp];
                occ_smooth[static_cast<size_t>(y) * sw + x] = static_cast<int32_t>((sum * ONE) >> 4);
            }
        }

        const int32_t eff_min = std::max(4, min_area);
        const int32_t domain_min = std::min(w, h);
        // Hydrological initiation gate: scale catchment requirement on macro maps (>= 256)
        // so minor slope rills do not turn into a total-domain river solve.
        const int32_t gated_min = (domain_min >= 256)
            ? std::max(eff_min, (eff_min * domain_min) / 144)
            : eff_min;
        const int64_t high_thresh = std::max<int64_t>(4 * ONE, (static_cast<int64_t>(gated_min) * ONE * 3) / 4);
        const int64_t low_thresh = std::max<int64_t>(2 * ONE, (high_thresh * 3) / 5);

        std::vector<uint8_t> mask(sub_total, 0);
        std::vector<int32_t> bfs_q;
        bfs_q.reserve(sub_total / 16);

        for (size_t i = 0; i < sub_total; ++i) {
            if (occ_smooth[i] >= high_thresh) {
                mask[i] = 1;
                bfs_q.push_back(static_cast<int32_t>(i));
            }
        }
        if (bfs_q.empty()) return;

        const int32_t dxs8[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
        const int32_t dys8[8] = {-1, -1, -1, 0, 0, 1, 1, 1};

        for (size_t head = 0; head < bfs_q.size(); ++head) {
            int32_t cur = bfs_q[head];
            int32_t cx = cur % sw;
            int32_t cy = cur / sw;
            for (int k = 0; k < 8; ++k) {
                int32_t nx = cx + dxs8[k];
                int32_t ny = cy + dys8[k];
                if (nx >= 0 && nx < sw && ny >= 0 && ny < sh) {
                    size_t n_idx = static_cast<size_t>(ny) * sw + nx;
                    if (!mask[n_idx] && occ_smooth[n_idx] >= low_thresh) {
                        mask[n_idx] = 1;
                        bfs_q.push_back(static_cast<int32_t>(n_idx));
                    }
                }
            }
        }

        // 3. Zhang-Suen topological skeletonization on active pixels
        std::vector<int32_t> active;
        active.reserve(bfs_q.size());
        for (int32_t idx : bfs_q) {
            int32_t x = idx % sw;
            int32_t y = idx / sw;
            if (x > 0 && x < sw - 1 && y > 0 && y < sh - 1) {
                active.push_back(idx);
            } else {
                mask[idx] = 0;
            }
        }

        std::vector<int32_t> to_delete;
        std::vector<int32_t> next_active;
        bool changed = true;
        while (changed && !active.empty()) {
            changed = false;
            for (int step = 0; step < 2; ++step) {
                to_delete.clear();
                next_active.clear();
                for (int32_t idx : active) {
                    if (!mask[idx]) continue;
                    // Clockwise 8-neighbors starting at North: p2..p9
                    uint8_t p2 = mask[idx - sw];
                    uint8_t p3 = mask[idx - sw + 1];
                    uint8_t p4 = mask[idx + 1];
                    uint8_t p5 = mask[idx + sw + 1];
                    uint8_t p6 = mask[idx + sw];
                    uint8_t p7 = mask[idx + sw - 1];
                    uint8_t p8 = mask[idx - 1];
                    uint8_t p9 = mask[idx - sw - 1];

                    int b = p2 + p3 + p4 + p5 + p6 + p7 + p8 + p9;
                    if (b < 2 || b > 6) {
                        next_active.push_back(idx);
                        continue;
                    }
                    int a = (!p2 && p3) + (!p3 && p4) + (!p4 && p5) + (!p5 && p6) +
                            (!p6 && p7) + (!p7 && p8) + (!p8 && p9) + (!p9 && p2);
                    if (a != 1) {
                        next_active.push_back(idx);
                        continue;
                    }
                    bool cond = (step == 0)
                        ? ((p2 * p4 * p6 == 0) && (p4 * p6 * p8 == 0))
                        : ((p2 * p4 * p8 == 0) && (p2 * p6 * p8 == 0));
                    if (cond) {
                        to_delete.push_back(idx);
                    } else {
                        next_active.push_back(idx);
                    }
                }
                if (!to_delete.empty()) {
                    changed = true;
                    for (int32_t d : to_delete) mask[d] = 0;
                }
                active.swap(next_active);
            }
        }
        if (active.empty()) return;

        // 4. Spur pruning (peel endpoints N iterations, then regrow surviving tips along sk0)
        std::vector<uint8_t> sk0 = mask;
        const int32_t prune_iters = std::min(8, std::max(2, std::min(w, h) / 16));
        auto count_nbrs = [&](const std::vector<uint8_t>& m, int32_t idx) -> int {
            return m[idx - sw - 1] + m[idx - sw] + m[idx - sw + 1] +
                   m[idx - 1] + m[idx + 1] +
                   m[idx + sw - 1] + m[idx + sw] + m[idx + sw + 1];
        };

        for (int iter = 0; iter < prune_iters; ++iter) {
            to_delete.clear();
            next_active.clear();
            for (int32_t idx : active) {
                if (!mask[idx]) continue;
                if (count_nbrs(mask, idx) <= 1) {
                    to_delete.push_back(idx);
                } else {
                    next_active.push_back(idx);
                }
            }
            for (int32_t d : to_delete) mask[d] = 0;
            active.swap(next_active);
        }

        // Regrow surviving endpoints along original skeleton sk0
        std::vector<int32_t> front;
        for (int32_t idx : active) {
            if (mask[idx] && count_nbrs(mask, idx) <= 1) {
                front.push_back(idx);
            }
        }
        for (int iter = 0; iter < prune_iters && !front.empty(); ++iter) {
            std::vector<int32_t> next_front;
            for (int32_t idx : front) {
                int32_t cx = idx % sw;
                int32_t cy = idx / sw;
                for (int k = 0; k < 8; ++k) {
                    int32_t nx = cx + dxs8[k];
                    int32_t ny = cy + dys8[k];
                    if (nx > 0 && nx < sw - 1 && ny > 0 && ny < sh - 1) {
                        int32_t n_idx = ny * sw + nx;
                        if (sk0[n_idx] && !mask[n_idx]) {
                            mask[n_idx] = 1;
                            active.push_back(n_idx);
                            next_front.push_back(n_idx);
                        }
                    }
                }
            }
            front.swap(next_front);
        }

        // Sort active skeleton indices deterministically for canonical node IDs
        std::sort(active.begin(), active.end());
        const int32_t n_skel = static_cast<int32_t>(active.size());
        if (n_skel < 3) return;

        std::vector<int32_t> pid(sub_total, -1);
        std::vector<int32_t> p_row(n_skel);
        std::vector<int32_t> p_col(n_skel);
        for (int32_t i = 0; i < n_skel; ++i) {
            int32_t idx = active[i];
            pid[idx] = i;
            p_row[i] = idx / sw;
            p_col[i] = idx % sw;
        }

        // 5. Pooled unit heading (7x7 box sum over field.hx, field.hy) at each skeleton pixel
        std::vector<int64_t> hxp(n_skel, 0);
        std::vector<int64_t> hyp(n_skel, 0);
        for (int32_t i = 0; i < n_skel; ++i) {
            int32_t r0 = std::max(0, p_row[i] - 3);
            int32_t r1 = std::min(sh - 1, p_row[i] + 3);
            int32_t c0 = std::max(0, p_col[i] - 3);
            int32_t c1 = std::min(sw - 1, p_col[i] + 3);
            int64_t sx = 0, sy = 0;
            for (int32_t r = r0; r <= r1; ++r) {
                size_t row_off = static_cast<size_t>(r) * sw;
                for (int32_t c = c0; c <= c1; ++c) {
                    sx += field.hx[row_off + c];
                    sy += field.hy[row_off + c];
                }
            }
            // Scale down safely before length normalization
            while (std::abs(sx) > (1LL << 28) || std::abs(sy) > (1LL << 28)) {
                sx >>= 2;
                sy >>= 2;
            }
            Vec2Fixed hn = Vec2Fixed(sx, sy).normalized();
            hxp[i] = hn.x;
            hyp[i] = hn.y;
        }

        // 6. Nearest-skeleton Voronoi label within radius 12 sub-pixels (multi-source BFS)
        std::vector<int32_t> nearest_id(sub_total, -1);
        std::vector<int32_t> best_dsq(sub_total, 145); // > 12^2
        std::vector<int32_t> vqueue;
        vqueue.reserve(n_skel * 8);

        for (int32_t i = 0; i < n_skel; ++i) {
            int32_t sub = active[i];
            nearest_id[sub] = i;
            best_dsq[sub] = 0;
            vqueue.push_back(sub);
        }
        for (size_t head = 0; head < vqueue.size(); ++head) {
            int32_t cur = vqueue[head];
            int32_t sid = nearest_id[cur];
            int32_t sr = p_row[sid];
            int32_t sc = p_col[sid];
            int32_t cx = cur % sw;
            int32_t cy = cur / sw;
            for (int k = 0; k < 8; ++k) {
                int32_t nx = cx + dxs8[k];
                int32_t ny = cy + dys8[k];
                if (nx >= 0 && nx < sw && ny >= 0 && ny < sh) {
                    int32_t dr = ny - sr;
                    int32_t dc = nx - sc;
                    int32_t dsq = dr * dr + dc * dc;
                    if (dsq <= 144) {
                        size_t n_idx = static_cast<size_t>(ny) * sw + nx;
                        if (dsq < best_dsq[n_idx]) {
                            best_dsq[n_idx] = dsq;
                            nearest_id[n_idx] = sid;
                            vqueue.push_back(static_cast<int32_t>(n_idx));
                        }
                    }
                }
            }
        }

        // 7. Mass-preserving 5x5 binomial blur (sigma ~ 1.0) of occ + cross-sectional Q integral
        std::vector<int64_t> occ_tmp(sub_total, 0);
        std::vector<int64_t> occ_q(sub_total, 0); // 16.16 fixed-point
        for (int32_t y = 0; y < sh; ++y) {
            size_t row_off = static_cast<size_t>(y) * sw;
            for (int32_t x = 0; x < sw; ++x) {
                int64_t s =
                    field.occ[row_off + std::max(0, x - 2)] +
                    4 * field.occ[row_off + std::max(0, x - 1)] +
                    6 * field.occ[row_off + x] +
                    4 * field.occ[row_off + std::min(sw - 1, x + 1)] +
                    field.occ[row_off + std::min(sw - 1, x + 2)];
                occ_tmp[row_off + x] = s;
            }
        }
        for (int32_t y = 0; y < sh; ++y) {
            int32_t ym2 = std::max(0, y - 2);
            int32_t ym1 = std::max(0, y - 1);
            int32_t yp1 = std::min(sh - 1, y + 1);
            int32_t yp2 = std::min(sh - 1, y + 2);
            for (int32_t x = 0; x < sw; ++x) {
                int64_t s =
                    occ_tmp[static_cast<size_t>(ym2) * sw + x] +
                    4 * occ_tmp[static_cast<size_t>(ym1) * sw + x] +
                    6 * occ_tmp[static_cast<size_t>(y) * sw + x] +
                    4 * occ_tmp[static_cast<size_t>(yp1) * sw + x] +
                    occ_tmp[static_cast<size_t>(yp2) * sw + x];
                occ_q[static_cast<size_t>(y) * sw + x] = (s * ONE) >> 8; // / 256
            }
        }

        std::vector<int64_t> q_skel(n_skel, 0); // 16.16 fixed-point cells of drainage
        for (int32_t i = 0; i < n_skel; ++i) {
            int64_t pr_fp = static_cast<int64_t>(p_row[i]) * ONE;
            int64_t pc_fp = static_cast<int64_t>(p_col[i]) * ONE;
            int64_t hx_i = hxp[i];
            int64_t hy_i = hyp[i];

            int64_t q_sum = 0;
            // Integrate along channel normal t in [-8, +8] with 0.5 sub-pixel step,
            // Voronoi-gated to <= 4 sub-pixels (1 macro cell) to avoid counting neighboring sheet flow
            for (int32_t ti = -16; ti <= 16; ++ti) {
                int64_t t_fp = static_cast<int64_t>(ti) * (ONE / 2);
                int64_t si_fp = pr_fp + mul_fixed(t_fp, hx_i);
                int64_t sj_fp = pc_fp - mul_fixed(t_fp, hy_i);

                int32_t ri = std::clamp(static_cast<int32_t>((si_fp + ONE / 2) >> 16), 0, sh - 1);
                int32_t rj = std::clamp(static_cast<int32_t>((sj_fp + ONE / 2) >> 16), 0, sw - 1);
                int32_t lab = nearest_id[static_cast<size_t>(ri) * sw + rj];
                if (lab < 0) continue;
                int32_t dr = p_row[lab] - p_row[i];
                int32_t dc = p_col[lab] - p_col[i];
                if (dr * dr + dc * dc > 16) continue; // Gate to <= 4 sub-pixels (1 cell)

                int64_t csi = std::clamp(si_fp, 0LL, static_cast<int64_t>(sh - 1) * ONE);
                int64_t csj = std::clamp(sj_fp, 0LL, static_cast<int64_t>(sw - 1) * ONE);
                int32_t i0 = std::min(sh - 2, static_cast<int32_t>(csi >> 16));
                int32_t j0 = std::min(sw - 2, static_cast<int32_t>(csj >> 16));
                int64_t fi = csi - static_cast<int64_t>(i0) * ONE;
                int64_t fj = csj - static_cast<int64_t>(j0) * ONE;
                int64_t w00 = mul_fixed(ONE - fi, ONE - fj);
                int64_t w01 = mul_fixed(ONE - fi, fj);
                int64_t w10 = mul_fixed(fi, ONE - fj);
                int64_t w11 = ONE - w00 - w01 - w10;

                size_t b_idx = static_cast<size_t>(i0) * sw + j0;
                int64_t val = mul_fixed(occ_q[b_idx], w00) + mul_fixed(occ_q[b_idx + 1], w01) +
                              mul_fixed(occ_q[b_idx + sw], w10) + mul_fixed(occ_q[b_idx + sw + 1], w11);
                q_sum += val;
            }
            q_skel[i] = q_sum / 2; // * 0.5 sub-pixel step
        }

        // 8. Directional downstream pointer linking across skeleton (radius 1..3)
        std::vector<int32_t> down(n_skel, -1);
        for (int32_t rad = 1; rad <= 3; ++rad) {
            int64_t min_dot = (rad > 1) ? 19661LL : 13107LL; // 0.3 vs 0.2
            for (int32_t i = 0; i < n_skel; ++i) {
                if (down[i] >= 0) continue;
                int64_t best_dot = min_dot;
                int32_t best_q = -1;
                for (int32_t di = -rad; di <= rad; ++di) {
                    for (int32_t dj = -rad; dj <= rad; ++dj) {
                        if (std::max(std::abs(di), std::abs(dj)) != rad) continue;
                        int32_t ni = p_row[i] + di;
                        int32_t nj = p_col[i] + dj;
                        if (ni >= 0 && ni < sh && nj >= 0 && nj < sw) {
                            int32_t q_idx = pid[static_cast<size_t>(ni) * sw + nj];
                            if (q_idx >= 0) {
                                int64_t len = sqrt_fixed(static_cast<int64_t>(di * di + dj * dj) * ONE);
                                int64_t dot = (hxp[i] * dj + hyp[i] * di);
                                int64_t cos_a = (len > 0) ? div_fixed(dot, len) : 0;
                                if (cos_a > best_dot) {
                                    best_dot = cos_a;
                                    best_q = q_idx;
                                }
                            }
                        }
                    }
                }
                if (best_q >= 0) down[i] = best_q;
            }
        }

        // Second-pass confluence snapping for remaining inland skeleton ends (allows ~90-104 deg tributary junctions)
        for (int32_t i = 0; i < n_skel; ++i) {
            if (down[i] >= 0) continue;
            if (p_row[i] < 4 || p_row[i] >= sh - 4 || p_col[i] < 4 || p_col[i] >= sw - 4) continue;
            int32_t i_cell = std::clamp(p_row[i] / S, 0, h - 1) * w + std::clamp(p_col[i] / S, 0, w - 1);
            int64_t z_tol = field.z_ramped[i_cell] + (static_cast<int64_t>(ONE / 2) << 8);

            int32_t snap_q = -1;
            int64_t best_score = -1000000000LL;
            for (int32_t rad = 1; rad <= 3 && snap_q < 0; ++rad) {
                for (int32_t di = -rad; di <= rad; ++di) {
                    for (int32_t dj = -rad; dj <= rad; ++dj) {
                        if (std::max(std::abs(di), std::abs(dj)) != rad) continue;
                        int32_t ni = p_row[i] + di;
                        int32_t nj = p_col[i] + dj;
                        if (ni < 0 || ni >= sh || nj < 0 || nj >= sw) continue;
                        int32_t q_idx = pid[static_cast<size_t>(ni) * sw + nj];
                        if (q_idx < 0 || q_idx == i) continue;
                        if (q_skel[q_idx] <= q_skel[i]) continue;
                        int32_t q_cell = std::clamp(ni / S, 0, h - 1) * w + std::clamp(nj / S, 0, w - 1);
                        if (field.z_ramped[q_cell] > z_tol) continue;

                        int64_t len = sqrt_fixed(static_cast<int64_t>(di * di + dj * dj) * ONE);
                        int64_t dot = (hxp[i] * dj + hyp[i] * di);
                        int64_t cos_a = (len > 0) ? div_fixed(dot, len) : 0;
                        if (cos_a >= -16384LL) { // >= -0.25 (~104 deg max confluence angle)
                            int64_t score = cos_a + std::min<int64_t>(ONE, div_fixed(q_skel[q_idx] - q_skel[i], std::max<int64_t>(ONE, q_skel[i])) / 4);
                            if (score > best_score) {
                                best_score = score;
                                snap_q = q_idx;
                            }
                        }
                    }
                }
            }
            if (snap_q >= 0) down[i] = snap_q;
        }

        // Filter kept skeleton nodes (Q >= gated_min) and restrict links to kept subgraph
        const int64_t q_thresh = static_cast<int64_t>(gated_min) * ONE;
        std::vector<int32_t> remap(n_skel, -1);
        std::vector<int32_t> kept_orig;
        kept_orig.reserve(n_skel);
        for (int32_t i = 0; i < n_skel; ++i) {
            if (q_skel[i] >= q_thresh) {
                remap[i] = static_cast<int32_t>(kept_orig.size());
                kept_orig.push_back(i);
            }
        }
        const int32_t n_kept = static_cast<int32_t>(kept_orig.size());
        if (n_kept < 3) return;

        auto sub_to_world = [&](int32_t sr, int32_t sc) -> Vec2Fixed {
            int64_t gx_fp = ((static_cast<int64_t>(sc) * 2 + 1) * ONE) / (2 * S) - ONE / 2;
            int64_t gy_fp = ((static_cast<int64_t>(sr) * 2 + 1) * ONE) / (2 * S) - ONE / 2;
            gx_fp = std::clamp(gx_fp, 0LL, static_cast<int64_t>(w - 1) * ONE);
            gy_fp = std::clamp(gy_fp, 0LL, static_cast<int64_t>(h - 1) * ONE);
            return Vec2Fixed(mul_fixed(gx_fp, cell_size_fixed), mul_fixed(gy_fp, cell_size_fixed));
        };

        auto sample_bed_elev = [&](const Vec2Fixed& wp) -> int64_t {
            int64_t gx_fp = std::clamp(div_fixed(wp.x, cell_size_fixed), 0LL, static_cast<int64_t>(w - 1) * ONE);
            int64_t gy_fp = std::clamp(div_fixed(wp.y, cell_size_fixed), 0LL, static_cast<int64_t>(h - 1) * ONE);
            int32_t gx = std::min(w - 2, static_cast<int32_t>(gx_fp >> 16));
            int32_t gy = std::min(h - 2, static_cast<int32_t>(gy_fp >> 16));
            int64_t fx = gx_fp - static_cast<int64_t>(gx) * ONE;
            int64_t fy = gy_fp - static_cast<int64_t>(gy) * ONE;
            int64_t w00 = mul_fixed(ONE - fx, ONE - fy);
            int64_t w10 = mul_fixed(fx, ONE - fy);
            int64_t w01 = mul_fixed(ONE - fx, fy);
            int64_t w11 = ONE - w00 - w10 - w01;
            size_t b_idx = static_cast<size_t>(gy) * w + gx;
            return mul_fixed(elevation[b_idx], w00) + mul_fixed(elevation[b_idx + 1], w10) +
                   mul_fixed(elevation[b_idx + w], w01) + mul_fixed(elevation[b_idx + w + 1], w11);
        };

        std::vector<int32_t> dn(n_kept, -1);
        for (int32_t k = 0; k < n_kept; ++k) {
            int32_t d = down[kept_orig[k]];
            if (d >= 0) dn[k] = remap[d];
        }

        // Guarantee 100% cycle-free DAG by breaking any rare skeleton cycle at its lowest-drop link
        std::vector<uint8_t> color(n_kept, 0); // 0=unvisited, 1=visiting, 2=done
        std::vector<int32_t> trace_stack;
        for (int32_t start = 0; start < n_kept; ++start) {
            if (color[start] != 0) continue;
            trace_stack.clear();
            int32_t cur = start;
            while (cur >= 0 && color[cur] == 0) {
                color[cur] = 1;
                trace_stack.push_back(cur);
                cur = dn[cur];
            }
            if (cur >= 0 && color[cur] == 1) {
                // Break edge pointing back into visiting stack at minimum z_ramped node
                int32_t break_u = trace_stack.back();
                dn[break_u] = -1;
            }
            for (int32_t node : trace_stack) color[node] = 2;
        }

        // Compute in-degree and identify spring headwaters
        std::vector<int32_t> in_deg(n_kept, 0);
        for (int32_t k = 0; k < n_kept; ++k) {
            if (dn[k] >= 0) in_deg[dn[k]]++;
        }

        std::vector<int32_t> springs;
        for (int32_t k = 0; k < n_kept; ++k) {
            if (in_deg[k] == 0 && dn[k] >= 0) {
                springs.push_back(k);
            }
        }

        // Sort springs from highest elevation to lowest so trunk rivers trace first to ocean/border
        std::sort(springs.begin(), springs.end(), [&](int32_t a, int32_t b) {
            int32_t oa = kept_orig[a], ob = kept_orig[b];
            int32_t ga = std::clamp(p_row[oa] / S, 0, h - 1) * w + std::clamp(p_col[oa] / S, 0, w - 1);
            int32_t gb = std::clamp(p_row[ob] / S, 0, h - 1) * w + std::clamp(p_col[ob] / S, 0, w - 1);
            if (field.z_ramped[ga] != field.z_ramped[gb]) return field.z_ramped[ga] > field.z_ramped[gb];
            return a < b;
        });

        // 9. Trace centerline splines & compute Eulerian width + infill/spill water elevation
        std::vector<uint32_t> node_to_reach(n_kept, 0);
        std::vector<int64_t> node_water_elev(n_kept, 0);
        uint32_t next_id = 1;

        for (int32_t spring_k : springs) {
            if (node_to_reach[spring_k] != 0) continue;

            std::vector<int32_t> raw_nodes;
            int32_t cur = spring_k;
            uint32_t down_reach = 0;
            int32_t conf_node = -1;

            while (cur >= 0) {
                raw_nodes.push_back(cur);
                int32_t nxt = dn[cur];
                if (nxt < 0) break;
                if (node_to_reach[nxt] != 0) {
                    down_reach = node_to_reach[nxt];
                    conf_node = nxt;
                    raw_nodes.push_back(nxt);
                    break;
                }
                cur = nxt;
            }
            const size_t min_raw_nodes = (domain_min >= 256) ? 14 : ((w >= 32 && h >= 32) ? 8 : 3);
            if (raw_nodes.size() < min_raw_nodes) continue;

            // Decimate dense sub-pixels (~every 2 sub-pixels = 0.5 cell) preserving endpoints
            std::vector<Vec2Fixed> pts;
            std::vector<int64_t> qs;
            std::vector<int32_t> kept_raw;
            for (size_t i = 0; i < raw_nodes.size(); ++i) {
                if (i == 0 || i + 1 == raw_nodes.size() || (i % 2 == 0)) {
                    int32_t orig = kept_orig[raw_nodes[i]];
                    pts.push_back(sub_to_world(p_row[orig], p_col[orig]));
                    qs.push_back(q_skel[orig]);
                    kept_raw.push_back(raw_nodes[i]);
                }
            }

            // If terminal node is just inland of ocean/border, extend 1 waypoint to coastline for estuary detection
            if (down_reach == 0 && pts.size() >= 2) {
                Vec2Fixed p_end = pts.back();
                Vec2Fixed p_prev = pts[pts.size() - 2];
                Vec2Fixed tan = (p_end - p_prev).normalized();
                if (sample_bed_elev(p_end) > 0 && (tan.x != 0 || tan.y != 0)) {
                    Vec2Fixed p_probe = p_end + tan * cell_size_fixed;
                    int32_t gx = std::clamp(static_cast<int32_t>(div_fixed(p_probe.x, cell_size_fixed) >> 16), 0, w - 1);
                    int32_t gy = std::clamp(static_cast<int32_t>(div_fixed(p_probe.y, cell_size_fixed) >> 16), 0, h - 1);
                    if (elevation[static_cast<size_t>(gy) * w + gx] <= 0) {
                        pts.push_back(p_probe);
                        qs.push_back(qs.back());
                        kept_raw.push_back(-1);
                    }
                }
            }
            if (pts.size() < 3) continue;

            // Light Laplacian smoothing on interior spline waypoints (anchors preserved)
            for (int pass = 0; pass < 2; ++pass) {
                std::vector<Vec2Fixed> sm = pts;
                for (size_t k = 1; k + 1 < pts.size(); ++k) {
                    sm[k].x = (pts[k - 1].x + pts[k].x * 2 + pts[k + 1].x) / 4;
                    sm[k].y = (pts[k - 1].y + pts[k].y * 2 + pts[k + 1].y) / 4;
                }
                pts = std::move(sm);
            }

            // Trim sheer high-elevation mountain chute tails before channel spring ignition (low Q on steep slope)
            if (domain_min >= 128) {
                const int64_t steep_gate = (45 * ONE) / 100; // ~0.45 slope
                const int64_t chute_q_gate = (q_thresh * 3) / 2;
                size_t trim_start = 0;
                while (trim_start + 4 < pts.size() && qs[trim_start] < chute_q_gate) {
                    int64_t b0 = sample_bed_elev(pts[trim_start]);
                    int64_t b1 = sample_bed_elev(pts[trim_start + 1]);
                    int64_t seg_l = (pts[trim_start + 1] - pts[trim_start]).length();
                    if (seg_l <= 0 || (b0 - b1) <= mul_fixed(seg_l, steep_gate)) break;
                    trim_start++;
                }
                if (trim_start > 0) {
                    pts.erase(pts.begin(), pts.begin() + static_cast<ptrdiff_t>(trim_start));
                    qs.erase(qs.begin(), qs.begin() + static_cast<ptrdiff_t>(trim_start));
                    kept_raw.erase(kept_raw.begin(), kept_raw.begin() + static_cast<ptrdiff_t>(trim_start));
                }
            }
            if (pts.size() < 3) continue;

            const size_t n_pts = pts.size();
            std::vector<int64_t> beds(n_pts, 0);
            std::vector<int64_t> widths(n_pts, 0);
            std::vector<int64_t> arc_lens(n_pts, 0);

            int64_t q_mono = 0;
            int64_t q_peak = 0;
            int64_t acc_len = 0;
            for (size_t k = 0; k < n_pts; ++k) {
                if (k > 0) acc_len += (pts[k] - pts[k - 1]).length();
                arc_lens[k] = acc_len;

                // Anchor bed sample against thalweg minimum of enclosing cell corners so bend smoothing never climbs walls
                int64_t b_interp = sample_bed_elev(pts[k]);
                int64_t gx_fp = std::clamp(div_fixed(pts[k].x, cell_size_fixed), 0LL, static_cast<int64_t>(w - 1) * ONE);
                int64_t gy_fp = std::clamp(div_fixed(pts[k].y, cell_size_fixed), 0LL, static_cast<int64_t>(h - 1) * ONE);
                int32_t gx0 = std::min(w - 2, static_cast<int32_t>(gx_fp >> 16));
                int32_t gy0 = std::min(h - 2, static_cast<int32_t>(gy_fp >> 16));
                size_t c_idx = static_cast<size_t>(gy0) * w + gx0;
                int64_t b_corner_min = std::min({
                    static_cast<int64_t>(elevation[c_idx]),
                    static_cast<int64_t>(elevation[c_idx + 1]),
                    static_cast<int64_t>(elevation[c_idx + w]),
                    static_cast<int64_t>(elevation[c_idx + w + 1])
                });
                beds[k] = std::min(b_interp, (b_interp + b_corner_min) / 2);

                q_mono = std::max(q_mono, qs[k]);
                q_peak = std::max(q_peak, qs[k]);
                int64_t base_w = ONE + mul_fixed(sqrt_fixed(q_mono), 13107LL); // 1.0m + 0.20 * sqrt(Q)
                int64_t scaled_w = mul_fixed(base_w, width_multiplier);
                widths[k] = std::clamp(scaled_w, ONE, 16 * ONE);
            }

            // Persistence & topology gate before committing reach ownership:
            // prune minor disconnected mid-slope segments and tiny tributary whiskers
            if (domain_min >= 64) {
                const int64_t min_isolated_len = ((domain_min >= 256) ? 24 : 10) * cell_size_fixed;
                const int64_t min_trib_len = ((domain_min >= 256) ? 14 : 6) * cell_size_fixed;
                if (down_reach == 0 && beds.back() > 3 * ONE && acc_len < min_isolated_len) continue;
                if (down_reach != 0 && acc_len < min_trib_len && q_peak < q_thresh * 3) continue;
            }

            // Mark ownership for interior nodes only after passing persistence gates
            uint32_t rid = next_id++;
            for (size_t i = 0; i < raw_nodes.size(); ++i) {
                if (static_cast<int32_t>(raw_nodes[i]) != conf_node) {
                    node_to_reach[raw_nodes[i]] = rid;
                }
            }

            // Smooth spring headwater taper
            widths[0] = std::max(ONE / 2, mul_fixed(widths[0], 16384LL));   // 0.25x
            widths[1] = std::max(ONE, mul_fixed(widths[1], 36045LL));       // 0.55x
            if (n_pts > 2) widths[2] = std::max((ONE * 3) / 2, mul_fixed(widths[2], 52429LL)); // 0.80x

            // Eulerian Infill & Spill along centerline spline (channel-hugging depth)
            std::vector<int64_t> water_elevs(n_pts, 0);
            const int64_t MIN_WATER_DEPTH = 13107LL; // 0.20m
            const int64_t MAX_WATER_DEPTH = 2 * ONE; // 2.00m

            for (size_t k = 0; k < n_pts; ++k) {
                int64_t d = MIN_WATER_DEPTH + mul_fixed(widths[k], 3277LL);
                d = std::clamp(d, MIN_WATER_DEPTH, MAX_WATER_DEPTH);
                water_elevs[k] = (beds[k] <= 0) ? 0 : (beds[k] + d);
            }
            if (conf_node >= 0 && down_reach != 0) {
                water_elevs.back() = std::max(beds.back() + MIN_WATER_DEPTH, node_water_elev[conf_node]);
            } else if (beds.back() <= 0) {
                water_elevs.back() = 0;
            }

            // Backward infill pass (level slackwater pools behind downstream sills)
            for (int32_t k = static_cast<int32_t>(n_pts) - 2; k >= 0; --k) {
                if (beds[k] <= 0) {
                    water_elevs[k] = 0;
                } else {
                    if (water_elevs[k] < water_elevs[k + 1]) {
                        water_elevs[k] = water_elevs[k + 1];
                    }
                    int64_t min_h = beds[k] + MIN_WATER_DEPTH;
                    if (water_elevs[k] < min_h) water_elevs[k] = min_h;
                }
            }

            // Forward spill pass (strictly monotonic downhill surface to outlet/confluence)
            for (size_t k = 0; k + 1 < n_pts; ++k) {
                if (water_elevs[k + 1] > water_elevs[k]) {
                    water_elevs[k + 1] = water_elevs[k];
                }
                if (beds[k + 1] <= 0) water_elevs[k + 1] = 0;
            }

            // Record water surface elevation at skeleton nodes for tributary confluence matching
            for (size_t k = 0; k < n_pts; ++k) {
                if (kept_raw[k] >= 0 && kept_raw[k] != conf_node) {
                    node_water_elev[kept_raw[k]] = water_elevs[k];
                }
            }

            ParametricRiverReach reach;
            reach.id = rid;
            reach.downstream_reach_id = down_reach;
            reach.waypoints = std::move(pts);
            reach.bed_elevations = std::move(beds);
            reach.widths = std::move(widths);
            reach.water_elevations = std::move(water_elevs);
            reach.arc_lengths = std::move(arc_lens);
            reach.total_length = acc_len;
            reach.tier = acc_len > 120 * cell_size_fixed ? 3 : (acc_len > 40 * cell_size_fixed ? 2 : 1);
            reach.flow_rate = reach.tier * 35 * ONE;
            reach.max_capacity = reach.flow_rate;
            reach.base_width = reach.widths[0];
            reaches.push_back(std::move(reach));
        }

        // Populate upstream_reach_ids back-links
        for (size_t i = 0; i < reaches.size(); ++i) {
            if (reaches[i].downstream_reach_id != 0) {
                for (size_t j = 0; j < reaches.size(); ++j) {
                    if (reaches[j].id == reaches[i].downstream_reach_id) {
                        reaches[j].upstream_reach_ids.push_back(reaches[i].id);
                        if (reaches[j].upstream_reach_id == 0) {
                            reaches[j].upstream_reach_id = reaches[i].id;
                        }
                        break;
                    }
                }
            }
        }
    }

    // Rasterizes continuous centerline reaches & Eulerian infill/spill depths onto 2D grid buffers
    void populate_water_fields(
        const std::vector<int32_t>& elevation,
        int32_t w, int32_t h,
        int64_t cell_size_fixed,
        std::vector<int32_t>& out_water_depth,
        std::vector<int64_t>& out_water_flux,
        std::vector<int32_t>& out_flow_x,
        std::vector<int32_t>& out_flow_y,
        int64_t max_water_depth = 4 * ONE
    ) const {
        if (w <= 0 || h <= 0 || cell_size_fixed <= 0) return;
        size_t total = static_cast<size_t>(w) * h;
        out_water_depth.assign(total, 0);
        out_water_flux.assign(total, 0);
        out_flow_x.assign(total, 0);
        out_flow_y.assign(total, 0);
        if (elevation.size() < total) return;

        for (const auto& r : reaches) {
            if (r.waypoints.size() < 2) continue;
            for (size_t i = 0; i + 1 < r.waypoints.size(); ++i) {
                Vec2Fixed a = r.waypoints[i];
                Vec2Fixed b = r.waypoints[i + 1];
                Vec2Fixed ab = b - a;
                int64_t ab_len_sq = ab.length_sq();
                if (ab_len_sq <= 16) continue;

                Vec2Fixed tangent = ab.normalized();
                int64_t w_a = i < r.widths.size() ? r.widths[i] : r.base_width;
                int64_t w_b = (i + 1) < r.widths.size() ? r.widths[i + 1] : r.base_width;
                int64_t zw_a = i < r.water_elevations.size() ? r.water_elevations[i] : 0;
                int64_t zw_b = (i + 1) < r.water_elevations.size() ? r.water_elevations[i + 1] : 0;
                int64_t zb_a = i < r.bed_elevations.size() ? r.bed_elevations[i] : 0;
                int64_t zb_b = (i + 1) < r.bed_elevations.size() ? r.bed_elevations[i + 1] : 0;
                int64_t max_w = std::max(w_a, w_b);

                int32_t x0 = std::clamp(static_cast<int32_t>((std::min(a.x, b.x) - max_w / 2) / cell_size_fixed), 0, w - 1);
                int32_t x1 = std::clamp(static_cast<int32_t>((std::max(a.x, b.x) + max_w / 2) / cell_size_fixed + 1), 0, w - 1);
                int32_t y0 = std::clamp(static_cast<int32_t>((std::min(a.y, b.y) - max_w / 2) / cell_size_fixed), 0, h - 1);
                int32_t y1 = std::clamp(static_cast<int32_t>((std::max(a.y, b.y) + max_w / 2) / cell_size_fixed + 1), 0, h - 1);

                for (int32_t gy = y0; gy <= y1; ++gy) {
                    int64_t py = static_cast<int64_t>(gy) * cell_size_fixed;
                    for (int32_t gx = x0; gx <= x1; ++gx) {
                        size_t idx = static_cast<size_t>(gy) * w + gx;
                        if (elevation[idx] <= 0) continue;

                        int64_t px = static_cast<int64_t>(gx) * cell_size_fixed;
                        Vec2Fixed p(px, py);
                        Vec2Fixed ap = p - a;
                        int64_t t = std::clamp(div_fixed(ap.dot(ab), ab_len_sq), 0LL, ONE);
                        Vec2Fixed closest = a + (ab * t);
                        int64_t dist = (p - closest).length();

                        int64_t local_w = w_a + mul_fixed(t, w_b - w_a);
                        int64_t half_w = std::max(cell_size_fixed / 2, local_w / 2);
                        if (dist <= half_w) {
                            int64_t zw = zw_a + mul_fixed(t, zw_b - zw_a);
                            int64_t zb = zb_a + mul_fixed(t, zb_b - zb_a);
                            int64_t center_depth = std::clamp(zw - zb, ONE / 2, max_water_depth);
                            int64_t u = div_fixed(dist, half_w);
                            int64_t profile = std::max(0LL, ONE - mul_fixed(u, u));
                            int32_t d = static_cast<int32_t>(std::max<int64_t>(ONE / 4, mul_fixed(center_depth, profile)));

                            if (d > out_water_depth[idx]) {
                                out_water_depth[idx] = d;
                                out_flow_x[idx] = static_cast<int32_t>(tangent.x);
                                out_flow_y[idx] = static_cast<int32_t>(tangent.y);
                            }
                            if (r.flow_rate > out_water_flux[idx]) {
                                out_water_flux[idx] = r.flow_rate;
                            }
                        }
                    }
                }
            }
        }
    }

    // Identifies all estuary mouth transition points where reaches terminate at sea level
    std::vector<EstuaryMouth> get_estuary_mouths(int64_t sea_level_fixed = 0) const {
        std::vector<EstuaryMouth> mouths;
        for (const auto& r : reaches) {
            if (r.waypoints.size() < 2) continue;
            const auto& p_last = r.waypoints.back();
            const auto& p_prev = r.waypoints[r.waypoints.size() - 2];
            int64_t bed_last = r.bed_elevations.empty() ? 0 : r.bed_elevations.back();

            if (bed_last <= sea_level_fixed + 3 * ONE) {
                Vec2Fixed dir = (p_last - p_prev).normalized();
                int64_t w_last = r.widths.empty() ? r.base_width : r.widths.back();
                int64_t speed = r.is_dry ? 0 : std::clamp(r.flow_rate / 20, ONE / 2, 4 * ONE);

                EstuaryMouth m;
                m.reach_id = r.id;
                m.position = p_last;
                m.outflow_direction = dir;
                m.width = w_last;
                m.discharge_velocity = speed;
                mouths.push_back(m);
            }
        }
        return mouths;
    }

    // Evaluates the 2D estuary boundary mask and plume velocity field across the grid.
    // - out_boundary_mask: 1.0 (ONE) inside inland carved channels and on land; 0 in open ocean.
    // - out_plume_vel: 2D velocity vector of the river outflow jet discharging into the ocean.
    void compute_estuary_fields(
        const std::vector<int32_t>& elevation,
        int32_t w, int32_t h,
        int64_t cell_size_fixed,
        int64_t sea_level_fixed,
        std::vector<int64_t>& out_boundary_mask,
        std::vector<Vec2Fixed>& out_plume_vel
    ) const {
        size_t total = static_cast<size_t>(w) * h;
        out_boundary_mask.assign(total, 0);
        out_plume_vel.assign(total, Vec2Fixed(0, 0));
        if (w <= 0 || h <= 0 || cell_size_fixed <= 0) return;

        auto mouths = get_estuary_mouths(sea_level_fixed);

        // 1. Mark inland carved river channels as solid Dirichlet boundaries (boundary_mask = ONE)
        // This strictly prevents ocean wave cages from expanding into inland carved trenches!
        for (const auto& r : reaches) {
            if (r.waypoints.size() < 2) continue;
            for (size_t i = 0; i + 1 < r.waypoints.size(); ++i) {
                Vec2Fixed a = r.waypoints[i];
                Vec2Fixed b = r.waypoints[i + 1];
                Vec2Fixed ab = b - a;
                int64_t ab_len_sq = ab.length_sq();
                if (ab_len_sq <= 16) continue;

                int64_t w_a = i < r.widths.size() ? r.widths[i] : r.base_width;
                int64_t w_b = (i + 1) < r.widths.size() ? r.widths[i + 1] : r.base_width;
                int64_t max_w = std::max(w_a, w_b);

                int64_t min_x = (std::min(a.x, b.x) - max_w) / cell_size_fixed;
                int64_t max_x = (std::max(a.x, b.x) + max_w) / cell_size_fixed + 1;
                int64_t min_y = (std::min(a.y, b.y) - max_w) / cell_size_fixed;
                int64_t max_y = (std::max(a.y, b.y) + max_w) / cell_size_fixed + 1;

                int32_t x0 = std::clamp(static_cast<int32_t>(min_x), 0, w - 1);
                int32_t x1 = std::clamp(static_cast<int32_t>(max_x), 0, w - 1);
                int32_t y0 = std::clamp(static_cast<int32_t>(min_y), 0, h - 1);
                int32_t y1 = std::clamp(static_cast<int32_t>(max_y), 0, h - 1);

                for (int32_t gy = y0; gy <= y1; ++gy) {
                    int64_t py = static_cast<int64_t>(gy) * cell_size_fixed;
                    for (int32_t gx = x0; gx <= x1; ++gx) {
                        int64_t px = static_cast<int64_t>(gx) * cell_size_fixed;
                        Vec2Fixed p(px, py);
                        Vec2Fixed ap = p - a;
                        int64_t t = std::clamp(div_fixed(ap.dot(ab), ab_len_sq), 0LL, ONE);
                        Vec2Fixed closest = a + (ab * t);
                        int64_t dist = (p - closest).length();

                        int64_t local_w = w_a + mul_fixed(t, w_b - w_a);
                        size_t idx = static_cast<size_t>(gy) * w + gx;

                        // Inside river channel or bank
                        if (dist <= local_w) {
                            out_boundary_mask[idx] = ONE; // Pin as Dirichlet boundary
                        }
                    }
                }
            }
        }

        // 2. Compute Estuary Outflow Plume for Ocean Cells (Turbulent expanding jet into the sea)
        int64_t plume_reach_len = 35 * cell_size_fixed; // ~70m plume extent

        for (const auto& m : mouths) {
            int64_t min_x = (m.position.x - plume_reach_len) / cell_size_fixed;
            int64_t max_x = (m.position.x + plume_reach_len) / cell_size_fixed + 1;
            int64_t min_y = (m.position.y - plume_reach_len) / cell_size_fixed;
            int64_t max_y = (m.position.y + plume_reach_len) / cell_size_fixed + 1;

            int32_t x0 = std::clamp(static_cast<int32_t>(min_x), 0, w - 1);
            int32_t x1 = std::clamp(static_cast<int32_t>(max_x), 0, w - 1);
            int32_t y0 = std::clamp(static_cast<int32_t>(min_y), 0, h - 1);
            int32_t y1 = std::clamp(static_cast<int32_t>(max_y), 0, h - 1);

            for (int32_t gy = y0; gy <= y1; ++gy) {
                int64_t py = static_cast<int64_t>(gy) * cell_size_fixed;
                for (int32_t gx = x0; gx <= x1; ++gx) {
                    int64_t px = static_cast<int64_t>(gx) * cell_size_fixed;
                    Vec2Fixed p(px, py);
                    Vec2Fixed delta = p - m.position;
                    int64_t dist = delta.length();
                    if (dist == 0 || dist > plume_reach_len) continue;

                    int64_t cos_theta = div_fixed(delta.dot(m.outflow_direction), dist);
                    if (cos_theta > 0) { // Seaward outflow cone
                        int64_t cos_sq = mul_fixed(cos_theta, cos_theta);
                        int64_t rad_falloff = ONE - div_fixed(dist, plume_reach_len);
                        int64_t width_factor = div_fixed(m.width, m.width + dist / 4);

                        int64_t intensity = mul_fixed(mul_fixed(cos_sq, rad_falloff), width_factor);
                        int64_t cur_speed = mul_fixed(m.discharge_velocity, intensity);

                        size_t idx = static_cast<size_t>(gy) * w + gx;
                        Vec2Fixed jet = m.outflow_direction * cur_speed;
                        out_plume_vel[idx] = out_plume_vel[idx] + jet;

                        // Near mouth in the ocean plume, clear boundary_mask so wave particles can interact
                        if (dist > m.width / 2 && elevation[idx] < sea_level_fixed) {
                            out_boundary_mask[idx] = 0;
                        }
                    }
                }
            }
        }
    }
};

} // namespace Hydrology
