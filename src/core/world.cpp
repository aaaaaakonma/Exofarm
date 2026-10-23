#include "world.h"
#include "item.h"
#include "player.h"
#include <cstring>
#include <algorithm>
#include <iostream>

/**
 * SimWorld Constructor
 * Initializes tick counters, ID generators, and pre-allocates memory for core vectors
 * to minimize reallocations during the high-frequency simulation loop.
 */
SimWorld::SimWorld() : _tick(0), _next_id(1), _rng_shotgun(0), _rng_tornado(0), _rng_crit(0) {
    _hit_pairs.reserve(2048);
    _tick_hit_pairs.reserve(256);
}

/**
 * Sets the master seed for the simulation.
 * All subsequent PRNG states (shotgun spread, tornado patterns, crits) are branched
 * from this master seed to ensure 100% determinism.
 */
void SimWorld::set_seed(uint64_t seed) {
    _tectonic_config.seed = seed;
    SplitMix64 master(seed);
    _shotgun_seed = master.next();
    _tornado_seed = master.next();
    _crit_seed = master.next();
}

/**
 * Main Simulation Tick
 * This is the entry point for every logic frame. It handles entity hibernation (optimization),
 * movement, collision, bots, environment, and crops.
 */
void SimWorld::tick(const InputFrame &input) {
    // Process standard gameplay inputs
    if (input.kind == InputFrame::Kind::PlayerInput) {
        // Re-seed frame-local PRNGs with the tick offset to maintain determinism
        _rng_shotgun.state = _shotgun_seed + _tick;
        _rng_tornado.state = _tornado_seed + _tick;
        _rng_crit.state = _crit_seed + _tick;

        // --- Hibernation System ---
        // Optimization: Only run expensive AI and collision logic for entities near the player.
        
        // 1. Locate the player to establish the "Active Zone" center
        int64_t px = 0, py = 0;
        bool player_found = false;
        int64_t act_sq = mul_fixed(ACTIVATION_RADIUS, ACTIVATION_RADIUS);
        ChaosState& chaos = _entities.get_chaos();
        for (size_t i = 0; i < chaos.size(); ++i) {
            if (chaos.types[i] == EntityType::PLAYER && !(chaos.flags[i] & (uint8_t)EntityFlags::DEAD)) {
                px = chaos.positions_x[i];
                py = chaos.positions_y[i];
                chaos.flags[i] |= (uint8_t)EntityFlags::ACTIVE;
                player_found = true;
                break;
            }
        }

        // Update chunk tiers based on player locations
        update_chunk_tiers();

        int32_t chunk_w = (_map_width + CHUNK_SIZE - 1) / CHUNK_SIZE;
        int32_t chunk_h = (_map_height + CHUNK_SIZE - 1) / CHUNK_SIZE;

        // Iterate through all entities to update their hibernation state based on their chunk tier
        for (size_t i = 0; i < chaos.size(); ++i) {
            if (chaos.types[i] == EntityType::PLAYER) {
                // Players are always exempt from hibernation
                chaos.flags[i] |= (uint8_t)EntityFlags::ACTIVE;
                chaos.flags[i] &= ~(uint8_t)EntityFlags::HIBERNATE;
                continue;
            }
            if (chaos.flags[i] & (uint8_t)EntityFlags::DEAD) continue;

            int64_t ex = chaos.positions_x[i];
            int64_t ey = chaos.positions_y[i];

            int32_t cell_x = static_cast<int32_t>(ex / _cell_size_fixed);
            if (ex < 0 && ex % _cell_size_fixed != 0) cell_x--;
            int32_t cell_y = static_cast<int32_t>(ey / _cell_size_fixed);
            if (ey < 0 && ey % _cell_size_fixed != 0) cell_y--;

            int32_t ecx = cell_x / CHUNK_SIZE;
            if (cell_x < 0 && cell_x % CHUNK_SIZE != 0) ecx--;
            int32_t ecy = cell_y / CHUNK_SIZE;
            if (cell_y < 0 && cell_y % CHUNK_SIZE != 0) ecy--;

            bool is_active_chunk = false;
            if (ecx >= 0 && ecx < chunk_w && ecy >= 0 && ecy < chunk_h && !_chunk_tiers.empty()) {
                is_active_chunk = (_chunk_tiers[ecy * chunk_w + ecx] == TIER_ACTIVE);
            }

            if (is_active_chunk) {
                chaos.flags[i] |= (uint8_t)EntityFlags::ACTIVE;
                chaos.flags[i] &= ~(uint8_t)EntityFlags::HIBERNATE;
            } else {
                chaos.flags[i] &= ~(uint8_t)EntityFlags::ACTIVE;
                chaos.flags[i] |= (uint8_t)EntityFlags::HIBERNATE;
                chaos.velocities_x[i] = 0;
                chaos.velocities_y[i] = 0;
            }
        }

        // Reset frame-local collision tracking
        _tick_hit_pairs.clear();

        // Step 1: Resolve AI decisions and basic movement
        tick_movement(input, px, py, player_found);

        tick_bots();

        // Step 2: Resolve Physics and Combat collisions
        tick_collision(px, py, player_found, act_sq);

        // Step 3: Merge frame-local hits into the permanent history
        // Uses set_union to maintain a sorted, unique history of "who hit whom"
        if (!_tick_hit_pairs.items.empty()) {
            _tick_hit_pairs_sorted.clear();
            _tick_hit_pairs_sorted.insert(_tick_hit_pairs_sorted.end(), _tick_hit_pairs.items.begin(), _tick_hit_pairs.items.end());
            std::sort(_tick_hit_pairs_sorted.begin(), _tick_hit_pairs_sorted.end());
            
            _hit_pairs_scratch.clear();
            _hit_pairs_scratch.reserve(_hit_pairs.size() + _tick_hit_pairs_sorted.size());
            std::set_union(_hit_pairs.begin(), _hit_pairs.end(),
                           _tick_hit_pairs_sorted.begin(), _tick_hit_pairs_sorted.end(),
                           std::back_inserter(_hit_pairs_scratch));
            std::swap(_hit_pairs, _hit_pairs_scratch);
        }

        // Step 4: Finalize entity deaths and memory recycling
        tick_cleanup();
        
        // Step 5: Tick environment (soil moisture, aquifers, evaporation)
        tick_environment();

        // Step 6: Tick crops
        tick_crops();

        _tick++;
    } else if (input.kind == InputFrame::Kind::EquipItem) {
        // Handle equipment change events (stat recalculation)
        _do_equip(input.payload.event.event_entity, input.payload.event.event_slot, input.payload.event.event_item_id);
    } else if (input.kind == InputFrame::Kind::UnequipItem) {
        // Handle unequip events
        _do_equip(input.payload.event.event_entity, input.payload.event.event_slot, 0);
    } else if (input.kind == InputFrame::Kind::PlaceStructure) {
        _do_place_structure(input.payload.structure.x, input.payload.structure.y, input.payload.structure.structure_type);
    } else if (input.kind == InputFrame::Kind::RemoveStructure) {
        _do_remove_structure(input.payload.structure.x, input.payload.structure.y);
    } else if (input.kind == InputFrame::Kind::SpawnBot) {
        _do_spawn_bot(input.payload.bot.bot_id, input.payload.bot.bot_type, input.payload.bot.x, input.payload.bot.y);
    } else if (input.kind == InputFrame::Kind::SetBotJob) {
        _do_set_bot_job(input.payload.bot.bot_id, input.payload.bot.job, input.payload.bot.x, input.payload.bot.y, input.payload.bot.seed_type);
    } else if (input.kind == InputFrame::Kind::UploadBotProgram) {
        _do_upload_bot_program(input.payload.bot_program.bot_id, input.payload.bot_program.opcodes, input.payload.bot_program.program_size);
    } else if (input.kind == InputFrame::Kind::WireBotPorts) {
        _do_wire_bot_ports(input.payload.bot.bot_id, input.payload.bot.sensor_x, input.payload.bot.sensor_y, input.payload.bot.actuator_x, input.payload.bot.actuator_y);
    }
}

/**
 * Captures a complete, bit-perfect copy of the current simulation state.
 * Used for rollbacks, state synchronization, and long-term replay storage.
 */
BisectSnapshot SimWorld::snapshot() const {
    BisectSnapshot snap;
    snap.tick = _tick;
    snap.next_id = _next_id;
    snap.shotgun_seed = _shotgun_seed;
    snap.tornado_seed = _tornado_seed;
    snap.crit_seed = _crit_seed;
    
    // Capture ChaosState
    snap.chaos = _entities.get_chaos();

    // Bulk-sync bounds into snapshot (derived from position and radius)
    for (size_t i = 0; i < snap.chaos.size(); ++i) {
        int64_t x = snap.chaos.positions_x[i];
        int64_t y = snap.chaos.positions_y[i];
        int64_t r = snap.chaos.radii[i];
        snap.chaos.tree_min_x[i] = x - r;
        snap.chaos.tree_min_y[i] = y - r;
        snap.chaos.tree_max_x[i] = x + r;
        snap.chaos.tree_max_y[i] = y + r;
        snap.chaos.node_ids[i] = -1;
    }

    snap.heavy_stats = _entities.get_heavy_stats();
    snap.heavy_equip = _entities.get_heavy_equip();
    snap.entity_recycle_bin = _entity_recycle_bin;
    snap.attack_recycle_bin = _attack_recycle_bin;
    snap.hit_pairs = _hit_pairs;

    // Map properties for farming game
    snap.map_width = _map_width;
    snap.map_height = _map_height;
    snap.cell_size_fixed = _cell_size_fixed;
    snap.water_table = _water_table;
    snap.aquifers = _aquifers;
    snap.rivers = _rivers;
    snap.lakes = _lakes;
    snap.structure_type = _structure_type;
    snap.water_pressure = _water_pressure;
    snap.actuator_activated = _actuator_activated;
    snap.bot_states = _bot_states;
    snap.chunk_tiers = _chunk_tiers;
    snap.weather_state = _weather_state;
    snap.weather_timer = _weather_timer;
    snap.co2_level = _co2_level;
    snap.elevation = _elevation;
    snap.biome = _biome;
    snap.soil_nitrogen = _soil_nitrogen;
    snap.soil_phosphorus = _soil_phosphorus;
    snap.soil_potassium = _soil_potassium;
    snap.soil_calcium = _soil_calcium;
    snap.soil_magnesium = _soil_magnesium;
    snap.soil_sulfur = _soil_sulfur;
    snap.soil_compactness = _soil_compactness;
    snap.crop_type = _crop_type;
    snap.crop_growth = _crop_growth;
    snap.crop_health = _crop_health;
    snap.crop_prefix = _crop_prefix;
    snap.crop_suffix = _crop_suffix;
    snap.crop_implicit = _crop_implicit;
    snap.planted_tile_indices = _planted_tile_indices;
    snap.surface_type = _surface_type;
    snap.mineral_type = _mineral_type;
    snap.starter_spawn_x = _starter_spawn_x;
    snap.starter_spawn_y = _starter_spawn_y;
    snap.starter_pond_volume = _starter_pond_volume;
    snap.starter_pond_max = _starter_pond_max;
    snap.hydraulic_reservoirs = _hydraulic_graph.get_reservoirs();
    snap.hydraulic_river_belts = _hydraulic_graph.get_river_belts();

    return snap;
}

/**
 * Restores the simulation to a previously captured snapshot.
 * Rebuilds spatial indices (Tree, WallGrid) to ensure consistency.
 */
void SimWorld::restore(const BisectSnapshot &snap) {
    _tick = snap.tick;
    _next_id = snap.next_id;
    _shotgun_seed = snap.shotgun_seed;
    _tornado_seed = snap.tornado_seed;
    _crit_seed = snap.crit_seed;
    
    _entities.get_heavy_stats() = snap.heavy_stats;
    _entities.get_heavy_equip() = snap.heavy_equip;
    _entities.get_chaos() = snap.chaos;
    _entities.rebuild_id_map();

    _entity_recycle_bin = snap.entity_recycle_bin;
    _attack_recycle_bin = snap.attack_recycle_bin;
    _hit_pairs = snap.hit_pairs;

    // Map properties for farming game
    _map_width = snap.map_width;
    _map_height = snap.map_height;
    _cell_size_fixed = snap.cell_size_fixed;
    _water_table = snap.water_table;
    _aquifers = snap.aquifers;
    _rivers = snap.rivers;
    _lakes = snap.lakes;
    _structure_type = snap.structure_type;
    _water_pressure = snap.water_pressure;
    _actuator_activated = snap.actuator_activated;
    _bot_states = snap.bot_states;
    _chunk_tiers = snap.chunk_tiers;
    _weather_state = snap.weather_state;
    _weather_timer = snap.weather_timer;
    _co2_level = snap.co2_level;
    _elevation = snap.elevation;
    _biome = snap.biome;
    _soil_nitrogen = snap.soil_nitrogen;
    _soil_phosphorus = snap.soil_phosphorus;
    _soil_potassium = snap.soil_potassium;
    _soil_calcium = snap.soil_calcium;
    _soil_magnesium = snap.soil_magnesium;
    _soil_sulfur = snap.soil_sulfur;
    _soil_compactness = snap.soil_compactness;
    _crop_type = snap.crop_type;
    _crop_growth = snap.crop_growth;
    _crop_health = snap.crop_health;
    _crop_prefix = snap.crop_prefix;
    _crop_suffix = snap.crop_suffix;
    _crop_implicit = snap.crop_implicit;
    _planted_tile_indices = snap.planted_tile_indices;
    _surface_type = snap.surface_type;
    _mineral_type = snap.mineral_type;
    _starter_spawn_x = snap.starter_spawn_x;
    _starter_spawn_y = snap.starter_spawn_y;
    _starter_pond_volume = snap.starter_pond_volume;
    _starter_pond_max = snap.starter_pond_max;
    _hydraulic_graph.set_reservoirs(snap.hydraulic_reservoirs);
    _hydraulic_graph.set_river_belts(snap.hydraulic_river_belts);
    
    // Geometry Rebuild
    
    // Rebuild Entity Grid using entity positions
    auto& chaos = _entities.get_chaos();
    for (size_t i = 0; i < chaos.size(); ++i) {
        if (!(chaos.flags[i] & (uint8_t)EntityFlags::DEAD)) {
        }
        chaos.node_ids[i] = -1;
    }
}

