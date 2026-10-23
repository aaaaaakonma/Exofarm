#include "sim_bridge.h"
#include "item.h"
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

SimBridge *SimBridge::singleton = nullptr;

void SimBridge::_bind_methods() {
    ClassDB::bind_method(D_METHOD("load_tile_registry", "path"), &SimBridge::load_tile_registry);
    ClassDB::bind_method(D_METHOD("set_seed", "seed"), &SimBridge::set_seed);
    ClassDB::bind_method(D_METHOD("set_config", "params"), &SimBridge::set_config);
    ClassDB::bind_method(D_METHOD("set_params", "params"), &SimBridge::set_params);
    ClassDB::bind_method(D_METHOD("get_config"), &SimBridge::get_config);
    ClassDB::bind_method(D_METHOD("set_tectonic_config", "plates_min", "plates_max", "center_bias", "continent_chance", "vel_min", "vel_max"), &SimBridge::set_tectonic_config, DEFVAL(50), DEFVAL(300));
    ClassDB::bind_method(D_METHOD("step", "move_x", "move_y", "aim_x", "aim_y", "actions"), &SimBridge::step);
    ClassDB::bind_method(D_METHOD("spawn_entity", "type", "x", "y", "radius", "health", "tags"), &SimBridge::spawn_entity, DEFVAL(0));
    ClassDB::bind_method(D_METHOD("kill_entity", "id"), &SimBridge::kill_entity);
    ClassDB::bind_method(D_METHOD("clear_entities"), &SimBridge::clear_entities);
    ClassDB::bind_method(D_METHOD("equip_item", "entity_id", "slot", "item_id"), &SimBridge::equip_item);
    ClassDB::bind_method(D_METHOD("unequip_item", "entity_id", "slot"), &SimBridge::unequip_item);
    ClassDB::bind_method(D_METHOD("add_wall", "x", "y", "w", "h"), &SimBridge::add_wall);
    ClassDB::bind_method(D_METHOD("generate_map_cellular_automata", "seed", "width", "height", "cell_size_fixed", "iterations", "fill_prob"), &SimBridge::generate_map_cellular_automata);
    ClassDB::bind_method(D_METHOD("generate_player_map", "seed", "width", "height", "cell_size_fixed"), &SimBridge::generate_player_map);
    ClassDB::bind_method(D_METHOD("get_room_type_info"), &SimBridge::get_room_type_info);
    ClassDB::bind_method(D_METHOD("get_map_width"), &SimBridge::get_map_width);
    ClassDB::bind_method(D_METHOD("get_map_height"), &SimBridge::get_map_height);
    ClassDB::bind_method(D_METHOD("get_water_table"), &SimBridge::get_water_table);
    ClassDB::bind_method(D_METHOD("get_aquifers"), &SimBridge::get_aquifers);
    ClassDB::bind_method(D_METHOD("get_rivers"), &SimBridge::get_rivers);
    ClassDB::bind_method(D_METHOD("get_flow_targets"), &SimBridge::get_flow_targets);
    ClassDB::bind_method(D_METHOD("get_lakes"), &SimBridge::get_lakes);
    ClassDB::bind_method(D_METHOD("get_structure_types"), &SimBridge::get_structure_types);
    ClassDB::bind_method(D_METHOD("get_water_pressures"), &SimBridge::get_water_pressures);
    ClassDB::bind_method(D_METHOD("get_chunk_tiers"), &SimBridge::get_chunk_tiers);
    ClassDB::bind_method(D_METHOD("get_soil_nitrogen"), &SimBridge::get_soil_nitrogen);
    ClassDB::bind_method(D_METHOD("get_soil_phosphorus"), &SimBridge::get_soil_phosphorus);
    ClassDB::bind_method(D_METHOD("get_soil_potassium"), &SimBridge::get_soil_potassium);
    ClassDB::bind_method(D_METHOD("get_soil_calcium"), &SimBridge::get_soil_calcium);
    ClassDB::bind_method(D_METHOD("get_soil_magnesium"), &SimBridge::get_soil_magnesium);
    ClassDB::bind_method(D_METHOD("get_soil_sulfur"), &SimBridge::get_soil_sulfur);
    ClassDB::bind_method(D_METHOD("get_soil_compactness"), &SimBridge::get_soil_compactness);
    ClassDB::bind_method(D_METHOD("plant_crop", "x", "y", "crop_type", "prefix", "suffix", "implicit"), &SimBridge::plant_crop, DEFVAL(0), DEFVAL(0), DEFVAL(0));
    ClassDB::bind_method(D_METHOD("harvest_crop", "x", "y"), &SimBridge::harvest_crop);
    ClassDB::bind_method(D_METHOD("place_structure", "x", "y", "structure_type"), &SimBridge::place_structure);
    ClassDB::bind_method(D_METHOD("remove_structure", "x", "y"), &SimBridge::remove_structure);
    ClassDB::bind_method(D_METHOD("apply_fertilizer", "x", "y", "n", "p", "k", "ca", "mg", "s"), &SimBridge::apply_fertilizer);
    ClassDB::bind_method(D_METHOD("get_crop_types"), &SimBridge::get_crop_types);
    ClassDB::bind_method(D_METHOD("get_crop_prefixes"), &SimBridge::get_crop_prefixes);
    ClassDB::bind_method(D_METHOD("get_crop_suffixes"), &SimBridge::get_crop_suffixes);
    ClassDB::bind_method(D_METHOD("get_crop_implicits"), &SimBridge::get_crop_implicits);
    ClassDB::bind_method(D_METHOD("get_crop_growth"), &SimBridge::get_crop_growth);
    ClassDB::bind_method(D_METHOD("get_crop_health"), &SimBridge::get_crop_health);
    
    ClassDB::bind_method(D_METHOD("add_polygon", "x", "y", "vx", "vy"), &SimBridge::add_polygon);
    ClassDB::bind_method(D_METHOD("clear_polygons"), &SimBridge::clear_polygons);
    ClassDB::bind_method(D_METHOD("get_polygons"), &SimBridge::get_polygons);

    ClassDB::bind_method(D_METHOD("clear_walls"), &SimBridge::clear_walls);
    ClassDB::bind_method(D_METHOD("get_walls"), &SimBridge::get_walls);
    ClassDB::bind_method(D_METHOD("get_entity_state", "id"), &SimBridge::get_entity_state);
    ClassDB::bind_method(D_METHOD("get_active_entities"), &SimBridge::get_active_entities);
    ClassDB::bind_method(D_METHOD("get_render_states"), &SimBridge::get_render_states);
    ClassDB::bind_method(D_METHOD("generate_item", "id"), &SimBridge::generate_item);
    ClassDB::bind_method(D_METHOD("current_tick"), &SimBridge::current_tick);
    ClassDB::bind_method(D_METHOD("get_elevation"), &SimBridge::get_elevation);
    ClassDB::bind_method(D_METHOD("get_biome"), &SimBridge::get_biome);
    ClassDB::bind_method(D_METHOD("get_surface_types"), &SimBridge::get_surface_types);
    ClassDB::bind_method(D_METHOD("get_mineral_types"), &SimBridge::get_mineral_types);
    ClassDB::bind_method(D_METHOD("get_starter_spawn"), &SimBridge::get_starter_spawn);
    ClassDB::bind_method(D_METHOD("get_starter_pond_volume"), &SimBridge::get_starter_pond_volume);
    ClassDB::bind_method(D_METHOD("get_starter_pond_max"), &SimBridge::get_starter_pond_max);
    ClassDB::bind_method(D_METHOD("get_starter_pond_percent"), &SimBridge::get_starter_pond_percent);
    ClassDB::bind_method(D_METHOD("get_reservoir_volumes"), &SimBridge::get_reservoir_volumes);
    ClassDB::bind_method(D_METHOD("get_river_count"), &SimBridge::get_river_count);
    ClassDB::bind_method(D_METHOD("get_river_water_levels"), &SimBridge::get_river_water_levels);
    ClassDB::bind_method(D_METHOD("get_river_flow_rates"), &SimBridge::get_river_flow_rates);
    ClassDB::bind_method(D_METHOD("get_river_max_rates"), &SimBridge::get_river_max_rates);
    ClassDB::bind_method(D_METHOD("get_river_pump_rates"), &SimBridge::get_river_pump_rates);
    ClassDB::bind_method(D_METHOD("get_river_pipe_info", "index"), &SimBridge::get_river_pipe_info);
    
    ClassDB::bind_method(D_METHOD("get_state_hash"), &SimBridge::get_state_hash);
    ClassDB::bind_method(D_METHOD("take_snapshot"), &SimBridge::take_snapshot);
    ClassDB::bind_method(D_METHOD("restore_snapshot", "data"), &SimBridge::restore_snapshot);

    ClassDB::bind_method(D_METHOD("get_spatial_info"), &SimBridge::get_spatial_info);
    ClassDB::bind_method(D_METHOD("get_cell_occupancy", "x", "y"), &SimBridge::get_cell_occupancy);

    ClassDB::bind_method(D_METHOD("spawn_bot", "bot_type", "x", "y"), &SimBridge::spawn_bot);
    ClassDB::bind_method(D_METHOD("set_bot_job", "bot_id", "job", "tx", "ty", "seed_type"), &SimBridge::set_bot_job);
    ClassDB::bind_method(D_METHOD("upload_bot_program", "bot_id", "opcodes"), &SimBridge::upload_bot_program);
    ClassDB::bind_method(D_METHOD("wire_bot_ports", "bot_id", "sx", "sy", "ax", "ay"), &SimBridge::wire_bot_ports);
    ClassDB::bind_method(D_METHOD("get_bot_state", "bot_id"), &SimBridge::get_bot_state);
    ClassDB::bind_method(D_METHOD("get_all_bots"), &SimBridge::get_all_bots);
    ClassDB::bind_method(D_METHOD("get_actuator_activated"), &SimBridge::get_actuator_activated);
    ClassDB::bind_method(D_METHOD("get_weather_state"), &SimBridge::get_weather_state);
    ClassDB::bind_method(D_METHOD("get_weather_timer"), &SimBridge::get_weather_timer);
    ClassDB::bind_method(D_METHOD("get_co2_level"), &SimBridge::get_co2_level);

    ClassDB::bind_method(D_METHOD("get_tectonic_sample", "x", "y"), &SimBridge::get_tectonic_sample);
    ClassDB::bind_method(D_METHOD("get_tectonic_nodes"), &SimBridge::get_tectonic_nodes);
    ClassDB::bind_method(D_METHOD("get_macro_edges"), &SimBridge::get_macro_edges);
    ClassDB::bind_method(D_METHOD("get_macro_plates"), &SimBridge::get_macro_plates);
    ClassDB::bind_method(D_METHOD("get_wave_cage_data", "sea_level"), &SimBridge::get_wave_cage_data, DEFVAL(0.0));
}


