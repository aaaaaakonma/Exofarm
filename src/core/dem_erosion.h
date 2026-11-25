#pragma once

#include "../math/fixed.h"
#include "../math/fixed_3d.h"
#include "../math/rng.h"
#include <vector>
#include <queue>
#include <cstdint>
#include <algorithm>
#include <cmath>

namespace Erosion {

// ===== Particle-Based Hydraulic Droplet Erosion =====
// Simulates discrete raindrops/droplets flowing across the terrain with deterministic 16.16 fixed-point math.
// Transports sediment according to slope, velocity, and capacity factor, carving natural dendritic rills
// and depositing alluvial sediment in depressions and low-gradient flats.
struct DropletErosionConfig {
    int32_t droplet_count = 85000;
    int64_t inertia = 6554;          // 0.1 * ONE
    int64_t capacity_factor = 786432; // 12.0 * ONE
    int64_t min_slope = 655;         // 0.01 * ONE
    int64_t erosion_rate = 65536;    // 1.0 * ONE
    int64_t deposition_rate = 5243;  // 0.08 * ONE
    int64_t evaporation_rate = 1311; // 0.02 * ONE
    int64_t gravity = 1048576;       // 16.0 * ONE
    int32_t max_steps = 128;
    uint64_t seed = 1337;

    // Lagrangian Water Depth & Flux calibration (16.16 fixed-point)
    int64_t flux_threshold = 220 * ONE;   // Minimum flux threshold to form standing water (e.g. 220 converging droplets)
    int64_t depth_scale = 10000;          // Hydraulic depth scaling factor (10000 / 65536 ~= 0.152)
    int64_t max_water_depth = 4 * ONE;    // Max river depth cap (4.0m)

    // Proof of Concept: Frozen read-only hydrology mode
    bool read_only = false;
    bool uniform_spawn = false; // Force uniform random spawning across all valid land cells
    bool lake_spillway_glide = false; // Overcome flat-lake kinetic traps via discrete spillway guidance
    bool supercover_hits = false; // Rasterize full step segment [pos, new_pos] with exact corner-touching supercover
    bool float_direction = false; // Diagnostic control: compute gradient & renormalize direction in double (no len_sq<=64 snap)
    bool spawn_full_map = false; // Spawn over full [0, w-1) x [0, h-1) domain instead of interior-only [1..w-2]
};

struct DropletTerminationStats {
    int32_t edge = 0;     // Exited boundary or entered ocean
    int32_t pit = 0;      // Zero gradient or lost kinetic energy climbing uphill
    int32_t step_cap = 0; // Reached max_steps
    int32_t other = 0;    // Water evaporated