/**
 * Generates a 64-bit FNV-1a hash of the entire simulation state.
 * This "fingerprint" is used by the server to verify that the client's 
 * simulation results match the server's authoritative replay.
 */
uint64_t SimWorld::get_state_hash() const {
    uint64_t hash = 14695981039346656037ULL; // FNV offset basis
    auto hash_combine = [&hash](const void* data, size_t len) {
        const uint8_t* ptr = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < len; ++i) {
            hash ^= ptr[i];
            hash *= 1099511628211ULL; // FNV prime
        }
    };
    
    // Hash core counters
    hash_combine(&_tick, sizeof(_tick));
    hash_combine(&_next_id, sizeof(_next_id));
    hash_combine(&_shotgun_seed, sizeof(_shotgun_seed));
    hash_combine(&_tornado_seed, sizeof(_tornado_seed));
    hash_combine(&_crit_seed, sizeof(_crit_seed));

    // Helper to hash vectors of POD data
    auto hash_vec = [&hash_combine](const auto& vec) {
        if (!vec.empty()) {
            hash_combine(vec.data(), vec.size() * sizeof(vec[0]));
        }
    };

    // Hash the entire SoA ChaosState
    const ChaosState& chaos = _entities.get_chaos();
    hash_vec(chaos.dense_ids);
    // node_ids is excluded (internal handles that vary after restore rebuild)
    hash_vec(chaos.positions_x);
    hash_vec(chaos.positions_y);
    hash_vec(chaos.velocities_x);
    hash_vec(chaos.velocities_y);
    hash_vec(chaos.radii);
    hash_vec(chaos.frictions);
    hash_vec(chaos.healths);
    hash_vec(chaos.pierces);
    hash_vec(chaos.types);
    hash_vec(chaos.tags);
    hash_vec(chaos.flags);
    
    // Hash new Tree Preservation fields
    hash_vec(chaos.tree_min_x);
    hash_vec(chaos.tree_min_y);
    hash_vec(chaos.tree_max_x);
    hash_vec(chaos.tree_max_y);
    
    // Hash heavy data and geometry
    hash_vec(_entities.get_heavy_stats());
    hash_vec(_entities.get_heavy_equip());
    hash_vec(_entity_recycle_bin);
    hash_vec(_attack_recycle_bin);
    hash_vec(_hit_pairs);

    // Hash map properties for farming game
    hash_combine(&_map_width, sizeof(_map_width));
    hash_combine(&_map_height, sizeof(_map_height));
    hash_combine(&_cell_size_fixed, sizeof(_cell_size_fixed));
    hash_vec(_water_table);
    hash_vec(_aquifers);
    hash_vec(_rivers);
    hash_vec(_lakes);
    hash_vec(_structure_type);
    hash_vec(_water_pressure);
    hash_vec(_actuator_activated);
    for (const auto& b : _bot_states) {
        hash_combine(&b.entity_id, sizeof(b.entity_id));
        hash_combine(&b.bot_type, sizeof(b.bot_type));
        hash_combine(&b.current_job, sizeof(b.current_job));
        hash_combine(&b.target_x, sizeof(b.target_x));
        hash_combine(&b.target_y, sizeof(b.target_y));
        hash_combine(&b.seed_type, sizeof(b.seed_type));
        hash_combine(b.registers, sizeof(b.registers));
        hash_combine(b.program, sizeof(b.program));
        hash_combine(&b.program_size, sizeof(b.program_size));
        hash_combine(&b.pc, sizeof(b.pc));
        hash_combine(&b.cmp_result, sizeof(b.cmp_result));
        hash_combine(&b.sensor_x, sizeof(b.sensor_x));
        hash_combine(&b.sensor_y, sizeof(b.sensor_y));
        hash_combine(&b.actuator_x, sizeof(b.actuator_x));
        hash_combine(&b.actuator_y, sizeof(b.actuator_y));
    }
    hash_vec(_chunk_tiers);
    hash_combine(&_weather_state, sizeof(_weather_state));
    hash_combine(&_weather_timer, sizeof(_weather_timer));
    hash_combine(&_co2_level, sizeof(_co2_level));
    hash_vec(_elevation);
    hash_vec(_biome);
    hash_vec(_soil_nitrogen);
    hash_vec(_soil_phosphorus);
    hash_vec(_soil_potassium);
    hash_vec(_soil_calcium);
    hash_vec(_soil_magnesium);
    hash_vec(_soil_sulfur);
    hash_vec(_soil_compactness);
    hash_vec(_crop_type);
    hash_vec(_crop_growth);
    hash_vec(_crop_health);
    hash_vec(_crop_prefix);
    hash_vec(_crop_suffix);
    hash_vec(_crop_implicit);
    hash_vec(_planted_tile_indices);
    hash_vec(_surface_type);
    hash_vec(_mineral_type);
    hash_combine(&_starter_spawn_x, sizeof(_starter_spawn_x));
    hash_combine(&_starter_spawn_y, sizeof(_starter_spawn_y));
    hash_combine(&_starter_pond_volume, sizeof(_starter_pond_volume));
    hash_combine(&_starter_pond_max, sizeof(_starter_pond_max));
    for (const auto& r : _hydraulic_graph.get_reservoirs()) {
        hash_combine(&r.id, sizeof(r.id));
        hash_combine(&r.current_volume, sizeof(r.current_volume));
        hash_combine(&r.max_capacity, sizeof(r.max_capacity));
        hash_combine(&r.water_level, sizeof(r.water_level));
        hash_combine(&r.tile_count, sizeof(r.tile_count));
        hash_combine(&r.inflow_rate, sizeof(r.inflow_rate));
        hash_combine(&r.outflow_rate, sizeof(r.outflow_rate));
        hash_combine(&r.extraction_rate, sizeof(r.extraction_rate));
        hash_combine(&r.seepage_rate, sizeof(r.seepage_rate));
        hash_combine(&r.is_infinite, sizeof(r.is_infinite));
        hash_combine(&r.is_dry, sizeof(r.is_dry));
        hash_vec(r.tile_indices);
    }
    for (const auto& b : _hydraulic_graph.get_river_belts()) {
        hash_combine(&b.id, sizeof(b.id));
        hash_combine(&b.from_node, sizeof(b.from_node));
        hash_combine(&b.to_node, sizeof(b.to_node));
        hash_combine(&b.upstream_belt_id, sizeof(b.upstream_belt_id));
        hash_combine(&b.downstream_belt_id, sizeof(b.downstream_belt_id));
        hash_combine(&b.tile_count, sizeof(b.tile_count));
        hash_combine(&b.max_rate, sizeof(b.max_rate));
        hash_combine(&b.pump_rate, sizeof(b.pump_rate));
        hash_combine(&b.flow_rate, sizeof(b.flow_rate));
        hash_combine(&b.water_level, sizeof(b.water_level));
        hash_combine(&b.is_dry, sizeof(b.is_dry));
        hash_vec(b.tile_indices);
    }
    
    return hash;
}

/**
 * Processes movement and AI logic for all active entities.
 */
void SimWorld::tick_movement(const InputFrame &input, int64_t px, int64_t py, bool player_found) {
    ChaosState& chaos = _entities.get_chaos();
    auto v = chaos.get_restricted_view();
    size_t count = chaos.size();
    size_t start_capacity = chaos.dense_ids.capacity();

    for (size_t i = 0; i < count; ++i) {
        if (!(v.flags[i] & (uint8_t)EntityFlags::ACTIVE)) continue;
        if (v.flags[i] & (uint8_t)EntityFlags::DEAD) continue;

        EntityType type = v.types[i];

        if (type == EntityType::PLAYER) {
            // Run Player control tick
            Player::tick(v.dense_ids[i], input, *this);
            if (chaos.dense_ids.capacity() != start_capacity) {
                v = chaos.get_restricted_view();
                start_capacity = chaos.dense_ids.capacity();
            }
        }
    }
}

/**
 * Resolves all collisions.
 */
void SimWorld::tick_collision(int64_t px, int64_t py, bool player_found, int64_t act_sq) {
}

/**
 * High-level Factory: Spawn a Generic Entity
 */
EntityID SimWorld::spawn_entity(EntityType type, int64_t x, int64_t y, int64_t radius, int32_t health, uint32_t tags) {
    EntityID id = _next_id++;
    _entities.spawn(id, type, x, y, radius, health, tags);
    uint32_t idx = _entities.get_dense_idx(id);
    _entities.get_chaos().flags[idx] |= (uint8_t)EntityFlags::ACTIVE;

    _sync_node_aabb(idx);

    return id;
}

/**
 * Removes dead entities from the simulation and cleans up cross-referenced hit records.
 */
void SimWorld::tick_cleanup() {
    ChaosState& chaos = _entities.get_chaos();
    auto v = chaos.get_restricted_view();
    
    std::vector<EntityID> dead_ids;
    dead_ids.reserve(chaos.size() / 4);

    // Process deaths from back to front to minimize SoA shift overhead
    for (int i = (int)chaos.size() - 1; i >= 0; --i) {
        if (v.flags[i] & (uint8_t)EntityFlags::DEAD) {
            dead_ids.push_back(v.dense_ids[i]);
            kill_entity(v.dense_ids[i]);
            v = chaos.get_restricted_view();
        }
    }

    // Clean up combat history for entities that no longer exist
    if (!dead_ids.empty() && !_hit_pairs.empty()) {
        std::sort(dead_ids.begin(), dead_ids.end());
        auto it = std::remove_if(_hit_pairs.begin(), _hit_pairs.end(), [&](uint64_t p) {
            uint32_t a = (uint32_t)(p >> 32);
            uint32_t b = (uint32_t)(p & 0xFFFFFFFF);
            return std::binary_search(dead_ids.begin(), dead_ids.end(), a) || 
                   std::binary_search(dead_ids.begin(), dead_ids.end(), b);
        });
        _hit_pairs.erase(it, _hit_pairs.end());
    }
}

/**
 * Physical removal of an entity. Updates spatial trees and SoA indices.
 */
void SimWorld::kill_entity(EntityID id) {
    _entities.kill(id, _entity_recycle_bin, _hit_pairs, _attack_recycle_bin);
}

// --- Basic Utility Accessors ---

bool SimWorld::has_entity(EntityID id) const {
    return _entities.has_entity(id);
}

uint32_t SimWorld::get_dense_idx(EntityID id) const {
    return _entities.get_dense_idx(id);
}

const EntityStats& SimWorld::get_stats(EntityID id) const {
    return _entities.get_heavy_stats()[_entities.get_dense_idx(id)];
}

EntityStats& SimWorld::get_stats_mut(EntityID id) {
    return _entities.get_heavy_stats()[_entities.get_dense_idx(id)];
}

const Equipment& SimWorld::get_equipment(EntityID id) const {
    return _entities.get_heavy_equip()[_entities.get_dense_idx(id)];
}

Equipment& SimWorld::get_equipment_mut(EntityID id) {
    return _entities.get_heavy_equip()[_entities.get_dense_idx(id)];
}

/**
 * Adds static geometry to the world.
 */
