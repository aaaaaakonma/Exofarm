#pragma once

#include "../math/fixed.h"
#include "../math/fixed_3d.h"
#include "../math/bicubic.h"
#include "../math/noise.h"
#include "dem_erosion.h"
#include "river_network.h"
#include <vector>
#include <string>
#include <fstream>
#include <cstdint>
#include <algorithm>
#include <iostream>
#include <cstring>

namespace Topo {

struct OpenTopoData {
    int32_t width = 0;
    int32_t height = 0;
    int32_t min_elev = 0; // 16.16
    int32_t max_elev = 0; // 16.16
    std::vector<int32_t> elevation_grid; // 16.16 fixed-point heights
    bool loaded = false;
};

class OpenTopoHeightmap {
public:
    OpenTopoData data;

    OpenTopoHeightmap() = default;

    bool is_loaded() const { return data.loaded && !data.elevation_grid.empty(); }

    // Loads a raw 16.16 fixed-point binary heightmap file (.bin)
    bool load_binary(const std::string& path, int32_t expected_w = 512, int32_t expected_h = 512) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            return false;
        }

        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        size_t num_elements = static_cast<size_t>(size / sizeof(int32_t));
        if (num_elements != static_cast<size_t>(expected_w) * expected_h) {
            // Check if it's a square grid of different size
            int32_t side = static_cast<int32_t>(std::sqrt(num_elements));
            if (static_cast<size_t>(side) * side == num_elements) {
                expected_w = side;
                expected_h = side;
            } else {
                return false;
            }
        }

        data.width = expected_w;
        data.height = expected_h;
        data.elevation_grid.resize(num_elements);

        if (!file.read(reinterpret_cast<char*>(data.elevation_grid.data()), size)) {
            data.elevation_grid.clear();
            data.loaded = false;
            return false;
        }