SimBridge::SimBridge() {
    singleton = this;
}

SimBridge::~SimBridge() {
    if (singleton == this) {
        singleton = nullptr;
    }
}

SimBridge *SimBridge::get_singleton() {
    return singleton;
}

void SimBridge::set_seed(int64_t seed) {
    _world.set_seed((uint64_t)seed);
}

void SimBridge::set_config(const godot::Dictionary &params) {
    auto& cfg = _world.get_tectonic_config_mut();

    if (params.has("seed")) {
        int64_t s = params["seed"];
        set_seed(s);
    }
    if (params.has("num_plates")) cfg.num_plates = (int32_t)(int64_t)params["num_plates"];
    if (params.has("num_fine_plates")) cfg.num_fine_plates = (int32_t)(int64_t)params["num_fine_plates"];
    if (params.has("continent_chance")) {
        double c = params["continent_chance"];
        cfg.continent_chance_pct = (c <= 1.0) ? (int32_t)(c * 100.0) : (int32_t)c;
    }
    if (params.has("island_arc_chance")) {
        double c = params["island_arc_chance"];
        cfg.island_arc_chance_pct = (c <= 1.0) ? (int32_t)(c * 100.0) : (int32_t)c;
    }
    if (params.has("warp_scale")) cfg.warp_scale = (int64_t)((double)params["warp_scale"] * 65536.0);
    if (params.has("warp_frequency")) cfg.warp_frequency = (int64_t)params["warp_frequency"];
    if (params.has("collision_uplift")) cfg.collision_uplift_scale = (int64_t)((double)params["collision_uplift"] * 65536.0);
    if (params.has("subduction_arc")) cfg.subduction_arc_scale = (int64_t)((double)params["subduction_arc"] * 65536.0);
    if (params.has("subduction_trench")) cfg.subduction_trench_scale = (int64_t)((double)params["subduction_trench"] * 65536.0);
    if (params.has("fold_belt_width")) cfg.fold_belt_base_width = (int64_t)((double)params["fold_belt_width"] * 65536.0);
    if (params.has("rift_graben")) cfg.rift_graben_scale = (int64_t)((double)params["rift_graben"] * 65536.0);
    if (params.has("rift_shoulder")) cfg.rift_shoulder_scale = (int64_t)((double)params["rift_shoulder"] * 65536.0);
    if (params.has("continental_base_elevation")) cfg.continental_base_elevation = (int64_t)((double)params["continental_base_elevation"] * 65536.0);
    if (params.has("oceanic_base_elevation")) cfg.oceanic_base_elevation = (int64_t)((double)params["oceanic_base_elevation"] * 65536.0);
    if (params.has("island_arc_base_elevation")) cfg.island_arc_base_elevation = (int64_t)((double)params["island_arc_base_elevation"] * 65536.0);
    if (params.has("coastal_boundary_elevation")) cfg.coastal_boundary_elevation = (int64_t)((double)params["coastal_boundary_elevation"] * 65536.0);
    if (params.has("shelf_width")) cfg.shelf_width = (int64_t)((double)params["shelf_width"] * 65536.0);
    if (params.has("shelf_depth")) cfg.shelf_depth = (int64_t)((double)params["shelf_depth"] * 65536.0);
    if (params.has("detail_noise_scale")) cfg.detail_noise_scale = (int64_t)((double)params["detail_noise_scale"] * 65536.0);
    if (params.has("noise_octaves")) cfg.noise_octaves = (int)(int64_t)params["noise_octaves"];
}