void SimWorld::generate_player_map(uint64_t seed, int64_t width, int64_t height, int64_t cell_size_fixed) {
    set_map_dimensions((int32_t)width, (int32_t)height, cell_size_fixed);

    Tectonics::ContinuousTectonicHeightmap::Config cfg = _tectonic_config;
    cfg.seed = seed;
    cfg.world_width = width * cell_size_fixed;
    cfg.world_height = height * cell_size_fixed;
    if (cfg.num_plates <= 0) cfg.num_plates = 14;
    _tectonic_heightmap.init(cfg);

    int32_t w = static_cast<int32_t>(width);
    int32_t h = static_cast<int32_t>(height);
    size_t total_cells = static_cast<size_t>(w) * h;

    // --- Generate tectonic heightfield ---
    _tectonic_heightmap.generate_grid(w, h, cell_size_fixed, _elevation);

    int32_t best_spawn_x = w / 2;
    int32_t best_spawn_y = h / 2;
    int64_t min_spawn_dist_to_ideal = 0x7FFFFFFFFFFFFFFF;

    // Gradient sampling offset for orographic moisture (one cell offset)
    const int64_t GRAD_STEP = cell_size_fixed;

    for (int32_t y = 0; y < h; ++y) {
        int64_t py = static_cast<int64_t>(y) * cell_size_fixed;
        for (int32_t x = 0; x < w; ++x) {
            int64_t px = static_cast<int64_t>(x) * cell_size_fixed;
            size_t idx = static_cast<size_t>(y) * w + x;

            // --- Read elevation ---
            int64_t el = _elevation[idx];

            // --- Tectonic Sample for structural metadata (boundary type, plate type) ---
            Tectonics::TectonicSample sample = _tectonic_heightmap.sample(px, py);

            // --- Climate Noise ---
            // Base moisture from FBm (scale independent of elevation)
            int64_t moisture_base = SimMath::fbm2d(mul_fixed(px, 1200), mul_fixed(py, 1200), seed + 555, 4);
            // Base temperature: latitude-like gradient + noise
            int64_t temp = SimMath::fbm2d(mul_fixed(px, 1000), mul_fixed(py, 1000), seed + 777, 3);

            // --- Orographic Moisture Effect ---
            // Sample elevation one step in the prevailing wind direction (west→east, i.e. +x)
            // to find local rain-shadow vs. windward slope
            int64_t el_windward = (x > 0) ? _elevation[static_cast<size_t>(y) * w + (x - 1)] : _tectonic_heightmap.sample(px - GRAD_STEP, py).elevation_final;
            int64_t slope_rise = el - el_windward; // positive = ascending slope = windward = more rain
            // Windward slopes get +moisture, leeward get -moisture
            int64_t orographic_boost = std::clamp(slope_rise * 3, -18000LL, 18000LL);
            int64_t moisture = moisture_base + orographic_boost;

            // --- Biome + Surface Classification ---
            // Biome codes: 0=OCEAN 1=TUNDRA 2=DESERT 3=GRASSLAND 4=FOREST
            //              5=TROPICAL_RAINFOREST 6=SWAMP 7=MOUNTAIN 8=BEACH/COAST
            uint8_t b = 0;  // OCEAN default
            uint8_t st = 0; // WATER default

            // Sea level = 0, mountain threshold = 16m (relative to 8m base), beach = 0..3m
            if (el <= 0) {
                b = 0;   // OCEAN
                st = 0;  // WATER
            } else if (el > (16LL * ONE)) {
                b = 7;   // MOUNTAIN
                st = 4;  // STONE / ROCK
            } else if (el < (3LL * ONE)) {
                // Coastal beach/shoreline zone
                b = 8;   // BEACH / COAST
                st = 2;  // SAND
            } else {
                // Land biome classification: temperature × moisture Whittaker diagram
                if (temp < -16000) {
                    // Very cold: tundra regardless of moisture
                    b = 1;  // TUNDRA / GLACIER
                    st = 2; // FROZEN DIRT / TUNDRA
                } else if (moisture < -14000) {
                    // Very dry
                    if (temp > 8000) {
                        b = 2;  // HOT DESERT
                        st = 2; // SAND
                    } else {
                        b = 2;  // COLD DESERT / STEPPE
                        st = 2; // GRAVEL / DRY SOIL
                    }
                } else if (moisture > 24000) {
                    // Very wet
                    if (temp > 10000) {
                        b = 5;  // TROPICAL RAINFOREST
                        st = 1; // RICH SOIL
                    } else {
                        b = 4;  // TEMPERATE RAINFOREST
                        st = 1; // SOIL
                    }
                } else if (moisture > 8000) {
                    b = 4;  // FOREST
                    st = 1; // SOIL
                } else if (el < (8LL * ONE) && moisture > 4000) {
                    b = 6;  // SWAMP / WETLANDS
                    st = 1; // MUD / SOIL
                } else {
                    b = 3;  // GRASSLAND / SAVANNA
                    st = 1; // GRASS / SOIL
                }

                // Spawn scoring: prefer grassland/forest at moderate elevation (~8..25m) near center
                if (b == 3 || b == 4) {
                    int64_t dist_to_center = abs_fixed(x - w / 2) + abs_fixed(y - h / 2);
                    int64_t score = dist_to_center + abs_fixed(el - (18LL * ONE)) / 4;
                    if (score < min_spawn_dist_to_ideal) {
                        min_spawn_dist_to_ideal = score;
                        best_spawn_x = x;
                        best_spawn_y = y;
                    }
                }
            }

            _biome[idx] = b;
            _surface_type[idx] = st;

            // --- Soil Nutrients (biome-differentiated profiles) ---
            if (b != 0 && b != 8) {
                // Base compactness from slope: steeper = more compact, harder to till
                int64_t slope_mag = abs_fixed(slope_rise);
                int64_t compactness_base = 30LL * ONE + std::clamp(slope_mag * 2, 0LL, 30LL * ONE);

                switch (b) {
                    case 1: // TUNDRA — cold, low N, high compactness (permafrost)
                        _soil_nitrogen[idx]     = static_cast<int32_t>(std::clamp(40LL * ONE + moisture_base, 15LL * ONE, 80LL * ONE));
                        _soil_phosphorus[idx]   = static_cast<int32_t>(50LL * ONE);
                        _soil_potassium[idx]    = static_cast<int32_t>(45LL * ONE);
                        _soil_calcium[idx]      = static_cast<int32_t>(55LL * ONE);
                        _soil_magnesium[idx]    = static_cast<int32_t>(30LL * ONE);
                        _soil_sulfur[idx]       = static_cast<int32_t>(20LL * ONE);
                        _soil_compactness[idx]  = static_cast<int32_t>(std::clamp(compactness_base + 20LL * ONE, 40LL * ONE, 90LL * ONE));
                        break;
                    case 2: // DESERT — low everything, high compaction, some potassium from mineral weathering
                        _soil_nitrogen[idx]     = static_cast<int32_t>(20LL * ONE);
                        _soil_phosphorus[idx]   = static_cast<int32_t>(30LL * ONE);
                        _soil_potassium[idx]    = static_cast<int32_t>(std::clamp(70LL * ONE + el / 2, 40LL * ONE, 120LL * ONE));
                        _soil_calcium[idx]      = static_cast<int32_t>(80LL * ONE); // calcareous desert soils
                        _soil_magnesium[idx]    = static_cast<int32_t>(35LL * ONE);
                        _soil_sulfur[idx]       = static_cast<int32_t>(15LL * ONE);
                        _soil_compactness[idx]  = static_cast<int32_t>(std::clamp(compactness_base + 15LL * ONE, 50LL * ONE, 95LL * ONE));
                        break;
                    case 3: // GRASSLAND — good N, moderate P, lower compaction
                        _soil_nitrogen[idx]     = static_cast<int32_t>(std::clamp(110LL * ONE + moisture * 2, 60LL * ONE, 200LL * ONE));
                        _soil_phosphorus[idx]   = static_cast<int32_t>(std::clamp(90LL * ONE + temp, 40LL * ONE, 160LL * ONE));
                        _soil_potassium[idx]    = static_cast<int32_t>(std::clamp(100LL * ONE + el / 3, 50LL * ONE, 180LL * ONE));
                        _soil_calcium[idx]      = static_cast<int32_t>(70LL * ONE);
                        _soil_magnesium[idx]    = static_cast<int32_t>(50LL * ONE);
                        _soil_sulfur[idx]       = static_cast<int32_t>(35LL * ONE);
                        _soil_compactness[idx]  = static_cast<int32_t>(std::clamp(compactness_base, 25LL * ONE, 60LL * ONE));
                        break;
                    case 4: // FOREST — rich N from litter, good P, moderate everything
                        _soil_nitrogen[idx]     = static_cast<int32_t>(std::clamp(140LL * ONE + moisture * 2, 80LL * ONE, 240LL * ONE));
                        _soil_phosphorus[idx]   = static_cast<int32_t>(std::clamp(100LL * ONE + temp, 50LL * ONE, 200LL * ONE));
                        _soil_potassium[idx]    = static_cast<int32_t>(std::clamp(110LL * ONE + el / 2, 60LL * ONE, 200LL * ONE));
                        _soil_calcium[idx]      = static_cast<int32_t>(65LL * ONE);
                        _soil_magnesium[idx]    = static_cast<int32_t>(55LL * ONE);
                        _soil_sulfur[idx]       = static_cast<int32_t>(40LL * ONE);
                        _soil_compactness[idx]  = static_cast<int32_t>(std::clamp(compactness_base, 20LL * ONE, 55LL * ONE));
                        break;
                    case 5: // TROPICAL RAINFOREST — very high N/P but leached, low compaction
                        _soil_nitrogen[idx]     = static_cast<int32_t>(std::clamp(160LL * ONE + moisture, 100LL * ONE, 250LL * ONE));
                        _soil_phosphorus[idx]   = static_cast<int32_t>(std::clamp(60LL * ONE + temp, 30LL * ONE, 130LL * ONE)); // leached
                        _soil_potassium[idx]    = static_cast<int32_t>(70LL * ONE);
                        _soil_calcium[idx]      = static_cast<int32_t>(45LL * ONE);
                        _soil_magnesium[idx]    = static_cast<int32_t>(40LL * ONE);
                        _soil_sulfur[idx]       = static_cast<int32_t>(30LL * ONE);
                        _soil_compactness[idx]  = static_cast<int32_t>(std::clamp(compactness_base - 5LL * ONE, 15LL * ONE, 45LL * ONE));
                        break;
                    case 6: // SWAMP / WETLAND — waterlogged, anaerobic, high N, very low compaction
                        _soil_nitrogen[idx]     = static_cast<int32_t>(std::clamp(130LL * ONE + moisture, 80LL * ONE, 220LL * ONE));
                        _soil_phosphorus[idx]   = static_cast<int32_t>(75LL * ONE);
                        _soil_potassium[idx]    = static_cast<int32_t>(60LL * ONE);
                        _soil_calcium[idx]      = static_cast<int32_t>(50LL * ONE);
                        _soil_magnesium[idx]    = static_cast<int32_t>(45LL * ONE);
                        _soil_sulfur[idx]       = static_cast<int32_t>(50LL * ONE); // anaerobic sulfur
                        _soil_compactness[idx]  = static_cast<int32_t>(std::clamp(compactness_base - 15LL * ONE, 10LL * ONE, 35LL * ONE));
                        break;
                    case 7: // MOUNTAIN — rocky, thin soil, high compaction, low nutrients
                        _soil_nitrogen[idx]     = static_cast<int32_t>(std::clamp(30LL * ONE + moisture_base, 10LL * ONE, 70LL * ONE));
                        _soil_phosphorus[idx]   = static_cast<int32_t>(40LL * ONE);
                        _soil_potassium[idx]    = static_cast<int32_t>(std::clamp(50LL * ONE + el / 4, 25LL * ONE, 100LL * ONE));
                        _soil_calcium[idx]      = static_cast<int32_t>(90LL * ONE); // limestone/carbonate rich
                        _soil_magnesium[idx]    = static_cast<int32_t>(60LL * ONE); // ultramafic uplift
                        _soil_sulfur[idx]       = static_cast<int32_t>(25LL * ONE);
                        _soil_compactness[idx]  = static_cast<int32_t>(std::clamp(compactness_base + 25LL * ONE, 60LL * ONE, 100LL * ONE));
                        break;
                    default:
                        _soil_nitrogen[idx]     = static_cast<int32_t>(60LL * ONE);
                        _soil_phosphorus[idx]   = static_cast<int32_t>(50LL * ONE);
                        _soil_potassium[idx]    = static_cast<int32_t>(55LL * ONE);
                        _soil_calcium[idx]      = static_cast<int32_t>(60LL * ONE);
                        _soil_magnesium[idx]    = static_cast<int32_t>(40LL * ONE);
                        _soil_sulfur[idx]       = static_cast<int32_t>(30LL * ONE);
                        _soil_compactness[idx]  = static_cast<int32_t>(compactness_base);
                        break;
                }
            }

            // --- Mineral Type Generation ---
            // Based on tectonic context: boundary type, plate type, elevation
            // Mineral codes: 0=none, 1=silica/quartz, 2=iron_ore, 3=copper, 4=limestone, 5=coal, 6=sulfur/volcanic, 7=granite/metamorphic
            uint8_t mineral = 0;
            if (b != 0) { // Only assign minerals on land
                Tectonics::BoundaryType btype = sample.boundary_type;
                bool is_continental = (sample.primary_type == Tectonics::PlateType::CONTINENTAL);

                // Use a hash-based noise for sparse placement (not every tile has minerals)
                uint32_t mineral_hash = SimMath::hash2d((int32_t)x, (int32_t)y, seed + 999);
                bool has_mineral = (mineral_hash & 0xF) < 3; // ~18% of land tiles have minerals

                if (has_mineral) {
                    if (b == 7) {
                        // Mountains: metamorphic and igneous rocks dominate
                        uint32_t mtype = mineral_hash & 0xFF;
                        if (mtype < 50)       mineral = 7; // granite/metamorphic
                        else if (mtype < 110) mineral = 2; // iron ore (magmatic)
                        else if (mtype < 160) mineral = 3; // copper (hydrothermal)
                        else                  mineral = 1; // quartz/silica
                    } else if (btype == Tectonics::BoundaryType::CONVERGENT_SUBDUCTION ||
                               btype == Tectonics::BoundaryType::CONVERGENT_ISLAND_ARC) {
                        // Volcanic arcs: sulfur, copper, iron
                        uint32_t mtype = mineral_hash & 0xFF;
                        if (mtype < 80)       mineral = 6; // sulfur/volcanic
                        else if (mtype < 160) mineral = 3; // copper porphyry deposits
                        else                  mineral = 2; // iron
                    } else if (btype == Tectonics::BoundaryType::CONVERGENT_CONTINENTAL_COLLISION) {
                        // Collision zones: metamorphic minerals
                        mineral = ((mineral_hash & 0x3) < 2) ? 7 : 2;
                    } else if (btype == Tectonics::BoundaryType::DIVERGENT_CONTINENTAL_RIFT) {
                        // Rift valleys: evaporites, potassic minerals
                        mineral = 4; // limestone/evaporite
                    } else if (is_continental) {
                        // Stable continental interior: sedimentary minerals
                        uint32_t mtype = mineral_hash & 0xFF;
                        if (moisture > 10000 && el < (20LL * ONE)) {
                            mineral = 5; // coal (swampy carboniferous deposits)
                        } else if (mtype < 80) {
                            mineral = 4; // limestone
                        } else if (mtype < 140) {
                            mineral = 1; // silica/quartz
                        } else {
                            mineral = 2; // iron
                        }
                    }
                }
            }
            _mineral_type[idx] = mineral;
        }
    }

    _starter_spawn_x = best_spawn_x;
    _starter_spawn_y = best_spawn_y;
}


void SimWorld::clear_entities() {


    _entities.clear();
    _entity_recycle_bin.clear();
    _attack_recycle_bin.clear();
    _hit_pairs.clear();
    _tick = 0;
    _next_id = 1;
}

/**
 * Updates an entity's composite stats by re-evaluating all equipped items.
 * Called whenever equipment changes.
 */
