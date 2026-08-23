#include <iostream>
#include <vector>
#include <cassert>
#include <cmath>

#include "src/core/hydrology_graph.h"
#include "src/core/world.h"
#include "src/math/rng.h"
#include "src/core/input.h"

#include "src/core/world.cpp"
#include "src/core/item.cpp"
#include "src/core/collision.cpp"
#include "src/math/cordic.cpp"
#include "src/core/map_generator.cpp"
#include "src/entities/player.cpp"
#include "src/entities/enemy.cpp"
#include "src/entities/wall.cpp"

void test_direct_hydrology_graph() {
    std::cout << "--- Testing HydraulicGraph Direct Pipe & Reservoir Model ---" << std::endl;
    Hydrology::HydraulicGraph graph;
    graph.set_dimensions(100, 100);

    // 1. Create Lake A (Upstream Reservoir, 10 tiles, 50,000 capacity)
    std::vector<int32_t> lake_a_tiles = {10, 11, 12, 13, 14};
    uint32_t lake_a = graph.add_reservoir(50000LL * ONE, 50000LL * ONE, false, lake_a_tiles);
    assert(lake_a == 1);

    // 2. Create Lake B (Downstream Reservoir, 10 tiles, 50,000 capacity)
    std::vector<int32_t> lake_b_tiles = {50, 51, 52, 53, 54};
    uint32_t lake_b = graph.add_reservoir(10000LL * ONE, 50000LL * ONE, false, lake_b_tiles);
    assert(lake_b == 2);

    // 3. Create River Pipe 1 (connecting Lake A -> Lake B, 8 tiles)
    std::vector<int32_t> river_1_tiles = {20, 21, 22, 23, 24, 25, 26, 27};
    int64_t max_rate_1 = (int64_t)river_1_tiles.size() * Hydrology::FLOW_CAPACITY_PER_TILE; // 8 * 10.0 = 80.0
    uint32_t river_1 = graph.add_river_belt(lake_a, lake_b, max_rate_1, river_1_tiles);
    assert(river_1 == 1);

    const auto* r1 = graph.get_river_belt(river_1);
    assert(r1 != nullptr);
    assert(r1->tile_count == 8);
    assert(r1->max_rate == 80LL * ONE);
    assert(r1->flow_rate == 80LL * ONE);
    assert(r1->water_level == 100LL * ONE);
    assert(!r1->is_dry);
    std::cout << "  [PASS] Initial River Pipe measured from 8 tiles: max_rate = " << (r1->max_rate / ONE) << ".0 units/tick, water level = 100%" << std::endl;

    // 4. Parasitize River 1 with Pump (rate = 20.0 units/tick, e.g. at tile 22)
    int64_t pump_rate = 20LL * ONE;
    graph.add_pump(22, pump_rate);

    r1 = graph.get_river_belt(river_1);
    assert(r1->pump_rate == 20LL * ONE);
    assert(r1->flow_rate == 60LL * ONE); // 80 - 20 = 60
    assert(r1->water_level == 75LL * ONE); // (60 / 80) * 100 = 75%
    assert(!r1->is_dry);
    std::cout << "  [PASS] Pump parasitizes River 1: pump_rate = 20.0, effective flow = " << (r1->flow_rate / ONE) << ".0, water level = " << (r1->water_level / ONE) << "%" << std::endl;

    // Verify Upstream Lake A is NOT parasitized (extraction_rate == 0)
    const auto* res_a = graph.get_reservoir(lake_a);
    assert(res_a->extraction_rate == 0);
    std::cout << "  [PASS] Upstream Reservoir (Lake A) extraction rate = 0 (unaffected by river pump)" << std::endl;

    // 5. Test Over-Pumping (increase pump rate to 80.0 or 100.0, exceeding max_rate of 80.0)
    graph.add_pump(23, 70LL * ONE); // Total pump = 20 + 70 = 90.0 > 80.0

    r1 = graph.get_river_belt(river_1);
    assert(r1->pump_rate == 90LL * ONE);
    assert(r1->flow_rate == 0);
    assert(r1->water_level == 0);
    assert(r1->is_dry == true);
    std::cout << "  [PASS] Over-pumping River 1 (pump 90.0 >= max 80.0): flow = 0, water level = 0%, is_dry = true" << std::endl;

    // 6. Test tick_hydrology under over-pumping: Lake B receives 0 inflow from River 1
    int64_t initial_vol_b = graph.get_reservoir(lake_b)->current_volume;
    graph.tick_hydrology(1, 1, 0); // dt=1, RAIN weather, rain_rate=0
    int64_t new_vol_b = graph.get_reservoir(lake_b)->current_volume;
    assert(new_vol_b == initial_vol_b); // No flow transferred because River 1 is dry
    std::cout << "  [PASS] Downstream Lake B receives 0 inflow when upstream river is over-pumped dry" << std::endl;

    // 7. Remove pumps and verify recovery
    graph.remove_pump(22);
    graph.remove_pump(23);
    r1 = graph.get_river_belt(river_1);
    assert(r1->pump_rate == 0);
    assert(r1->flow_rate == 80LL * ONE);
    assert(r1->water_level == 100LL * ONE);
    assert(!r1->is_dry);
    std::cout << "  [PASS] Removing pumps restores River 1 to 100% water level and full flow" << std::endl;
}