    // Diagnostic breakdown of pit terminations
    int32_t pit_dir_zero_step0 = 0;
    int32_t pit_dir_zero_later = 0;
    int32_t pit_v_sq_le_0 = 0;
};

// Exact integer supercover segment rasterizer in 16.16 fixed-point grid coordinates.
// Stamps every cell crossed by [x0_fp, y0_fp] -> [x1_fp, y1_fp], including both corner-adjacent
// cells on an exact grid vertex crossing (e.g., (10.7, 10.7) -> (11.4, 11.4) stamps all 4 cells).
inline void stamp_supercover_segment(
    int64_t x0_fp, int64_t y0_fp,
    int64_t x1_fp, int64_t y1_fp,
    int32_t w, int32_t h,
    int32_t droplet_id,
    std::vector<int32_t>& stamp,
    std::vector<int32_t>& hits
) {
    auto try_stamp = [&](int32_t cx, int32_t cy) {
        if (cx >= 0 && cx < w && cy >= 0 && cy < h) {
            size_t idx = static_cast<size_t>(cy) * w + cx;
            if (stamp[idx] != droplet_id) {
                stamp[idx] = droplet_id;
                hits[idx]++;
            }
        }
    };

    int32_t gx = static_cast<int32_t>(x0_fp >> 16);
    int32_t gy = static_cast<int32_t>(y0_fp >> 16);
    int32_t gx1 = static_cast<int32_t>(x1_fp >> 16);
    int32_t gy1 = static_cast<int32_t>(y1_fp >> 16);

    try_stamp(gx, gy);
    if (gx == gx1 && gy == gy1) return;

    int64_t dx = x1_fp - x0_fp;
    int64_t dy = y1_fp - y0_fp;
    int32_t sx = (gx1 > gx) ? 1 : (gx1 < gx ? -1 : 0);
    int32_t sy = (gy1 > gy) ? 1 : (gy1 < gy ? -1 : 0);
    int64_t abs_dx = (dx >= 0) ? dx : -dx;
    int64_t abs_dy = (dy >= 0) ? dy : -dy;

    for (int iter = 0; iter < 64 && (gx != gx1 || gy != gy1); ++iter) {
        if (gx == gx1) sx = 0;
        if (gy == gy1) sy = 0;
        if (sx == 0 && sy == 0) break;

        if (sx == 0) {
            gy += sy;
            try_stamp(gx, gy);
        } else if (sy == 0) {
            gx += sx;
            try_stamp(gx, gy);
        } else {
            int64_t next_x = (sx > 0) ? (static_cast<int64_t>(gx + 1) << 16) : (static_cast<int64_t>(gx) << 16);
            int64_t next_y = (sy > 0) ? (static_cast<int64_t>(gy + 1) << 16) : (static_cast<int64_t>(gy) << 16);
            int64_t dist_x = next_x - x0_fp; if (dist_x < 0) dist_x = -dist_x;
            int64_t dist_y = next_y - y0_fp; if (dist_y < 0) dist_y = -dist_y;

            int64_t lhs = dist_x * abs_dy;
            int64_t rhs = dist_y * abs_dx;

            if (lhs < rhs) {
                gx += sx;
                try_stamp(gx, gy);
            } else if (rhs < lhs) {
                gy += sy;
                try_stamp(gx, gy);
            } else {
                try_stamp(gx + sx, gy);
                try_stamp(gx, gy + sy);
                gx += sx;
                gy += sy;
                try_stamp(gx, gy);
            }
        }
    }
}

inline void apply_hydraulic_droplet_erosion(
    std::vector<int32_t>& elevation,
    int32_t w, int32_t h,
    int64_t cell_size_fixed,
    const DropletErosionConfig& cfg,
    const std::vector<int32_t>* precipitation = nullptr,
    std::vector<int32_t>* out_water_depth = nullptr,
    std::vector<int32_t>* out_sediment = nullptr,
    std::vector<int32_t>* out_flow_x = nullptr,
    std::vector<int32_t>* out_flow_y = nullptr,
    std::vector<int64_t>* out_water_flux = nullptr,
    std::vector<int32_t>* out_hits = nullptr,
    std::vector<int32_t>* out_hits_sub4 = nullptr,
    DropletTerminationStats* out_stats = nullptr,
    std::vector<double>* out_pit_death_grades = nullptr
) {
    if (w <= 2 || h <= 2 || elevation.size() != static_cast<size_t>(w) * h || cfg.droplet_count <= 0) return;
    size_t total = static_cast<size_t>(w) * h;
    const int64_t cs = (cell_size_fixed > 0) ? cell_size_fixed : ONE;
    if (out_stats) *out_stats = DropletTerminationStats{};

    // Optional field tracking for continuous water depth, flux, and flow vectors
    bool track_flux = (out_water_depth != nullptr || out_water_flux != nullptr);
    bool track_flow = (out_flow_x != nullptr && out_flow_y != nullptr);
    bool track_sediment = (out_sediment != nullptr);

    std::vector<int64_t> accum_flux;
    std::vector<int64_t> accum_flow_x;
    std::vector<int64_t> accum_flow_y;
    std::vector<int64_t> accum_sediment;

    if (track_flux) accum_flux.assign(total, 0);
    if (track_flow) {
        accum_flow_x.assign(total, 0);
        accum_flow_y.assign(total, 0);
    }
    if (track_sediment) accum_sediment.assign(total, 0);

    // Stamping arrays for unique droplet hits per cell (Phase 2)
    std::vector<int32_t> stamp;
    if (out_hits) {
        stamp.assign(total, -1);
        out_hits->assign(total, 0);
    }
    std::vector<int32_t> stamp_sub4;
    size_t total_sub4 = static_cast<size_t>(w * 4) * (h * 4);
    if (out_hits_sub4) {
        stamp_sub4.assign(total_sub4, -1);
        out_hits_sub4->assign(total_sub4, 0);
    }

    // Collect all candidate land cells (interior cells with elevation > 0, or full bilinear domain if spawn_full_map)
    std::vector<int32_t> land_indices;
    land_indices.reserve(total);
    int32_t spawn_start = cfg.spawn_full_map ? 0 : 1;
    for (int32_t y = spawn_start; y < h - 1; ++y) {
        for (int32_t x = spawn_start; x < w - 1; ++x) {
            size_t idx = static_cast<size_t>(y) * w + x;
            if (elevation[idx] > 0) {
                land_indices.push_back(static_cast<int32_t>(idx));
            }
        }
    }

    if (land_indices.empty()) return;

    if (out_pit_death_grades) out_pit_death_grades->clear();

    // Precipitation cumulative distribution function for weighted droplet spawn
    std::vector<int64_t> precip_cdf;
    int64_t total_precip = 0;
    bool use_precip = (precipitation != nullptr && precipitation->size() == total);
    if (use_precip) {
        precip_cdf.resize(land_indices.size());
        for (size_t i = 0; i < land_indices.size(); ++i) {
            int64_t p = (*precipitation)[land_indices[i]];
            if (p < 0) p = 0;
            p += ONE / 4; // Base ambient spawn probability
            total_precip += p;
            precip_cdf[i] = total_precip;
        }
        if (total_precip <= 0) use_precip = false;
    }

    SplitMix64 rng(cfg.seed);
    const int64_t max_bound_x = static_cast<int64_t>(w - 1) * ONE;
    const int64_t max_bound_y = static_cast<int64_t>(h - 1) * ONE;

    for (int32_t d = 0; d < cfg.droplet_count; ++d) {
        // Spawn droplet across land cells
        int32_t cell_idx = 0;
        if (use_precip && !cfg.uniform_spawn) {
            uint64_t r = rng.next() % static_cast<uint64_t>(total_precip);
            auto it = std::upper_bound(precip_cdf.begin(), precip_cdf.end(), static_cast<int64_t>(r));
            size_t idx = std::distance(precip_cdf.begin(), it);
            if (idx >= land_indices.size()) idx = land_indices.size() - 1;
            cell_idx = land_indices[idx];
        } else {
            int32_t rand_i = rng.next_range(0, static_cast<int32_t>(land_indices.size() - 1));
            cell_idx = land_indices[rand_i];
        }

        int32_t cx = cell_idx % w;
        int32_t cy = cell_idx / w;
        int64_t sub_x = rng.next_fixed_range(0, ONE - 1);
        int64_t sub_y = rng.next_fixed_range(0, ONE - 1);

        Vec2Fixed pos(static_cast<int64_t>(cx) * ONE + sub_x, static_cast<int64_t>(cy) * ONE + sub_y);
        Vec2Fixed dir(0, 0);
        double d_dir_x = 0.0;
        double d_dir_y = 0.0;
        int64_t v = ONE;
        int64_t water = ONE;
        int64_t sediment = 0;

        int32_t term_reason = 2; // 0=edge, 1=pit, 2=step_cap, 3=other
        for (int32_t step = 0; step < cfg.max_steps; ++step) {
            if (pos.x < 0 || pos.x >= max_bound_x || pos.y < 0 || pos.y >= max_bound_y) {
                term_reason = 0;
                break;
            }

            int32_t gx = static_cast<int32_t>(pos.x >> 16);
            int32_t gy = static_cast<int32_t>(pos.y >> 16);
            if (gx < 0 || gx >= w - 1 || gy < 0 || gy >= h - 1) {
                term_reason = 0;
                break;
            }

            int64_t fx = pos.x & 0xFFFF;
            int64_t fy = pos.y & 0xFFFF;

            int64_t w00 = mul_fixed(ONE - fx, ONE - fy);
            int64_t w10 = mul_fixed(fx, ONE - fy);
            int64_t w01 = mul_fixed(ONE - fx, fy);
            int64_t w11 = ONE - w00 - w10 - w01;

            size_t idx00 = static_cast<size_t>(gy) * w + gx;
            size_t idx10 = idx00 + 1;
            size_t idx01 = idx00 + w;
            size_t idx11 = idx01 + 1;

            int64_t h00 = elevation[idx00];
            int64_t h10 = elevation[idx10];
            int64_t h01 = elevation[idx01];
            int64_t h11 = elevation[idx11];

            int64_t h_curr = mul_fixed(h00, w00) + mul_fixed(h10, w10) + mul_fixed(h01, w01) + mul_fixed(h11, w11);
            if (h_curr <= 0) {
                term_reason = 0;
                break; // Droplet reached ocean
            }

            // Endpoint stamping (when supercover_hits is false)
            if (!cfg.supercover_hits) {
                if (out_hits) {
                    size_t c_idx = static_cast<size_t>(gy) * w + gx;
                    if (stamp[c_idx] != d) {
                        stamp[c_idx] = d;
                        (*out_hits)[c_idx]++;
                    }
                }
                if (out_hits_sub4) {
                    int32_t sub_gx = static_cast<int32_t>((pos.x * 4) >> 16);
                    int32_t sub_gy = static_cast<int32_t>((pos.y * 4) >> 16);
                    if (sub_gx >= 0 && sub_gx < w * 4 && sub_gy >= 0 && sub_gy < h * 4) {
                        size_t s_idx = static_cast<size_t>(sub_gy) * (w * 4) + sub_gx;
                        if (stamp_sub4[s_idx] != d) {
                            stamp_sub4[s_idx] = d;
                            (*out_hits_sub4)[s_idx]++;
                        }
                    }
                }
            }

            // Record continuous water flux along the Lagrangian droplet path
            if (track_flux) {
                accum_flux[idx00] += mul_fixed(water, w00);
                accum_flux[idx10] += mul_fixed(water, w10);
                accum_flux[idx01] += mul_fixed(water, w01);
                accum_flux[idx11] += mul_fixed(water, w11);
            }

            // Continuous sub-pixel bilinear gradient
            int64_t raw_gx = mul_fixed(h10 - h00, ONE - fy) + mul_fixed(h11 - h01, fy);
            int64_t raw_gy = mul_fixed(h01 - h00, ONE - fx) + mul_fixed(h11 - h10, fx);
            int64_t grad_x = div_fixed(raw_gx, cs);
            int64_t grad_y = div_fixed(raw_gy, cs);

            // Exact double grade for diagnostic histogram
            double dfx = static_cast<double>(fx) / 65536.0;
            double dfy = static_cast<double>(fy) / 65536.0;
            double d_gx = (static_cast<double>(h10 - h00) * (1.0 - dfy) + static_cast<double>(h11 - h01) * dfy) / static_cast<double>(cs);
            double d_gy = (static_cast<double>(h01 - h00) * (1.0 - dfx) + static_cast<double>(h11 - h10) * dfx) / static_cast<double>(cs);

            // Lake spillway gliding: if micro-gradient truncated to zero on flat/lake terrain
            if (cfg.lake_spillway_glide && raw_gx == 0 && raw_gy == 0) {
                int64_t best_drop = 0;
                int32_t best_dx = 0, best_dy = 0;
                const int32_t dx8[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
                const int32_t dy8[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
                for (int i = 0; i < 8; ++i) {
                    int32_t nx = gx + dx8[i];
                    int32_t ny = gy + dy8[i];
                    if (nx >= 0 && nx < w && ny >= 0 && ny < h) {
                        int64_t dr = h00 - elevation[static_cast<size_t>(ny) * w + nx];
                        if (dr > best_drop) {
                            best_drop = dr;
                            best_dx = dx8[i];
                            best_dy = dy8[i];
                        }
                    }
                }
                if (best_drop > 0) {
                    grad_x = -best_dx * ONE;
                    grad_y = -best_dy * ONE;
                    d_gx = static_cast<double>(-best_dx);
                    d_gy = static_cast<double>(-best_dy);
                }
            }

            if (cfg.float_direction) {
                double d_inertia = static_cast<double>(cfg.inertia) / 65536.0;
                d_dir_x = d_dir_x * d_inertia - d_gx * (1.0 - d_inertia);
                d_dir_y = d_dir_y * d_inertia - d_gy * (1.0 - d_inertia);
                double d_len = std::hypot(d_dir_x, d_dir_y);
                if (d_len > 0.0) {
                    d_dir_x /= d_len;
                    d_dir_y /= d_len;
                    dir.x = static_cast<int64_t>(std::llround(d_dir_x * 65536.0));
                    dir.y = static_cast<int64_t>(std::llround(d_dir_y * 65536.0));
                } else {
                    dir.x = 0;
                    dir.y = 0;
                }
            } else {
                // Update direction combining inertia and slope gradient (dir = dir * inertia - grad * (1 - inertia))
                dir.x = mul_fixed(dir.x, cfg.inertia) - mul_fixed(grad_x, ONE - cfg.inertia);
                dir.y = mul_fixed(dir.y, cfg.inertia) - mul_fixed(grad_y, ONE - cfg.inertia);
                dir = dir.normalized();
            }

            if (cfg.lake_spillway_glide && dir.x == 0 && dir.y == 0) {
                const int32_t dx8[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
                const int32_t dy8[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
                int64_t best_drop = 0;
                int32_t best_dx = 0, best_dy = 0;
                for (int i = 0; i < 8; ++i) {
                    int32_t nx = gx + dx8[i];
                    int32_t ny = gy + dy8[i];
                    if (nx >= 0 && nx < w && ny >= 0 && ny < h) {
                        int64_t dr = h00 - elevation[static_cast<size_t>(ny) * w + nx];
                        if (dr > best_drop) {
                            best_drop = dr;
                            best_dx = dx8[i];
                            best_dy = dy8[i];
                        }
                    }
                }
                if (best_drop > 0) {
                    dir.x = best_dx * ONE;
                    dir.y = best_dy * ONE;
                    dir = dir.normalized();
                    d_dir_x = static_cast<double>(dir.x) / 65536.0;
                    d_dir_y = static_cast<double>(dir.y) / 65536.0;
                    v = std::max(v, ONE);
                }
            }

            if (track_flow) {
                int64_t fw_x = mul_fixed(dir.x, v);
                int64_t fw_y = mul_fixed(dir.y, v);
                accum_flow_x[idx00] += mul_fixed(fw_x, w00);
                accum_flow_x[idx10] += mul_fixed(fw_x, w10);
                accum_flow_x[idx01] += mul_fixed(fw_x, w01);
                accum_flow_x[idx11] += mul_fixed(fw_x, w11);
                accum_flow_y[idx00] += mul_fixed(fw_y, w00);
                accum_flow_y[idx10] += mul_fixed(fw_y, w10);
                accum_flow_y[idx01] += mul_fixed(fw_y, w01);
                accum_flow_y[idx11] += mul_fixed(fw_y, w11);
            }

            if (dir.x == 0 && dir.y == 0) {
                term_reason = 1;
                if (out_stats) {
                    if (step == 0) out_stats->pit_dir_zero_step0++;
                    else out_stats->pit_dir_zero_later++;
                }
                if (out_pit_death_grades) {
                    out_pit_death_grades->push_back(std::hypot(d_gx, d_gy));
                }
                if (cfg.supercover_hits) {
                    if (out_hits) {
                        size_t c_idx = static_cast<size_t>(gy) * w + gx;
                        if (stamp[c_idx] != d) { stamp[c_idx] = d; (*out_hits)[c_idx]++; }
                    }
                    if (out_hits_sub4) {
                        int32_t sub_gx = static_cast<int32_t>((pos.x * 4) >> 16);
                        int32_t sub_gy = static_cast<int32_t>((pos.y * 4) >> 16);
                        if (sub_gx >= 0 && sub_gx < w * 4 && sub_gy >= 0 && sub_gy < h * 4) {
                            size_t s_idx = static_cast<size_t>(sub_gy) * (w * 4) + sub_gx;
                            if (stamp_sub4[s_idx] != d) { stamp_sub4[s_idx] = d; (*out_hits_sub4)[s_idx]++; }
                        }
                    }
                }
                // Flat area / pit bottom: deposit sediment and terminate
                if (sediment > 0 && !cfg.read_only) {
                    int64_t dep = mul_fixed(sediment, cfg.deposition_rate);
                    if (dep > 0) {
                        sediment -= dep;
                        int64_t d0 = mul_fixed(dep, w00);
                        int64_t d1 = mul_fixed(dep, w10);
                        int64_t d2 = mul_fixed(dep, w01);
                        int64_t d3 = dep - d0 - d1 - d2;
                        elevation[idx00] += static_cast<int32_t>(d0);
                        elevation[idx10] += static_cast<int32_t>(d1);
                        elevation[idx01] += static_cast<int32_t>(d2);
                        elevation[idx11] += static_cast<int32_t>(d3);
                        if (track_sediment) {
                            accum_sediment[idx00] += d0;
                            accum_sediment[idx10] += d1;
                            accum_sediment[idx01] += d2;
                            accum_sediment[idx11] += d3;
                        }
                    }
                }
                break;
            }

            Vec2Fixed new_pos = pos + dir;
            if (new_pos.x < 0 || new_pos.x >= max_bound_x || new_pos.y < 0 || new_pos.y >= max_bound_y) {
                if (cfg.supercover_hits && out_hits) {
                    size_t c_idx = static_cast<size_t>(gy) * w + gx;
                    if (stamp[c_idx] != d) { stamp[c_idx] = d; (*out_hits)[c_idx]++; }
                }
                term_reason = 0;
                break; // Exited map bounds
            }

            int32_t n_gx = static_cast<int32_t>(new_pos.x >> 16);
            int32_t n_gy = static_cast<int32_t>(new_pos.y >> 16);
            if (n_gx < 0 || n_gx >= w - 1 || n_gy < 0 || n_gy >= h - 1) {
                if (cfg.supercover_hits && out_hits) {
                    size_t c_idx = static_cast<size_t>(gy) * w + gx;
                    if (stamp[c_idx] != d) { stamp[c_idx] = d; (*out_hits)[c_idx]++; }
                }
                term_reason = 0;
                break;
            }

            int64_t n_fx = new_pos.x & 0xFFFF;
            int64_t n_fy = new_pos.y & 0xFFFF;

            int64_t n_w00 = mul_fixed(ONE - n_fx, ONE - n_fy);
            int64_t n_w10 = mul_fixed(n_fx, ONE - n_fy);
            int64_t n_w01 = mul_fixed(ONE - n_fx, n_fy);
            int64_t n_w11 = ONE - n_w00 - n_w10 - n_w01;

            size_t n_idx00 = static_cast<size_t>(n_gy) * w + n_gx;
            int64_t n_h = mul_fixed(elevation[n_idx00], n_w00) +
                          mul_fixed(elevation[n_idx00 + 1], n_w10) +
                          mul_fixed(elevation[n_idx00 + w], n_w01) +
                          mul_fixed(elevation[n_idx00 + w + 1], n_w11);

            if (n_h <= 0) {
                if (cfg.supercover_hits) {
                    if (out_hits) stamp_supercover_segment(pos.x, pos.y, new_pos.x, new_pos.y, w, h, d, stamp, *out_hits);
                    if (out_hits_sub4) stamp_supercover_segment(pos.x * 4, pos.y * 4, new_pos.x * 4, new_pos.y * 4, w * 4, h * 4, d, stamp_sub4, *out_hits_sub4);
                }
                term_reason = 0;
                break; // Entered ocean
            }

            int64_t delta_h = n_h - h_curr;
            int64_t drop = -delta_h; // h_curr - n_h

            // Speed update based on height difference: v = sqrt(v^2 + drop * gravity)
            int64_t v_sq = mul_fixed(v, v) + mul_fixed(drop, cfg.gravity);
            if (v_sq <= 0) {
                if (cfg.lake_spillway_glide && drop >= 0) {
                    v = ONE; // Maintain minimum transport velocity across flat lake surface
                } else {
                    if (out_stats) out_stats->pit_v_sq_le_0++;
                    if (out_pit_death_grades) {
                        out_pit_death_grades->push_back(std::hypot(d_gx, d_gy));
                    }
                    if (cfg.supercover_hits) {
                        if (out_hits) {
                            size_t c_idx = static_cast<size_t>(gy) * w + gx;
                            if (stamp[c_idx] != d) { stamp[c_idx] = d; (*out_hits)[c_idx]++; }
                        }
                        if (out_hits_sub4) {
                            int32_t sub_gx = static_cast<int32_t>((pos.x * 4) >> 16);
                            int32_t sub_gy = static_cast<int32_t>((pos.y * 4) >> 16);
                            if (sub_gx >= 0 && sub_gx < w * 4 && sub_gy >= 0 && sub_gy < h * 4) {
                                size_t s_idx = static_cast<size_t>(sub_gy) * (w * 4) + sub_gx;
                                if (stamp_sub4[s_idx] != d) { stamp_sub4[s_idx] = d; (*out_hits_sub4)[s_idx]++; }
                            }
                        }
                    }
                    term_reason = 1;
                    // Lost kinetic energy climbing uphill / entered depression
                    if (drop < 0 && sediment > 0 && !cfg.read_only) {
                        int64_t fill = std::min(sediment, -drop);
                        if (fill > 0) {
                            sediment -= fill;
                            int64_t d0 = mul_fixed(fill, w00);
                            int64_t d1 = mul_fixed(fill, w10);
                            int64_t d2 = mul_fixed(fill, w01);
                            int64_t d3 = fill - d0 - d1 - d2;
                            elevation[idx00] += static_cast<int32_t>(d0);
                            elevation[idx10] += static_cast<int32_t>(d1);
                            elevation[idx01] += static_cast<int32_t>(d2);
                            elevation[idx11] += static_cast<int32_t>(d3);
                            if (track_sediment) {
                                accum_sediment[idx00] += d0;
                                accum_sediment[idx10] += d1;
                                accum_sediment[idx01] += d2;
                                accum_sediment[idx11] += d3;
                            }
                        }
                    }
                    break;
                }
            } else {
                v = sqrt_fixed(v_sq);
            }

            // Supercover segment rasterization for accepted step [pos -> new_pos]
            if (cfg.supercover_hits) {
                if (out_hits) stamp_supercover_segment(pos.x, pos.y, new_pos.x, new_pos.y, w, h, d, stamp, *out_hits);
                if (out_hits_sub4) stamp_supercover_segment(pos.x * 4, pos.y * 4, new_pos.x * 4, new_pos.y * 4, w * 4, h * 4, d, stamp_sub4, *out_hits_sub4);
            }

            // Sediment capacity: C = max(-delta_h, min_slope) * v * w * capacity_factor
            int64_t eff_slope = std::max(drop, cfg.min_slope);
            int64_t C = mul_fixed(mul_fixed(eff_slope, v), mul_fixed(water, cfg.capacity_factor));

            if (!cfg.read_only) {
                // Deposit if sediment > C using bilinear 4-cell distribution
                if (sediment > C) {
                    int64_t dep = mul_fixed(sediment - C, cfg.deposition_rate);
                    if (dep > 0) {
                        sediment -= dep;
                        int64_t d0 = mul_fixed(dep, w00);
                        int64_t d1 = mul_fixed(dep, w10);
                        int64_t d2 = mul_fixed(dep, w01);
                        int64_t d3 = dep - d0 - d1 - d2;
                        elevation[idx00] += static_cast<int32_t>(d0);
                        elevation[idx10] += static_cast<int32_t>(d1);
                        elevation[idx01] += static_cast<int32_t>(d2);
                        elevation[idx11] += static_cast<int32_t>(d3);
                        if (track_sediment) {
                            accum_sediment[idx00] += d0;
                            accum_sediment[idx10] += d1;
                            accum_sediment[idx01] += d2;
                            accum_sediment[idx11] += d3;
                        }
                    }
                } else {
                    // Erode if sediment < C using bilinear 4-cell distribution, capped by -delta_h.
                    // Never push land below sea level (1); only enforce the downstream n_h floor on sheer cliff
                    // plunges (drop > 2m/step) so normal river valleys incise V-thalwegs instead of widening flat beds.
                    int64_t erode = mul_fixed(C - sediment, cfg.erosion_rate);
                    erode = std::min(erode, drop); // Capped by -delta_h
                    if (erode > 0) {
                        int64_t floor_h = (drop > 2 * ONE) ? std::max<int64_t>(1, n_h) : 1;
                        int64_t e0 = std::min(mul_fixed(erode, w00), std::max<int64_t>(0, static_cast<int64_t>(elevation[idx00]) - floor_h));
                        int64_t e1 = std::min(mul_fixed(erode, w10), std::max<int64_t>(0, static_cast<int64_t>(elevation[idx10]) - floor_h));
                        int64_t e2 = std::min(mul_fixed(erode, w01), std::max<int64_t>(0, static_cast<int64_t>(elevation[idx01]) - floor_h));
                        int64_t e3 = std::min(erode - mul_fixed(erode, w00) - mul_fixed(erode, w10) - mul_fixed(erode, w01),
                                              std::max<int64_t>(0, static_cast<int64_t>(elevation[idx11]) - floor_h));
                        int64_t actual_erode = e0 + e1 + e2 + e3;
                        if (actual_erode > 0) {
                            sediment += actual_erode;
                            elevation[idx00] -= static_cast<int32_t>(e0);
                            elevation[idx10] -= static_cast<int32_t>(e1);
                            elevation[idx01] -= static_cast<int32_t>(e2);
                            elevation[idx11] -= static_cast<int32_t>(e3);
                        }
                    }
                }
            }

            // Evaporate water: water = mul_fixed(water, ONE - evaporation_rate)
            water = mul_fixed(water, ONE - cfg.evaporation_rate);
            if (water <= 0) {
                term_reason = 3;
                break;
            }

            pos = new_pos;
        }

        if (out_stats) {
            if (term_reason == 0) out_stats->edge++;
            else if (term_reason == 1) out_stats->pit++;
            else if (term_reason == 2) out_stats->step_cap++;
            else if (term_reason == 3) out_stats->other++;
        }
    }

    // Populate out_water_flux
    if (out_water_flux) {
        out_water_flux->assign(total, 0);
        for (size_t i = 0; i < total; ++i) {
            (*out_water_flux)[i] = accum_flux[i];
        }
    }

    // Populate continuous out_water_depth (calibrated in 16.16 fixed-point meters)
    if (out_water_depth) {
        out_water_depth->assign(total, 0);
        for (size_t i = 0; i < total; ++i) {
            if (elevation[i] <= 0) {
                (*out_water_depth)[i] = 0; // Ocean has sea-level water plane
                continue;
            }
            int64_t flux = accum_flux[i];
            if (flux > cfg.flux_threshold) {
                int64_t excess = flux - cfg.flux_threshold;
                int64_t q_sqrt = sqrt_fixed(excess);
                int64_t d = mul_fixed(q_sqrt, cfg.depth_scale);
                if (d > cfg.max_water_depth) d = cfg.max_water_depth;
                (*out_water_depth)[i] = static_cast<int32_t>(d);
            }
        }
    }

    // Populate cumulative out_sediment
    if (out_sediment) {
        out_sediment->assign(total, 0);
        for (size_t i = 0; i < total; ++i) {
            (*out_sediment)[i] = static_cast<int32_t>(accum_sediment[i]);
        }
    }

    // Populate out_flow_x and out_flow_y (normalized direction vectors)
    if (out_flow_x && out_flow_y) {
        out_flow_x->assign(total, 0);
        out_flow_y->assign(total, 0);
        for (size_t i = 0; i < total; ++i) {
            int64_t fx = accum_flow_x[i];
            int64_t fy = accum_flow_y[i];
            int64_t len_sq = mul_fixed(fx, fx) + mul_fixed(fy, fy);
            if (len_sq > 64) {
                int64_t inv_len = inv_sqrt_fixed(len_sq);
                (*out_flow_x)[i] = static_cast<int32_t>(mul_fixed(fx, inv_len));
                (*out_flow_y)[i] = static_cast<int32_t>(mul_fixed(fy, inv_len));
            }
        }
    }
}


// ===== Priority-Flood Sink Infilling (Barnes / Planchon-Darboux) =====
// Guarantees continuous downhill drainage from any land cell to boundaries/sea level (0.0m).
// Eliminates spurious pits without altering natural ridges or summits.
inline void priority_flood_infilling(std::vector<int32_t>& elevation, int32_t w, int32_t h, int64_t sea_level = 0) {
    if (w <= 2 || h <= 2 || elevation.size() != static_cast<size_t>(w) * h) return;

    struct Cell {
        int64_t elev;
        int32_t x;
        int32_t y;
        bool operator>(const Cell& o) const {
            if (elev != o.elev) return elev > o.elev;
            if (y != o.y) return y > o.y;
            return x > o.x;
        }
    };

    std::priority_queue<Cell, std::vector<Cell>, std::greater<Cell>> pq;
    std::vector<uint8_t> visited(static_cast<size_t>(w) * h, 0);

    // Initialize borders as initial discharge outlets
    for (int32_t x = 0; x < w; ++x) {
        for (int32_t y : {0, h - 1}) {
            size_t idx = static_cast<size_t>(y) * w + x;
            pq.push({elevation[idx], x, y});
            visited[idx] = 1;
        }
    }
    for (int32_t y = 1; y < h - 1; ++y) {
        for (int32_t x : {0, w - 1}) {
            size_t idx = static_cast<size_t>(y) * w + x;
            pq.push({elevation[idx], x, y});
            visited[idx] = 1;
        }
    }

    const int32_t dxs[8] = {-1, 1, 0, 0, -1, 1, -1, 1};
    const int32_t dys[8] = {0, 0, -1, 1, -1, -1, 1, 1};

    while (!pq.empty()) {
        Cell c = pq.top();
        pq.pop();

        for (int i = 0; i < 8; ++i) {
            int32_t nx = c.x + dxs[i];
            int32_t ny = c.y + dys[i];
            if (nx >= 0 && nx < w && ny >= 0 && ny < h) {
                size_t n_idx = static_cast<size_t>(ny) * w + nx;
                if (!visited[n_idx]) {
                    visited[n_idx] = 1;
                    if (elevation[n_idx] < c.elev) {
                        elevation[n_idx] = static_cast<int32_t>(c.elev); // Infill to spillway height
                    }
                    pq.push({elevation[n_idx], nx, ny});
                }
            }
        }
    }
}

// ===== D8 Flow Routing & Catchment Area Accumulation =====
// Evaluates deterministic D8 flow directions and accumulates drainage catchment area A(x, y).
inline void compute_drainage_and_flow(
    const std::vector<int32_t>& elevation,
    int32_t w, int32_t h,
    std::vector<int32_t>& out_flow_targets,
    std::vector<int64_t>& out_area,
    const std::vector<int32_t>* precipitation = nullptr
) {
    size_t total = static_cast<size_t>(w) * h;
    out_flow_targets.assign(total, -1);
    out_area.assign(total, ONE); // 1.0 initial cell runoff weight

    if (precipitation && precipitation->size() == total) {
        for (size_t i = 0; i < total; ++i) {
            int64_t p = (*precipitation)[i];
            out_area[i] = std::clamp(p, ONE / 2, 4LL * ONE);
        }
    }

    // 1. Sort indices in descending elevation order with deterministic tie-breaking
    std::vector<int32_t> sorted_indices(total);
    for (size_t i = 0; i < total; ++i) sorted_indices[i] = static_cast<int32_t>(i);
    std::sort(sorted_indices.begin(), sorted_indices.end(), [&](int32_t a, int32_t b) {
        if (elevation[a] != elevation[b]) return elevation[a] > elevation[b];
        return a < b;
    });

    const int32_t dxs[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
    const int32_t dys[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
    // Distance in 16.16 fixed-point (diag: sqrt(2) ~= 92681, card: 65536)
    const int64_t dists[8] = {92681, ONE, 92681, ONE, ONE, 92681, ONE, 92681};

    // Find steepest downhill descent neighbor
    for (int32_t idx : sorted_indices) {
        int32_t cx = idx % w;
        int32_t cy = idx / w;
        int64_t c_elev = elevation[idx];
        if (c_elev <= 0) continue; // Ocean cells do not route flow

        int64_t best_slope = 0;
        int32_t best_target = -1;

        for (int i = 0; i < 8; ++i) {
            int32_t nx = cx + dxs[i];
            int32_t ny = cy + dys[i];
            if (nx >= 0 && nx < w && ny >= 0 && ny < h) {
                int32_t n_idx = ny * w + nx;
                int64_t drop = c_elev - elevation[n_idx];
                if (drop > 0) {
                    int64_t s = div_fixed(drop, dists[i]);
                    if (s > best_slope || (s == best_slope && (best_target == -1 || n_idx < best_target))) {
                        best_slope = s;
                        best_target = n_idx;
                    }
                }
            }
        }

        if (best_target != -1) {
            out_flow_targets[idx] = best_target;
        }
    }

    // Accumulate drainage area from highest to lowest elevation
    for (int32_t idx : sorted_indices) {
        int32_t target = out_flow_targets[idx];
        if (target >= 0 && target < static_cast<int32_t>(total)) {
            out_area[target] += out_area[idx];
        }
    }
}

// ===== Targeted Downrange Rain Runoff Channel Incision =====
// Focuses erosion strictly along concentrated downrange drainage paths where rain gathers.
// High peaks, ridge crests, and rolling foothill crowns (A < threshold) remain 100% UNTOUCHED.
// When water converges (A >= threshold), it carves a smooth parabolic cross-section ravine.
inline void carve_downrange_channels(
    std::vector<int32_t>& elevation,
    int32_t w, int32_t h,
    int64_t cell_size_fixed,
    const std::vector<int32_t>& flow_targets,
    const std::vector<int64_t>& area,
    int32_t channel_init_thresh_cells = 18,
    int64_t k_carve = 2400, // 16.16 (e.g. 2400 / 65536 ~= 0.0366)
    int64_t max_depth_fixed = 8 * ONE // Max 8.0m incision cap
) {
    if (w <= 2 || h <= 2 || elevation.size() != static_cast<size_t>(w) * h) return;
    size_t total = static_cast<size_t>(w) * h;
    int64_t thresh_fixed = static_cast<int64_t>(channel_init_thresh_cells) * ONE;

    std::vector<int32_t> channel_incision(total, 0);

    const int32_t dxs[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
    const int32_t dys[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
    const int64_t dists[8] = {92681, ONE, 92681, ONE, ONE, 92681, ONE, 92681};

    for (size_t idx = 0; idx < total; ++idx) {
        if (elevation[idx] <= 0) continue; // Skip ocean
        int64_t a = area[idx];

        // 1. Threshold check: Zero incision on peaks, ridges, and slopes where water hasn't gathered
        if (a < thresh_fixed) continue;

        int32_t tgt = flow_targets[idx];
        if (tgt < 0 || tgt >= static_cast<int32_t>(total)) continue;

        int32_t cx = static_cast<int32_t>(idx % w);
        int32_t cy = static_cast<int32_t>(idx / w);
        int32_t tx = static_cast<int32_t>(tgt % w);
        int32_t ty = static_cast<int32_t>(tgt / w);

        int32_t step_x = abs(cx - tx);
        int32_t step_y = abs(cy - ty);
        int64_t dist = (step_x != 0 && step_y != 0) ? 92681 : ONE;

        int64_t drop = std::max(0LL, static_cast<int64_t>(elevation[idx] - elevation[tgt]));
        int64_t slope = div_fixed(drop, dist);

        // Soft slope saturation: prevents runaway knife chasms on extreme cliffs
        // S_sat = (S * S_ref) / (S + S_ref) where S_ref = 0.5 * ONE (~26 degrees)
        const int64_t S_REF = 32768; // 0.5 * ONE
        int64_t s_sat = div_fixed(mul_fixed(slope, S_REF), slope + S_REF);

        // Effective runoff flow: Q = sqrt(A - threshold)
        int64_t q_excess = a - thresh_fixed;
        int64_t q_sqrt = sqrt_fixed(q_excess);

        // Centerline channel depth
        int64_t power = mul_fixed(q_sqrt, s_sat);
        int64_t depth = mul_fixed(k_carve, power);
        depth = std::clamp(depth, 0LL, max_depth_fixed);
        if (depth <= 0) continue;

        // Channel radius in tiles: widening downrange from 2 tiles to 5 tiles for deep valleys
        int32_t radius = 2;
        if (a > 200LL * ONE) {
            radius = 5;
        } else if (a > 120LL * ONE) {
            radius = 4;
        } else if (a > 50LL * ONE) {
            radius = 3;
        }

        // Apply smooth parabolic cross-section to centerline and banks
        for (int32_t dy = -radius; dy <= radius; ++dy) {
            int32_t ny = cy + dy;
            if (ny < 0 || ny >= h) continue;
            for (int32_t dx = -radius; dx <= radius; ++dx) {
                int32_t nx = cx + dx;
                if (nx < 0 || nx >= w) continue;

                int32_t dist_sq = dx * dx + dy * dy;
                int32_t r_sq = radius * radius;
                if (dist_sq <= r_sq) {
                    size_t n_idx = static_cast<size_t>(ny) * w + nx;
                    // Parabolic cross-section: 1 - dist_sq / (r_sq + 1)
                    int64_t falloff = ONE - div_fixed(dist_sq * ONE, (r_sq + 1) * ONE);
                    int32_t bank_depth = static_cast<int32_t>(mul_fixed(depth, falloff));
                    if (bank_depth > channel_incision[n_idx]) {
                        channel_incision[n_idx] = bank_depth;
                    }
                }
            }
        }
    }

    // Apply carved incision to land terrain without submerging land below sea level
    for (size_t i = 0; i < total; ++i) {
        if (channel_incision[i] > 0 && elevation[i] > 0) {
            elevation[i] = std::max<int32_t>(1, elevation[i] - channel_incision[i]);
        }
    }
}

// ===== Thermal / Gravitational Talus Relaxation =====
// Relaxes slopes exceeding the critical angle of repose (scree friction threshold).
// Only shears off unnatural sharp vertical steps (> ~42 degrees) without altering general mountain profiles.
inline void apply_talus_relaxation(
    std::vector<int32_t>& elevation,
    int32_t w, int32_t h,
    int64_t talus_threshold = 55705, // 0.85 * ONE (~40-42 degrees)
    int32_t iterations = 3,
    int64_t cell_size_fixed = 2 * ONE
) {
    if (w <= 2 || h <= 2 || elevation.size() != static_cast<size_t>(w) * h || cell_size_fixed <= 0) return;
    const int32_t dxs[4] = {-1, 1, 0, 0};
    const int32_t dys[4] = {0, 0, -1, 1};

    int64_t max_stable_drop = mul_fixed(talus_threshold, cell_size_fixed);
    std::vector<int32_t> scratch = elevation;

    for (int32_t it = 0; it < iterations; ++it) {
        for (int32_t y = 1; y < h - 1; ++y) {
            for (int32_t x = 1; x < w - 1; ++x) {
                size_t idx = static_cast<size_t>(y) * w + x;
                int64_t c_elev = elevation[idx];

                for (int i = 0; i < 4; ++i) {
                    size_t n_idx = static_cast<size_t>(y + dys[i]) * w + (x + dxs[i]);
                    int64_t raw_drop = c_elev - elevation[n_idx];
                    int64_t excess = raw_drop - max_stable_drop;
                    if (excess > 0) {
                        int64_t transfer = excess / 4;
                        scratch[idx] -= static_cast<int32_t>(transfer);
                        scratch[n_idx] += static_cast<int32_t>(transfer);
                    }
                }
            }
        }
        elevation = scratch;
    }
}

// ===== Alluvial Deposition & Soil Genesis =====
// Enhances soil nutrients in flatter basins where sediment deposits.
inline void apply_alluvial_soil_genesis(
    const std::vector<int32_t>& elevation,
    const std::vector<int64_t>& area,
    std::vector<int32_t>& soil_n,
    std::vector<int32_t>& soil_p,
    std::vector<int32_t>& soil_compactness,
    int32_t w, int32_t h
) {
    size_t total = static_cast<size_t>(w) * h;
    for (size_t i = 0; i < total; ++i) {
        if (elevation[i] <= 0 || elevation[i] > 16 * ONE) continue;
        if (area[i] > 80 * ONE) { // Significant drainage accumulation
            // Boost Nitrogen & Phosphorus (alluvial silt)
            soil_n[i] = static_cast<int32_t>(std::clamp(static_cast<int64_t>(soil_n[i]) + 50LL * ONE, 60LL * ONE, 240LL * ONE));
            soil_p[i] = static_cast<int32_t>(std::clamp(static_cast<int64_t>(soil_p[i]) + 40LL * ONE, 40LL * ONE, 180LL * ONE));
            // Reduce compactness (soft fertile loess/loam)
            soil_compactness[i] = static_cast<int32_t>(std::clamp(static_cast<int64_t>(soil_compactness[i]) - 15LL * ONE, 15LL * ONE, 50LL * ONE));
        }
    }
}

// ===== Wave-Cage Guided Coastal Shoreline Erosion (Jeschke & Wojtan 2020) =====
// Harnesses boundary-aware wave cage physics (ellipsoidal compression & vertical shoaling surge)
// as an analytic formulation of breaker dynamics to drive coastal geomorphology:
// 1. Identifies shoreline boundaries where water (H <= 0) meets land (H > 0).
// 2. Evaluates local wave cage parameters:
//    - a1: seaward shore normal vector pointing out to sea
//    - a2: along-shore tangent vector for littoral drift
//    - r3: bathymetric offshore water depth clearance
//    - Open-ocean fetch length & swell propagation alignment
// 3. Evaluates vertical wave breaker surge:
//    In shallow water, wave energy flux F = E * cg is compressed into ellipsoidal strain.
//    Where r3 drops rapidly close to shore (steep bathymetric gradient), vertical acceleration
//    spikes (|a_y| > g), driving plunging breakers and hydraulic quarrying:
//    - Exposed Headlands & Capes (High Wave Surge):
//      Excavates an intertidal wave-cut notch at H ~= 0, undercuts adjacent sea cliffs,
//      and forms a flat, intertidal rock platform (bench) at H in [-0.5m, 0.0m].
//    - Sheltered Bays & Estuaries (Low Wave Surge):
//      Wave energy dissipates over shallow shelves; eroded sediment transported down-drift
//      by littoral currents is deposited, building sandy beaches and protective sandbars.
// 4. Closed-loop sediment conservation: excavated rock is routed strictly through the littoral
//    drift budget into sheltered zones without spontaneous mass creation or leakage.
inline void apply_wave_cage_coastal_erosion(
    std::vector<int32_t>& elevation,
    int32_t w, int32_t h,
    int64_t cell_size_fixed = 2 * ONE,
    int32_t iterations = 3,
    int64_t strength_fixed = 98304 // 1.5 * ONE
) {
    if (w <= 6 || h <= 6 || elevation.size() != static_cast<size_t>(w) * h || iterations <= 0 || strength_fixed <= 0) return;

    size_t total = static_cast<size_t>(w) * h;
    const int32_t dx8[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
    const int32_t dy8[8] = {-1, -1, -1, 0, 0, 1, 1, 1};

    // Prevailing ocean swell vector in 16.16: matches wave_compute_driver.gd [0.85, 0.52]
    const int64_t SWELL_DIR_X = 55705; // 0.85 * ONE
    const int64_t SWELL_DIR_Y = 34078; // 0.52 * ONE

    const int64_t WAVE_POWER_THRESH = 13107; // 0.20 * ONE

    for (int32_t it = 0; it < iterations; ++it) {
        std::vector<int32_t> next_elev = elevation;
        std::vector<int32_t> sediment_delta(total, 0);
        std::vector<int32_t> cell_pwave(total, 0);

        for (int32_t y = 3; y < h - 3; ++y) {
            for (int32_t x = 3; x < w - 3; ++x) {
                size_t idx = static_cast<size_t>(y) * w + x;
                int64_t h_curr = elevation[idx];

                // Focus on coastal zone: from shallow seabed (-4m) up to coastal bluff (+24m)
                if (h_curr < -4LL * ONE || h_curr > 24LL * ONE) continue;

                // 1. Detect coastal interface and compute shoreline normal vector
                int32_t water_count = 0;
                int64_t nx_sum = 0;
                int64_t ny_sum = 0;

                for (int i = 0; i < 8; ++i) {
                    size_t n_idx = static_cast<size_t>(y + dy8[i]) * w + (x + dx8[i]);
                    if (elevation[n_idx] <= 0) {
                        water_count++;
                        nx_sum += dx8[i] * ONE;
                        ny_sum += dy8[i] * ONE;
                    }
                }

                // If not adjacent to water (interior land or deep water), skip
                if (water_count == 0 || (h_curr <= 0 && water_count == 8)) continue;

                // Normalize coastal vector pointing toward water (seaward cage normal a1)
                int64_t len_sq = mul_fixed(nx_sum, nx_sum) + mul_fixed(ny_sum, ny_sum);
                if (len_sq <= 16) continue;
                int64_t inv_len = inv_sqrt_fixed(len_sq);
                int64_t nx = mul_fixed(nx_sum, inv_len);
                int64_t ny = mul_fixed(ny_sum, inv_len);

                // 2. Sub-pixel raymarching: sample offshore water depth r3 and fetch along a1
                int64_t rx = static_cast<int64_t>(x) * ONE + ONE / 2;
                int64_t ry = static_cast<int64_t>(y) * ONE + ONE / 2;

                int64_t r3 = 0;
                int32_t depth_samples = 0;
                int32_t fetch_steps = 0;

                for (int step = 1; step <= 20; ++step) {
                    rx += nx;
                    ry += ny;
                    int32_t gx = static_cast<int32_t>(rx >> 16);
                    int32_t gy = static_cast<int32_t>(ry >> 16);

                    if (gx < 0 || gx >= w || gy < 0 || gy >= h) {
                        fetch_steps += (20 - step + 1);
                        break;
                    }

                    size_t g_idx = static_cast<size_t>(gy) * w + gx;
                    int64_t gh = elevation[g_idx];

                    if (gh <= 0) {
                        fetch_steps++;
                        if (depth_samples < 5 && gh < 0) {
                            r3 += (-gh);
                            depth_samples++;
                        }
                    } else {
                        break; // Blocked by island, headland, or opposite coast
                    }
                }
                if (depth_samples > 0) r3 /= depth_samples;
                int64_t fetch_factor = div_fixed(fetch_steps * ONE, 20 * ONE);
                fetch_factor = std::clamp(fetch_factor, 13107LL, ONE); // [0.2, 1.0]

                // 3. Swell alignment & Jeschke vertical shoaling breaker surge
                int64_t swell_dot = -mul_fixed(nx, SWELL_DIR_X) - mul_fixed(ny, SWELL_DIR_Y);
                int64_t align = std::clamp(swell_dot, 0LL, ONE);
                int64_t e_swell = 22000 + mul_fixed(43536, align);

                int64_t depth_grad = div_fixed(r3, 2 * ONE);
                depth_grad = std::clamp(depth_grad, 16384LL, 2 * ONE);

                int64_t p_wave = mul_fixed(mul_fixed(e_swell, fetch_factor), depth_grad);
                cell_pwave[idx] = static_cast<int32_t>(p_wave);

                // 4. Geomorphological Carving & Breaker Penetration
                if (p_wave >= WAVE_POWER_THRESH && h_curr > 0) {
                    int64_t eroded_vol = 0;

                    // Breaker penetration reach: 1 to 4 cells inland based on wave power & strength
                    int32_t reach = 1 + static_cast<int32_t>(mul_fixed(p_wave, strength_fixed) >> 16);
                    reach = std::clamp(reach, 1, 4);

                    int64_t prev_h = -ONE / 4;
                    for (int32_t d = 0; d < reach; ++d) {
                        int32_t cx = x;
                        int32_t cy = y;
                        if (d > 0) {
                            int64_t step_fixed_x = nx * d;
                            int64_t step_fixed_y = ny * d;
                            int32_t step_x = (step_fixed_x >= 0) ? static_cast<int32_t>((step_fixed_x + 32768) >> 16)
                                                                 : static_cast<int32_t>((step_fixed_x - 32768) >> 16);
                            int32_t step_y = (step_fixed_y >= 0) ? static_cast<int32_t>((step_fixed_y + 32768) >> 16)
                                                                 : static_cast<int32_t>((step_fixed_y - 32768) >> 16);
                            cx = x - step_x;
                            cy = y - step_y;
                        }
                        if (cx < 0 || cx >= w || cy < 0 || cy >= h) continue;

                        size_t c_idx = static_cast<size_t>(cy) * w + cx;
                        int64_t ch = next_elev[c_idx];
                        if (ch <= -ONE) continue;

                        // Shoreline cell (d=0) planes down to wave-cut rock bench (-0.25m)
                        // Inland cells (d>0) undercut cliff face down to low intertidal platform
                        int64_t target_platform = (d == 0) ? (-ONE / 4) : ((d * ONE) / 2);
                        int64_t base_carve = mul_fixed(p_wave, (ONE * 3) / 4);
                        int64_t carve = mul_fixed(base_carve, strength_fixed) / (d + 1);

                        // Cliff failure / mass wasting: upper cliff collapses when base is notched
                        const int64_t MAX_STABLE_CLIFF = 3 * ONE; // 3m cliff stability limit
                        if (ch > prev_h + MAX_STABLE_CLIFF) {
                            int64_t collapse = mul_fixed(ch - (prev_h + MAX_STABLE_CLIFF), ONE / 2);
                            carve += collapse;
                        }

                        int64_t new_ch = std::max(target_platform, ch - carve);
                        if (new_ch < ch) {
                            eroded_vol += (ch - new_ch);
                            next_elev[c_idx] = static_cast<int32_t>(new_ch);
                        }
                        prev_h = new_ch;
                    }

                    // 5. Littoral Drift: multi-cell longshore current transport
                    if (eroded_vol > 0) {
                        int64_t cross_prod = mul_fixed(nx, SWELL_DIR_Y) - mul_fixed(ny, SWELL_DIR_X);
                        int32_t drift_dir = (cross_prod >= 0) ? 1 : -1;
                        int64_t tx = -ny * drift_dir;
                        int64_t ty = nx * drift_dir;

                        int32_t drift_steps = 2 + static_cast<int32_t>(mul_fixed(p_wave, strength_fixed) >> 16);
                        drift_steps = std::clamp(drift_steps, 2, 6);

                        int64_t sed_per_step = eroded_vol / drift_steps;
                        int64_t cur_tx = static_cast<int64_t>(x) * ONE + ONE / 2;
                        int64_t cur_ty = static_cast<int64_t>(y) * ONE + ONE / 2;

                        for (int32_t s = 1; s <= drift_steps; ++s) {
                            cur_tx += tx;
                            cur_ty += ty;
                            int32_t sx = static_cast<int32_t>(cur_tx >> 16);
                            int32_t sy = static_cast<int32_t>(cur_ty >> 16);
                            if (sx >= 0 && sx < w && sy >= 0 && sy < h) {
                                size_t s_idx = static_cast<size_t>(sy) * w + sx;
                                sediment_delta[s_idx] += static_cast<int32_t>(sed_per_step);
                            }
                        }
                    }
                }
            }
        }

        // 6. Closed-Loop Deposition: deposit sediment strictly in sheltered shallows & calm embayments
        for (size_t i = 0; i < total; ++i) {
            int64_t sed = sediment_delta[i];
            if (sed <= 0) continue;

            // Only deposit in sheltered bays/embayments; prevent dumping sand onto exposed erosive headlands
            if (cell_pwave[i] >= WAVE_POWER_THRESH) continue;

            int64_t eh = next_elev[i];
            // Intertidal & beach berm deposition [-3.0m, +2.5m]
            if (eh >= -3 * ONE && eh <= (5 * ONE) / 2) {
                int64_t max_dep = mul_fixed(ONE, strength_fixed);
                int64_t target_beach = (5 * ONE) / 2; // +2.5m beach crest
                int64_t deposit = std::min(sed, max_dep);
                deposit = std::min(deposit, target_beach - eh);
                if (deposit > 0) {
                    next_elev[i] += static_cast<int32_t>(deposit);
                    sed -= deposit;
                }
            }
            // Subtidal bar deposition [-4.0m, -3.0m]
            if (sed > 0 && eh >= -4 * ONE && eh < -3 * ONE) {
                int64_t sub_deposit = std::min(sed, mul_fixed(ONE / 2, strength_fixed));
                next_elev[i] += static_cast<int32_t>(sub_deposit);
            }
        }

        elevation = next_elev;
    }
}

// ===== Staged Droplet Geomorphic Erosion: Network Search -> Converged Equilibrium -> Pure Deepening =====
// Achieves crisp, naturally equilibrated dendritic channels strictly through particle droplet physics:
//   Stage 1 (Network Search): High-inertia exploratory droplets with full capacity to break micro-divides
//       and organize basin-wide dendritic drainage topology.
//   Stage 2 (Converged Equilibrium): Moderate-inertia transport-balanced droplets (erosion + deposition)
//       + scree talus relaxation + optional coastal wave-cage shaping to grade valleys to equilibrium.
//   Stage 3 (Pure Deepening): Zero-deposition, low-inertia droplets with boosted unsaturated capacity so
//       converged trunk flows hug existing valley thalwegs and purely incise without wandering or blurring.
struct StagedErosionStats {
    int32_t search_droplets = 0;
    int32_t equilibrium_droplets = 0;
    int32_t deepening_droplets = 0;
};

struct StagedErosionConfig {
    DropletErosionConfig droplet;
    bool apply_droplets = true;
    bool apply_coastal = true;
    int32_t coastal_iterations = 12;
    int64_t coastal_strength = ONE;
    int64_t talus_threshold = 55705; // ~0.85 (~40-42 deg)
    int32_t talus_iterations = 3;
};

inline void apply_staged_geomorphic_erosion(
    std::vector<int32_t>& elevation,
    int32_t w, int32_t h,
    int64_t cell_size_fixed,
    const StagedErosionConfig& cfg,
    std::vector<int32_t>* out_sediment = nullptr,
    StagedErosionStats* out_stats = nullptr
) {
    if (w <= 2 || h <= 2 || elevation.size() != static_cast<size_t>(w) * h) return;
    const int64_t cs = (cell_size_fixed > 0) ? cell_size_fixed : ONE;
    if (out_stats) *out_stats = StagedErosionStats{};

    int32_t total_droplets = cfg.apply_droplets ? std::max(0, cfg.droplet.droplet_count) : 0;
    int32_t search_droplets = (total_droplets * 35) / 100;
    int32_t eq_droplets = (total_droplets * 45) / 100;
    int32_t deep_droplets = total_droplets - search_droplets - eq_droplets;

    // --- Stage 1: Network Search (Diffuse Exploratory Droplet Regime) ---
    priority_flood_infilling(elevation, w, h, 0);
    if (search_droplets > 0) {
        DropletErosionConfig search_cfg = cfg.droplet;
        search_cfg.droplet_count = search_droplets;
        search_cfg.inertia = std::clamp(cfg.droplet.inertia, 3277LL, 9830LL); // ~0.05..0.15: connect rills without wide inertia swaths
        search_cfg.lake_spillway_glide = true;
        apply_hydraulic_droplet_erosion(elevation, w, h, cs, search_cfg);
        if (out_stats) out_stats->search_droplets = search_droplets;
    }

    // --- Stage 2: Converged Equilibrium (Diffuse Transport Balance + Talus + Coastal) ---
    priority_flood_infilling(elevation, w, h, 0);
    if (eq_droplets > 0) {
        DropletErosionConfig eq_cfg = cfg.droplet;
        eq_cfg.droplet_count = eq_droplets;
        eq_cfg.seed = cfg.droplet.seed + 101ULL;
        eq_cfg.lake_spillway_glide = true;
        apply_hydraulic_droplet_erosion(elevation, w, h, cs, eq_cfg, nullptr, nullptr, out_sediment);
        if (out_stats) out_stats->equilibrium_droplets = eq_droplets;
    }
    if (cfg.talus_iterations > 0) {
        apply_talus_relaxation(elevation, w, h, cfg.talus_threshold, std::min(cfg.talus_iterations, 2), cs);
    }
    if (cfg.apply_coastal && cfg.coastal_iterations > 0) {
        apply_wave_cage_coastal_erosion(elevation, w, h, cs, cfg.coastal_iterations, cfg.coastal_strength);
    }

    // --- Stage 3: Gentle Pure Deepening (Zero-Deposition, Low-Inertia Thalweg Finish) ---
    priority_flood_infilling(elevation, w, h, 0);
    if (deep_droplets > 0) {
        DropletErosionConfig deep_cfg = cfg.droplet;
        deep_cfg.droplet_count = deep_droplets;
        deep_cfg.seed = cfg.droplet.seed + 202ULL;
        deep_cfg.deposition_rate = 0; // Avoid dumping sediment back inside converged channels
        deep_cfg.inertia = std::min<int64_t>(cfg.droplet.inertia, 1966LL); // <= 0.03: hug narrow thalweg without widening bends
        deep_cfg.erosion_rate = std::max<int64_t>(8192LL, cfg.droplet.erosion_rate / 2); // Gentle finishing incision (no deep trenching)
        deep_cfg.lake_spillway_glide = true;
        apply_hydraulic_droplet_erosion(elevation, w, h, cs, deep_cfg);
        if (out_stats) out_stats->deepening_droplets = deep_droplets;
    }
    priority_flood_infilling(elevation, w, h, 0);
}

// ===== Sub-Grid MFD-Field Droplet Advection (Convergent Continuous Streamlines) =====
// Combines an eps-ramped Priority-Flood DEM with a downhill-only MFD (p=2) velocity field
// and unit-count sub-grid (S=4) advection to produce cycle-free, sub-pixel river occupancy
// and flow heading fields that converge with D8/MFD drainage without grid staircases.
struct SubgridDropletField {
    static constexpr int32_t SUBGRID_SCALE = 4; // S = 4 sub-pixels per terrain cell
    int32_t width = 0;
    int32_t height = 0;
    int32_t sub_w = 0;
    int32_t sub_h = 0;
    std::vector<int32_t> occ;       // Sub-pixel droplet transition counts (units: 1 = 1 cell)
    std::vector<int64_t> hx;        // Summed unit flow vector X (16.16 fixed-point)
    std::vector<int64_t> hy;        // Summed unit flow vector Y (16.16 fixed-point)
    std::vector<int64_t> z_ramped;  // Eps-ramped Priority-Flood elevations ((int64_t)elev << 8)
};

inline void compute_subgrid_mfd_droplets(
    const std::vector<int32_t>& elevation,
    int32_t w, int32_t h,
    SubgridDropletField& out_field,
    uint64_t seed = 1337
) {
    out_field = SubgridDropletField{};
    if (w <= 2 || h <= 2) return;
    size_t total = static_cast<size_t>(w) * h;
    if (elevation.size() < total) return;

    out_field.width = w;
    out_field.height = h;
    constexpr int32_t S = SubgridDropletField::SUBGRID_SCALE;
    out_field.sub_w = w * S;
    out_field.sub_h = h * S;
    size_t sub_total = static_cast<size_t>(out_field.sub_w) * out_field.sub_h;
    out_field.occ.assign(sub_total, 0);
    out_field.hx.assign(sub_total, 0);
    out_field.hy.assign(sub_total, 0);

    // 1. Priority-Flood with strict sub-LSB ramp (<< 8 scale, eps = 1)
    // Borders and ocean cells (elevation <= 0) act as outlets so land never traps droplets.
    out_field.z_ramped.resize(total);
    for (size_t i = 0; i < total; ++i) {
        out_field.z_ramped[i] = static_cast<int64_t>(elevation[i]) << 8;
    }

    struct RampCell {
        int64_t z;
        int32_t idx;
        bool operator>(const RampCell& o) const {
            if (z != o.z) return z > o.z;
            return idx > o.idx;
        }
    };
    std::priority_queue<RampCell, std::vector<RampCell>, std::greater<RampCell>> pq;
    std::vector<uint8_t> visited(total, 0);

    for (int32_t y = 0; y < h; ++y) {
        for (int32_t x = 0; x < w; ++x) {
            size_t idx = static_cast<size_t>(y) * w + x;
            bool is_border = (x == 0 || x == w - 1 || y == 0 || y == h - 1);
            if (is_border || elevation[idx] <= 0) {
                visited[idx] = 1;
                pq.push({out_field.z_ramped[idx], static_cast<int32_t>(idx)});
            }
        }
    }

    const int32_t dxs[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
    const int32_t dys[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
    const int64_t dists[8] = {92681, ONE, 92681, ONE, ONE, 92681, ONE, 92681};
    const int64_t unit_dx[8] = {-46341, 0, 46341, -ONE, ONE, -46341, 0, 46341};
    const int64_t unit_dy[8] = {-46341, -ONE, -46341, 0, 0, 46341, ONE, 46341};

    while (!pq.empty()) {
        RampCell c = pq.top();
        pq.pop();
        int32_t cx = c.idx % w;
        int32_t cy = c.idx / w;
        for (int k = 0; k < 8; ++k) {
            int32_t nx = cx + dxs[k];
            int32_t ny = cy + dys[k];
            if (nx >= 0 && nx < w && ny >= 0 && ny < h) {
                size_t n_idx = static_cast<size_t>(ny) * w + nx;
                if (!visited[n_idx]) {
                    visited[n_idx] = 1;
                    if (out_field.z_ramped[n_idx] <= c.z) {
                        out_field.z_ramped[n_idx] = c.z + 1; // Minimal strictly-downhill ramp
                    }
                    pq.push({out_field.z_ramped[n_idx], static_cast<int32_t>(n_idx)});
                }
            }
        }
    }

    // 2. Downhill-only MFD slope-weighted velocity field (p = 2, scale-invariant fixed-point)
    std::vector<int32_t> vx(total, 0);
    std::vector<int32_t> vy(total, 0);

    for (int32_t y = 0; y < h; ++y) {
        for (int32_t x = 0; x < w; ++x) {
            size_t idx = static_cast<size_t>(y) * w + x;
            int64_t zc = out_field.z_ramped[idx];

            int64_t slopes[8] = {0};
            int64_t max_slope = 0;
            for (int k = 0; k < 8; ++k) {
                int32_t nx = x + dxs[k];
                int32_t ny = y + dys[k];
                if (nx >= 0 && nx < w && ny >= 0 && ny < h) {
                    size_t n_idx = static_cast<size_t>(ny) * w + nx;
                    int64_t drop = zc - out_field.z_ramped[n_idx];
                    if (drop > 0) {
                        // Scale drop safely into [1..max] relative units without 64-bit overflow
                        int64_t s = (drop <= (1LL << 40)) ? ((drop << 16) / dists[k]) : (drop / (dists[k] >> 8));
                        if (s <= 0) s = 1;
                        slopes[k] = s;
                        if (s > max_slope) max_slope = s;
                    }
                }
            }

            if (max_slope <= 0) continue;

            int64_t sum_vx = 0;
            int64_t sum_vy = 0;
            int64_t w_sum = 0;
            for (int k = 0; k < 8; ++k) {
                if (slopes[k] > 0) {
                    int64_t rel = div_fixed(slopes[k], max_slope); // (0, ONE]
                    int64_t w2 = mul_fixed(rel, rel);              // p = 2
                    sum_vx += mul_fixed(w2, unit_dx[k]);
                    sum_vy += mul_fixed(w2, unit_dy[k]);
                    w_sum += w2;
                }
            }
            if (w_sum > 0) {
                Vec2Fixed v(div_fixed(sum_vx, w_sum), div_fixed(sum_vy, w_sum));
                Vec2Fixed vn = v.normalized();
                vx[idx] = static_cast<int32_t>(vn.x);
                vy[idx] = static_cast<int32_t>(vn.y);
            }
        }
    }

    // 3. Sub-grid (S = 4) unit-step droplet advection across interior land cells
    SplitMix64 rng(seed ^ 0x9E3779B97F4A7C15ULL);
    const int64_t step_size = ONE / S; // 0.25 cell = 1 sub-pixel
    const int32_t max_steps = std::max(512, (w + h) * 8);
    const int32_t sw = out_field.sub_w;
    const int32_t sh = out_field.sub_h;
    const int64_t min_edge = ONE / 2;
    const int64_t max_edge_x = static_cast<int64_t>(w - 1) * ONE - ONE / 2;
    const int64_t max_edge_y = static_cast<int64_t>(h - 1) * ONE - ONE / 2;

    auto to_sub_idx = [&](int64_t px, int64_t py) -> int32_t {
        int32_t sj = static_cast<int32_t>(((px + ONE / 2) * S) >> 16);
        int32_t si = static_cast<int32_t>(((py + ONE / 2) * S) >> 16);
        sj = std::clamp(sj, 0, sw - 1);
        si = std::clamp(si, 0, sh - 1);
        return si * sw + sj;
    };

    for (int32_t cy = 1; cy < h - 1; ++cy) {
        for (int32_t cx = 1; cx < w - 1; ++cx) {
            size_t cell_idx = static_cast<size_t>(cy) * w + cx;
            if (elevation[cell_idx] <= 0) continue; // Ocean cells do not spawn land runoff

            // 1 jittered droplet per cell in [-0.5, +0.5)
            int64_t jx = static_cast<int64_t>(rng.next() & 0xFFFF) - (ONE / 2);
            int64_t jy = static_cast<int64_t>(rng.next() & 0xFFFF) - (ONE / 2);
            int64_t px = std::clamp(static_cast<int64_t>(cx) * ONE + jx, min_edge, max_edge_x);
            int64_t py = std::clamp(static_cast<int64_t>(cy) * ONE + jy, min_edge, max_edge_y);

            int32_t last_sub = to_sub_idx(px, py);
            out_field.occ[last_sub] += 1;

            for (int32_t step = 0; step < max_steps; ++step) {
                int32_t gx = std::clamp(static_cast<int32_t>(px >> 16), 0, w - 2);
                int32_t gy = std::clamp(static_cast<int32_t>(py >> 16), 0, h - 2);
                int64_t fx = std::clamp(px - (static_cast<int64_t>(gx) * ONE), 0LL, ONE);
                int64_t fy = std::clamp(py - (static_cast<int64_t>(gy) * ONE), 0LL, ONE);

                int64_t w00 = mul_fixed(ONE - fx, ONE - fy);
                int64_t w10 = mul_fixed(fx, ONE - fy);
                int64_t w01 = mul_fixed(ONE - fx, fy);
                int64_t w11 = ONE - w00 - w10 - w01;

                size_t i00 = static_cast<size_t>(gy) * w + gx;
                int64_t bx = mul_fixed(vx[i00], w00) + mul_fixed(vx[i00 + 1], w10) +
                             mul_fixed(vx[i00 + w], w01) + mul_fixed(vx[i00 + w + 1], w11);
                int64_t by = mul_fixed(vy[i00], w00) + mul_fixed(vy[i00 + 1], w10) +
                             mul_fixed(vy[i00 + w], w01) + mul_fixed(vy[i00 + w + 1], w11);

                Vec2Fixed dir = Vec2Fixed(bx, by).normalized();
                if (dir.x == 0 && dir.y == 0) break;

                px += mul_fixed(dir.x, step_size);
                py += mul_fixed(dir.y, step_size);

                bool at_border = (px <= min_edge || px >= max_edge_x || py <= min_edge || py >= max_edge_y);
                int32_t rx = std::clamp(static_cast<int32_t>((px + ONE / 2) >> 16), 0, w - 1);
                int32_t ry = std::clamp(static_cast<int32_t>((py + ONE / 2) >> 16), 0, h - 1);
                bool in_ocean = (elevation[static_cast<size_t>(ry) * w + rx] <= 0);

                int32_t sub = to_sub_idx(
                    std::clamp(px, 0LL, static_cast<int64_t>(w - 1) * ONE),
                    std::clamp(py, 0LL, static_cast<int64_t>(h - 1) * ONE)
                );
                if (sub != last_sub) {
                    out_field.occ[sub] += 1;
                    out_field.hx[sub] += dir.x;
                    out_field.hy[sub] += dir.y;
                    last_sub = sub;
                }

                if (at_border || in_ocean) break;
            }
        }
    }
}

// ===== Backwards-Compatible Full Pass Wrapper =====
inline void compute_drainage_and_stream_power(
    std::vector<int32_t>& elevation,
    int32_t w, int32_t h,
    std::vector<int32_t>& out_flow_targets,
    std::vector<int64_t>& out_area,
    int64_t k_erodibility = 2400
) {
    compute_drainage_and_flow(elevation, w, h, out_flow_targets, out_area);
    carve_downrange_channels(elevation, w, h, 2 * ONE, out_flow_targets, out_area, 18, k_erodibility, 8 * ONE);
}

} // namespace Erosion