void SimWorld::recalculate_stats(EntityID id) {
    uint32_t idx = get_dense_idx(id);
    if (idx == 0xFFFFFFFF) return;

    EntityStats &stats = _entities.get_heavy_stats()[idx];
    Equipment &equip = _entities.get_heavy_equip()[idx];

    // Reset current modifiers
    stats.flat_hp = 0; stats.pct_hp = 0;
    stats.flat_damage = 0; stats.pct_damage = 0; stats.more_damage = 0;
    stats.flat_speed = 0; stats.pct_speed = 0;
    stats.flat_armor = 0; stats.pct_armor = 0;
    stats.flat_attack_speed = 0; stats.pct_attack_speed = 0;
    stats.flat_pierce = 0;

    // Aggregate affixes from all 3 slots
    for (int i = 0; i < 3; ++i) {
        if (equip.slots[i] != 0) {
            ItemData item = ItemData::generate(equip.slots[i]);

            auto apply_affix = [&](const ItemAffix &affix) {
                switch(affix.type) {
                    case 0: stats.flat_hp += affix.value; break;
                    case 1: stats.pct_hp += affix.value; break;
                    case 2: stats.flat_damage += affix.value; break;
                    case 3: stats.pct_damage += affix.value; break;
                    case 4: stats.flat_speed += ((int64_t)affix.value * 65536) / 10; break;
 
                    case 5: stats.pct_speed += affix.value; break;
                    case 6: stats.flat_armor += affix.value; break;
                    case 7: stats.pct_armor += affix.value; break;
                    case 8: stats.flat_attack_speed += affix.value; break;
                    case 9: stats.pct_attack_speed += affix.value; break;
                    case 10: stats.flat_pierce += affix.value; break;
                    default: break;
                }
            };
            for (int p = 0; p < item.prefix_count; ++p) apply_affix(item.prefixes[p]);
            for (int s = 0; s < item.suffix_count; ++s) apply_affix(item.suffixes[s]);
        }
    }
}

/**
 * Synchronizes the Spatial Tree bounds with the ChaosState SoA for a single entity.
 * This ensures that tree-related data is captured in snapshots and hashes incrementally.
 */
void SimWorld::_sync_node_aabb(uint32_t idx) {
    ChaosState& chaos = _entities.get_chaos();
    if (idx < chaos.size()) {
        int64_t x = chaos.positions_x[idx];
        int64_t y = chaos.positions_y[idx];
        int64_t r = chaos.radii[idx];
        chaos.tree_min_x[idx] = x - r;
        chaos.tree_min_y[idx] = y - r;
        chaos.tree_max_x[idx] = x + r;
        chaos.tree_max_y[idx] = y + r;
    }
}

/**
 * Internal: Apply equipment change and trigger side-effects (re-calcs, health clamping).
 */
void SimWorld::_do_equip(EntityID entity_id, int slot, uint64_t item_id) {
    if (slot < 0 || slot >= 3) return;
    uint32_t idx = get_dense_idx(entity_id);
    if (idx != 0xFFFFFFFF) {
        _entities.get_heavy_equip()[idx].slots[slot] = item_id;
        recalculate_stats(entity_id);
        
        int32_t max_hp = _entities.get_heavy_stats()[idx].get_max_hp();
        if (_entities.get_chaos().healths[idx] > max_hp) {
            _entities.get_chaos().healths[idx] = max_hp;
        }
        _entities.get_chaos().flags[idx] |= (uint8_t)EntityFlags::DIRTY;
    }
}

/**
 * Equips an item to an entity.
 */
void SimWorld::equip_item(EntityID entity_id, int slot, uint64_t item_id) {
    _do_equip(entity_id, slot, item_id);
}

void SimWorld::unequip_item(EntityID entity_id, int slot) {
    _do_equip(entity_id, slot, 0);
}

/**
 * Converts a BisectSnapshot into a compact binary buffer for storage.
 */
std::vector<uint8_t> SimWorld::serialize_snapshot(const BisectSnapshot &snap) {
    std::vector<uint8_t> buffer;
    auto write_vec = [&](const auto& vec) {
        uint32_t size = (uint32_t)vec.size();
        buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&size), reinterpret_cast<const uint8_t*>(&size) + sizeof(uint32_t));
        if (size > 0) {
            buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(vec.data()), reinterpret_cast<const uint8_t*>(vec.data()) + size * sizeof(vec[0]));
        }
    };

    // Serialize Header
    buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&snap.tick), reinterpret_cast<const uint8_t*>(&snap.tick) + sizeof(uint32_t));
    buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&snap.next_id), reinterpret_cast<const uint8_t*>(&snap.next_id) + sizeof(EntityID));
    buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&snap.shotgun_seed), reinterpret_cast<const uint8_t*>(&snap.shotgun_seed) + sizeof(uint64_t));
    buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&snap.tornado_seed), reinterpret_cast<const uint8_t*>(&snap.tornado_seed) + sizeof(uint64_t));
    buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&snap.crit_seed), reinterpret_cast<const uint8_t*>(&snap.crit_seed) + sizeof(uint64_t));

    // Serialize Vectors
    write_vec(snap.chaos.dense_ids);
    write_vec(snap.chaos.node_ids);
    write_vec(snap.chaos.tree_min_x);
    write_vec(snap.chaos.tree_min_y);
    write_vec(snap.chaos.tree_max_x);
    write_vec(snap.chaos.tree_max_y);
    write_vec(snap.chaos.positions_x);
    write_vec(snap.chaos.positions_y);
    write_vec(snap.chaos.velocities_x);
    write_vec(snap.chaos.velocities_y);
    write_vec(snap.chaos.radii);
    write_vec(snap.chaos.frictions);
    write_vec(snap.chaos.healths);
    write_vec(snap.chaos.pierces);
    write_vec(snap.chaos.types);
    write_vec(snap.chaos.tags);
    write_vec(snap.chaos.flags);
    write_vec(snap.heavy_stats);
    write_vec(snap.heavy_equip);
    write_vec(snap.entity_recycle_bin);
    write_vec(snap.attack_recycle_bin);
    write_vec(snap.hit_pairs);

    // Map properties for farming game
    buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&snap.map_width), reinterpret_cast<const uint8_t*>(&snap.map_width) + sizeof(int32_t));
    buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&snap.map_height), reinterpret_cast<const uint8_t*>(&snap.map_height) + sizeof(int32_t));
    buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&snap.cell_size_fixed), reinterpret_cast<const uint8_t*>(&snap.cell_size_fixed) + sizeof(int64_t));
    write_vec(snap.water_table);
    write_vec(snap.aquifers);
    write_vec(snap.rivers);
    write_vec(snap.lakes);
    write_vec(snap.structure_type);
    write_vec(snap.water_pressure);
    write_vec(snap.actuator_activated);
    write_vec(snap.bot_states);
    write_vec(snap.chunk_tiers);
    write_vec(snap.soil_nitrogen);
    write_vec(snap.soil_phosphorus);
    write_vec(snap.soil_potassium);
    write_vec(snap.soil_calcium);
    write_vec(snap.soil_magnesium);
    write_vec(snap.soil_sulfur);
    write_vec(snap.soil_compactness);
    write_vec(snap.crop_type);
    write_vec(snap.crop_growth);
    write_vec(snap.crop_health);
    write_vec(snap.crop_prefix);
    write_vec(snap.crop_suffix);
    write_vec(snap.crop_implicit);
    write_vec(snap.planted_tile_indices);
    
    buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&snap.weather_state), reinterpret_cast<const uint8_t*>(&snap.weather_state) + sizeof(uint8_t));
    buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&snap.weather_timer), reinterpret_cast<const uint8_t*>(&snap.weather_timer) + sizeof(int32_t));
    buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&snap.co2_level), reinterpret_cast<const uint8_t*>(&snap.co2_level) + sizeof(int32_t));
    write_vec(snap.elevation);
    write_vec(snap.biome);
    write_vec(snap.surface_type);
    write_vec(snap.mineral_type);
    buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&snap.starter_spawn_x), reinterpret_cast<const uint8_t*>(&snap.starter_spawn_x) + sizeof(int32_t));
    buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&snap.starter_spawn_y), reinterpret_cast<const uint8_t*>(&snap.starter_spawn_y) + sizeof(int32_t));
    buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&snap.starter_pond_volume), reinterpret_cast<const uint8_t*>(&snap.starter_pond_volume) + sizeof(int64_t));
    buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&snap.starter_pond_max), reinterpret_cast<const uint8_t*>(&snap.starter_pond_max) + sizeof(int64_t));
    
    // Hydraulic Reservoirs
    uint32_t num_res = (uint32_t)snap.hydraulic_reservoirs.size();
    buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&num_res), reinterpret_cast<const uint8_t*>(&num_res) + sizeof(uint32_t));
    for (const auto& r : snap.hydraulic_reservoirs) {
        buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&r.id), reinterpret_cast<const uint8_t*>(&r.id) + sizeof(uint32_t));
        buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&r.current_volume), reinterpret_cast<const uint8_t*>(&r.current_volume) + sizeof(int64_t));
        buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&r.max_capacity), reinterpret_cast<const uint8_t*>(&r.max_capacity) + sizeof(int64_t));
        buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&r.water_level), reinterpret_cast<const uint8_t*>(&r.water_level) + sizeof(int64_t));
        buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&r.tile_count), reinterpret_cast<const uint8_t*>(&r.tile_count) + sizeof(int32_t));
        buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&r.inflow_rate), reinterpret_cast<const uint8_t*>(&r.inflow_rate) + sizeof(int64_t));
        buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&r.outflow_rate), reinterpret_cast<const uint8_t*>(&r.outflow_rate) + sizeof(int64_t));
        buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&r.extraction_rate), reinterpret_cast<const uint8_t*>(&r.extraction_rate) + sizeof(int64_t));
        buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&r.seepage_rate), reinterpret_cast<const uint8_t*>(&r.seepage_rate) + sizeof(int64_t));
        buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&r.is_infinite), reinterpret_cast<const uint8_t*>(&r.is_infinite) + sizeof(bool));
        buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&r.is_dry), reinterpret_cast<const uint8_t*>(&r.is_dry) + sizeof(bool));
        write_vec(r.tile_indices);
    }
    
    // Hydraulic River Belts
    uint32_t num_belts = (uint32_t)snap.hydraulic_river_belts.size();
    buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&num_belts), reinterpret_cast<const uint8_t*>(&num_belts) + sizeof(uint32_t));
    for (const auto& b : snap.hydraulic_river_belts) {
        buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&b.id), reinterpret_cast<const uint8_t*>(&b.id) + sizeof(uint32_t));
        buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&b.from_node), reinterpret_cast<const uint8_t*>(&b.from_node) + sizeof(uint32_t));
        buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&b.to_node), reinterpret_cast<const uint8_t*>(&b.to_node) + sizeof(uint32_t));
        buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&b.upstream_belt_id), reinterpret_cast<const uint8_t*>(&b.upstream_belt_id) + sizeof(uint32_t));
        buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&b.downstream_belt_id), reinterpret_cast<const uint8_t*>(&b.downstream_belt_id) + sizeof(uint32_t));
        buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&b.tile_count), reinterpret_cast<const uint8_t*>(&b.tile_count) + sizeof(int32_t));
        buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&b.max_rate), reinterpret_cast<const uint8_t*>(&b.max_rate) + sizeof(int64_t));
        buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&b.pump_rate), reinterpret_cast<const uint8_t*>(&b.pump_rate) + sizeof(int64_t));
        buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&b.flow_rate), reinterpret_cast<const uint8_t*>(&b.flow_rate) + sizeof(int64_t));
        buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&b.water_level), reinterpret_cast<const uint8_t*>(&b.water_level) + sizeof(int64_t));
        buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&b.is_dry), reinterpret_cast<const uint8_t*>(&b.is_dry) + sizeof(bool));
        write_vec(b.tile_indices);
    }

    return buffer;
}

/**
 * Reconstructs a BisectSnapshot from a binary buffer.
 */
