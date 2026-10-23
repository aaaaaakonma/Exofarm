#include <iostream>
#include <vector>
#include <cassert>
#include <iomanip>
#include <string>
#include <chrono>
#include <cstring>
#include <numeric>

// Include core headers
#include "src/core/world.h"
#include "src/math/rng.h"
#include "src/core/input.h"

// For simplicity in this standalone tool, we include the implementations.
#include "src/core/world.cpp"
#include "src/core/item.cpp"
#include "src/entities/player.cpp"

template<typename T>
bool compare_vec(const std::string& name, const std::vector<T>& a, const std::vector<T>& b, const std::vector<EntityID>& ids) {
    if (a.size() != b.size()) {
        std::cout << "  [DIVERGENCE] " << name << " size mismatch: " << a.size() << " vs " << b.size() << std::endl;
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (!(a[i] == b[i])) {
            EntityID id = (i < ids.size()) ? ids[i] : 0;
            std::cout << "  [DIVERGENCE] " << name << "[" << i << "] mismatch (EntityID: " << id << ")" << std::endl;
            if constexpr (std::is_arithmetic_v<T>) {
                std::cout << "    Expected: " << (int64_t)a[i] << ", Got: " << (int64_t)b[i] << std::endl;
            }
            return false;
        }
    }
    return true;
}