        data.min_elev = *std::min_element(data.elevation_grid.begin(), data.elevation_grid.end());
        data.max_elev = *std::max_element(data.elevation_grid.begin(), data.elevation_grid.end());
        data.loaded = true;
        return true;
    }

    // Loads directly from an in-memory byte buffer
    bool load_from_memory(const uint8_t* buffer, size_t size, int32_t w = 512, int32_t h = 512) {
        if (!buffer || size < static_cast<size_t>(w) * h * sizeof(int32_t)) return false;
        data.width = w;
        data.height = h;
        data.elevation_grid.resize(static_cast<size_t>(w) * h);
        std::memcpy(data.elevation_grid.data(), buffer, data.elevation_grid.size() * sizeof(int32_t));
        data.min_elev = *std::min_element(data.elevation_grid.begin(), data.elevation_grid.end());
        data.max_elev = *std::max_element(data.elevation_grid.begin(), data.elevation_grid.end());
        data.loaded = true;
        return true;
    }

    // Evaluates continuous sub-tile height using 16.16 Bicubic Catmull-Rom interpolation
    int64_t sample_continuous(int64_t x_fixed, int64_t y_fixed, int64_t cell_size_fixed) const {
        if (!data.loaded || data.elevation_grid.empty()) return 0;
        int32_t gw = data.width;
        int32_t gh = data.height;

        int64_t max_x = static_cast<int64_t>(gw - 1) * cell_size_fixed;
        int64_t max_y = static_cast<int64_t>(gh - 1) * cell_size_fixed;

        int64_t cx = std::clamp(x_fixed, 0LL, max_x);
        int64_t cy = std::clamp(y_fixed, 0LL, max_y);

        int32_t gx = static_cast<int32_t>(cx / cell_size_fixed);
        int32_t gy = static_cast<int32_t>(cy / cell_size_fixed);
        if (gx >= gw - 1) gx = gw - 2;
        if (gy >= gh - 1) gy = gh - 2;
        if (gx < 0) gx = 0;
        if (gy < 0) gy = 0;

        int64_t rem_x = cx - (static_cast<int64_t>(gx) * cell_size_fixed);
        int64_t rem_y = cy - (static_cast<int64_t>(gy) * cell_size_fixed);
        int64_t tx = div_fixed(rem_x, cell_size_fixed);
        int64_t ty = div_fixed(rem_y, cell_size_fixed);

        auto get_val = [&](int32_t ix, int32_t iy) -> int64_t {
            ix = std::clamp(ix, 0, gw - 1);
            iy = std::clamp(iy, 0, gh - 1);
            return data.elevation_grid[static_cast<size_t>(iy) * gw + ix];
        };

        int64_t col[4];
        for (int j = -1; j <= 2; ++j) {
            int32_t iy = gy + j;
            int64_t p0 = get_val(gx - 1, iy);
            int64_t p1 = get_val(gx + 0, iy);
            int64_t p2 = get_val(gx + 1, iy);
            int64_t p3 = get_val(gx + 2, iy);
            col[j + 1] = SimMath::cubic_catmull_rom(p0, p1, p2, p3, tx);
        }

        return SimMath::cubic_catmull_rom(col[0], col[1], col[2], col[3], ty);
    }

    // Fills world elevation grid with seed-based domain transformation, perimeter ocean buffering,
    // and optional geomorphological erosion + riverbed carving.
    void generate_grid(
        int32_t w, int32_t h,
        int64_t cell_size_fixed,
        std::vector<int32_t>& out_elevation,
        uint64_t seed,
        int64_t sea_level_offset = 0,
        int32_t sea_buffer_tiles = 32,
        bool apply_runtime_erosion = true,
        Hydrology::ParameterizedRiverNetwork* out_rivers = nullptr
    ) {
        size_t total = static_cast<size_t>(w) * h;
        out_elevation.resize(total);

        // Fallback: If no binary data loaded, generate procedural fallback or load default preset
        if (!data.loaded) {
            if (!load_binary("assets/data/topo/alpine_basin.bin", 512, 512) &&
                !load_binary("assets/data/topo/river_valley.bin", 512, 512)) {
                // Synthesize smooth fallback terrain if assets not found
                for (size_t i = 0; i < total; ++i) {
                    int32_t x = static_cast<int32_t>(i % w);
                    int32_t y = static_cast<int32_t>(i / w);
                    int64_t el = SimMath::fbm2d(x * 1200, y * 1200, seed, 4);
                    out_elevation[i] = static_cast<int32_t>(el * 40);
                }
                return;
            }
        }

        // Seed-based domain transformations
        // 1. Translation offset
        int64_t offset_x = (static_cast<int64_t>(SimMath::hash2d(13, 37, seed) % 64)) * cell_size_fixed;
        int64_t offset_y = (static_cast<int64_t>(SimMath::hash2d(73, 91, seed) % 64)) * cell_size_fixed;

        // 2. Subtle organic domain warping
        int64_t warp_strength = 6 * ONE; // 6.0 meters

        int64_t center_x = (static_cast<int64_t>(w) * cell_size_fixed) / 2;
        int64_t center_y = (static_cast<int64_t>(h) * cell_size_fixed) / 2;

        for (int32_t y = 0; y < h; ++y) {
            int64_t py = static_cast<int64_t>(y) * cell_size_fixed;
            for (int32_t x = 0; x < w; ++x) {
                int64_t px = static_cast<int64_t>(x) * cell_size_fixed;
                size_t idx = static_cast<size_t>(y) * w + x;

                // Domain warp
                int64_t wx = mul_fixed(warp_strength, SimMath::fbm2d(px / 16, py / 16, seed + 101, 2));
                int64_t wy = mul_fixed(warp_strength, SimMath::fbm2d(py / 16, px / 16, seed + 202, 2));

                int64_t qx = px + offset_x + wx;
                int64_t qy = py + offset_y + wy;

                int64_t el = sample_continuous(qx, qy, cell_size_fixed);

                // Apply sea level offset
                el += sea_level_offset;

                // Perimeter sea buffer: smoothly submerge border down to -20m
                if (sea_buffer_tiles > 0) {
                    int32_t dist_x = std::min(x, w - 1 - x);
                    int32_t dist_y = std::min(y, h - 1 - y);
                    int32_t edge_dist = std::min(dist_x, dist_y);

                    if (edge_dist < sea_buffer_tiles) {
                        int64_t u = div_fixed(static_cast<int64_t>(edge_dist) * ONE, static_cast<int64_t>(sea_buffer_tiles) * ONE);
                        // Quintic smoothstep: 6u^5 - 15u^4 + 10u^3
                        int64_t u2 = mul_fixed(u, u);
                        int64_t u3 = mul_fixed(u2, u);
                        int64_t u4 = mul_fixed(u3, u);
                        int64_t u5 = mul_fixed(u4, u);
                        int64_t weight = (6 * u5) - (15 * u4) + (10 * u3);
                        weight = std::clamp(weight, 0LL, ONE);

                        int64_t ocean_floor = -20LL * ONE;
                        el = ocean_floor + mul_fixed(weight, el - ocean_floor);
                    }
                }

                out_elevation[idx] = static_cast<int32_t>(el);
            }
        }

        // Run multi-phase geomorphological erosion suite
        if (apply_runtime_erosion) {
            // Staged Droplet Erosion: Network Search -> Converged Equilibrium -> Pure Deepening
            Erosion::StagedErosionConfig staged_cfg;
            staged_cfg.droplet.droplet_count = std::max(1024, (w * h) / 4);
            staged_cfg.droplet.erosion_rate = 32768; // 0.5 * ONE
            staged_cfg.droplet.deposition_rate = 16384; // 0.25 * ONE
            staged_cfg.droplet.seed = seed;
            staged_cfg.apply_droplets = true;
            staged_cfg.apply_coastal = false;
            staged_cfg.talus_threshold = 55705;
            staged_cfg.talus_iterations = 3;
            Erosion::apply_staged_geomorphic_erosion(out_elevation, w, h, cell_size_fixed, staged_cfg);

            // Parameterized Sub-Grid River Extraction & Bed Carving
            if (out_rivers) {
                out_rivers->extract_from_subgrid_droplets(out_elevation, w, h, cell_size_fixed, 110, ONE, seed);
                out_rivers->carve_riverbeds(out_elevation, w, h, cell_size_fixed);
            }
        }
    }
};

} // namespace Topo