const uint8_t* SimWorld::deserialize_snapshot(BisectSnapshot &snap, const uint8_t *ptr) {
    auto read_pod = [&](auto& val) {
        memcpy(&val, ptr, sizeof(val));
        ptr += sizeof(val);
    };
    auto read_vec = [&](auto& vec) {
        uint32_t size;
        memcpy(&size, ptr, sizeof(uint32_t));
        ptr += sizeof(uint32_t);
        vec.resize(size);
        if (size > 0) {
            memcpy(vec.data(), ptr, size * sizeof(vec[0]));
            ptr += size * sizeof(vec[0]);
        }
    };

    read_pod(snap.tick);
    read_pod(snap.next_id);
    read_pod(snap.shotgun_seed);
    read_pod(snap.tornado_seed);
    read_pod(snap.crit_seed);

    read_vec(snap.chaos.dense_ids);
    read_vec(snap.chaos.node_ids);
    read_vec(snap.chaos.tree_min_x);
    read_vec(snap.chaos.tree_min_y);
    read_vec(snap.chaos.tree_max_x);
    read_vec(snap.chaos.tree_max_y);
    read_vec(snap.chaos.positions_x);
    read_vec(snap.chaos.positions_y);
    read_vec(snap.chaos.velocities_x);
    read_vec(snap.chaos.velocities_y);
    read_vec(snap.chaos.radii);
    read_vec(snap.chaos.frictions);
    read_vec(snap.chaos.healths);
    read_vec(snap.chaos.pierces);
    read_vec(snap.chaos.types);
    read_vec(snap.chaos.tags);
    read_vec(snap.chaos.flags);
    read_vec(snap.heavy_stats);
    read_vec(snap.heavy_equip);
    read_vec(snap.entity_recycle_bin);
    read_vec(snap.attack_recycle_bin);
    read_vec(snap.hit_pairs);

    // Map properties for farming game
    read_pod(snap.map_width);
    read_pod(snap.map_height);
    read_pod(snap.cell_size_fixed);
    read_vec(snap.water_table);
    read_vec(snap.aquifers);
    read_vec(snap.rivers);
    read_vec(snap.lakes);
    read_vec(snap.structure_type);
    read_vec(snap.water_pressure);
    read_vec(snap.actuator_activated);
    read_vec(snap.bot_states);
    read_vec(snap.chunk_tiers);
    read_vec(snap.soil_nitrogen);
    read_vec(snap.soil_phosphorus);
    read_vec(snap.soil_potassium);
    read_vec(snap.soil_calcium);
    read_vec(snap.soil_magnesium);
    read_vec(snap.soil_sulfur);
    read_vec(snap.soil_compactness);
    read_vec(snap.crop_type);
    read_vec(snap.crop_growth);
    read_vec(snap.crop_health);
    read_vec(snap.crop_prefix);
    read_vec(snap.crop_suffix);
    read_vec(snap.crop_implicit);
    read_vec(snap.planted_tile_indices);
    read_pod(snap.weather_state);
    read_pod(snap.weather_timer);
    read_pod(snap.co2_level);
    read_vec(snap.elevation);
    read_vec(snap.biome);
    read_vec(snap.surface_type);
    read_vec(snap.mineral_type);
    read_pod(snap.starter_spawn_x);
    read_pod(snap.starter_spawn_y);
    read_pod(snap.starter_pond_volume);
    read_pod(snap.starter_pond_max);

    // Hydraulic Reservoirs
    uint32_t num_res = 0;
    read_pod(num_res);
    snap.hydraulic_reservoirs.resize(num_res);
    for (uint32_t i = 0; i < num_res; ++i) {
        auto& r = snap.hydraulic_reservoirs[i];
        read_pod(r.id);
        read_pod(r.current_volume);
        read_pod(r.max_capacity);
        read_pod(r.water_level);
        read_pod(r.tile_count);
        read_pod(r.inflow_rate);
        read_pod(r.outflow_rate);
        read_pod(r.extraction_rate);
        read_pod(r.seepage_rate);
        read_pod(r.is_infinite);
        read_pod(r.is_dry);
        read_vec(r.tile_indices);
    }

    // Hydraulic River Belts
    uint32_t num_belts = 0;
    read_pod(num_belts);
    snap.hydraulic_river_belts.resize(num_belts);
    for (uint32_t i = 0; i < num_belts; ++i) {
        auto& b = snap.hydraulic_river_belts[i];
        read_pod(b.id);
        read_pod(b.from_node);
        read_pod(b.to_node);
        read_pod(b.upstream_belt_id);
        read_pod(b.downstream_belt_id);
        read_pod(b.tile_count);
        read_pod(b.max_rate);
        read_pod(b.pump_rate);
        read_pod(b.flow_rate);
        read_pod(b.water_level);
        read_pod(b.is_dry);
        read_vec(b.tile_indices);
    }

    return ptr;
}

/**
 * Checks if a specific attacker has already hit a target during its current lifetime.
 * Uses both the permanent history and the frame-local temporary buffer.
 */
bool SimWorld::has_hit(EntityID attacker_id, EntityID target_id) const {
    uint64_t pair = ((uint64_t)attacker_id << 32) | (uint32_t)target_id;
    if (std::binary_search(_hit_pairs.begin(), _hit_pairs.end(), pair)) return true;
    return _tick_hit_pairs.contains(pair);
}

/**
 * Records a hit for the current frame.
 */
void SimWorld::add_hit(EntityID attacker_id, EntityID target_id) {
    uint64_t pair = ((uint64_t)attacker_id << 32) | (uint32_t)target_id;
    _tick_hit_pairs.insert(pair);
}

void SimWorld::update_chunk_tiers() {
    if (_map_width <= 0 || _map_height <= 0 || _cell_size_fixed <= 0) {
        return;
    }
    int32_t chunk_w = (_map_width + CHUNK_SIZE - 1) / CHUNK_SIZE;
    int32_t chunk_h = (_map_height + CHUNK_SIZE - 1) / CHUNK_SIZE;
    size_t total_chunks = (size_t)chunk_w * chunk_h;
    
    if (_chunk_tiers.size() != total_chunks) {
        _chunk_tiers.assign(total_chunks, TIER_FROZEN);
    } else {
        std::fill(_chunk_tiers.begin(), _chunk_tiers.end(), TIER_FROZEN);
    }

    ChaosState& chaos = _entities.get_chaos();
    
    std::vector<std::pair<int32_t, int32_t>> player_chunks;
    for (size_t i = 0; i < chaos.size(); ++i) {
        if (chaos.types[i] == EntityType::PLAYER && !(chaos.flags[i] & (uint8_t)EntityFlags::DEAD)) {
            int64_t px = chaos.positions_x[i];
            int64_t py = chaos.positions_y[i];
            
            int32_t cell_x = static_cast<int32_t>(px / _cell_size_fixed);
            if (px < 0 && px % _cell_size_fixed != 0) cell_x--;
            int32_t cell_y = static_cast<int32_t>(py / _cell_size_fixed);
            if (py < 0 && py % _cell_size_fixed != 0) cell_y--;
            
            int32_t pcx = cell_x / CHUNK_SIZE;
            if (cell_x < 0 && cell_x % CHUNK_SIZE != 0) pcx--;
            int32_t pcy = cell_y / CHUNK_SIZE;
            if (cell_y < 0 && cell_y % CHUNK_SIZE != 0) pcy--;
            
            player_chunks.push_back({pcx, pcy});
        }
    }

    if (player_chunks.empty()) {
        std::fill(_chunk_tiers.begin(), _chunk_tiers.end(), TIER_SLOW);
        return;
    }

    for (int32_t cy = 0; cy < chunk_h; ++cy) {
        for (int32_t cx = 0; cx < chunk_w; ++cx) {
            int32_t min_dist = 999999;
            for (const auto& pc : player_chunks) {
                int32_t dist = std::max(std::abs(cx - pc.first), std::abs(cy - pc.second));
                if (dist < min_dist) {
                    min_dist = dist;
                }
            }
            
            uint8_t tier = TIER_FROZEN;
            if (min_dist <= 1) {
                tier = TIER_ACTIVE;
            } else if (min_dist <= 5) {
                tier = TIER_SLOW;
            }
            
            _chunk_tiers[cy * chunk_w + cx] = tier;
        }
    }
}