void SimBridge::set_params(const godot::Dictionary &params) {
    set_config(params);
}

godot::Dictionary SimBridge::get_config() const {
    godot::Dictionary dict;
    const auto& cfg = _world.get_tectonic_config();

    dict["seed"] = (int64_t)cfg.seed;
    dict["num_plates"] = (int64_t)cfg.num_plates;
    dict["num_fine_plates"] = (int64_t)cfg.num_fine_plates;
    dict["continent_chance"] = (double)cfg.continent_chance_pct;
    dict["island_arc_chance"] = (double)cfg.island_arc_chance_pct;
    dict["warp_scale"] = (double)cfg.warp_scale / 65536.0;
    dict["warp_frequency"] = (int64_t)cfg.warp_frequency;
    dict["collision_uplift"] = (double)cfg.collision_uplift_scale / 65536.0;
    dict["subduction_arc"] = (double)cfg.subduction_arc_scale / 65536.0;
    dict["subduction_trench"] = (double)cfg.subduction_trench_scale / 65536.0;
    dict["fold_belt_width"] = (double)cfg.fold_belt_base_width / 65536.0;
    dict["rift_graben"] = (double)cfg.rift_graben_scale / 65536.0;
    dict["rift_shoulder"] = (double)cfg.rift_shoulder_scale / 65536.0;
    dict["continental_base_elevation"] = (double)cfg.continental_base_elevation / 65536.0;
    dict["oceanic_base_elevation"] = (double)cfg.oceanic_base_elevation / 65536.0;
    dict["island_arc_base_elevation"] = (double)cfg.island_arc_base_elevation / 65536.0;
    dict["coastal_boundary_elevation"] = (double)cfg.coastal_boundary_elevation / 65536.0;
    dict["shelf_width"] = (double)cfg.shelf_width / 65536.0;
    dict["shelf_depth"] = (double)cfg.shelf_depth / 65536.0;
    dict["detail_noise_scale"] = (double)cfg.detail_noise_scale / 65536.0;
    dict["noise_octaves"] = (int64_t)cfg.noise_octaves;

    return dict;
}

void SimBridge::set_tectonic_config(int64_t plates_min, int64_t plates_max, int64_t center_bias, int64_t continent_chance, int64_t vel_min, int64_t vel_max) {
    auto& cfg = _world.get_tectonic_config_mut();
    cfg.num_plates = (int32_t)plates_max;
    cfg.continent_chance_pct = (int32_t)continent_chance;
}

bool SimBridge::load_tile_registry(godot::String path) {
    _world.load_tile_registry(path.utf8().get_data());
    return true; // Or return success based on if _tile_registry actually loaded it successfully (load_from_file returns void currently)
}

void SimBridge::step(int64_t move_x, int64_t move_y, int64_t aim_x, int64_t aim_y, int64_t actions) {
    InputFrame input;
    input.payload.player.move_x = (int32_t)move_x;
    input.payload.player.move_y = (int32_t)move_y;
    input.payload.player.aim_x = aim_x;
    input.payload.player.aim_y = aim_y;
    input.payload.player.actions = (uint32_t)actions;
    input.tick = _world.current_tick();

    _world.tick(input);
}

int64_t SimBridge::spawn_entity(int64_t type, int64_t x, int64_t y, int64_t radius, int64_t health, int64_t tags) {
    return (int64_t)_world.spawn_entity((EntityType)type, x, y, radius, (int32_t)health, (uint32_t)tags);
}

void SimBridge::kill_entity(int64_t id) {
    _world.kill_entity((uint32_t)id);
}

void SimBridge::clear_entities() {
    _world.clear_entities();
}

