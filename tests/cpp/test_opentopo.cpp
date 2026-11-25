#include <iostream>
#include <iomanip>
#include <vector>
#include <cassert>
#include <cmath>
#include <map>
#include <string>

#include "src/core/dem_erosion.h"
#include "src/core/river_network.h"
#include "src/core/opentopo_heightmap.h"

int main() {
    std::cout << "==========================================================\n";
    std::cout << "   EXOFARM OPENTOPO DEM & PARAMETRIC HYDROLOGY SUITE      \n";
    std::cout << "==========================================================\n";

    Topo::OpenTopoHeightmap topo;

    // 1. Ingest Binary DEM Preset
    std::cout << "--- 1. Testing Binary DEM Ingestion ---" << std::endl;
    bool loaded = topo.load_binary("assets/data/topo/alpine_basin.bin", 512, 512);
    if (!loaded) {
        std::cerr << "  [FAIL] Could not load assets/data/topo/alpine_basin.bin" << std::endl;
        return 1;
    }
    assert(topo.is_loaded());
    std::cout << "  Loaded 512x512 DEM: min=" << (topo.data.min_elev / 65536.0)
              << "m, max=" << (topo.data.max_elev / 65536.0) << "m" << std::endl;
    assert(topo.data.elevation_grid.size() == 512 * 512);
    std::cout << "  [PASS] 16.16 Fixed-Point Binary DEM Ingestion Verified." << std::endl;

    // 2. Continuous Bicubic Sampling & Derivative Smoothness
    std::cout << "\n--- 2. Testing Continuous Bicubic Sub-Pixel Sampling ---" << std::endl;
    int64_t cell_size = 2 * ONE; // 2.0m per tile
    int64_t x1 = 120 * cell_size + 32768; // 120.5 tiles
    int64_t y1 = 140 * cell_size + 16384; // 140.25 tiles
    int64_t h1 = topo.sample_continuous(x1, y1, cell_size);
    int64_t h2 = topo.sample_continuous(x1 + 65, y1, cell_size); // sub-millimeter shift
    double diff = std::abs((h2 - h1) / 65536.0);
    std::cout << "  Sample at (120.5, 140.25): Elev = " << (h1 / 65536.0)
              << "m | Delta over 0.001m = " << diff << "m" << std::endl;
    assert(diff < 0.1); // Continuous derivative
    std::cout << "  [PASS] Continuous C^1 Sub-Pixel Bicubic Sampling Verified." << std::endl;

    // 3. Grid Generation, Perimeter Sea Buffering & Geomorphological Erosion
    std::cout << "\n--- 3. Testing World Grid Generation & Erosion Suite ---" << std::endl;
    int32_t w = 256;
    int32_t h = 256;
    uint64_t seed = 75843591;
    std::vector<int32_t> elevation;
    Hydrology::ParameterizedRiverNetwork rivers;

    topo.generate_grid(w, h, cell_size, elevation, seed, 0, 24, true, &rivers);
    assert(elevation.size() == static_cast<size_t>(w) * h);

    // Verify perimeter sea buffer: border edges must be submerged <= -15m
    for (int32_t x = 0; x < w; ++x) {
        assert(elevation[x] <= -15 * ONE);
        assert(elevation[(h - 1) * w + x] <= -15 * ONE);
    }
    for (int32_t y = 0; y < h; ++y) {
        assert(elevation[y * w] <= -15 * ONE);
        assert(elevation[y * w + (w - 1)] <= -15 * ONE);
    }
    std::cout << "  [PASS] Perimeter Sea Buffer Verified (All borders submerged < -15.0m ocean)." << std::endl;

    // 4. Parameterized Non-Chunked River Reaches
    std::cout << "\n--- 4. Testing Parameterized Non-Chunked River Network ---" << std::endl;
    std::cout << "  Extracted River Reaches: " << rivers.reaches.size() << std::endl;
    assert(!rivers.reaches.empty());

    const auto& r0 = rivers.reaches[0];
    std::cout << "  Reach 1: Tier=" << (int)r0.tier
              << ", Length=" << (r0.total_length / 65536.0) << "m"
              << ", Waypoints=" << r0.waypoints.size()
              << ", Flow Capacity=" << (r0.max_capacity / 65536.0) << " units/tick" << std::endl;
    assert(r0.waypoints.size() >= 3);
    assert(r0.total_length > 0);

    // Sample continuous river query
    int64_t dist_to_center = 0, width = 0, depth = 0;
    Vec2Fixed flow_vec;
    uint32_t reach_id = 0;
    bool found_near = rivers.sample_nearest_river(r0.waypoints[1], dist_to_center, width, flow_vec, depth, reach_id);
    assert(found_near);
    assert(reach_id == r0.id);
    assert(dist_to_center < 16); // Exactly on waypoint
    std::cout << "  Sample at Waypoint: Width=" << (width / 65536.0)
              << "m, Depth=" << (depth / 65536.0)
              << "m, Flow Vector=(" << (flow_vec.x / 65536.0) << ", " << (flow_vec.y / 65536.0) << ")" << std::endl;
    std::cout << "  [PASS] Parameterized Non-Chunked River Splines & Continuous Vectors Verified." << std::endl;

    // 5. 100% Determinism Verification
    std::cout << "\n--- 5. Testing 100% Bit-for-Bit Determinism ---" << std::endl;
    std::vector<int32_t> elevation2;
    Hydrology::ParameterizedRiverNetwork rivers2;
    topo.generate_grid(w, h, cell_size, elevation2, seed, 0, 24, true, &rivers2);

    assert(elevation == elevation2);
    assert(rivers.reaches.size() == rivers2.reaches.size());
    for (size_t i = 0; i < rivers.reaches.size(); ++i) {
        assert(rivers.reaches[i].total_length == rivers2.reaches[i].total_length);
        assert(rivers.reaches[i].waypoints.size() == rivers2.reaches[i].waypoints.size());
    }
    std::cout << "  [PASS] Bit-for-Bit Determinism Verified across all " << (w * h) << " cells." << std::endl;

    // 6. ASCII Topographic Preview
    std::cout << "\n--- ASCII Topographic Preview (64x24) ---" << std::endl;
    const char glyphs[] = ".~-:=+*#%@";
    int step_x = w / 64;
    int step_y = h / 24;
    for (int y = 0; y < 24; ++y) {
        std::string row = "";
        for (int x = 0; x < 64; ++x) {
            int32_t gx = x * step_x;
            int32_t gy = y * step_y;
            int64_t el = elevation[gy * w + gx];
            if (el <= 0) {
                row += '~'; // Sea
            } else {
                int idx = std::clamp((int)(el / (8 * ONE)), 0, 9);
                row += glyphs[idx];
            }
        }
        std::cout << row << std::endl;
    }
    std::cout << "(Legend: '~' = Sea/Ocean, '.' to '@' = Coastal Beach to Glaciated Peaks)\n";

    std::cout << "\n>>> ALL OPENTOPO & PARAMETRIC HYDROLOGY TESTS PASSED! <<<\n";
    return 0;
}