void SimWorld::tick_environment() {
    if (_map_width <= 0 || _map_height <= 0 || _cell_size_fixed <= 0) {
        return;
    }

    // A. Update atmospheric CO2 Cycle
    _co2_level += 2; // base global background increase
    
    // Pump and bot emissions
    for (int32_t i = 0; i < _map_width * _map_height; ++i) {
        if (_structure_type[i] == (uint8_t)StructureType::PUMP && _water_pressure[i] > 0) {
            _co2_level += 15;
        }
    }
    for (const auto& b : _bot_states) {
        if (b.bot_type != (uint8_t)BotType::IOT_NODE) {
            _co2_level += 10;
        }
    }
    // Crop absorption
    for (int32_t c_idx : _planted_tile_indices) {
        if (_crop_type[c_idx] != 0) {
            int64_t absorption = 8;
            if (_crop_implicit[c_idx] == (uint8_t)CropImplicit::WILD) {
                absorption = 16;
            }
            _co2_level -= absorption;
        }
    }
    // Clamp CO2 (100 to 1500 ppm)
    if (_co2_level < 100 * ONE) _co2_level = 100 * ONE;
    if (_co2_level > 1500 * ONE) _co2_level = 1500 * ONE;

    // B. Weather Timer and state transitions
    _weather_timer--;
    if (_weather_timer <= 0) {
        SplitMix64 wrng(_shotgun_seed + _tick);
        int32_t roll = wrng.next_range(0, 99);
        int32_t ppm = _co2_level / ONE;
        
        if (ppm < 400) { // low co2: sunny/clear with gentle rain
            if (roll < 80) _weather_state = (uint8_t)WeatherState::CLEAR;
            else _weather_state = (uint8_t)WeatherState::RAIN;
        } else if (ppm < 800) { // medium co2: clear, rain, storms, early acid rain
            if (roll < 40) _weather_state = (uint8_t)WeatherState::CLEAR;
            else if (roll < 75) _weather_state = (uint8_t)WeatherState::RAIN;
            else if (roll < 90) _weather_state = (uint8_t)WeatherState::STORM;
            else if (roll < 98) _weather_state = (uint8_t)WeatherState::ACID_RAIN;
            else _weather_state = (uint8_t)WeatherState::FLOOD;
        } else { // high co2: extreme storms, floods, and acid rain dominate!
            if (roll < 10) _weather_state = (uint8_t)WeatherState::CLEAR;
            else if (roll < 30) _weather_state = (uint8_t)WeatherState::RAIN;
            else if (roll < 60) _weather_state = (uint8_t)WeatherState::STORM;
            else if (roll < 90) _weather_state = (uint8_t)WeatherState::ACID_RAIN;
            else _weather_state = (uint8_t)WeatherState::FLOOD;
        }
        _weather_timer = wrng.next_range(300, 800);
    }

    // C. STORM Lightning Strike check (0.5% chance)
    if (_weather_state == (uint8_t)WeatherState::STORM) {
        SplitMix64 lrng(_shotgun_seed + _tick + 77);
        if (lrng.next_range(0, 999) < 5) {
            int32_t rx = lrng.next_range(0, _map_width - 1);
            int32_t ry = lrng.next_range(0, _map_height - 1);
            int32_t r_idx = ry * _map_width + rx;
            if (_crop_type[r_idx] != 0) {
                damage_crop(r_idx, 9999 * ONE);
            }
            if (_structure_type[r_idx] != 0) {
                _structure_type[r_idx] = 0;
                _water_pressure[r_idx] = 0;
            }
        }
    }

    // 1. Calculate water pressure across structures
    // Initialize/reset non-source pressures
    for (int32_t i = 0; i < _map_width * _map_height; ++i) {
        uint8_t type = _structure_type[i];
        if (type == (uint8_t)StructureType::NONE) {
            _water_pressure[i] = 0;
        } else if (_actuator_activated[i]) {
            _water_pressure[i] = static_cast<int32_t>(100 * ONE);
        } else if (type == (uint8_t)StructureType::WELL) {
            _water_pressure[i] = _aquifers[i] ? static_cast<int32_t>(100 * ONE) : 0;
        } else if (type == (uint8_t)StructureType::PUMP) {
            int32_t x = i % _map_width;
            int32_t y = i / _map_width;
            bool adjacent_water = false;
            int dxs[] = {1, -1, 0, 0};
            int dys[] = {0, 0, 1, -1};
            for (int n = 0; n < 4; ++n) {
                int32_t nx = x + dxs[n];
                int32_t ny = y + dys[n];
                if (nx >= 0 && nx < _map_width && ny >= 0 && ny < _map_height) {
                    int32_t n_idx = ny * _map_width + nx;
                    if (_rivers[n_idx] != 0 || _lakes[n_idx] != 0) {
                        adjacent_water = true;
                        break;
                    }
                }
            }
            _water_pressure[i] = adjacent_water ? static_cast<int32_t>(100 * ONE) : 0;
        } else {
            _water_pressure[i] = 0;
        }
    }

    // Propagate pressure (12 iterations)
    std::vector<int32_t> next_pressures = _water_pressure;
    for (int iter = 0; iter < 12; ++iter) {
        bool changed = false;
        for (int32_t y = 0; y < _map_height; ++y) {
            for (int32_t x = 0; x < _map_width; ++x) {
                int32_t idx = y * _map_width + x;
                uint8_t type = _structure_type[idx];
                if (type == (uint8_t)StructureType::NONE || type == (uint8_t)StructureType::WELL || type == (uint8_t)StructureType::PUMP) {
                    continue;
                }

                int32_t max_p = 0;
                int dxs[] = {1, -1, 0, 0};
                int dys[] = {0, 0, 1, -1};
                for (int n = 0; n < 4; ++n) {
                    int32_t nx = x + dxs[n];
                    int32_t ny = y + dys[n];
                    if (nx >= 0 && nx < _map_width && ny >= 0 && ny < _map_height) {
                        int32_t n_idx = ny * _map_width + nx;
                        if (_water_pressure[n_idx] > max_p) {
                            max_p = _water_pressure[n_idx];
                        }
                    }
                }

                int32_t loss = static_cast<int32_t>(5 * ONE);
                if (type == (uint8_t)StructureType::CHANNEL) {
                    loss = static_cast<int32_t>(12 * ONE);
                }

                int32_t next_p = max_p - loss;
                if (next_p < 0) next_p = 0;

                if (next_pressures[idx] != next_p) {
                    next_pressures[idx] = next_p;
                    changed = true;
                }
            }
        }
        _water_pressure = next_pressures;
        if (!changed) break;
    }

    int32_t chunk_w = (_map_width + CHUNK_SIZE - 1) / CHUNK_SIZE;
    int32_t chunk_h = (_map_height + CHUNK_SIZE - 1) / CHUNK_SIZE;

    std::vector<int32_t> next_wt = _water_table;
    bool any_updated = false;

    for (int32_t y = 0; y < _map_height; ++y) {
        for (int32_t x = 0; x < _map_width; ++x) {
            int32_t cx = x / CHUNK_SIZE;
            int32_t cy = y / CHUNK_SIZE;
            
            if (cx < 0 || cx >= chunk_w || cy < 0 || cy >= chunk_h) continue;

            uint8_t tier = _chunk_tiers[cy * chunk_w + cx];
            if (tier == TIER_FROZEN) continue;

            int32_t dt = 0;
            if (tier == TIER_ACTIVE) {
                dt = 1;
            } else if (tier == TIER_SLOW) {
                if (_tick % 30 == 0) {
                    dt = 30;
                }
            }

            if (dt > 0) {
                any_updated = true;
                int64_t idx = (int64_t)y * _map_width + x;
                int64_t diff_sum = 0;
                int dxs[] = {1, -1, 0, 0};
                int dys[] = {0, 0, 1, -1};
                for (int n = 0; n < 4; ++n) {
                    int32_t nx = x + dxs[n];
                    int32_t ny = y + dys[n];
                    if (nx >= 0 && nx < _map_width && ny >= 0 && ny < _map_height) {
                        diff_sum += _water_table[(int64_t)ny * _map_width + nx] - _water_table[idx];
                    }
                }

                int64_t diffusion = (327LL * dt * diff_sum) / 65536;
                int64_t next_val = _water_table[idx];

                if (_aquifers[idx]) {
                    int64_t replenish = ((70LL * ONE - next_val) * dt) / 120;
                    next_val += replenish;
                }

                if (_rivers[idx] == 0 && _lakes[idx] == 0) {
                    int64_t evap = 327LL * dt;
                    if (_weather_state == (uint8_t)WeatherState::RAIN || _weather_state == (uint8_t)WeatherState::ACID_RAIN || _weather_state == (uint8_t)WeatherState::STORM) {
                        evap = 0; // No evaporation during rain
                    } else if (_weather_state == (uint8_t)WeatherState::CLEAR) {
                        evap = 450LL * dt; // Clear sun evaporates more
                    }
                    next_val -= evap;
                } else if (_rivers[idx] != 0) {
                    if (next_val < 85 * ONE) {
                        next_val = 85 * ONE;
                    }
                } else if (_lakes[idx] != 0) {
                    if (next_val < 95 * ONE) {
                        next_val = 95 * ONE;
                    }
                }

                // Check if this tile is within the river/lake floodplain (distance <= 3)
                bool near_floodplain = false;
                for (int32_t dy = -3; dy <= 3; ++dy) {
                    for (int32_t dx = -3; dx <= 3; ++dx) {
                        int32_t nx = x + dx;
                        int32_t ny = y + dy;
                        if (nx >= 0 && nx < _map_width && ny >= 0 && ny < _map_height) {
                            int32_t n_idx = ny * _map_width + nx;
                            if (_rivers[n_idx] != 0 || _lakes[n_idx] != 0) {
                                near_floodplain = true;
                                break;
                            }
                        }
                    }
                    if (near_floodplain) break;
                }

                // Apply Weather precipitation directly to water table
                if (_weather_state == (uint8_t)WeatherState::RAIN || _weather_state == (uint8_t)WeatherState::ACID_RAIN) {
                    next_val += 150LL * dt;
                } else if (_weather_state == (uint8_t)WeatherState::STORM) {
                    next_val += 400LL * dt;
                } else if (_weather_state == (uint8_t)WeatherState::FLOOD) {
                    if (near_floodplain) {
                        next_val = 92LL * ONE; // flood submerges floodplain ground
                    } else {
                        next_val += 150LL * dt; // gentle rain elsewhere
                    }
                }

                // Apply Acid Rain / Flood crop damage and soil changes
                if (dt > 0) {
                    if (_weather_state == (uint8_t)WeatherState::ACID_RAIN) {
                        int64_t new_comp = _soil_compactness[idx] + 20LL * dt;
                        if (new_comp > 100 * ONE) new_comp = 100 * ONE;
                        _soil_compactness[idx] = static_cast<int32_t>(new_comp);

                        if (_crop_type[idx] != 0) {
                            bool resilient = (_crop_implicit[idx] == (uint8_t)CropImplicit::ORGANIC) || (_crop_suffix[idx] == (uint8_t)CropSuffix::TUBEROUS_ROOT);
                            if (!resilient) {
                                damage_crop(static_cast<int32_t>(idx), static_cast<int32_t>(300LL * dt));
                            }
                        }
                    } else if (_weather_state == (uint8_t)WeatherState::FLOOD) {
                        if (near_floodplain) {
                            // Drown non-adventitious crops
                            if (_crop_type[idx] != 0) {
                                bool resilient = (_crop_suffix[idx] == (uint8_t)CropSuffix::ADVENTITIOUS_ROOT);
                                if (!resilient) {
                                    damage_crop(static_cast<int32_t>(idx), static_cast<int32_t>(1000LL * dt));
                                }
                            }
                            // Flood silting/depositing minerals and nitrogen (fertility increase)
                            auto add_clamp = [](int32_t& current, int64_t amount) {
                                int64_t val = (int64_t)current + amount;
                                if (val > 100 * ONE) val = 100 * ONE;
                                current = static_cast<int32_t>(val);
                            };
                            add_clamp(_soil_nitrogen[idx], 120LL * dt);
                            add_clamp(_soil_phosphorus[idx], 80LL * dt);
                            add_clamp(_soil_potassium[idx], 80LL * dt);
                        }
                    }
                }

                // Add structure irrigation
                int32_t channel_effect = 0;
                if (_structure_type[idx] == (uint8_t)StructureType::CHANNEL && _water_pressure[idx] > 0) {
                    channel_effect = _water_pressure[idx];
                } else {
                    for (int n = 0; n < 4; ++n) {
                        int32_t nx = x + dxs[n];
                        int32_t ny = y + dys[n];
                        if (nx >= 0 && nx < _map_width && ny >= 0 && ny < _map_height) {
                            int32_t n_idx = ny * _map_width + nx;
                            if (_structure_type[n_idx] == (uint8_t)StructureType::CHANNEL && _water_pressure[n_idx] > channel_effect) {
                                channel_effect = _water_pressure[n_idx];
                            }
                        }
                    }
                }
                if (channel_effect > 0) {
                    int64_t seep = (channel_effect * dt) / 300;
                    if (next_val < 80 * ONE) {
                        next_val += seep;
                        if (next_val > 80 * ONE) next_val = 80 * ONE;
                    }
                }

                int32_t sprinkler_effect = 0;
                int32_t pivot_effect = 0;

                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        int32_t nx = x + dx;
                        int32_t ny = y + dy;
                        if (nx >= 0 && nx < _map_width && ny >= 0 && ny < _map_height) {
                            int32_t n_idx = ny * _map_width + nx;
                            if (_structure_type[n_idx] == (uint8_t)StructureType::SPRINKLER_SMALL && _water_pressure[n_idx] > 0) {
                                sprinkler_effect = std::max(sprinkler_effect, _water_pressure[n_idx]);
                            }
                        }
                    }
                }

                for (int dy = -3; dy <= 3; ++dy) {
                    for (int dx = -3; dx <= 3; ++dx) {
                        if (dx * dx + dy * dy > 9) continue;
                        int32_t nx = x + dx;
                        int32_t ny = y + dy;
                        if (nx >= 0 && nx < _map_width && ny >= 0 && ny < _map_height) {
                            int32_t n_idx = ny * _map_width + nx;
                            if (_structure_type[n_idx] == (uint8_t)StructureType::SPRINKLER_PIVOT && _water_pressure[n_idx] >= 50 * ONE) {
                                pivot_effect = std::max(pivot_effect, _water_pressure[n_idx]);
                            }
                        }
                    }
                }

                if (sprinkler_effect > 0) {
                    int64_t spray = (15LL * dt * ONE) / 1000;
                    if (next_val < 85 * ONE) {
                        next_val += spray;
                        if (next_val > 85 * ONE) next_val = 85 * ONE;
                    }
                }

                if (pivot_effect > 0) {
                    int64_t spray = (30LL * dt * ONE) / 1000;
                    if (next_val < 90 * ONE) {
                        next_val += spray;
                        if (next_val > 90 * ONE) next_val = 90 * ONE;
                    }
                }

                next_val += diffusion;

                if (next_val < 0) next_val = 0;
                if (next_val > 100 * ONE) next_val = 100 * ONE;

                next_wt[idx] = static_cast<int32_t>(next_val);
            }
        }
    }

    if (any_updated) {
        _water_table = std::move(next_wt);
    }

    // D. Tick Hydraulic Graph (Belt & Reservoir Simulation)
    int64_t rain_rate = 0;
    if (_weather_state == (uint8_t)WeatherState::RAIN || _weather_state == (uint8_t)WeatherState::ACID_RAIN) {
        rain_rate = 10LL * ONE;
    } else if (_weather_state == (uint8_t)WeatherState::STORM || _weather_state == (uint8_t)WeatherState::FLOOD) {
        rain_rate = 30LL * ONE;
    }
    _hydraulic_graph.tick_hydrology(1, _weather_state, rain_rate);

    if (!_hydraulic_graph.get_reservoirs().empty()) {
        _starter_pond_volume = _hydraulic_graph.get_reservoirs()[0].current_volume;
    }

    // Sync dry state to surface types
    for (const auto& r : _hydraulic_graph.get_reservoirs()) {
        if (r.is_infinite) continue;
        if (r.is_dry) {
            for (int32_t idx : r.tile_indices) {
                if (idx >= 0 && idx < (int32_t)_surface_type.size()) {
                    if (_surface_type[idx] == (uint8_t)Generation::MicroSurfaceType::WATER_DEEP) {
                        _surface_type[idx] = (uint8_t)Generation::MicroSurfaceType::MUD_BANK;
                    }
                    _water_pressure[idx] = 0;
                }
            }
        } else {
            for (int32_t idx : r.tile_indices) {
                if (idx >= 0 && idx < (int32_t)_surface_type.size()) {
                    if (_surface_type[idx] == (uint8_t)Generation::MicroSurfaceType::MUD_BANK) {
                        _surface_type[idx] = (uint8_t)Generation::MicroSurfaceType::WATER_DEEP;
                    }
                }
            }
        }
    }

    for (const auto& b : _hydraulic_graph.get_river_belts()) {
        if (b.is_dry) {
            for (int32_t idx : b.tile_indices) {
                if (idx >= 0 && idx < (int32_t)_surface_type.size()) {
                    if (_surface_type[idx] == (uint8_t)Generation::MicroSurfaceType::WATER_DEEP ||
                        _surface_type[idx] == (uint8_t)Generation::MicroSurfaceType::WATER_SHALLOW) {
                        _surface_type[idx] = (uint8_t)Generation::MicroSurfaceType::MUD_BANK;
                    }
                    _water_pressure[idx] = 0;
                }
            }
        } else {
            for (int32_t idx : b.tile_indices) {
                if (idx >= 0 && idx < (int32_t)_surface_type.size()) {
                    if (_surface_type[idx] == (uint8_t)Generation::MicroSurfaceType::MUD_BANK) {
                        if (_rivers[idx] == 2) {
                            _surface_type[idx] = (uint8_t)Generation::MicroSurfaceType::WATER_DEEP;
                        } else {
                            _surface_type[idx] = (uint8_t)Generation::MicroSurfaceType::WATER_SHALLOW;
                        }
                    }
                }
            }
        }
    }
}

void SimWorld::plant_crop(int32_t x, int32_t y, uint8_t crop_type, uint8_t prefix, uint8_t suffix, uint8_t implicit) {
    if (x < 0 || x >= _map_width || y < 0 || y >= _map_height) return;
    int32_t idx = y * _map_width + x;
    if (_crop_type[idx] == 0) {
        // Add to planted index list if it wasn't planted
        _planted_tile_indices.push_back(idx);
        std::sort(_planted_tile_indices.begin(), _planted_tile_indices.end());
    }
    _crop_type[idx] = crop_type;
    _crop_growth[idx] = 0;
    _crop_health[idx] = static_cast<int32_t>(100 * ONE);
    _crop_prefix[idx] = prefix;
    _crop_suffix[idx] = suffix;
    _crop_implicit[idx] = implicit;
}

void SimWorld::harvest_crop(int32_t x, int32_t y) {
    if (x < 0 || x >= _map_width || y < 0 || y >= _map_height) return;
    int32_t idx = y * _map_width + x;
    if (_crop_type[idx] != 0) {
        _crop_type[idx] = 0;
        _crop_growth[idx] = 0;
        _crop_health[idx] = 0;
        _crop_prefix[idx] = 0;
        _crop_suffix[idx] = 0;
        _crop_implicit[idx] = 0;
        // Remove from planted list
        auto it = std::lower_bound(_planted_tile_indices.begin(), _planted_tile_indices.end(), idx);
        if (it != _planted_tile_indices.end() && *it == idx) {
            _planted_tile_indices.erase(it);
        }
    }
}