void SimBridge::equip_item(int64_t entity_id, int64_t slot, int64_t item_id) {
    _world.equip_item((uint32_t)entity_id, (int)slot, (uint64_t)item_id);
}

void SimBridge::unequip_item(int64_t entity_id, int64_t slot) {
    _world.unequip_item((uint32_t)entity_id, (int)slot);
}

void SimBridge::add_wall(int64_t x, int64_t y, int64_t w, int64_t h) {
}

void SimBridge::add_polygon(int64_t x, int64_t y, godot::PackedInt64Array vx, godot::PackedInt64Array vy) {
}

void SimBridge::clear_polygons() {
}

godot::TypedArray<godot::Dictionary> SimBridge::get_polygons() const {
    return godot::TypedArray<godot::Dictionary>();
}

int64_t SimBridge::get_map_width() const {
    return (int64_t)_world.get_map_width();
}

int64_t SimBridge::get_map_height() const {
    return (int64_t)_world.get_map_height();
}

godot::PackedInt64Array SimBridge::get_water_table() const {
    godot::PackedInt64Array arr;
    const auto& wt = _world.get_water_table();
    arr.resize(wt.size());
    for (size_t i = 0; i < wt.size(); ++i) {
        arr[i] = wt[i];
    }
    return arr;
}

godot::PackedByteArray SimBridge::get_aquifers() const {
    godot::PackedByteArray arr;
    const auto& aq = _world.get_aquifers();
    arr.resize(aq.size());
    for (size_t i = 0; i < aq.size(); ++i) {
        arr[i] = aq[i];
    }
    return arr;
}

godot::PackedByteArray SimBridge::get_rivers() const {
    godot::PackedByteArray arr;
    const auto& r = _world.get_rivers();
    arr.resize(r.size());
    for (size_t i = 0; i < r.size(); ++i) {
        arr[i] = r[i];
    }
    return arr;
}

godot::PackedInt32Array SimBridge::get_flow_targets() const {
    godot::PackedInt32Array arr;
    const auto& ft = _world.get_flow_targets();
    arr.resize(ft.size());
    for (size_t i = 0; i < ft.size(); ++i) {
        arr[i] = ft[i];
    }
    return arr;
}

godot::PackedByteArray SimBridge::get_chunk_tiers() const {
    godot::PackedByteArray arr;
    const auto& t = _world.get_chunk_tiers();
    arr.resize(t.size());
    for (size_t i = 0; i < t.size(); ++i) {
        arr[i] = t[i];
    }
    return arr;
}

godot::PackedInt64Array SimBridge::get_soil_phosphorus() const {
    godot::PackedInt64Array arr;
    const auto& t = _world.get_soil_phosphorus();
    arr.resize(t.size());
    for (size_t i = 0; i < t.size(); ++i) {
        arr[i] = t[i];
    }
    return arr;
}

godot::PackedInt64Array SimBridge::get_soil_potassium() const {
    godot::PackedInt64Array arr;
    const auto& t = _world.get_soil_potassium();
    arr.resize(t.size());
    for (size_t i = 0; i < t.size(); ++i) {
        arr[i] = t[i];
    }
    return arr;
}

godot::PackedInt64Array SimBridge::get_soil_calcium() const {
    godot::PackedInt64Array arr;
    const auto& t = _world.get_soil_calcium();
    arr.resize(t.size());
    for (size_t i = 0; i < t.size(); ++i) {
        arr[i] = t[i];
    }
    return arr;
}

godot::PackedInt64Array SimBridge::get_soil_magnesium() const {
    godot::PackedInt64Array arr;
    const auto& t = _world.get_soil_magnesium();
    arr.resize(t.size());
    for (size_t i = 0; i < t.size(); ++i) {
        arr[i] = t[i];
    }
    return arr;
}

godot::PackedInt64Array SimBridge::get_soil_sulfur() const {
    godot::PackedInt64Array arr;
    const auto& t = _world.get_soil_sulfur();
    arr.resize(t.size());
    for (size_t i = 0; i < t.size(); ++i) {
        arr[i] = t[i];
    }
    return arr;
}


godot::PackedInt64Array SimBridge::get_soil_nitrogen() const {
    godot::PackedInt64Array arr;
    const auto& t = _world.get_soil_nitrogen();
    arr.resize(t.size());
    for (size_t i = 0; i < t.size(); ++i) {
        arr[i] = t[i];
    }
    return arr;
}

godot::PackedInt64Array SimBridge::get_soil_compactness() const {
    godot::PackedInt64Array arr;
    const auto& t = _world.get_soil_compactness();
    arr.resize(t.size());
    for (size_t i = 0; i < t.size(); ++i) {
        arr[i] = t[i];
    }
    return arr;
}

godot::Dictionary SimBridge::get_room_type_info() const {
    godot::Dictionary dict;
    for (const auto& pair : _world.get_tile_registry().get_blueprints()) {
        if (pair.second.type_hash != 0) {
            dict[(int64_t)pair.second.type_hash] = String(pair.second.type_name.c_str());
        }
    }
    return dict;
}

void SimBridge::generate_map_cellular_automata(int64_t seed, int64_t width, int64_t height, int64_t cell_size_fixed, int64_t iterations, int64_t fill_prob) {
}

void SimBridge::clear_walls() {
}

TypedArray<int64_t> SimBridge::get_entity_state(int64_t id) const {
    TypedArray<int64_t> state;
    uint32_t idx = _world.get_dense_idx((uint32_t)id);
    
    if (idx != 0xFFFFFFFF) {
        const ChaosState& chaos = _world.get_chaos();
        state.push_back(chaos.positions_x[idx]);
        state.push_back(chaos.positions_y[idx]);
        state.push_back(chaos.velocities_x[idx]);
        state.push_back(chaos.velocities_y[idx]);
        state.push_back((int64_t)chaos.healths[idx]);
        state.push_back((int64_t)chaos.flags[idx]);
        state.push_back(chaos.radii[idx]);
        state.push_back((int64_t)chaos.tags[idx]);
    }
    
    return state;
}

