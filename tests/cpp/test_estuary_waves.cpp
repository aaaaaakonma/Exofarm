#include <iostream>
#include <vector>
#include <cassert>
#include <cmath>
#include <iomanip>
#include "src/math/fixed.h"
#include "src/math/fixed_3d.h"
#include "src/core/river_network.h"
#include "src/core/opentopo_heightmap.h"

int main() {
    std::cout << "====================================================\n";
    std::cout << "ExoFarm Headless Verification: River-to-Sea Estuary & Waves\n";
    std::cout << "====================================================\n\n";

    // 1. Setup a test terrain grid (128x128) with a sloping hill down to an ocean
    int32_t w = 128;
    int32_t h = 128;
    int64_t cell_size_fixed = 2 * ONE; // 2.0m
    std::vector<int32_t> elevation(w * h, 0);

    // Land slopes from east to west: West (x < 40) is ocean (H < 0), East (x >= 40) is land (H >= 0)
    for (int32_t y = 0; y < h; ++y) {
        for (int32_t x = 0; x < w; ++x) {
            int32_t idx = y * w + x;
            // Slopes from -20m at x=0 to +60m at x=127
            // Coastline (H = 0m) is roughly at x = 32
            int64_t elev_m = (static_cast<int64_t>(x) - 32) * (ONE / 2);
            elevation[idx] = static_cast<int32_t>(elev_m);
        }
    }

    // 2. Synthesize a parametric river reach flowing westwards into the ocean
    Hydrology::ParameterizedRiverNetwork rivers;
    Hydrology::ParametricRiverReach reach;
    reach.id = 1;
    reach.tier = 2; // River
    reach.flow_rate = 50 * ONE;
    reach.base_width = 6 * ONE;

    // Reach starts inland at x=90, y=64 and terminates in the ocean at x=28, y=64
    for (int32_t x = 90; x >= 28; x -= 4) {
        Vec2Fixed pt(static_cast<int64_t>(x) * cell_size_fixed, static_cast<int64_t>(64) * cell_size_fixed);
        reach.waypoints.push_back(pt);
        reach.widths.push_back(6 * ONE);
        size_t idx = static_cast<size_t>(64) * w + x;
        reach.bed_elevations.push_back(elevation[idx]);
    }
    reach.total_length = static_cast<int64_t>(reach.waypoints.size() - 1) * 4 * cell_size_fixed;
    rivers.add_reach(reach);

    // Carve riverbed into elevation: cuts 2.0m deeper along centerline
    rivers.carve_riverbeds(elevation, w, h, cell_size_fixed);

    // Verify carved riverbed near coastline: at x=34 (natural elevation ~+1.0m),
    // carved bed is now < 0.0m (submerged river trench inland from sea!)
    size_t inland_trench_idx = 64 * w + 34;
    float carved_el = static_cast<float>(elevation[inland_trench_idx]) / 65536.0f;
    std::cout << "[+] Carved riverbed near coast (x=34, y=64): " << carved_el << "m (submerged below 0m)\n";
    assert(carved_el < 0.0f);

    // 3. Extract Estuary Mouths
    auto mouths = rivers.get_estuary_mouths(0);
    std::cout << "[+] Identified Estuary Mouths: " << mouths.size() << "\n";
    assert(mouths.size() == 1);
    const auto& m = mouths[0];
    float mouth_x = static_cast<float>(m.position.x) / (65536.0f * 2.0f);
    float mouth_y = static_cast<float>(m.position.y) / (65536.0f * 2.0f);
    float dir_x = static_cast<float>(m.outflow_direction.x) / 65536.0f;
    float dir_y = static_cast<float>(m.outflow_direction.y) / 65536.0f;
    float mouth_speed = static_cast<float>(m.discharge_velocity) / 65536.0f;

    std::cout << "    Reach ID: " << m.reach_id << "\n";
    std::cout << "    Mouth Grid Pos: (" << mouth_x << ", " << mouth_y << ")\n";
    std::cout << "    Outflow Direction: (" << dir_x << ", " << dir_y << ") [pointing seaward]\n";
    std::cout << "    Discharge Velocity: " << mouth_speed << " m/s\n";

    // Outflow direction should point West (-X) towards open ocean
    assert(dir_x < -0.9f);
    assert(std::abs(dir_y) < 0.1f);
    assert(mouth_speed > 1.0f);

    // 4. Compute Estuary Fields (Boundary Mask + Ocean Plume Velocity)
    std::vector<int64_t> boundary_mask;
    std::vector<Vec2Fixed> plume_vel;
    rivers.compute_estuary_fields(elevation, w, h, cell_size_fixed, 0, boundary_mask, plume_vel);

    // Check A: Inland carved river cell (x=34, y=64) MUST be pinned as a boundary (mask = ONE)
    // even though its elevation is negative (-1.0m)!
    int64_t trench_mask = boundary_mask[inland_trench_idx];
    std::cout << "[+] Inland carved river trench (x=34, y=64) Boundary Mask: "
              << (trench_mask == ONE ? "1.0 (PINNED DIRICHLET BOUNDARY)" : "0.0 (FAIL)") << "\n";
    assert(trench_mask == ONE);

    // Check B: Seaward Ocean Plume (x=20, y=64 - in open water west of river mouth)
    // Should have non-zero plume velocity pointing seaward (West / -X)
    size_t ocean_plume_idx = 64 * w + 20;
    float plume_vx = static_cast<float>(plume_vel[ocean_plume_idx].x) / 65536.0f;
    float plume_vy = static_cast<float>(plume_vel[ocean_plume_idx].y) / 65536.0f;
    int64_t ocean_mask = boundary_mask[ocean_plume_idx];

    std::cout << "[+] Seaward Ocean Plume (x=20, y=64): Plume Vel = (" << plume_vx << ", " << plume_vy << ") m/s, Mask = " << ocean_mask << "\n";
    assert(plume_vx < -0.2f); // Discharging westwards
    assert(ocean_mask == 0);  // Ocean is open water, NOT blocked

    // Check C: Distant Ocean Cell (x=5, y=10) should have ZERO plume velocity and ZERO mask
    size_t distant_ocean_idx = 10 * w + 5;
    float dist_vx = static_cast<float>(plume_vel[distant_ocean_idx].x) / 65536.0f;
    int64_t dist_mask = boundary_mask[distant_ocean_idx];
    std::cout << "[+] Distant Open Ocean (x=5, y=10): Plume Vel = (" << dist_vx << "), Mask = " << dist_mask << "\n";
    assert(std::abs(dist_vx) < 0.001f);
    assert(dist_mask == 0);

    // 5. Determinism Verification: Repeat and ensure bit-for-bit identity
    std::vector<int64_t> boundary_mask2;
    std::vector<Vec2Fixed> plume_vel2;
    rivers.compute_estuary_fields(elevation, w, h, cell_size_fixed, 0, boundary_mask2, plume_vel2);

    for (size_t i = 0; i < boundary_mask.size(); ++i) {
        assert(boundary_mask[i] == boundary_mask2[i]);
        assert(plume_vel[i].x == plume_vel2[i].x);
        assert(plume_vel[i].y == plume_vel2[i].y);
    }
    std::cout << "[+] 100% Bit-for-Bit Determinism across runs verified.\n\n";

    std::cout << "====================================================\n";
    std::cout << "All Estuary & Boundary Wave Integration Tests PASSED!\n";
    std::cout << "====================================================\n";
    return 0;
}