void SimWorld::tick_crops() {
    if (_map_width <= 0 || _map_height <= 0) return;
    
    int32_t chunk_w = (_map_width + CHUNK_SIZE - 1) / CHUNK_SIZE;
    int32_t chunk_h = (_map_height + CHUNK_SIZE - 1) / CHUNK_SIZE;
    size_t total_cells = (size_t)_map_width * _map_height;
    
    // Determine whether to use sparse iteration or dense scan
    // Threshold is 10% of total map cells
    bool use_sparse = (_planted_tile_indices.size() * 10 < total_cells);
    std::vector<int32_t> new_crops;
    
    auto update_crop_cell = [&](int32_t idx) {
        if (_crop_type[idx] == 0) return;
        
        int32_t x = idx % _map_width;
        int32_t y = idx / _map_width;
        int32_t cx = x / CHUNK_SIZE;
        int32_t cy = y / CHUNK_SIZE;
        
        if (cx < 0 || cx >= chunk_w || cy < 0 || cy >= chunk_h) return;
        
        uint8_t tier = _chunk_tiers[cy * chunk_w + cx];
        if (tier == TIER_FROZEN) return;
        
        int32_t dt = 0;
        if (tier == TIER_ACTIVE) {
            // Temporal interleaving: update 1/60th of the active crops on each frame
            if (idx % 60 == static_cast<int32_t>(_tick % 60)) {
                dt = 60;
            }
        } else if (tier == TIER_SLOW) {
            // Update TIER_SLOW crops less frequently
            if (_tick % 600 == 0) {
                dt = 600;
            }
        }
        
        if (dt > 0) {
            int64_t G_base = (ONE * dt) / 20; // grows fully in 20 seconds
            
            CropImplicit implicit = static_cast<CropImplicit>(_crop_implicit[idx]);
            CropPrefix prefix = static_cast<CropPrefix>(_crop_prefix[idx]);
            CropSuffix suffix = static_cast<CropSuffix>(_crop_suffix[idx]);

            // Apply Implicit growth rate modifiers
            if (implicit == CropImplicit::WILD) {
                G_base = (G_base * 80) / 100; // -20%
                
                // Wild fixes Nitrogen to soil
                int64_t n_fixed = (1LL * dt * ONE) / 1000;
                int64_t val = (int64_t)_soil_nitrogen[idx] + n_fixed;
                if (val > 100 * ONE) val = 100 * ONE;
                _soil_nitrogen[idx] = static_cast<int32_t>(val);
            } else if (implicit == CropImplicit::DOMESTICATED) {
                G_base = (G_base * 120) / 100; // +20%
            }

            // Apply Prefix growth rate modifiers
            if (prefix == CropPrefix::BROAD_LEAF) {
                G_base = (G_base * 120) / 100; // +20% from photosynthesis
            } else if (prefix == CropPrefix::SUCCULENT_FRUIT) {
                G_base = (G_base * 110) / 100; // +10% growth rate, +50% scion yield factor
            }

            // Apply Suffix growth modifiers
            if (suffix == CropSuffix::MYCORRHIZAL_ROOT) {
                // Symbiote generates Phosphorus
                int64_t p_fixed = (1LL * dt * ONE) / 1000;
                int64_t val = (int64_t)_soil_phosphorus[idx] + p_fixed;
                if (val > 100 * ONE) val = 100 * ONE;
                _soil_phosphorus[idx] = static_cast<int32_t>(val);
            }

            // Moisture Factor (optimal range 50% to 80%)
            int64_t moisture = _water_table[idx];
            int64_t M_factor = ONE;
            if (moisture < 50 * ONE) {
                int64_t penalty = ONE - div_fixed(moisture, 50 * ONE);
                // TUBEROUS_ROOT stores water, halves low moisture penalty
                if (suffix == CropSuffix::TUBEROUS_ROOT) {
                    penalty /= 2;
                }
                M_factor = ONE - penalty;
            } else if (moisture > 80 * ONE) {
                int64_t penalty = div_fixed(moisture - 80 * ONE, 20 * ONE);
                // ADVENTITIOUS_ROOT halves high moisture penalty
                if (suffix == CropSuffix::ADVENTITIOUS_ROOT) {
                    penalty /= 2;
                }
                M_factor = ONE - penalty;
                if (M_factor < 0) M_factor = 0;
            }

            // Organic gets half water penalty (double water resiliency)
            if (implicit == CropImplicit::ORGANIC && M_factor < ONE) {
                M_factor = ONE - (ONE - M_factor) / 2;
            }
            
            // Nutrient requirements & consumption rates per Crop Type
            int64_t req_n = 10 * ONE;
            int64_t req_p = 10 * ONE;
            int64_t req_k = 10 * ONE;
            int64_t req_ca = 5 * ONE;
            int64_t req_mg = 5 * ONE;
            int64_t req_s = 5 * ONE;

            int64_t consume_n = (2LL * dt * ONE) / 1000;
            int64_t consume_p = (2LL * dt * ONE) / 1000;
            int64_t consume_k = (2LL * dt * ONE) / 1000;
            int64_t consume_ca = (1LL * dt * ONE) / 1000;
            int64_t consume_mg = (1LL * dt * ONE) / 1000;
            int64_t consume_s = (1LL * dt * ONE) / 1000;

            uint8_t type = _crop_type[idx];
            if (type == 1) { // Wheat
                req_n = 15 * ONE; req_k = 15 * ONE; req_ca = 8 * ONE;
                consume_n *= 2; consume_k *= 2; consume_ca *= 2;
            } else if (type == 2) { // Corn
                req_n = 20 * ONE; req_p = 15 * ONE; req_mg = 8 * ONE;
                consume_n = (consume_n * 5) / 2; consume_p *= 2; consume_mg *= 2;
            } else if (type == 3) { // Soy
                req_n = 4 * ONE; req_p = 18 * ONE; req_k = 12 * ONE; req_s = 10 * ONE;
                consume_n /= 2; consume_p = (consume_p * 5) / 2; consume_k = (consume_k * 3) / 2; consume_s *= 3;
            }

            // Apply Implicit nutrient requirement modifiers
            if (implicit == CropImplicit::DOMESTICATED) {
                req_n = (req_n * 120) / 100;
                req_p = (req_p * 120) / 100;
                req_k = (req_k * 120) / 100;
                req_ca = (req_ca * 120) / 100;
                req_mg = (req_mg * 120) / 100;
                req_s = (req_s * 120) / 100;
                
                consume_n = (consume_n * 120) / 100;
                consume_p = (consume_p * 120) / 100;
                consume_k = (consume_k * 120) / 100;
                consume_ca = (consume_ca * 120) / 100;
                consume_mg = (consume_mg * 120) / 100;
                consume_s = (consume_s * 120) / 100;
            }

            // Apply Suffix root nutrient absorption modifiers
            if (suffix == CropSuffix::FIBROUS_ROOT) {
                consume_n = (consume_n * 70) / 100;
                consume_p = (consume_p * 70) / 100;
                consume_k = (consume_k * 70) / 100;
                consume_ca = (consume_ca * 70) / 100;
                consume_mg = (consume_mg * 70) / 100;
                consume_s = (consume_s * 70) / 100;
            }

            // Calculate factors for N, P, K, Ca, Mg, S
            auto calc_factor = [](int32_t val, int64_t req) {
                if (req <= 0) return ONE;
                if (val >= req) return ONE;
                return div_fixed(val, req);
            };

            int64_t N_factor = calc_factor(_soil_nitrogen[idx], req_n);
            int64_t P_factor = calc_factor(_soil_phosphorus[idx], req_p);
            int64_t K_factor = calc_factor(_soil_potassium[idx], req_k);
            int64_t Ca_factor = calc_factor(_soil_calcium[idx], req_ca);
            int64_t Mg_factor = calc_factor(_soil_magnesium[idx], req_mg);
            int64_t S_factor = calc_factor(_soil_sulfur[idx], req_s);
            
            // Compactness Factor (hinders above 50%)
            int64_t compactness = _soil_compactness[idx];
            int64_t C_factor = ONE;
            if (suffix != CropSuffix::TAP_ROOT) {
                if (compactness > 50 * ONE) {
                    C_factor = ONE - div_fixed(compactness - 50 * ONE, 100 * ONE);
                    if (C_factor < 13107) C_factor = 13107; // root growth clamped to min 20%
                }
            }
            
            // Calculate final growth rate
            int64_t growth_rate = mul_fixed(G_base, M_factor);
            growth_rate = mul_fixed(growth_rate, N_factor);
            growth_rate = mul_fixed(growth_rate, P_factor);
            growth_rate = mul_fixed(growth_rate, K_factor);
            growth_rate = mul_fixed(growth_rate, Ca_factor);
            growth_rate = mul_fixed(growth_rate, Mg_factor);
            growth_rate = mul_fixed(growth_rate, S_factor);
            growth_rate = mul_fixed(growth_rate, C_factor);
            
            _crop_growth[idx] += static_cast<int32_t>(growth_rate);
            if (_crop_growth[idx] > 100 * ONE) {
                _crop_growth[idx] = static_cast<int32_t>(100 * ONE);
            }
            
            // Consume nutrients from soil
            auto consume_nutrient = [](int32_t& current, int64_t amount) {
                int64_t val = (int64_t)current - amount;
                current = static_cast<int32_t>(val < 0 ? 0 : val);
            };

            consume_nutrient(_soil_nitrogen[idx], consume_n);
            consume_nutrient(_soil_phosphorus[idx], consume_p);
            consume_nutrient(_soil_potassium[idx], consume_k);
            consume_nutrient(_soil_calcium[idx], consume_ca);
            consume_nutrient(_soil_magnesium[idx], consume_mg);
            consume_nutrient(_soil_sulfur[idx], consume_s);
            
            // Health simulation (decay if too dry/flooded)
            int64_t health_decay = (dt * ONE) / 200;
            int64_t health_regen = (dt * ONE) / 500;
            
            if (prefix == CropPrefix::THICK_STEM) {
                health_decay = (health_decay * 70) / 100;
                health_regen = (health_regen * 130) / 100;
            }
            if (prefix == CropPrefix::THORNY_STEM) {
                health_decay = (health_decay * 70) / 100;
            }
            if (implicit == CropImplicit::HEIRLOOM) {
                health_decay = (health_decay * 90) / 100;
                health_regen = (health_regen * 110) / 100;
            }
            if (implicit == CropImplicit::ORGANIC) {
                health_decay = (health_decay * 70) / 100;
            }

            if (moisture < 10 * ONE || moisture > 95 * ONE) {
                _crop_health[idx] -= static_cast<int32_t>(health_decay);
            } else {
                _crop_health[idx] += static_cast<int32_t>(health_regen);
            }
            
            if (_crop_health[idx] < 0) {
                _crop_health[idx] = 0;
                // Crop dies: clear it
                _crop_type[idx] = 0;
                _crop_growth[idx] = 0;
                _crop_prefix[idx] = 0;
                _crop_suffix[idx] = 0;
                _crop_implicit[idx] = 0;
            } else if (_crop_health[idx] > 100 * ONE) {
                _crop_health[idx] = static_cast<int32_t>(100 * ONE);
            }

            // --- Breeding / Mutation Logic ---
            if (_crop_type[idx] != 0 && _crop_growth[idx] >= 100 * ONE) {
                SplitMix64 cell_rng(_crit_seed + _tick + idx);
                
                int32_t breed_check = (implicit == CropImplicit::HEIRLOOM) ? 150 : 300;
                
                if (cell_rng.next() % breed_check == 0) {
                    int32_t dx[4] = {-1, 1, 0, 0};
                    int32_t dy[4] = {0, 0, -1, 1};
                    int dir = cell_rng.next_range(0, 3);
                    int32_t nx = x + dx[dir];
                    int32_t ny = y + dy[dir];
                    
                    if (nx >= 0 && nx < _map_width && ny >= 0 && ny < _map_height) {
                        int32_t n_idx = ny * _map_width + nx;
                        if (_crop_type[n_idx] == 0) {
                            // Find partner adjacent to n_idx
                            int32_t partner_idx = -1;
                            for (int p_dir = 0; p_dir < 4; ++p_dir) {
                                int32_t px = nx + dx[p_dir];
                                int32_t py = ny + dy[p_dir];
                                if (px >= 0 && px < _map_width && py >= 0 && py < _map_height) {
                                    int32_t p_idx = py * _map_width + px;
                                    if (p_idx != idx && _crop_type[p_idx] != 0 && _crop_growth[p_idx] >= 100 * ONE) {
                                        partner_idx = p_idx;
                                        break;
                                    }
                                }
                            }

                            // If we have a partner, breed. Otherwise, 20% chance to self-pollinate.
                            bool should_breed = (partner_idx != -1) || (cell_rng.next() % 5 == 0);
                            if (should_breed) {
                                int32_t p_idx = (partner_idx != -1) ? partner_idx : idx;
                                
                                // Child crop type
                                uint8_t child_type = (cell_rng.next() % 2 == 0) ? _crop_type[idx] : _crop_type[p_idx];
                                
                                // Base mutation chance: 20%. Increased by WILD implicit and VARIEGATED_LEAF prefix.
                                uint32_t mutation_chance = 20;
                                if (_crop_implicit[idx] == static_cast<uint8_t>(CropImplicit::WILD)) {
                                    mutation_chance += 15;
                                }
                                if (_crop_implicit[p_idx] == static_cast<uint8_t>(CropImplicit::WILD)) {
                                    mutation_chance += 15;
                                }
                                if (_crop_prefix[idx] == static_cast<uint8_t>(CropPrefix::VARIEGATED_LEAF)) {
                                    mutation_chance += 15;
                                }
                                if (_crop_prefix[p_idx] == static_cast<uint8_t>(CropPrefix::VARIEGATED_LEAF)) {
                                    mutation_chance += 15;
                                }

                                // Implicit inheritance vs mutation
                                uint8_t child_implicit = 0;
                                if ((cell_rng.next() % 100) < mutation_chance) {
                                    child_implicit = static_cast<uint8_t>(cell_rng.next_range(1, static_cast<int>(CropImplicit::COUNT) - 1));
                                } else {
                                    child_implicit = (cell_rng.next() % 2 == 0) ? _crop_implicit[idx] : _crop_implicit[p_idx];
                                }

                                // Prefix inheritance vs mutation
                                uint8_t child_prefix = 0;
                                if ((cell_rng.next() % 100) < mutation_chance) {
                                    child_prefix = static_cast<uint8_t>(cell_rng.next_range(1, static_cast<int>(CropPrefix::COUNT) - 1));
                                } else {
                                    child_prefix = (cell_rng.next() % 2 == 0) ? _crop_prefix[idx] : _crop_prefix[p_idx];
                                }

                                // Suffix inheritance vs mutation
                                uint8_t child_suffix = 0;
                                if ((cell_rng.next() % 100) < mutation_chance) {
                                    child_suffix = static_cast<uint8_t>(cell_rng.next_range(1, static_cast<int>(CropSuffix::COUNT) - 1));
                                } else {
                                    child_suffix = (cell_rng.next() % 2 == 0) ? _crop_suffix[idx] : _crop_suffix[p_idx];
                                }

                                // Spawn the new hybrid seed
                                _crop_type[n_idx] = child_type;
                                _crop_growth[n_idx] = 0;
                                _crop_health[n_idx] = static_cast<int32_t>(100 * ONE);
                                _crop_prefix[n_idx] = child_prefix;
                                _crop_suffix[n_idx] = child_suffix;
                                _crop_implicit[n_idx] = child_implicit;
                                
                                new_crops.push_back(n_idx);
                            }
                        }
                    }
                }
            }
        }
    };
    
    if (use_sparse) {
        // Iterate only over planted tiles.
        size_t original_size = _planted_tile_indices.size();
        for (size_t i = 0; i < original_size; ++i) {
            update_crop_cell(_planted_tile_indices[i]);
        }
        // Clean up dead crops from _planted_tile_indices
        auto it = std::remove_if(_planted_tile_indices.begin(), _planted_tile_indices.end(), [&](int32_t idx) {
            return _crop_type[idx] == 0;
        });
        if (it != _planted_tile_indices.end()) {
            _planted_tile_indices.erase(it, _planted_tile_indices.end());
        }
    } else {
        // Dense scan
        for (size_t i = 0; i < total_cells; ++i) {
            update_crop_cell(static_cast<int32_t>(i));
        }
    }

    if (!new_crops.empty()) {
        for (int32_t n_idx : new_crops) {
            _planted_tile_indices.push_back(n_idx);
        }
        std::sort(_planted_tile_indices.begin(), _planted_tile_indices.end());
        _planted_tile_indices.erase(std::unique(_planted_tile_indices.begin(), _planted_tile_indices.end()), _planted_tile_indices.end());
    }
}