void compare_snapshots(const SimWorld& expected_world, const SimWorld& actual_world) {
    const BisectSnapshot expected = expected_world.snapshot();
    const BisectSnapshot actual = actual_world.snapshot();

    std::cout << "\n--- Deep State Divergence Analysis ---" << std::endl;
    std::cout << "Expected Tick: " << expected.tick << " | Actual Tick: " << actual.tick << std::endl;
    
    if (expected.tick != actual.tick) std::cout << "  [DIVERGENCE] Tick mismatch: " << expected.tick << " vs " << actual.tick << std::endl;
    if (expected.next_id != actual.next_id) std::cout << "  [DIVERGENCE] next_id mismatch: " << expected.next_id << " vs " << actual.next_id << std::endl;
    if (expected.shotgun_seed != actual.shotgun_seed) std::cout << "  [DIVERGENCE] shotgun_seed mismatch: " << expected.shotgun_seed << " vs " << actual.shotgun_seed << std::endl;
    if (expected.tornado_seed != actual.tornado_seed) std::cout << "  [DIVERGENCE] tornado_seed mismatch: " << expected.tornado_seed << " vs " << actual.tornado_seed << std::endl;
    if (expected.crit_seed != actual.crit_seed) std::cout << "  [DIVERGENCE] crit_seed mismatch: " << expected.crit_seed << " vs " << actual.crit_seed << std::endl;

    const ChaosState& ec = expected.chaos;
    const ChaosState& ac = actual.chaos;
    
    if (ec.size() != ac.size()) {
        std::cout << "  [DIVERGENCE] Entity count mismatch: " << ec.size() << " vs " << ac.size() << std::endl;
    }

    compare_vec("dense_ids", ec.dense_ids, ac.dense_ids, ec.dense_ids);
    compare_vec("positions_x", ec.positions_x, ac.positions_x, ec.dense_ids);
    compare_vec("positions_y", ec.positions_y, ac.positions_y, ec.dense_ids);
    compare_vec("velocities_x", ec.velocities_x, ac.velocities_x, ec.dense_ids);
    compare_vec("velocities_y", ec.velocities_y, ac.velocities_y, ec.dense_ids);
    compare_vec("radii", ec.radii, ac.radii, ec.dense_ids);
    compare_vec("frictions", ec.frictions, ac.frictions, ec.dense_ids);
    compare_vec("healths", ec.healths, ac.healths, ec.dense_ids);
    compare_vec("pierces", ec.pierces, ac.pierces, ec.dense_ids);
    compare_vec("tags", ec.tags, ac.tags, ec.dense_ids);
    compare_vec("flags", ec.flags, ac.flags, ec.dense_ids);
    compare_vec("tree_min_x", ec.tree_min_x, ac.tree_min_x, ec.dense_ids);
    compare_vec("tree_min_y", ec.tree_min_y, ac.tree_min_y, ec.dense_ids);
    compare_vec("tree_max_x", ec.tree_max_x, ac.tree_max_x, ec.dense_ids);
    compare_vec("tree_max_y", ec.tree_max_y, ac.tree_max_y, ec.dense_ids);

    if (ec.types.size() != ac.types.size()) {
        std::cout << "  [DIVERGENCE] types size mismatch" << std::endl;
    } else {
        for (size_t i = 0; i < ec.types.size(); ++i) {
            if (ec.types[i] != ac.types[i]) {
                EntityID id = (i < ec.dense_ids.size()) ? ec.dense_ids[i] : 0;
                std::cout << "  [DIVERGENCE] types[" << i << "] mismatch (EntityID: " << id << "): " << (int)ec.types[i] << " vs " << (int)ac.types[i] << std::endl;
                break;
            }
        }
    }

    auto& eh = expected.heavy_stats;
    auto& ah = actual.heavy_stats;

    if (eh.size() != ah.size()) {
        std::cout << "  [DIVERGENCE] heavy_stats size mismatch: " << eh.size() << " vs " << ah.size() << std::endl;
    } else {
        for (size_t i = 0; i < eh.size(); ++i) {
            if (memcmp(&eh[i], &ah[i], sizeof(EntityStats)) != 0) {
                EntityID id = (i < ec.dense_ids.size()) ? ec.dense_ids[i] : 0;
                std::cout << "  [DIVERGENCE] heavy_stats[" << i << "] mismatch (EntityID: " << id << ")" << std::endl;
                
                const auto& es = eh[i];
                const auto& as = ah[i];
                if (es.base_hp != as.base_hp) std::cout << "    base_hp: " << es.base_hp << " vs " << as.base_hp << std::endl;
                if (es.base_damage != as.base_damage) std::cout << "    base_damage: " << es.base_damage << " vs " << as.base_damage << std::endl;
                if (es.base_speed != as.base_speed) std::cout << "    base_speed: " << es.base_speed << " vs " << as.base_speed << std::endl;
                break;
            }
        }
    }

    std::vector<EntityID> empty_ids;
    compare_vec("hit_pairs", expected.hit_pairs, actual.hit_pairs, empty_ids);
    compare_vec("entity_recycle_bin", expected.entity_recycle_bin, actual.entity_recycle_bin, empty_ids);
    compare_vec("attack_recycle_bin", expected.attack_recycle_bin, actual.attack_recycle_bin, empty_ids);
    compare_vec("soil_nitrogen", expected.soil_nitrogen, actual.soil_nitrogen, empty_ids);
    compare_vec("soil_phosphorus", expected.soil_phosphorus, actual.soil_phosphorus, empty_ids);
    compare_vec("soil_potassium", expected.soil_potassium, actual.soil_potassium, empty_ids);
    compare_vec("soil_calcium", expected.soil_calcium, actual.soil_calcium, empty_ids);
    compare_vec("soil_magnesium", expected.soil_magnesium, actual.soil_magnesium, empty_ids);
    compare_vec("soil_sulfur", expected.soil_sulfur, actual.soil_sulfur, empty_ids);
    compare_vec("soil_compactness", expected.soil_compactness, actual.soil_compactness, empty_ids);
    compare_vec("crop_type", expected.crop_type, actual.crop_type, empty_ids);
    compare_vec("crop_growth", expected.crop_growth, actual.crop_growth, empty_ids);
    compare_vec("crop_health", expected.crop_health, actual.crop_health, empty_ids);
    compare_vec("crop_prefix", expected.crop_prefix, actual.crop_prefix, empty_ids);
    compare_vec("crop_suffix", expected.crop_suffix, actual.crop_suffix, empty_ids);
    compare_vec("crop_implicit", expected.crop_implicit, actual.crop_implicit, empty_ids);
    compare_vec("lakes", expected.lakes, actual.lakes, empty_ids);
    compare_vec("structure_type", expected.structure_type, actual.structure_type, empty_ids);
    compare_vec("water_pressure", expected.water_pressure, actual.water_pressure, empty_ids);
    compare_vec("bot_states", expected.bot_states, actual.bot_states, empty_ids);
    compare_vec("actuator_activated", expected.actuator_activated, actual.actuator_activated, empty_ids);
    compare_vec("planted_tile_indices", expected.planted_tile_indices, actual.planted_tile_indices, empty_ids);
    compare_vec("elevation", expected.elevation, actual.elevation, empty_ids);
    compare_vec("biome", expected.biome, actual.biome, empty_ids);
    compare_vec("surface_type", expected.surface_type, actual.surface_type, empty_ids);
    compare_vec("mineral_type", expected.mineral_type, actual.mineral_type, empty_ids);
    compare_vec("hydraulic_reservoirs", expected.hydraulic_reservoirs, actual.hydraulic_reservoirs, empty_ids);
    compare_vec("hydraulic_river_belts", expected.hydraulic_river_belts, actual.hydraulic_river_belts, empty_ids);
    if (expected.starter_spawn_x != actual.starter_spawn_x) std::cout << "  [DIVERGENCE] starter_spawn_x mismatch" << std::endl;
    if (expected.starter_spawn_y != actual.starter_spawn_y) std::cout << "  [DIVERGENCE] starter_spawn_y mismatch" << std::endl;
    if (expected.starter_pond_volume != actual.starter_pond_volume) std::cout << "  [DIVERGENCE] starter_pond_volume mismatch" << std::endl;
    std::cout << "--------------------------------------\n" << std::endl;
}

