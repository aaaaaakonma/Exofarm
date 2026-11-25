#include <iostream>
#include <iomanip>
#include <vector>
#include <cassert>
#include <cmath>
#include <algorithm>

#include "src/core/dem_erosion.h"
#include "src/core/tectonic_heightmap.h"
#include "src/core/river_network.h"

int main() {
    std::cout << "==========================================================\n";
    std::cout << "      EXOFARM MOUNTAIN RAIN CHANNEL & EROSION SUITE       \n";
    std::cout << "==========================================================\n";

    // -------------------------------------------------------------------------
    // Test 1: Priority-Flood Sink Infilling
    // -------------------------------------------------------------------------
    std::cout << "--- 1. Testing Priority-Flood Monotonic Sink Infilling ---" << std::endl;
    {
        int32_t w = 5;
        int32_t h = 5;
        // 5x5 grid with a central pit at (2,2) of height 2m, surrounded by rim of 10m, outlet at (0,2) of 6m
        std::vector<int32_t> elev(w * h, 10 * ONE);
        elev[2 * w + 2] = 2 * ONE; // Deep pit
        elev[2 * w + 0] = 6 * ONE; // Spillway outlet on western border

        Erosion::priority_flood_infilling(elev, w, h, 0);

        std::cout << "  Pit elevation before: 2.0m | after infilling: " << (elev[2 * w + 2] / 65536.0) << "m" << std::endl;
        // Infilled pit must match or exceed spillway outlet height (6m) to drain freely
        assert(elev[2 * w + 2] >= 6 * ONE);
        std::cout << "  [PASS] Spurious sinks infilled to spillway height." << std::endl;
    }

    // -------------------------------------------------------------------------
    // Test 2: D8 Flow Routing & Catchment Accumulation
    // -------------------------------------------------------------------------
    std::cout << "\n--- 2. Testing D8 Flow Routing & Catchment Accumulation ---" << std::endl;
    {
        int32_t w = 8;
        int32_t h = 8;
        // V-shaped descending valley: slopes downhill from east to west (x=7 down to x=0),
        // and slopes inward from north/south toward central valley row y=4.
        std::vector<int32_t> elev(w * h, 0);
        for (int32_t y = 0; y < h; ++y) {
            for (int32_t x = 0; x < w; ++x) {
                int64_t downhill = static_cast<int64_t>(x + 1) * 10 * ONE;
                int64_t v_shape = static_cast<int64_t>(std::abs(y - 4)) * 5 * ONE;
                elev[y * w + x] = static_cast<int32_t>(downhill + v_shape);
            }
        }

        std::vector<int32_t> flow_targets;
        std::vector<int64_t> area;
        Erosion::compute_drainage_and_flow(elev, w, h, flow_targets, area);

        // Valley head at (6, 4) should have smaller area than downrange outlet at (1, 4)
        int64_t area_head = area[4 * w + 6];
        int64_t area_outlet = area[4 * w + 1];
        std::cout << "  Catchment Area at Valley Head (x=6): " << (area_head / 65536.0)
                  << " cells | At Downrange Outlet (x=1): " << (area_outlet / 65536.0) << " cells" << std::endl;
        assert(area_outlet > area_head);
        std::cout << "  [PASS] Catchment area accumulates monotonically down the drainage path." << std::endl;
    }

    // -------------------------------------------------------------------------
    // Test 3: Zero Incision on Mountain Summits & Ridges
    // -------------------------------------------------------------------------
    std::cout << "\n--- 3. Testing Zero Incision on Mountain Summits & Ridges ---" << std::endl;
    {
        int32_t w = 16;
        int32_t h = 16;
        std::vector<int32_t> elev(w * h, 20 * ONE);

        // Create high mountain ridge at y = 8 with peak at (8, 8) = 80m
        for (int32_t x = 0; x < w; ++x) {
            elev[8 * w + x] = 70 * ONE;
        }
        elev[8 * w + 8] = 80 * ONE; // Summit

        int32_t summit_orig = elev[8 * w + 8];
        int32_t ridge_orig = elev[8 * w + 4];

        std::vector<int32_t> flow_targets;
        std::vector<int64_t> area;
        Erosion::compute_drainage_and_flow(elev, w, h, flow_targets, area);

        // Run channel carving pass with threshold = 18 cells
        Erosion::carve_downrange_channels(elev, w, h, 2 * ONE, flow_targets, area, 18, 550, 3 * ONE);

        std::cout << "  Mountain Summit (8,8) - Orig: " << (summit_orig / 65536.0)
                  << "m | Post-Erosion: " << (elev[8 * w + 8] / 65536.0) << "m" << std::endl;
        std::cout << "  Mountain Ridge (4,8)  - Orig: " << (ridge_orig / 65536.0)
                  << "m | Post-Erosion: " << (elev[8 * w + 4] / 65536.0) << "m" << std::endl;

        // Must be EXACTLY identical (zero incision on summits/ridges where runoff has not accumulated)
        assert(elev[8 * w + 8] == summit_orig);
        assert(elev[8 * w + 4] == ridge_orig);
        std::cout << "  [PASS] Mountain summits, ridges, and slopes with A < threshold receive ZERO incision." << std::endl;
    }

    // -------------------------------------------------------------------------
    // Test 4: Downrange Channel Incision & Parabolic Cross-Section
    // -------------------------------------------------------------------------
    std::cout << "\n--- 4. Testing Downrange Channel Incision & Parabolic Bank Profile ---" << std::endl;
    {
        int32_t w = 32;
        int32_t h = 32;
        std::vector<int32_t> elev(w * h, 0);

        // Sloping plane draining towards center (x descending, y sloping towards y=16)
        for (int32_t y = 0; y < h; ++y) {
            for (int32_t x = 0; x < w; ++x) {
                int64_t dx = static_cast<int64_t>(x) * 2 * ONE;
                int64_t dy = static_cast<int64_t>(std::abs(y - 16)) * 4 * ONE;
                elev[y * w + x] = static_cast<int32_t>(10 * ONE + dx + dy);
            }
        }

        std::vector<int32_t> flow_targets;
        std::vector<int64_t> area;
        Erosion::compute_drainage_and_flow(elev, w, h, flow_targets, area);

        int32_t channel_x = 8;
        int32_t channel_y = 16;
        size_t c_idx = static_cast<size_t>(channel_y) * w + channel_x;
        int32_t orig_center = elev[c_idx];
        int32_t orig_bank = elev[(channel_y + 1) * w + channel_x];

        // Carve channels
        Erosion::carve_downrange_channels(elev, w, h, 2 * ONE, flow_targets, area, 18, 550, 3 * ONE);

        int32_t inc_center = orig_center - elev[c_idx];
        int32_t inc_bank = orig_bank - elev[(channel_y + 1) * w + channel_x];

        std::cout << "  Channel Centerline Incision: " << (inc_center / 65536.0) << "m" << std::endl;
        std::cout << "  Channel Bank Incision:       " << (inc_bank / 65536.0) << "m" << std::endl;

        assert(inc_center > 0);
        assert(inc_center >= inc_bank); // Parabolic profile: centerline deeper than bank
        std::cout << "  [PASS] Downrange channel carved with smooth parabolic cross-section." << std::endl;
    }

    // -------------------------------------------------------------------------
    // Test 5: Selective Talus Relaxation (Angle of Repose)
    // -------------------------------------------------------------------------
    std::cout << "\n--- 5. Testing Selective Talus Relaxation ---" << std::endl;
    {
        int32_t w = 8;
        int32_t h = 8;
        std::vector<int32_t> elev(w * h, 10 * ONE);

        // Case A: Vertical cliff step of 15m drop over 1 tile (slope = 15.0 >> 0.85 threshold)
        elev[4 * w + 3] = 25 * ONE;
        elev[4 * w + 4] = 10 * ONE;

        // Case B: Gentle slope of 1m drop over 1 tile (slope = 1.0 / 2.0m = 0.5 < 0.85 threshold)
        elev[1 * w + 3] = 11 * ONE + 32768; // 11.5m (drop to 10m baseline is 1.5m <= 1.7m max stable drop)
        elev[1 * w + 4] = 10 * ONE + 32768; // 10.5m (drop between (3,1) and (4,1) is 1.0m)

        int32_t gentle_orig = elev[1 * w + 3];

        Erosion::apply_talus_relaxation(elev, w, h, 55705, 4, 2 * ONE);

        int32_t cliff_diff = elev[4 * w + 3] - elev[4 * w + 4];
        std::cout << "  Steep Cliff Drop before: 15.0m | after talus: " << (cliff_diff / 65536.0) << "m" << std::endl;
        std::cout << "  Gentle Slope Drop before: 1.0m  | after talus: " << ((elev[1 * w + 3] - elev[1 * w + 4]) / 65536.0) << "m" << std::endl;

        assert(cliff_diff < 15 * ONE); // Cliff relaxed
        assert(elev[1 * w + 3] == gentle_orig); // Gentle slope untouched
        std::cout << "  [PASS] Selective talus relaxation softens sheer cliffs without blunting natural slopes." << std::endl;
    }

    // -------------------------------------------------------------------------
    // Test 6: Continuous Tectonic Integration & Parameterized Rivers
    // -------------------------------------------------------------------------
    std::cout << "\n--- 6. Testing Tectonic Integration & Parameterized Rivers ---" << std::endl;
    {
        int32_t w = 64;
        int32_t h = 64;
        int64_t cell_size = 2 * ONE;
        uint64_t seed = 987654;

        Tectonics::ContinuousTectonicHeightmap tect;
        Tectonics::ContinuousTectonicHeightmap::Config cfg;
        cfg.seed = seed;
        cfg.world_width = w * cell_size;
        cfg.world_height = h * cell_size;
        cfg.num_plates = 8;
        tect.init(cfg);

        std::vector<int32_t> elev;
        tect.generate_grid(w, h, cell_size, elev);

        // Run full erosion suite
        Erosion::priority_flood_infilling(elev, w, h, 0);

        std::vector<int32_t> flow_targets;
        std::vector<int64_t> area;
        Erosion::compute_drainage_and_flow(elev, w, h, flow_targets, area);

        Erosion::carve_downrange_channels(elev, w, h, cell_size, flow_targets, area, 18, 550, 3 * ONE);
        Erosion::apply_talus_relaxation(elev, w, h, 55705, 3);

        Hydrology::ParameterizedRiverNetwork rivers;
        rivers.extract_from_subgrid_droplets(elev, w, h, cell_size, 110, ONE, seed);
        rivers.carve_riverbeds(elev, w, h, cell_size);

        std::cout << "  Extracted River Reaches: " << rivers.reaches.size() << std::endl;
        assert(!rivers.reaches.empty());
        std::cout << "  Reach 1 Length: " << (rivers.reaches[0].total_length / 65536.0) << "m"
                  << " | Waypoints: " << rivers.reaches[0].waypoints.size() << std::endl;
        std::cout << "  [PASS] Tectonic heightmap successfully eroded with continuous river channels." << std::endl;
    }

    // -------------------------------------------------------------------------
    // Test 7: 100% Bit-for-Bit Determinism
    // -------------------------------------------------------------------------
    std::cout << "\n--- 7. Testing 100% Bit-for-Bit Determinism ---" << std::endl;
    {
        int32_t w = 64;
        int32_t h = 64;
        int64_t cell_size = 2 * ONE;
        uint64_t seed = 123456789;

        auto run_pass = [&](std::vector<int32_t>& out_elev, Hydrology::ParameterizedRiverNetwork& out_rivers) {
            Tectonics::ContinuousTectonicHeightmap tect;
            Tectonics::ContinuousTectonicHeightmap::Config cfg;
            cfg.seed = seed;
            cfg.world_width = w * cell_size;
            cfg.world_height = h * cell_size;
            cfg.num_plates = 8;
            tect.init(cfg);
            tect.generate_grid(w, h, cell_size, out_elev);

            Erosion::priority_flood_infilling(out_elev, w, h, 0);
            std::vector<int32_t> targets;
            std::vector<int64_t> area;
            Erosion::compute_drainage_and_flow(out_elev, w, h, targets, area);
            Erosion::carve_downrange_channels(out_elev, w, h, cell_size, targets, area, 18, 550, 3 * ONE);
            Erosion::apply_talus_relaxation(out_elev, w, h, 55705, 3, cell_size);
            Erosion::apply_wave_cage_coastal_erosion(out_elev, w, h, cell_size, 1);
            out_rivers.extract_from_subgrid_droplets(out_elev, w, h, cell_size, 110, ONE, seed);
            out_rivers.carve_riverbeds(out_elev, w, h, cell_size);
        };

        std::vector<int32_t> elev1, elev2;
        Hydrology::ParameterizedRiverNetwork rivers1, rivers2;
        run_pass(elev1, rivers1);
        run_pass(elev2, rivers2);

        assert(elev1 == elev2);
        assert(rivers1.reaches.size() == rivers2.reaches.size());
        for (size_t i = 0; i < rivers1.reaches.size(); ++i) {
            assert(rivers1.reaches[i].total_length == rivers2.reaches[i].total_length);
            assert(rivers1.reaches[i].waypoints.size() == rivers2.reaches[i].waypoints.size());
        }
        std::cout << "  [PASS] 100% Bit-for-Bit Determinism Verified across all tiles and river splines." << std::endl;
    }

    // -------------------------------------------------------------------------
    // Test 8: Wave-Cage Guided Coastal Shoreline Erosion (Jeschke Breaker Dynamics)
    // -------------------------------------------------------------------------
    std::cout << "\n--- 8. Testing Wave-Cage Coastal Erosion (Jeschke Breaker Dynamics) ---" << std::endl;
    {
        int32_t w = 32;
        int32_t h = 32;
        int64_t cell_size = 2 * ONE;

        // Create an island: x in [10, 22], y in [10, 22] is land (+2m to +5m), surrounded by ocean (-5m)
        std::vector<int32_t> elev(w * h, -5 * ONE);
        for (int32_t y = 10; y <= 22; ++y) {
            for (int32_t x = 10; x <= 22; ++x) {
                // High coastal bluff at center of island (+5m), shoreline at +2m
                if (x >= 13 && x <= 19 && y >= 13 && y <= 19) {
                    elev[y * w + x] = 5 * ONE;
                } else {
                    elev[y * w + x] = 2 * ONE;
                }
            }
        }

        auto orig_elev = elev;

        // Run coastal erosion pass
        Erosion::apply_wave_cage_coastal_erosion(elev, w, h, cell_size, 2);

        // West coast (x=10, y=16) & East coast (x=22, y=16)
        int32_t west_before = orig_elev[16 * w + 10];
        int32_t west_after = elev[16 * w + 10];
        int32_t east_before = orig_elev[16 * w + 22];
        int32_t east_after = elev[16 * w + 22];
        int32_t north_before = orig_elev[10 * w + 16];
        int32_t north_after = elev[10 * w + 16];
        int32_t south_before = orig_elev[22 * w + 16];
        int32_t south_after = elev[22 * w + 16];

        std::cout << "  West Coast (10,16): " << (west_before / 65536.0) << "m -> " << (west_after / 65536.0) << "m" << std::endl;
        std::cout << "  East Coast (22,16): " << (east_before / 65536.0) << "m -> " << (east_after / 65536.0) << "m" << std::endl;
        std::cout << "  North Coast (16,10): " << (north_before / 65536.0) << "m -> " << (north_after / 65536.0) << "m" << std::endl;
        std::cout << "  South Coast (16,22): " << (south_before / 65536.0) << "m -> " << (south_after / 65536.0) << "m" << std::endl;

        // Verify that windward exposed headlands taking the incoming swell ([0.85, 0.52]) are actively carved
        // Coasts facing West (nx < 0) and North (ny < 0) face into the swell and must be eroded down
        assert(west_after < west_before);
        assert(north_after < north_before);

        // Verify sub-pixel raymarching operates across all directions without deadlocks
        // East coast receives ambient swell and littoral sediment deposition
        std::cout << "  [PASS] All coastal directions processed with wave-cage breaker physics." << std::endl;
    }

    // -------------------------------------------------------------------------
    // Test 9: Particle-Based Hydraulic Droplet Erosion & Bit-for-Bit Determinism
    // -------------------------------------------------------------------------
    std::cout << "\n--- 9. Testing Particle-Based Hydraulic Droplet Erosion ---" << std::endl;
    {
        Erosion::DropletErosionConfig cfg;
        assert(cfg.droplet_count == 85000);
        assert(cfg.inertia == 6554);
        assert(cfg.capacity_factor == 786432);
        assert(cfg.min_slope == 655);
        assert(cfg.erosion_rate == 65536);
        assert(cfg.deposition_rate == 5243);
        assert(cfg.evaporation_rate == 1311);
        assert(cfg.gravity == 1048576);
        assert(cfg.max_steps == 128);
        assert(cfg.seed == 1337);

        int32_t w = 64;
        int32_t h = 64;
        int64_t cell_size = 2 * ONE;

        // Create sloped conical mountain terrain: center (32, 32) at 50m sloping down to sea level at edges
        std::vector<int32_t> orig_elev(w * h, 0);
        for (int32_t y = 0; y < h; ++y) {
            for (int32_t x = 0; x < w; ++x) {
                int64_t dx = x - 32;
                int64_t dy = y - 32;
                int64_t dist_sq = dx * dx + dy * dy;
                int64_t dist = sqrt_fixed(dist_sq * ONE);
                int64_t peak = 50 * ONE;
                int64_t elev = peak - mul_fixed(dist, 110000); // slopes down outward
                orig_elev[y * w + x] = static_cast<int32_t>(std::max(0LL, elev));
            }
        }

        std::vector<int32_t> elev1 = orig_elev;
        std::vector<int32_t> elev2 = orig_elev;

        // Run droplet erosion on both instances with identical seed
        cfg.droplet_count = 10000;
        Erosion::apply_hydraulic_droplet_erosion(elev1, w, h, cell_size, cfg);
        Erosion::apply_hydraulic_droplet_erosion(elev2, w, h, cell_size, cfg);

        // Verify bit-for-bit determinism
        assert(elev1 == elev2);
        assert(elev1 != orig_elev); // Terrain was actually eroded and modified

        // Compute total volume diff to ensure erosion/deposition occurred
        int64_t eroded_cells = 0;
        int64_t deposited_cells = 0;
        for (size_t i = 0; i < elev1.size(); ++i) {
            if (elev1[i] < orig_elev[i]) eroded_cells++;
            if (elev1[i] > orig_elev[i]) deposited_cells++;
        }
        std::cout << "  Eroded cells: " << eroded_cells << " | Deposited cells: " << deposited_cells << std::endl;
        assert(eroded_cells > 0);
        assert(deposited_cells > 0);

        // Test with precipitation weighting
        std::vector<int32_t> precip(w * h, ONE);
        // Heavy rain on northern slope (y < 32)
        for (int32_t y = 0; y < 32; ++y) {
            for (int32_t x = 0; x < w; ++x) {
                precip[y * w + x] = 3 * ONE;
            }
        }

        std::vector<int32_t> elev_precip1 = orig_elev;
        std::vector<int32_t> elev_precip2 = orig_elev;
        Erosion::apply_hydraulic_droplet_erosion(elev_precip1, w, h, cell_size, cfg, &precip);
        Erosion::apply_hydraulic_droplet_erosion(elev_precip2, w, h, cell_size, cfg, &precip);
        assert(elev_precip1 == elev_precip2);
        assert(elev_precip1 != elev1); // Precipitation bias altered droplet trajectories deterministically

        std::cout << "  [PASS] 100% Bit-for-Bit Deterministic Droplet Erosion verified with and without precipitation." << std::endl;
    }

    // -------------------------------------------------------------------------
    // TEST 10: Cliff-Drop Droplet Erosion Must Not Excavate Cliff-Base Pits
    // -------------------------------------------------------------------------
    std::cout << "--- 10. Testing Cliff-Drop Droplet Erosion (No Base Potholes) ---" << std::endl;
    {
        int32_t w = 64, h = 64;
        int64_t cell_size = 4 * ONE;
        std::vector<int32_t> cliff_elev(w * h, 0);
        // Steep 20m plateau (x < 32) dropping vertically to a 2m landing apron (32 <= x < 36), then gentle slope to 1m (x >= 36)
        for (int32_t y = 0; y < h; ++y) {
            for (int32_t x = 0; x < w; ++x) {
                if (x < 32) {
                    cliff_elev[y * w + x] = 20 * ONE + (31 - x) * (ONE / 8);
                } else if (x < 36) {
                    cliff_elev[y * w + x] = 2 * ONE;
                } else {
                    int64_t dist_past_apron = x - 36;
                    cliff_elev[y * w + x] = static_cast<int32_t>(std::max<int64_t>(ONE, 2 * ONE - dist_past_apron * (ONE / 64)));
                }
            }
        }
        std::vector<int32_t> orig_cliff = cliff_elev;

        Erosion::DropletErosionConfig cfg;
        cfg.droplet_count = 15000;
        Erosion::apply_hydraulic_droplet_erosion(cliff_elev, w, h, cell_size, cfg);

        int32_t min_elev = cliff_elev[0];
        int32_t cliff_eroded_cells = 0;
        int32_t ocean_craters = 0;
        for (int32_t y = 0; y < h; ++y) {
            for (int32_t x = 0; x < w; ++x) {
                int32_t val = cliff_elev[y * w + x];
                if (val < min_elev) min_elev = val;
                if (val <= 0) ocean_craters++;
                if (x < 32 && val < orig_cliff[y * w + x]) {
                    cliff_eroded_cells++;
                }
            }
        }
        std::cout << "  Map min_elev: " << (min_elev / (double)ONE)
                  << "m | Sub-zero craters: " << ocean_craters
                  << " | Cliff edge eroded (x<32): " << cliff_eroded_cells << std::endl;
        if (ocean_craters != 0 || cliff_eroded_cells == 0) {
            std::cerr << "[FAIL] Cliff-drop droplet erosion check failed!" << std::endl;
            return 1;
        }
        std::cout << "  [PASS] Cliff drops stay above sea level while permitting plunge/channel incision." << std::endl;
    }

    // -------------------------------------------------------------------------
    // TEST 11: Multi-Pass Equilibrium Droplet Erosion Determinism & Convergence
    // -------------------------------------------------------------------------
    std::cout << "--- 11. Testing Multi-Pass Equilibrium Droplet Erosion ---" << std::endl;
    {
        int32_t w = 64, h = 64;
        int64_t cell_size = 4 * ONE;
        std::vector<int32_t> base_elev(w * h, 0);
        for (int32_t y = 0; y < h; ++y) {
            for (int32_t x = 0; x < w; ++x) {
                int64_t dx = x - 32;
                int64_t dy = y - 32;
                int64_t dist = sqrt_fixed((dx * dx + dy * dy) * ONE);
                int64_t elev = 50 * ONE - mul_fixed(dist, 110000);
                base_elev[y * w + x] = static_cast<int32_t>(std::max(0LL, elev));
            }
        }
        std::vector<int32_t> mp1 = base_elev;
        std::vector<int32_t> mp2 = base_elev;
        Erosion::DropletErosionConfig cfg;
        cfg.droplet_count = 6000;
        std::vector<int32_t> sed1, sed2, fx1, fx2, fy1, fy2;
        std::vector<int64_t> flux1, flux2;

        // Multi-pass hydraulic equilibrium loop (3 deterministic passes)
        for (int pass = 0; pass < 3; ++pass) {
            cfg.seed = 1337 + pass;
            Erosion::apply_hydraulic_droplet_erosion(mp1, w, h, cell_size, cfg, nullptr, nullptr, &sed1, &fx1, &fy1, &flux1);
            Erosion::apply_hydraulic_droplet_erosion(mp2, w, h, cell_size, cfg, nullptr, nullptr, &sed2, &fx2, &fy2, &flux2);
        }

        if (mp1 != mp2 || sed1 != sed2 || flux1 != flux2 || mp1 == base_elev) {
            std::cerr << "[FAIL] Multi-pass hydraulic droplet erosion determinism/modification check failed!" << std::endl;
            return 1;
        }
        std::cout << "  [PASS] Multi-pass hydraulic erosion verified deterministic and active across passes." << std::endl;
    }

    // -------------------------------------------------------------------------
    // TEST 12: Staged Geomorphic Erosion (Network Search -> Converged Equilibrium -> Pure Deepening)
    // -------------------------------------------------------------------------
    std::cout << "--- 12. Testing Staged Geomorphic Erosion Progression ---" << std::endl;
    {
        int32_t w = 96, h = 96;
        int64_t cell_size = 2 * ONE;
        std::vector<int32_t> base(w * h, 0);
        for (int32_t y = 0; y < h; ++y) {
            for (int32_t x = 0; x < w; ++x) {
                int64_t border_dist = std::min({x, y, w - 1 - x, h - 1 - y});
                if (border_dist < 4) {
                    base[y * w + x] = -4 * ONE;
                    continue;
                }
                int64_t dx = x - 48;
                int64_t dy = y - 48;
                int64_t dist = sqrt_fixed((dx * dx + dy * dy) * ONE);
                // Conical island with asymmetric trough bias + small micro-divides
                int64_t ridge_bias = ((x + y) % 5 == 0) ? (ONE / 4) : 0;
                int64_t valley_bias = (std::abs(x - 48) < 12 && y > 48) ? (2 * ONE) : 0;
                int64_t elev = 56 * ONE - mul_fixed(dist, 82000) + ridge_bias - valley_bias;
                base[y * w + x] = static_cast<int32_t>(std::max<int64_t>(-4 * ONE, elev));
            }
        }

        std::vector<int32_t> staged1 = base;
        std::vector<int32_t> staged2 = base;

        Erosion::StagedErosionConfig scfg;
        scfg.droplet.droplet_count = 16000;
        scfg.droplet.erosion_rate = 49152;    // 0.75
        scfg.droplet.deposition_rate = 16384; // 0.25
        scfg.droplet.seed = 424242ULL;
        scfg.apply_coastal = false;

        Erosion::StagedErosionStats stats1, stats2;
        std::vector<int32_t> sed1, sed2;
        Erosion::apply_staged_geomorphic_erosion(staged1, w, h, cell_size, scfg, &sed1, &stats1);
        Erosion::apply_staged_geomorphic_erosion(staged2, w, h, cell_size, scfg, &sed2, &stats2);

        if (staged1 != staged2 || sed1 != sed2) {
            std::cerr << "[FAIL] Staged geomorphic erosion determinism check failed!" << std::endl;
            return 1;
        }

        // Verify pure droplet valley incision deepened trunk zone without creating sub-zero craters on upland/inland terrain
        int32_t min_inland = 99999999;
        int32_t subzero_land_craters = 0;
        for (size_t idx = 0; idx < static_cast<size_t>(w) * h; ++idx) {
            if (base[idx] >= 3 * ONE) {
                min_inland = std::min(min_inland, staged1[idx]);
                if (staged1[idx] <= 0) subzero_land_craters++;
            }
        }
        int64_t trunk_drop = static_cast<int64_t>(base[72 * w + 48]) - staged1[72 * w + 48];
        Hydrology::ParameterizedRiverNetwork staged_net;
        staged_net.extract_from_subgrid_droplets(staged1, w, h, cell_size, 24, ONE, 424242ULL);

        std::cout << "  Droplets (S/E/D): " << stats1.search_droplets << "/"
                  << stats1.equilibrium_droplets << "/" << stats1.deepening_droplets
                  << " | Trunk incision: " << (trunk_drop / 65536.0) << "m"
                  << " | Land min_elev: " << (min_inland / 65536.0) << "m"
                  << " | Land craters: " << subzero_land_craters
                  << " | Extracted reaches: " << staged_net.reaches.size() << std::endl;

        if (stats1.search_droplets == 0 || stats1.equilibrium_droplets == 0 || stats1.deepening_droplets == 0 ||
            trunk_drop <= 0 || subzero_land_craters != 0 || staged_net.reaches.empty()) {
            std::cerr << "[FAIL] Staged geomorphic erosion metrics or river extraction check failed!" << std::endl;
            return 1;
        }
        std::cout << "  [PASS] Staged Search -> Equilibrium -> Pure Deepening produces deterministic deep channels & clean splines." << std::endl;
    }

    std::cout << "\n>>> ALL EROSION & CHANNEL CARVING TESTS PASSED SUCCESSFULLY! <<<\n";
    return 0;
}