void SimWorld::apply_fertilizer(int32_t x, int32_t y, int64_t n, int64_t p, int64_t k, int64_t ca, int64_t mg, int64_t s) {
    if (x < 0 || x >= _map_width || y < 0 || y >= _map_height) return;
    int32_t idx = y * _map_width + x;

    auto add_clamp = [](int32_t& current, int64_t amount) {
        int64_t val = (int64_t)current + amount;
        if (val < 0) val = 0;
        if (val > 100 * ONE) val = 100 * ONE;
        current = static_cast<int32_t>(val);
    };

    add_clamp(_soil_nitrogen[idx], n);
    add_clamp(_soil_phosphorus[idx], p);
    add_clamp(_soil_potassium[idx], k);
    add_clamp(_soil_calcium[idx], ca);
    add_clamp(_soil_magnesium[idx], mg);
    add_clamp(_soil_sulfur[idx], s);
}

void SimWorld::_do_place_structure(int32_t x, int32_t y, uint8_t structure_type) {
    if (x < 0 || x >= _map_width || y < 0 || y >= _map_height) return;
    int32_t idx = y * _map_width + x;
    if (_crop_type[idx] != 0) return; // Cannot place on top of crop
    _structure_type[idx] = structure_type;
    _water_pressure[idx] = 0;

    // If placing a bridge
    if (structure_type == (uint8_t)StructureType::BRIDGE) {
        // Bridge placed
    } else if (structure_type == (uint8_t)StructureType::PUMP) {
        // Register pump extraction with hydraulic graph (1.5 units/tick)
        _hydraulic_graph.add_pump(idx, 15LL * ONE / 10);
    }
}

void SimWorld::_do_remove_structure(int32_t x, int32_t y) {
    if (x < 0 || x >= _map_width || y < 0 || y >= _map_height) return;
    int32_t idx = y * _map_width + x;
    if (_structure_type[idx] == (uint8_t)StructureType::BRIDGE) {
        // Bridge removed
    } else if (_structure_type[idx] == (uint8_t)StructureType::PUMP) {
        _hydraulic_graph.remove_pump(idx);
    }
    _structure_type[idx] = 0;
    _water_pressure[idx] = 0;
}

void SimWorld::place_structure(int32_t x, int32_t y, uint8_t structure_type) {
    _do_place_structure(x, y, structure_type);
}

void SimWorld::remove_structure(int32_t x, int32_t y) {
    _do_remove_structure(x, y);
}

void SimWorld::damage_crop(int32_t idx, int32_t amount) {
    if (idx < 0 || idx >= (int32_t)_crop_health.size()) return;
    if (_crop_type[idx] == 0) return;
    
    int64_t val = (int64_t)_crop_health[idx] - amount;
    if (val <= 0) {
        _crop_type[idx] = 0;
        _crop_growth[idx] = 0;
        _crop_health[idx] = 0;
        _crop_prefix[idx] = 0;
        _crop_suffix[idx] = 0;
        _crop_implicit[idx] = 0;
        auto it = std::lower_bound(_planted_tile_indices.begin(), _planted_tile_indices.end(), idx);
        if (it != _planted_tile_indices.end() && *it == idx) {
            _planted_tile_indices.erase(it);
        }
    } else {
        _crop_health[idx] = static_cast<int32_t>(val);
    }
}

void SimWorld::_do_spawn_bot(EntityID bot_id, uint8_t bot_type, int64_t x, int64_t y) {
    int64_t radius = 20LL * ONE;
    int32_t health = 100;
    _entities.spawn(bot_id, EntityType::BOT, x, y, radius, health, 0);
    uint32_t idx = _entities.get_dense_idx(bot_id);
    _entities.get_chaos().flags[idx] |= (uint8_t)EntityFlags::ACTIVE;

    _sync_node_aabb(idx);

    BotState b;
    b.entity_id = bot_id;
    b.bot_type = bot_type;
    b.current_job = 0;
    b.target_x = 0;
    b.target_y = 0;
    b.seed_type = 0;
    _bot_states.push_back(b);
}

void SimWorld::_do_set_bot_job(EntityID bot_id, uint8_t job, int32_t tx, int32_t ty, uint8_t seed_type) {
    for (auto& b : _bot_states) {
        if (b.entity_id == bot_id) {
            b.current_job = job;
            b.target_x = tx;
            b.target_y = ty;
            b.seed_type = seed_type;
            break;
        }
    }
}

void SimWorld::_do_upload_bot_program(EntityID bot_id, const uint16_t* opcodes, uint16_t size) {
    for (auto& b : _bot_states) {
        if (b.entity_id == bot_id) {
            b.program_size = size > 32 ? 32 : size;
            for (uint16_t i = 0; i < b.program_size; ++i) {
                b.program[i] = opcodes[i];
            }
            b.pc = 0;
            break;
        }
    }
}

void SimWorld::_do_wire_bot_ports(EntityID bot_id, int32_t sx, int32_t sy, int32_t ax, int32_t ay) {
    for (auto& b : _bot_states) {
        if (b.entity_id == bot_id) {
            b.sensor_x = sx;
            b.sensor_y = sy;
            b.actuator_x = ax;
            b.actuator_y = ay;
            break;
        }
    }
}

EntityID SimWorld::spawn_bot(uint8_t bot_type, int64_t x, int64_t y) {
    EntityID id = _next_id++;
    _do_spawn_bot(id, bot_type, x, y);
    return id;
}

void SimWorld::set_bot_job(EntityID bot_id, uint8_t job, int32_t tx, int32_t ty, uint8_t seed_type) {
    _do_set_bot_job(bot_id, job, tx, ty, seed_type);
}

void SimWorld::upload_bot_program(EntityID bot_id, const std::vector<uint16_t>& opcodes) {
    _do_upload_bot_program(bot_id, opcodes.data(), (uint16_t)opcodes.size());
}

void SimWorld::wire_bot_ports(EntityID bot_id, int32_t sx, int32_t sy, int32_t ax, int32_t ay) {
    _do_wire_bot_ports(bot_id, sx, sy, ax, ay);
}

void SimWorld::tick_bots() {
    auto& chaos = _entities.get_chaos();
    
    // Clear dynamic actuator signals at start of tick
    std::fill(_actuator_activated.begin(), _actuator_activated.end(), 0);

    for (auto& b : _bot_states) {
        uint32_t idx = _entities.get_dense_idx(b.entity_id);
        if (idx == (uint32_t)-1) continue;
        if (chaos.flags[idx] & (uint8_t)EntityFlags::DEAD) continue;

        // 1. Run IoT Node Scripting VM (stationary processor)
        if (b.bot_type == (uint8_t)BotType::IOT_NODE) {
            b.pc = 0;
            int instructions_executed = 0;
            while (b.pc < b.program_size && instructions_executed < 16) {
                uint16_t instr = b.program[b.pc];
                uint8_t opcode = (instr >> 8) & 0xFF;
                uint8_t arg = instr & 0xFF;
                
                b.pc++;
                instructions_executed++;

                switch (opcode) {
                    case 0: // HALT
                        b.pc = b.program_size;
                        break;
                    case 1: { // LOAD reg, val
                        uint8_t reg = (arg >> 6) & 0x3;
                        int32_t val = arg & 0x3F;
                        b.registers[reg] = val;
                        break;
                    }
                    case 2: { // READ_MOISTURE reg
                        uint8_t reg = arg & 0x3;
                        if (b.sensor_x >= 0 && b.sensor_x < _map_width && b.sensor_y >= 0 && b.sensor_y < _map_height) {
                            int32_t s_idx = b.sensor_y * _map_width + b.sensor_x;
                            b.registers[reg] = _water_table[s_idx] / ONE;
                        } else {
                            b.registers[reg] = 0;
                        }
                        break;
                    }
                    case 3: { // READ_CROP_GROWTH reg
                        uint8_t reg = arg & 0x3;
                        if (b.sensor_x >= 0 && b.sensor_x < _map_width && b.sensor_y >= 0 && b.sensor_y < _map_height) {
                            int32_t s_idx = b.sensor_y * _map_width + b.sensor_x;
                            b.registers[reg] = _crop_growth[s_idx] / ONE;
                        } else {
                            b.registers[reg] = 0;
                        }
                        break;
                    }
                    case 4: { // WRITE_ACTUATOR reg/val
                        uint8_t is_reg = (arg >> 7) & 0x1;
                        int32_t val = 0;
                        if (is_reg) {
                            uint8_t reg = (arg >> 5) & 0x3;
                            val = b.registers[reg];
                        } else {
                            val = arg & 0x7F;
                        }
                        if (b.actuator_x >= 0 && b.actuator_x < _map_width && b.actuator_y >= 0 && b.actuator_y < _map_height) {
                            int32_t a_idx = b.actuator_y * _map_width + b.actuator_x;
                            _actuator_activated[a_idx] = (val > 0) ? 1 : 0;
                        }
                        break;
                    }
                    case 5: { // CMP reg, val
                        uint8_t reg = (arg >> 6) & 0x3;
                        int32_t val = arg & 0x3F;
                        if (b.registers[reg] < val) b.cmp_result = -1;
                        else if (b.registers[reg] > val) b.cmp_result = 1;
                        else b.cmp_result = 0;
                        break;
                    }
                    case 6: { // JMP addr
                        b.pc = arg;
                        break;
                    }
                    case 7: { // JLT addr
                        if (b.cmp_result < 0) b.pc = arg;
                        break;
                    }
                    case 8: { // JGT addr
                        if (b.cmp_result > 0) b.pc = arg;
                        break;
                    }
                    case 9: { // ADD reg, val
                        uint8_t reg = (arg >> 6) & 0x3;
                        int32_t val = arg & 0x3F;
                        b.registers[reg] += val;
                        break;
                    }
                    case 10: { // SUB reg, val
                        uint8_t reg = (arg >> 6) & 0x3;
                        int32_t val = arg & 0x3F;
                        b.registers[reg] -= val;
                        break;
                    }
                    default:
                        break;
                }
            }
            continue; // Node bots are stationary and do not run job navigation
        }

        // 2. Job-Oriented Navigation for Mobile Bots (Humanoid, Drone, Combine)
        int64_t speed = 1LL * ONE; // Default Humanoid
        if (b.bot_type == (uint8_t)BotType::DRONE) speed = 4LL * ONE;
        else if (b.bot_type == (uint8_t)BotType::COMBINE) speed = 2LL * ONE;

        if (b.current_job == 0) { // IDLE
            chaos.velocities_x[idx] = 0;
            chaos.velocities_y[idx] = 0;
            continue;
        }

        // Target coordinates in world space (tile center)
        int64_t dest_x = b.target_x * _cell_size_fixed + _cell_size_fixed / 2;
        int64_t dest_y = b.target_y * _cell_size_fixed + _cell_size_fixed / 2;
        int64_t dx = dest_x - chaos.positions_x[idx];
        int64_t dy = dest_y - chaos.positions_y[idx];
        
        int64_t dist_sq = mul_fixed(dx, dx) + mul_fixed(dy, dy);
        int64_t dist = sqrt_fixed(dist_sq);
        
        if (dist > 10LL * ONE) {
            // Move toward target
            int64_t dir_x = div_fixed(dx, dist);
            int64_t dir_y = div_fixed(dy, dist);
            chaos.velocities_x[idx] = mul_fixed(dir_x, speed);
            chaos.velocities_y[idx] = mul_fixed(dir_y, speed);
            
            // Integrate position manually to ensure they update for this tick
            chaos.positions_x[idx] += chaos.velocities_x[idx];
            chaos.positions_y[idx] += chaos.velocities_y[idx];
            _sync_node_aabb(idx);
        } else {
            // Arrived! Do job
            chaos.velocities_x[idx] = 0;
            chaos.velocities_y[idx] = 0;
            
            int32_t tile_idx = b.target_y * _map_width + b.target_x;
            if (tile_idx >= 0 && tile_idx < _map_width * _map_height) {
                if (b.current_job == 1) { // PLANT
                    if (_crop_type[tile_idx] == 0 && _structure_type[tile_idx] == 0) {
                        plant_crop(b.target_x, b.target_y, b.seed_type);
                    }
                    b.current_job = 0; // Back to IDLE
                } else if (b.current_job == 2) { // HARVEST
                    if (_crop_type[tile_idx] != 0 && _crop_growth[tile_idx] >= 100 * ONE) {
                        harvest_crop(b.target_x, b.target_y);
                    }
                    b.current_job = 0;
                } else if (b.current_job == 3) { // WATER
                    // Apply low-level watering
                    int64_t new_w = _water_table[tile_idx] + 30LL * ONE;
                    if (new_w > 95LL * ONE) new_w = 95LL * ONE;
                    _water_table[tile_idx] = static_cast<int32_t>(new_w);
                    b.current_job = 0;
                }
            }
        }
    }
}


