#include "sim_bridge.h"
#include "item.h"
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

SimBridge *SimBridge::singleton = nullptr;

void SimBridge::_bind_methods() {
    ClassDB::bind_method(D_METHOD("load_tile_registry", "path"), &SimBridge::load_tile_registry);
    ClassDB::bind_method(D_METHOD("set_seed", "seed"), &SimBridge::set_seed);
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

void SimBridge::set_tectonic_config(int64_t plates_min, int64_t plates_max, int64_t center_bias, int64_t continent_chance, int64_t vel_min, int64_t vel_max) {
    Generation::TectonicWorld::config_num_plates_min = (int32_t)plates_min;
    Generation::TectonicWorld::config_num_plates_max = (int32_t)plates_max;
    Generation::TectonicWorld::config_center_bias = (int32_t)center_bias;
    Generation::TectonicWorld::config_continent_chance = (int32_t)continent_chance;
    Generation::TectonicWorld::config_vel_min = (int32_t)vel_min;
    Generation::TectonicWorld::config_vel_max = (int32_t)vel_max;
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
    _world.add_wall(x, y, w, h);
}

void SimBridge::add_polygon(int64_t x, int64_t y, godot::PackedInt64Array vx, godot::PackedInt64Array vy) {
    std::vector<int64_t> vec_x(vx.size());
    std::vector<int64_t> vec_y(vy.size());
    for (int i = 0; i < vx.size(); ++i) vec_x[i] = vx[i];
    for (int i = 0; i < vy.size(); ++i) vec_y[i] = vy[i];
    _world.add_polygon(x, y, vec_x, vec_y);
}

void SimBridge::clear_polygons() {
    _world.clear_polygons();
}

godot::TypedArray<godot::Dictionary> SimBridge::get_polygons() const {
    godot::TypedArray<godot::Dictionary> arr;
    for (const auto &p : _world.get_polygons()) {
        godot::Dictionary dict;
        dict["x"] = (int64_t)p.x;
        dict["y"] = (int64_t)p.y;
        godot::PackedInt64Array vx, vy;
        vx.resize(p.vx.size()); vy.resize(p.vy.size());
        for (size_t i = 0; i < p.vx.size(); ++i) { vx[i] = p.vx[i]; vy[i] = p.vy[i]; }
        dict["vx"] = vx;
        dict["vy"] = vy;
        arr.push_back(dict);
    }
    return arr;
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
    _world.generate_map_cellular_automata((uint64_t)seed, width, height, cell_size_fixed, (int32_t)iterations, (int32_t)fill_prob);
}



void SimBridge::clear_walls() {
    _world.clear_walls();
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
    godot::TypedArray<godot::Dictionary> arr;
    for (const auto &w : _world.get_walls()) {
        godot::Dictionary dict;
        dict["x"] = (int64_t)w.x;
        dict["y"] = (int64_t)w.y;
        dict["width"] = (int64_t)w.width;
        dict["height"] = (int64_t)w.height;
        arr.push_back(dict);
    }
    return arr;
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
    dict["min_cx"] = (int64_t)_world.get_grid().get_min_cx();
    dict["min_cy"] = (int64_t)_world.get_grid().get_min_cy();
    dict["width"] = (int64_t)_world.get_grid().get_width();
    dict["height"] = (int64_t)_world.get_grid().get_height();
    return dict;
}

int64_t SimBridge::get_cell_occupancy(int64_t x, int64_t y) const {
    return _world.get_grid().get_cell_occupancy(x, y);
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