godot::TypedArray<godot::Dictionary> SimBridge::get_walls() const {
    return godot::TypedArray<godot::Dictionary>();
}

godot::Dictionary SimBridge::get_active_entities() const {
    godot::Dictionary dict;
    const ChaosState& chaos = _world.get_chaos();
    for (size_t i = 0; i < chaos.size(); ++i) {
        if (!(chaos.flags[i] & (uint8_t)EntityFlags::DEAD)) {
            dict[(int64_t)chaos.dense_ids[i]] = (int64_t)chaos.types[i];
        }
    }
    return dict;
}

godot::PackedFloat64Array SimBridge::get_render_states() const {
    godot::PackedFloat64Array arr;
    const ChaosState& chaos = _world.get_chaos();
    
    size_t active_count = 0;
    for (size_t i = 0; i < chaos.size(); ++i) {
        if (!(chaos.flags[i] & (uint8_t)EntityFlags::DEAD)) {
            active_count++;
        }
    }
    
    arr.resize((int64_t)(active_count * 8));
    double* ptr = arr.ptrw();
    
    size_t out_idx = 0;
    for (size_t i = 0; i < chaos.size(); ++i) {
        if (!(chaos.flags[i] & (uint8_t)EntityFlags::DEAD)) {
            ptr[out_idx++] = (double)chaos.dense_ids[i];
            ptr[out_idx++] = (double)chaos.types[i];
            ptr[out_idx++] = (double)chaos.positions_x[i] / 65536.0;
            ptr[out_idx++] = (double)chaos.positions_y[i] / 65536.0;
            ptr[out_idx++] = (double)chaos.velocities_x[i] / 65536.0;
            ptr[out_idx++] = (double)chaos.velocities_y[i] / 65536.0;
            ptr[out_idx++] = (double)chaos.radii[i] / 65536.0;
            ptr[out_idx++] = (double)chaos.tags[i];
        }
    }
    
    return arr;
}

godot::Dictionary SimBridge::generate_item(int64_t id) const {
    ItemData item = ItemData::generate((uint64_t)id);
    return item.to_dict();
}

int64_t SimBridge::current_tick() const {
    return (int64_t)_world.current_tick();
}

int64_t SimBridge::get_state_hash() const {
    return (int64_t)_world.get_state_hash();
}

godot::PackedByteArray SimBridge::take_snapshot() const {
    std::vector<uint8_t> buf = SimWorld::serialize_snapshot(_world.snapshot());
    
    godot::PackedByteArray pba;
    pba.resize((int64_t)buf.size());
    memcpy(pba.ptrw(), buf.data(), buf.size());
    return pba;
}

void SimBridge::restore_snapshot(godot::PackedByteArray data) {
    if (data.is_empty()) return;
    BisectSnapshot snap;
    SimWorld::deserialize_snapshot(snap, data.ptr());
    _world.restore(snap);
}

godot::Dictionary SimBridge::get_spatial_info() const {
    godot::Dictionary dict;
    dict["min_cx"] = (int64_t)0;
    dict["min_cy"] = (int64_t)0;
    dict["width"] = (int64_t)_world.get_map_width();
    dict["height"] = (int64_t)_world.get_map_height();
    return dict;
}

int64_t SimBridge::get_cell_occupancy(int64_t x, int64_t y) const {
    return 0;
}

void SimBridge::plant_crop(int64_t x, int64_t y, int64_t crop_type, int64_t prefix, int64_t suffix, int64_t implicit) {
    _world.plant_crop((int32_t)x, (int32_t)y, (uint8_t)crop_type, (uint8_t)prefix, (uint8_t)suffix, (uint8_t)implicit);
}

void SimBridge::harvest_crop(int64_t x, int64_t y) {
    _world.harvest_crop((int32_t)x, (int32_t)y);
}

godot::PackedByteArray SimBridge::get_crop_types() const {
    godot::PackedByteArray arr;
    const auto& t = _world.get_crop_type();
    arr.resize(t.size());
    for (size_t i = 0; i < t.size(); ++i) {
        arr[i] = t[i];
    }
    return arr;
}

godot::PackedByteArray SimBridge::get_crop_prefixes() const {
    godot::PackedByteArray arr;
    const auto& t = _world.get_crop_prefix();
    arr.resize(t.size());
    for (size_t i = 0; i < t.size(); ++i) {
        arr[i] = t[i];
    }
    return arr;
}

godot::PackedByteArray SimBridge::get_crop_suffixes() const {
    godot::PackedByteArray arr;
    const auto& t = _world.get_crop_suffix();
    arr.resize(t.size());
    for (size_t i = 0; i < t.size(); ++i) {
        arr[i] = t[i];
    }
    return arr;
}

godot::PackedByteArray SimBridge::get_crop_implicits() const {
    godot::PackedByteArray arr;
    const auto& t = _world.get_crop_implicit();
    arr.resize(t.size());
    for (size_t i = 0; i < t.size(); ++i) {
        arr[i] = t[i];
    }
    return arr;
}

godot::PackedInt64Array SimBridge::get_crop_growth() const {
    godot::PackedInt64Array arr;
    const auto& t = _world.get_crop_growth();
    arr.resize(t.size());
    for (size_t i = 0; i < t.size(); ++i) {
        arr[i] = t[i];
    }
    return arr;
}

godot::PackedInt64Array SimBridge::get_crop_health() const {
    godot::PackedInt64Array arr;
    const auto& t = _world.get_crop_health();
    arr.resize(t.size());
    for (size_t i = 0; i < t.size(); ++i) {
        arr[i] = t[i];
    }
    return arr;
}

void SimBridge::apply_fertilizer(int64_t x, int64_t y, int64_t n, int64_t p, int64_t k, int64_t ca, int64_t mg, int64_t s) {
    _world.apply_fertilizer((int32_t)x, (int32_t)y, n, p, k, ca, mg, s);
}

godot::PackedByteArray SimBridge::get_lakes() const {
    godot::PackedByteArray arr;
    const auto& t = _world.get_lakes();
    arr.resize(t.size());
    for (size_t i = 0; i < t.size(); ++i) {
        arr[i] = t[i];
    }
    return arr;
}