void test_cascading_river_pipes() {
    std::cout << "\n--- Testing Cascading Multi-Segment River Pipes ---" << std::endl;
    Hydrology::HydraulicGraph graph;
    graph.set_dimensions(100, 100);

    // 1. Lake A (Source, 20 tiles, 100,000 cap)
    std::vector<int32_t> lake_a_tiles = {1, 2, 3, 4, 5};
    uint32_t lake_a = graph.add_reservoir(100000LL * ONE, 100000LL * ONE, false, lake_a_tiles);

    // 2. Lake B (Sink, 20 tiles, 100,000 cap)
    std::vector<int32_t> lake_b_tiles = {90, 91, 92, 93, 94};
    uint32_t lake_b = graph.add_reservoir(10000LL * ONE, 100000LL * ONE, false, lake_b_tiles);

    // 3. Pipe 1 (Upstream Segment: Lake A -> Pipe 2, 10 tiles, max_rate = 100.0)
    std::vector<int32_t> pipe_1_tiles = {10, 11, 12, 13, 14, 15, 16, 17, 18, 19};
    uint32_t pipe_1 = graph.add_river_belt(lake_a, 0, 0, pipe_1_tiles, 0, 2); // down_belt = 2

    // 4. Pipe 2 (Downstream Segment: Pipe 1 -> Lake B, 6 tiles, max_rate = 60.0)
    std::vector<int32_t> pipe_2_tiles = {20, 21, 22, 23, 24, 25};
    uint32_t pipe_2 = graph.add_river_belt(0, lake_b, 0, pipe_2_tiles, pipe_1, 0); // up_belt = pipe_1

    assert(pipe_1 == 1);
    assert(pipe_2 == 2);

    const auto* p1 = graph.get_river_belt(pipe_1);
    const auto* p2 = graph.get_river_belt(pipe_2);
    assert(p1->max_rate == 100LL * ONE);
    assert(p2->max_rate == 60LL * ONE);
    assert(p1->water_level == 100LL * ONE);
    assert(p2->water_level == 100LL * ONE);
    std::cout << "  [PASS] Setup Cascade: Lake A -> Pipe 1 (100.0 rate) -> Pipe 2 (60.0 rate) -> Lake B" << std::endl;

    // Normal flow transfer
    int64_t initial_vol_b = graph.get_reservoir(lake_b)->current_volume;
    graph.tick_hydrology(1, 1, 0);
    int64_t vol_after_normal = graph.get_reservoir(lake_b)->current_volume;
    assert(vol_after_normal > initial_vol_b);
    std::cout << "  [PASS] Normal cascade transferred " << ((vol_after_normal - initial_vol_b) / ONE) << ".0 units to Lake B" << std::endl;

    // Over-pump Upstream Pipe 1 (place pump of 120.0 > 100.0)
    graph.add_pump(12, 120LL * ONE);
    p1 = graph.get_river_belt(pipe_1);
    assert(p1->flow_rate == 0);
    assert(p1->water_level == 0);
    assert(p1->is_dry == true);
    std::cout << "  [PASS] Upstream Pipe 1 over-pumped (pump 120.0 >= max 100.0): flow = 0, water level = 0%" << std::endl;

    // Verify Downstream Starvation: Lake B volume does NOT increase on tick
    int64_t vol_before_starve = graph.get_reservoir(lake_b)->current_volume;
    graph.tick_hydrology(1, 1, 0);
    int64_t vol_after_starve = graph.get_reservoir(lake_b)->current_volume;
    assert(vol_after_starve == vol_before_starve);
    std::cout << "  [PASS] Downstream Pipe 2 and Lake B received 0 inflow due to upstream starvation" << std::endl;

    // Upstream Lake A extraction is still 0
    assert(graph.get_reservoir(lake_a)->extraction_rate == 0);
    std::cout << "  [PASS] Upstream Lake A extraction remains 0 (unaffected by river pump)" << std::endl;

    // Remove pump and verify full recovery
    graph.remove_pump(12);
    p1 = graph.get_river_belt(pipe_1);
    assert(p1->pump_rate == 0);
    assert(p1->flow_rate == 100LL * ONE);
    assert(p1->water_level == 100LL * ONE);
    assert(!p1->is_dry);

    graph.tick_hydrology(1, 1, 0);
    assert(graph.get_reservoir(lake_b)->current_volume > vol_after_starve);
    std::cout << "  [PASS] Removing pump restores Pipe 1 to 100% and resumes downstream flow to Lake B" << std::endl;
}