void run_test(uint64_t seed) {
    std::cout << "--- Determinism Tick-by-Tick Lockstep Test ---" << std::endl;
    std::cout << "Seed: " << seed << std::endl;
    
    SplitMix64 rng(seed);
    
    SimWorld world_gen;
    world_gen.set_seed(seed);

    int64_t cell_size = 64 * 65536; // 64.0 float units
    world_gen.generate_player_map(seed, 64, 64, cell_size);
    
    // Pre-allocate
    for(int i=0; i<1000; i++) world_gen.spawn_entity(EntityType::ENEMY, 0, 0, 0, 0);
    world_gen.clear_entities();
    
    EntityID player_id = world_gen.spawn_entity(EntityType::PLAYER, 32 * cell_size, 32 * cell_size, 32 << 16, 100);
    
    for (int i = 0; i < 200; ++i) {
        world_gen.spawn_entity(EntityType::ENEMY, 
            rng.next_range(5, 59) * cell_size, 
            rng.next_range(5, 59) * cell_size, 
            32 << 16, 50);
    }

    // Plant some crops to test field growth determinism
    for (int i = 0; i < 50; ++i) {
        int32_t cx = static_cast<int32_t>(rng.next_range(10, 50));
        int32_t cy = static_cast<int32_t>(rng.next_range(10, 50));
        uint8_t type = static_cast<uint8_t>(rng.next_range(1, 3));
        uint8_t prefix = static_cast<uint8_t>(rng.next_range(0, 5));
        uint8_t suffix = static_cast<uint8_t>(rng.next_range(0, 5));
        uint8_t implicit = static_cast<uint8_t>(rng.next_range(0, 4));
        world_gen.plant_crop(cx, cy, type, prefix, suffix, implicit);
    }

    // Place structures and spawn/program bots to test new features
    world_gen.place_structure(20, 20, 1); // WELL
    world_gen.place_structure(21, 20, 4); // SPRINKLER_SMALL
    world_gen.place_structure(22, 20, 2); // PUMP
    world_gen.place_structure(25, 20, 6); // BRIDGE

    EntityID b1 = world_gen.spawn_bot(0, 20 * cell_size, 20 * cell_size); // Humanoid Bot
    world_gen.set_bot_job(b1, 1, 22, 22, 1); // Job: plant at (22, 22) seed type 1

    EntityID b2 = world_gen.spawn_bot(1, 30 * cell_size, 30 * cell_size); // Drone Bot
    world_gen.set_bot_job(b2, 2, 22, 22, 0); // Job: harvest at (22, 22)

    EntityID b3 = world_gen.spawn_bot(3, 25 * cell_size, 25 * cell_size); // IoT Node Bot
    world_gen.wire_bot_ports(b3, 20, 20, 21, 20); // sensor=(20,20), actuator=(21,20)
    std::vector<uint16_t> prog = {
        0x0200, // READ_MOISTURE R0
        0x0500 | 20, // CMP R0, 20
        0x0700 | 6, // JLT 6 (PC 6 is label: turn on sprinkler)
        0x0140 | 0, // LOAD R1, 0
        0x0480 | 1 << 5, // WRITE_ACTUATOR R1 (R1=0, turn off)
        0x0600 | 8, // JMP 8 (exit)
        0x0140 | 1, // LOAD R1, 1
        0x0480 | 1 << 5, // WRITE_ACTUATOR R1 (R1=1, turn on)
        0x0000  // HALT
    };
    world_gen.upload_bot_program(b3, prog);

    // Force snapshot capture
    BisectSnapshot initial_snap = world_gen.snapshot();
    uint64_t initial_hash = world_gen.get_state_hash();

    int test_ticks = 500;
    std::vector<InputFrame> full_tape;
    std::vector<uint64_t> expected_hashes;
    
    std::cout << "Generating " << test_ticks << " ticks of reference execution..." << std::endl;
    auto gen_start = std::chrono::high_resolution_clock::now();

    for (int t = 0; t < test_ticks; ++t) {
        std::cout << "Tick " << t << "..." << std::endl; std::flush(std::cout);
        
        if (rng.next() % 50 == 0) {
            uint64_t item_id = rng.next();
            int slot = rng.next_range(0, 2);
            
            InputFrame equip_f;
            equip_f.kind = InputFrame::Kind::EquipItem;
            equip_f.payload.event.event_entity = player_id;
            equip_f.payload.event.event_slot = slot;
            equip_f.payload.event.event_item_id = item_id;
            equip_f.tick = world_gen.current_tick();
            
            world_gen.tick(equip_f);
            full_tape.push_back(equip_f);
            expected_hashes.push_back(world_gen.get_state_hash());
        }


        InputFrame input;
        input.kind = InputFrame::Kind::PlayerInput;
        input.payload.player.move_x = rng.next_range(-65536, 65536);
        input.payload.player.move_y = rng.next_range(-65536, 65536);
        input.payload.player.aim_x = (int64_t)rng.next_range(-1000, 1000) << 16;
        input.payload.player.aim_y = (int64_t)rng.next_range(-1000, 1000) << 16;
        input.tick = world_gen.current_tick();

        world_gen.tick(input);
        full_tape.push_back(input);
        expected_hashes.push_back(world_gen.get_state_hash());
    }
    
    auto gen_end = std::chrono::high_resolution_clock::now();
    double gen_ms = std::chrono::duration_cast<std::chrono::microseconds>(gen_end - gen_start).count() / 1000.0;
    std::cout << "Generation completed in " << gen_ms << " ms. Recorded " << full_tape.size() << " frames." << std::endl;

    std::cout << "Running tick-by-tick verification from restored snapshot..." << std::endl;
    auto rep_start = std::chrono::high_resolution_clock::now();

    SimWorld world_replay;
    world_replay.restore(initial_snap);

    if (world_replay.get_state_hash() != initial_hash) {
        std::cerr << "\n[!] INITIAL SNAPSHOT RESTORE FAILED HASH CHECK!" << std::endl;
        
        // Setup diag world to capture a clean pristine snapshot for comparison
        SimWorld diag_world;
        diag_world.set_seed(seed);
        diag_world.generate_player_map(seed, 64, 64, cell_size);
        
        // Pre-allocate to match main test
        for(int i=0; i<1000; i++) diag_world.spawn_entity(EntityType::ENEMY, 0, 0, 0, 0);
        diag_world.clear_entities();
        
        diag_world.spawn_entity(EntityType::PLAYER, 32 * cell_size, 32 * cell_size, 32 << 16, 100);
        SplitMix64 diag_rng(seed);
        for (int i = 0; i < 200; ++i) { // Match the 200 from run_test
            diag_world.spawn_entity(EntityType::ENEMY, 
                diag_rng.next_range(5, 59) * cell_size, 
                diag_rng.next_range(5, 59) * cell_size, 
                32 << 16, 50);
        }
        for (int i = 0; i < 50; ++i) {
            int32_t cx = static_cast<int32_t>(diag_rng.next_range(10, 50));
            int32_t cy = static_cast<int32_t>(diag_rng.next_range(10, 50));
            uint8_t type = static_cast<uint8_t>(diag_rng.next_range(1, 3));
            uint8_t prefix = static_cast<uint8_t>(diag_rng.next_range(0, 5));
            uint8_t suffix = static_cast<uint8_t>(diag_rng.next_range(0, 5));
            uint8_t implicit = static_cast<uint8_t>(diag_rng.next_range(0, 4));
            diag_world.plant_crop(cx, cy, type, prefix, suffix, implicit);
        }

        compare_snapshots(diag_world, world_replay);
        exit(1);
    }

    for (size_t i = 0; i < full_tape.size(); ++i) {
        world_replay.tick(full_tape[i]);
        uint64_t actual_hash = world_replay.get_state_hash();
        
        if (actual_hash != expected_hashes[i]) {
            std::cerr << "\n[!] DIVERGENCE DETECTED at Input Index " << i << " (Tick " << full_tape[i].tick << ", Kind " << (int)full_tape[i].kind << ")" << std::endl;
            
            std::cout << "Reconstructing expected world up to this exact moment for deep analysis..." << std::endl;
            SimWorld diag_world;
            diag_world.restore(initial_snap);
            for(size_t j = 0; j <= i; ++j) {
                diag_world.tick(full_tape[j]);
            }
            
            compare_snapshots(diag_world, world_replay);
            exit(1);
        }
    }
    
    auto rep_end = std::chrono::high_resolution_clock::now();
    double rep_ms = std::chrono::duration_cast<std::chrono::microseconds>(rep_end - rep_start).count() / 1000.0;
    
    std::cout << "Verification completed in " << rep_ms << " ms." << std::endl;
    std::cout << "\n[SUCCESS] Determinism verified over " << test_ticks << " ticks!" << std::endl;
}

int main(int argc, char** argv) {
    uint64_t seed = 42;
    if (argc > 1) {
        try { seed = std::stoull(argv[1]); } catch (...) {}
    }
    run_test(seed);
    return 0;
}