godot::PackedByteArray SimBridge::get_structure_types() const {
    godot::PackedByteArray arr;
    const auto& t = _world.get_structure_type();
    arr.resize(t.size());
    for (size_t i = 0; i < t.size(); ++i) {
        arr[i] = t[i];
    }
    return arr;
}

godot::PackedInt64Array SimBridge::get_water_pressures() const {
    godot::PackedInt64Array arr;
    const auto& t = _world.get_water_pressure();
    arr.resize(t.size());
    for (size_t i = 0; i < t.size(); ++i) {
        arr[i] = t[i];
    }
    return arr;
}

void SimBridge::place_structure(int64_t x, int64_t y, int64_t structure_type) {
    _world.place_structure((int32_t)x, (int32_t)y, (uint8_t)structure_type);
}

void SimBridge::remove_structure(int64_t x, int64_t y) {
    _world.remove_structure((int32_t)x, (int32_t)y);
}

int64_t SimBridge::spawn_bot(int64_t bot_type, int64_t x, int64_t y) {
    return (int64_t)_world.spawn_bot((uint8_t)bot_type, x, y);
}

void SimBridge::set_bot_job(int64_t bot_id, int64_t job, int64_t tx, int64_t ty, int64_t seed_type) {
    _world.set_bot_job((EntityID)bot_id, (uint8_t)job, (int32_t)tx, (int32_t)ty, (uint8_t)seed_type);
}

void SimBridge::upload_bot_program(int64_t bot_id, godot::PackedInt64Array opcodes) {
    std::vector<uint16_t> vec(opcodes.size());
    for (int i = 0; i < opcodes.size(); ++i) {
        vec[i] = (uint16_t)opcodes[i];
    }
    _world.upload_bot_program((EntityID)bot_id, vec);
}

void SimBridge::wire_bot_ports(int64_t bot_id, int64_t sx, int64_t sy, int64_t ax, int64_t ay) {
    _world.wire_bot_ports((EntityID)bot_id, (int32_t)sx, (int32_t)sy, (int32_t)ax, (int32_t)ay);
}

godot::Dictionary SimBridge::get_bot_state(int64_t bot_id) const {
    godot::Dictionary dict;
    for (const auto& b : _world.get_bot_states()) {
        if (b.entity_id == (EntityID)bot_id) {
            dict["entity_id"] = (int64_t)b.entity_id;
            dict["bot_type"] = (int64_t)b.bot_type;
            dict["current_job"] = (int64_t)b.current_job;
            dict["target_x"] = (int64_t)b.target_x;
            dict["target_y"] = (int64_t)b.target_y;
            dict["seed_type"] = (int64_t)b.seed_type;
            dict["pc"] = (int64_t)b.pc;
            dict["program_size"] = (int64_t)b.program_size;
            dict["cmp_result"] = (int64_t)b.cmp_result;
            dict["sensor_x"] = (int64_t)b.sensor_x;
            dict["sensor_y"] = (int64_t)b.sensor_y;
            dict["actuator_x"] = (int64_t)b.actuator_x;
            dict["actuator_y"] = (int64_t)b.actuator_y;
            
            godot::PackedInt64Array regs;
            regs.resize(4);
            for (int i = 0; i < 4; ++i) {
                regs[i] = b.registers[i];
            }
            dict["registers"] = regs;
            break;
        }
    }
    return dict;
}

godot::Array SimBridge::get_all_bots() const {
    godot::Array arr;
    for (const auto& b : _world.get_bot_states()) {
        arr.push_back((int64_t)b.entity_id);
    }
    return arr;
}

godot::PackedByteArray SimBridge::get_actuator_activated() const {
    godot::PackedByteArray arr;
    const auto& t = _world.get_actuator_activated();
    arr.resize(t.size());
    for (size_t i = 0; i < t.size(); ++i) {
        arr[i] = t[i];
    }
    return arr;
}

int64_t SimBridge::get_weather_state() const {
    return (int64_t)_world.get_weather_state();
}

int64_t SimBridge::get_weather_timer() const {
    return (int64_t)_world.get_weather_timer();
}

int64_t SimBridge::get_co2_level() const {
    return (int64_t)_world.get_co2_level();
}

godot::PackedInt32Array SimBridge::get_elevation() const {
    godot::PackedInt32Array arr;
    const auto& t = _world.get_elevation();
    arr.resize(t.size());
    for (size_t i = 0; i < t.size(); ++i) {
        arr[i] = t[i];
    }
    return arr;
}

godot::PackedByteArray SimBridge::get_biome() const {
    godot::PackedByteArray arr;
    const auto& t = _world.get_biome();
    arr.resize(t.size());
    for (size_t i = 0; i < t.size(); ++i) {
        arr[i] = t[i];
    }
    return arr;
}

void SimBridge::generate_player_map(int64_t seed, int64_t width, int64_t height, int64_t cell_size_fixed) {
    _world.generate_player_map((uint64_t)seed, width, height, cell_size_fixed);
}

godot::PackedByteArray SimBridge::get_surface_types() const {
    godot::PackedByteArray arr;
    const auto& t = _world.get_surface_type();
    arr.resize(t.size());
    for (size_t i = 0; i < t.size(); ++i) {
        arr[i] = t[i];
    }
    return arr;
}

godot::PackedByteArray SimBridge::get_mineral_types() const {
    godot::PackedByteArray arr;
    const auto& t = _world.get_mineral_type();
    arr.resize(t.size());
    for (size_t i = 0; i < t.size(); ++i) {
        arr[i] = t[i];
    }
    return arr;
}

godot::Vector2i SimBridge::get_starter_spawn() const {
    return godot::Vector2i(_world.get_starter_spawn_x(), _world.get_starter_spawn_y());
}

int64_t SimBridge::get_starter_pond_volume() const {
    return _world.get_starter_pond_volume();
}