void test_simworld_hydrology_integration() {
    std::cout << "\n--- Testing SimWorld Hydrology Integration ---" << std::endl;
    SimWorld world;
    world.set_seed(42);
    int64_t cell_size = 64 * 65536;
    world.generate_player_map(42, 64, 64, cell_size);

    const auto& graph = world.get_hydraulic_graph();
    std::cout << "  World generated: " << graph.get_reservoirs().size() << " reservoirs, " << graph.get_river_belts().size() << " river pipes" << std::endl;
    assert(!graph.get_reservoirs().empty());

    if (!graph.get_river_belts().empty()) {
        const auto& b0 = graph.get_river_belts()[0];
        std::cout << "  River Pipe 0: " << b0.tile_count << " tiles, max_rate = " << (b0.max_rate / ONE) << ".0, water_level = " << (b0.water_level / ONE) << "%" << std::endl;
        assert(b0.tile_count > 0);
        assert(b0.max_rate > 0);
        assert(b0.water_level == 100LL * ONE);

        // Place pump on river tile
        int32_t riv_tile = b0.tile_indices[0];
        int32_t rx = riv_tile % 64;
        int32_t ry = riv_tile / 64;
        world.place_structure(rx, ry, 2); // PUMP

        const auto& b0_after = world.get_hydraulic_graph().get_river_belts()[0];
        std::cout << "  After placing pump on river: pump_rate = " << (b0_after.pump_rate / (float)ONE) << ", water_level = " << (b0_after.water_level / (float)ONE) << "%" << std::endl;
        assert(b0_after.pump_rate > 0);

        world.remove_structure(rx, ry);
        const auto& b0_restored = world.get_hydraulic_graph().get_river_belts()[0];
        assert(b0_restored.pump_rate == 0);
        assert(b0_restored.water_level == 100LL * ONE);
        std::cout << "  [PASS] SimWorld pump placement & removal verified on river pipe" << std::endl;
    }

    std::cout << "--- All Hydrology Unit Tests Passed Successfully! ---" << std::endl;
}