int64_t SimBridge::get_starter_pond_max() const {
    return _world.get_starter_pond_max();
}

double SimBridge::get_starter_pond_percent() const {
    int64_t max_v = _world.get_starter_pond_max();
    if (max_v <= 0) return 0.0;
    return (double)_world.get_starter_pond_volume() / (double)max_v * 100.0;
}

godot::PackedInt64Array SimBridge::get_reservoir_volumes() const {
    godot::PackedInt64Array arr;
    const auto& res = _world.get_hydraulic_graph().get_reservoirs();
    arr.resize(res.size());
    for (size_t i = 0; i < res.size(); ++i) {
        arr[i] = res[i].current_volume;
    }
    return arr;
}

int64_t SimBridge::get_river_count() const {
    return static_cast<int64_t>(_world.get_hydraulic_graph().get_river_belts().size());
}

godot::PackedInt64Array SimBridge::get_river_water_levels() const {
    godot::PackedInt64Array arr;
    const auto& rivers = _world.get_hydraulic_graph().get_river_belts();
    arr.resize(rivers.size());
    for (size_t i = 0; i < rivers.size(); ++i) {
        arr[i] = rivers[i].water_level;
    }
    return arr;
}

godot::PackedInt64Array SimBridge::get_river_flow_rates() const {
    godot::PackedInt64Array arr;
    const auto& rivers = _world.get_hydraulic_graph().get_river_belts();
    arr.resize(rivers.size());
    for (size_t i = 0; i < rivers.size(); ++i) {
        arr[i] = rivers[i].flow_rate;
    }
    return arr;
}

godot::PackedInt64Array SimBridge::get_river_max_rates() const {
    godot::PackedInt64Array arr;
    const auto& rivers = _world.get_hydraulic_graph().get_river_belts();
    arr.resize(rivers.size());
    for (size_t i = 0; i < rivers.size(); ++i) {
        arr[i] = rivers[i].max_rate;
    }
    return arr;
}

godot::PackedInt64Array SimBridge::get_river_pump_rates() const {
    godot::PackedInt64Array arr;
    const auto& rivers = _world.get_hydraulic_graph().get_river_belts();
    arr.resize(rivers.size());
    for (size_t i = 0; i < rivers.size(); ++i) {
        arr[i] = rivers[i].pump_rate;
    }
    return arr;
}

godot::Dictionary SimBridge::get_river_pipe_info(int64_t index) const {
    godot::Dictionary dict;
    const auto& rivers = _world.get_hydraulic_graph().get_river_belts();
    if (index >= 0 && index < static_cast<int64_t>(rivers.size())) {
        const auto& b = rivers[index];
        dict["id"] = (int64_t)b.id;
        dict["from_node"] = (int64_t)b.from_node;
        dict["to_node"] = (int64_t)b.to_node;
        dict["tile_count"] = (int64_t)b.tile_count;
        dict["max_rate"] = b.max_rate;
        dict["pump_rate"] = b.pump_rate;
        dict["flow_rate"] = b.flow_rate;
        dict["water_level"] = b.water_level;
        dict["water_level_pct"] = (double)b.water_level / 65536.0;
        dict["is_dry"] = b.is_dry;
    }
    return dict;
}

godot::Dictionary SimBridge::get_tectonic_sample(double x, double y) const {
    godot::Dictionary dict;
    int64_t xf = static_cast<int64_t>(x * 65536.0);
    int64_t yf = static_cast<int64_t>(y * 65536.0);
    const auto& tmap = _world.get_tectonic_heightmap();
    Tectonics::TectonicSample sample = tmap.sample(xf, yf);

    dict["elevation"] = sample.final_elevation_float();
    dict["elevation_base"] = sample.base_elevation_float();
    dict["elevation_tectonic"] = sample.tectonic_elevation_float();
    dict["elevation_noise"] = static_cast<double>(sample.elevation_noise) / 65536.0;
    dict["primary_plate_id"] = static_cast<int64_t>(sample.primary_plate_id);
    dict["secondary_plate_id"] = static_cast<int64_t>(sample.secondary_plate_id);
    dict["dist_to_boundary"] = static_cast<double>(sample.dist_to_boundary) / 65536.0;
    dict["convergence_rate"] = static_cast<double>(sample.convergence_rate) / 65536.0;
    dict["shear_rate"] = static_cast<double>(sample.shear_rate) / 65536.0;
    dict["boundary_type"] = static_cast<int64_t>(sample.boundary_type);
    return dict;
}

godot::TypedArray<godot::Dictionary> SimBridge::get_tectonic_nodes() const {
    godot::TypedArray<godot::Dictionary> arr;
    const auto& nodes = _world.get_tectonic_heightmap().get_nodes();
    for (const auto& node : nodes) {
        godot::Dictionary d;
        d["id"] = static_cast<int64_t>(node.id);
        d["plate_id"] = static_cast<int64_t>(node.get_plate_id());
        d["pos_x"] = static_cast<double>(node.position.x) / 65536.0;
        d["pos_y"] = static_cast<double>(node.position.y) / 65536.0;
        d["vel_x"] = static_cast<double>(node.velocity.x) / 65536.0;
        d["vel_y"] = static_cast<double>(node.velocity.y) / 65536.0;
        d["angular_velocity"] = static_cast<double>(node.angular_velocity) / 65536.0;
        d["tilt_x"] = static_cast<double>(node.tilt_vector.x) / 65536.0;
        d["tilt_y"] = static_cast<double>(node.tilt_vector.y) / 65536.0;
        d["plate_type"] = static_cast<int64_t>(node.plate_type);
        d["base_elevation"] = static_cast<double>(node.base_elevation) / 65536.0;
        d["crust_thickness"] = static_cast<double>(node.crust_thickness) / 65536.0;
        d["crust_rigidity"] = static_cast<double>(node.crust_rigidity) / 65536.0;
        arr.push_back(d);
    }
    return arr;
}

godot::TypedArray<godot::Dictionary> SimBridge::get_macro_edges() const {
    godot::TypedArray<godot::Dictionary> arr;
    const auto& edges = _world.get_tectonic_heightmap().get_macro_edges();
    for (const auto& e : edges) {
        godot::Dictionary d;
        d["id"] = static_cast<int64_t>(e.id);
        d["plate_a"] = static_cast<int64_t>(e.plate_a);
        d["plate_b"] = static_cast<int64_t>(e.plate_b);
        d["plate_id_a"] = static_cast<int64_t>(e.plate_id_a);
        d["plate_id_b"] = static_cast<int64_t>(e.plate_id_b);
        d["plate_type_a"] = static_cast<int64_t>(e.plate_type_a);
        d["plate_type_b"] = static_cast<int64_t>(e.plate_type_b);
        d["v0_x"] = static_cast<double>(e.v0.x) / 65536.0;
        d["v0_y"] = static_cast<double>(e.v0.y) / 65536.0;
        d["v1_x"] = static_cast<double>(e.v1.x) / 65536.0;
        d["v1_y"] = static_cast<double>(e.v1.y) / 65536.0;
        d["length"] = static_cast<double>(e.length) / 65536.0;
        d["normal_x"] = static_cast<double>(e.normal.x) / 65536.0;
        d["normal_y"] = static_cast<double>(e.normal.y) / 65536.0;
        d["tangent_x"] = static_cast<double>(e.tangent.x) / 65536.0;
        d["tangent_y"] = static_cast<double>(e.tangent.y) / 65536.0;
        d["convergence_rate"] = static_cast<double>(e.convergence_rate) / 65536.0;
        d["shear_rate"] = static_cast<double>(e.shear_rate) / 65536.0;
        d["impact_force"] = static_cast<double>(e.impact_force) / 65536.0;
        d["boundary_type"] = static_cast<int64_t>(e.boundary_type);
        d["alpha"] = static_cast<double>(e.alpha) / 65536.0;
        d["peak_uplift"] = static_cast<double>(e.peak_uplift) / 65536.0;
        d["a_overrides"] = e.a_overrides;
        arr.push_back(d);
    }
    return arr;
}

godot::TypedArray<godot::Dictionary> SimBridge::get_macro_plates() const {
    godot::TypedArray<godot::Dictionary> arr;
    const auto& plates = _world.get_tectonic_heightmap().get_macro_plates();
    for (const auto& plate : plates) {
        godot::Dictionary d;
        d["id"] = static_cast<int64_t>(plate.id);
        d["plate_id"] = static_cast<int64_t>(plate.get_plate_id());
        d["pos_x"] = static_cast<double>(plate.position.x) / 65536.0;
        d["pos_y"] = static_cast<double>(plate.position.y) / 65536.0;
        d["vel_x"] = static_cast<double>(plate.velocity.x) / 65536.0;
        d["vel_y"] = static_cast<double>(plate.velocity.y) / 65536.0;
        d["angular_velocity"] = static_cast<double>(plate.angular_velocity) / 65536.0;
        d["tilt_x"] = static_cast<double>(plate.tilt_vector.x) / 65536.0;
        d["tilt_y"] = static_cast<double>(plate.tilt_vector.y) / 65536.0;
        d["plate_type"] = static_cast<int64_t>(plate.plate_type);
        d["base_elevation"] = static_cast<double>(plate.base_elevation) / 65536.0;
        d["crust_thickness"] = static_cast<double>(plate.crust_thickness) / 65536.0;
        d["crust_rigidity"] = static_cast<double>(plate.crust_rigidity) / 65536.0;
        arr.push_back(d);
    }
    return arr;
}

godot::PackedFloat32Array SimBridge::get_wave_cage_data(double sea_level) const {
    godot::PackedFloat32Array cage;
    int32_t w = _world.get_map_width();
    int32_t h = _world.get_map_height();
    if (w <= 0 || h <= 0) return cage;

    const auto& elevation = _world.get_elevation();
    if (elevation.size() < static_cast<size_t>(w * h)) return cage;

    size_t total_floats = static_cast<size_t>(w * h * 4);
    cage.resize(static_cast<int64_t>(total_floats));
    float* ptr = cage.ptrw();

    float sea_lvl = static_cast<float>(sea_level);

    for (int32_t y = 0; y < h; ++y) {
        for (int32_t x = 0; x < w; ++x) {
            size_t idx = static_cast<size_t>(y * w + x);
            float el = static_cast<float>(elevation[idx]) / 65536.0f;
            float depth = std::max(0.0f, sea_lvl - el);

            // Compute local gradient for shoreline normal vectors (uphill direction towards land)
            int32_t x_left = std::max(0, x - 1);
            int32_t x_right = std::min(w - 1, x + 1);
            int32_t y_down = std::max(0, y - 1);
            int32_t y_up = std::min(h - 1, y + 1);

            float el_l = static_cast<float>(elevation[y * w + x_left]) / 65536.0f;
            float el_r = static_cast<float>(elevation[y * w + x_right]) / 65536.0f;
            float el_d = static_cast<float>(elevation[y_down * w + x]) / 65536.0f;
            float el_u = static_cast<float>(elevation[y_up * w + x]) / 65536.0f;

            float grad_x = (el_r - el_l) * 0.5f;
            float grad_y = (el_u - el_d) * 0.5f;
            float grad_len = std::sqrt(grad_x * grad_x + grad_y * grad_y);

            float norm_x = 0.0f;
            float norm_y = 0.0f;
            if (grad_len > 0.001f) {
                norm_x = grad_x / grad_len;
                norm_y = grad_y / grad_len;
            }

            // Continuous signed distance to shoreline approximation
            float dist_to_shore = el - sea_lvl;

            size_t out_idx = idx * 4;
            ptr[out_idx + 0] = depth;          // R: Water depth (0 on land, >0 in water)
            ptr[out_idx + 1] = dist_to_shore;  // G: Signed distance / elevation relative to sea level
            ptr[out_idx + 2] = norm_x;         // B: Shore normal X (uphill towards land)
            ptr[out_idx + 3] = norm_y;         // A: Shore normal Y
        }
    }

    return cage;
}