void test_map_gen_river_topology() {
    std::cout << "\n--- Testing Map Generation River Topology (Loops, Dead-ends, Confluences) ---" << std::endl;
    int total_seeds = 200;
    int num_loops_found = 0;
    int num_deadends_found = 0;
    int num_tiny_brooks = 0;
    int num_immediate_confluences = 0;

    int64_t cell_size = 64 * 65536;

    for (int seed = 1; seed <= total_seeds; ++seed) {
        SimWorld world;
        world.set_seed(seed);
        world.generate_player_map(seed, 256, 144, cell_size);

        int32_t w = world.get_map_width();
        int32_t h = world.get_map_height();
        const auto& elevation = world.get_elevation();
        const auto& rivers = world.get_rivers();
        const auto& lakes = world.get_lakes();
        const auto& flow_targets = world.get_flow_targets();

        int loop_count_seed = 0;
        int deadend_count_seed = 0;

        // 1. Check for cycles in flow_targets and rivers
        for (int32_t i = 0; i < w * h; ++i) {
            if (rivers[i] > 0 && lakes[i] == 0 && elevation[i] > 0) {
                // Trace from i
                std::vector<int32_t> visited;
                int32_t curr = i;
                bool cycle = false;
                bool reached_sink = false;

                while (curr >= 0 && curr < w * h) {
                    if (std::find(visited.begin(), visited.end(), curr) != visited.end()) {
                        cycle = true;
                        break;
                    }
                    visited.push_back(curr);
                    if (elevation[curr] <= 0 || lakes[curr] > 0) {
                        reached_sink = true;
                        break;
                    }
                    if (flow_targets.empty()) break;
                    int32_t nxt = flow_targets[curr];
                    if (nxt == curr || nxt < 0 || nxt >= w * h) {
                        break;
                    }
                    curr = nxt;
                }

                if (cycle) {
                    loop_count_seed++;
                } else if (!reached_sink) {
                    deadend_count_seed++;
                    if (seed == 1 && deadend_count_seed == 1) {
                        std::cout << "  DEBUG Seed 1 Deadend: start_tile=" << i 
                                  << " (x=" << (i % w) << ", y=" << (i / w) << ", el=" << (elevation[i] / (float)ONE) << ")"
                                  << " last_tile=" << curr << " (x=" << (curr % w) << ", y=" << (curr / w) << ", el=" << (elevation[curr] / (float)ONE) << ")"
                                  << " nxt=" << (flow_targets.empty() ? -2 : flow_targets[curr])
                                  << " path_length=" << visited.size() << std::endl;
                    }
                }
            }
        }

        if (loop_count_seed > 0) {
            num_loops_found++;
            std::cout << "  Seed " << seed << ": FOUND " << loop_count_seed << " river tiles in loops!" << std::endl;
        }
        if (deadend_count_seed > 0) {
            num_deadends_found++;
            std::cout << "  Seed " << seed << ": FOUND " << deadend_count_seed << " river tiles in dead ends!" << std::endl;
        }

        // Check river reaches in hydraulic graph
        const auto& graph = world.get_hydraulic_graph();
        const auto& belts = graph.get_river_belts();
        for (const auto& b : belts) {
            if (b.tier == 1 && b.tile_count < 4) {
                num_tiny_brooks++;
            }
            if (b.tier == 1 && !b.upstream_belt_ids.empty()) {
                num_immediate_confluences++;
            }
        }
    }

    std::cout << "Summary across " << total_seeds << " seeds:" << std::endl;
    std::cout << "  Seeds with loops: " << num_loops_found << " / " << total_seeds << std::endl;
    std::cout << "  Seeds with dead-ends: " << num_deadends_found << " / " << total_seeds << std::endl;
    std::cout << "  Tiny brooks (<4 tiles): " << num_tiny_brooks << std::endl;
    std::cout << "  Brooks with upstream tributaries: " << num_immediate_confluences << std::endl;
}

int main() {
    test_direct_hydrology_graph();
    test_cascading_river_pipes();
    return 0;
}

