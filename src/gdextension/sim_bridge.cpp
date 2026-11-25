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
    ClassDB::bind_method(D_METHOD("till_soil", "x", "y"), &SimBridge::till_soil);
    ClassDB::bind_method(D_METHOD("water_soil", "x", "y", "amount"), &SimBridge::water_soil, DEFVAL(30.0));
    ClassDB::bind_method(D_METHOD("plant_crop", "x", "y", "crop_type", "prefix", "suffix", "implicit"), &SimBridge::plant_crop, DEFVAL(0), DEFVAL(0), DEFVAL(0));
    ClassDB::bind_method(D_METHOD("harvest_crop", "x", "y"), &SimBridge::harvest_crop);
    ClassDB::bind_method(D_METHOD("is_crop_mature", "x", "y"), &SimBridge::is_crop_mature);
    ClassDB::bind_method(D_METHOD("get_soil_moisture", "x", "y"), &SimBridge::get_soil_moisture);
    ClassDB::bind_method(D_METHOD("get_soil_moisture_at", "x", "y"), &SimBridge::get_soil_moisture);
    ClassDB::bind_method(D_METHOD("get_soil_moisture_fixed", "x", "y"), &SimBridge::get_soil_moisture_fixed);
    ClassDB::bind_method(D_METHOD("get_soil_compactness_at", "x", "y"), &SimBridge::get_soil_compactness_at);
    ClassDB::bind_method(D_METHOD("get_soil_compactness_fixed_at", "x", "y"), &SimBridge::get_soil_compactness_fixed_at);
    ClassDB::bind_method(D_METHOD("get_last_dropped_item"), &SimBridge::get_last_dropped_item);
    ClassDB::bind_method(D_METHOD("get_last_generated_item"), &SimBridge::get_last_generated_item);
    ClassDB::bind_method(D_METHOD("place_structure", "x", "y", "structure_type"), &SimBridge::place_structure);
    ClassDB::bind_method(D_METHOD("remove_structure", "x", "y"), &SimBridge::remove_structure);
    ClassDB::bind_method(D_METHOD("modify_terrain", "x", "y", "delta_elev"), &SimBridge::modify_terrain);
    ClassDB::bind_method(D_METHOD("can_modify_terrain", "x", "y"), &SimBridge::can_modify_terrain);
    ClassDB::bind_method(D_METHOD("apply_fertilizer", "x", "y", "n", "p", "k", "ca", "mg", "s"), &SimBridge::apply_fertilizer);
    ClassDB::bind_method(D_METHOD("get_crop_types"), &SimBridge::get_crop_types);
    ClassDB::bind_method(D_METHOD("get_crop_prefixes"), &SimBridge::get_crop_prefixes);
    ClassDB::bind_method(D_METHOD("get_crop_suffixes"), &SimBridge::get_crop_suffixes);
    ClassDB::bind_method(D_METHOD("get_crop_implicits"), &SimBridge::get_crop_implicits);
    ClassDB::bind_method(D_METHOD("get_crop_growth"), &SimBridge::get_crop_growth);
    ClassDB::bind_method(D_METHOD("get_crop_health"), &SimBridge::get_crop_health);
    
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

    ClassDB::bind_method(D_METHOD("set_world_gen_mode", "mode"), &SimBridge::set_world_gen_mode);
    ClassDB::bind_method(D_METHOD("get_world_gen_mode"), &SimBridge::get_world_gen_mode);
    ClassDB::bind_method(D_METHOD("load_opentopo_dataset", "path"), &SimBridge::load_opentopo_dataset);
    ClassDB::bind_method(D_METHOD("set_opentopo_sea_level_offset", "offset_meters"), &SimBridge::set_opentopo_sea_level_offset);
    ClassDB::bind_method(D_METHOD("get_opentopo_sea_level_offset"), &SimBridge::get_opentopo_sea_level_offset);
    ClassDB::bind_method(D_METHOD("set_opentopo_sea_buffer_tiles", "tiles"), &SimBridge::set_opentopo_sea_buffer_tiles);
    ClassDB::bind_method(D_METHOD("get_opentopo_sea_buffer_tiles"), &SimBridge::get_opentopo_sea_buffer_tiles);
    ClassDB::bind_method(D_METHOD("set_apply_coastal_erosion", "enable"), &SimBridge::set_apply_coastal_erosion);
    ClassDB::bind_method(D_METHOD("get_apply_coastal_erosion"), &SimBridge::get_apply_coastal_erosion);
    ClassDB::bind_method(D_METHOD("set_coastal_erosion_iterations", "iterations"), &SimBridge::set_coastal_erosion_iterations);
    ClassDB::bind_method(D_METHOD("get_coastal_erosion_iterations"), &SimBridge::get_coastal_erosion_iterations);
    ClassDB::bind_method(D_METHOD("set_coastal_erosion_strength", "strength"), &SimBridge::set_coastal_erosion_strength);
    ClassDB::bind_method(D_METHOD("get_coastal_erosion_strength"), &SimBridge::get_coastal_erosion_strength);
    ClassDB::bind_method(D_METHOD("set_apply_droplet_erosion", "enable"), &SimBridge::set_apply_droplet_erosion);
    ClassDB::bind_method(D_METHOD("get_apply_droplet_erosion"), &SimBridge::get_apply_droplet_erosion);
    ClassDB::bind_method(D_METHOD("set_droplet_count", "count"), &SimBridge::set_droplet_count);
    ClassDB::bind_method(D_METHOD("get_droplet_count"), &SimBridge::get_droplet_count);
    ClassDB::bind_method(D_METHOD("set_droplet_erosion_rate", "rate"), &SimBridge::set_droplet_erosion_rate);
    ClassDB::bind_method(D_METHOD("get_droplet_erosion_rate"), &SimBridge::get_droplet_erosion_rate);
    ClassDB::bind_method(D_METHOD("set_droplet_deposition_rate", "rate"), &SimBridge::set_droplet_deposition_rate);
    ClassDB::bind_method(D_METHOD("get_droplet_deposition_rate"), &SimBridge::get_droplet_deposition_rate);
    ClassDB::bind_method(D_METHOD("set_river_min_area", "area"), &SimBridge::set_river_min_area);
    ClassDB::bind_method(D_METHOD("get_river_min_area"), &SimBridge::get_river_min_area);
    ClassDB::bind_method(D_METHOD("set_river_width_multiplier", "mult"), &SimBridge::set_river_width_multiplier);
    ClassDB::bind_method(D_METHOD("get_river_width_multiplier"), &SimBridge::get_river_width_multiplier);
    ClassDB::bind_method(D_METHOD("set_river_carve_depth", "depth"), &SimBridge::set_river_carve_depth);
    ClassDB::bind_method(D_METHOD("get_river_carve_depth"), &SimBridge::get_river_carve_depth);
    ClassDB::bind_method(D_METHOD("get_nearest_river_sample", "x", "y"), &SimBridge::get_nearest_river_sample);
    ClassDB::bind_method(D_METHOD("get_water_depth"), &SimBridge::get_water_depth);
    ClassDB::bind_method(D_METHOD("get_water_depth_data"), &SimBridge::get_water_depth_data);
    ClassDB::bind_method(D_METHOD("get_water_depth_texture"), &SimBridge::get_water_depth_texture);
    ClassDB::bind_method(D_METHOD("get_parametric_river_reaches"), &SimBridge::get_parametric_river_reaches);
    ClassDB::bind_method(D_METHOD("get_estuary_mouths"), &SimBridge::get_estuary_mouths);
    ClassDB::bind_method(D_METHOD("get_river_flow_vector_at", "x", "y"), &SimBridge::get_river_flow_vector_at);

    // Terrain Tree / Chunked Vegetation APIs
    ClassDB::bind_method(D_METHOD("chop_tree", "world_pos", "damage", "max_reach"), &SimBridge::chop_tree, DEFVAL(25), DEFVAL(0.0));
    ClassDB::bind_method(D_METHOD("chop_tree_fixed", "world_x", "world_z", "damage", "max_reach"), &SimBridge::chop_tree_fixed, DEFVAL(25), DEFVAL(0));
    ClassDB::bind_method(D_METHOD("get_tree_health", "tree_id"), &SimBridge::get_tree_health);
    ClassDB::bind_method(D_METHOD("is_tree_alive", "tree_id"), &SimBridge::is_tree_alive);
    ClassDB::bind_method(D_METHOD("get_nearest_tree", "world_pos", "max_reach"), &SimBridge::get_nearest_tree, DEFVAL(0.0));
    ClassDB::bind_method(D_METHOD("get_tree_count"), &SimBridge::get_tree_count);
    ClassDB::bind_method(D_METHOD("get_trees_in_chunk", "cx", "cy"), &SimBridge::get_trees_in_chunk);
    ClassDB::bind_method(D_METHOD("get_all_trees"), &SimBridge::get_all_trees);
    ClassDB::bind_method(D_METHOD("get_tree_render_data", "cx", "cy"), &SimBridge::get_tree_render_data, DEFVAL(-1), DEFVAL(-1));
    ClassDB::bind_method(D_METHOD("fast_forward_chunk_trees", "cx", "cy", "target_tick"), &SimBridge::fast_forward_chunk_trees);
    ClassDB::bind_method(D_METHOD("get_chunk_tree_counts"), &SimBridge::get_chunk_tree_counts);
    ClassDB::bind_method(D_METHOD("get_chunk_grid_size"), &SimBridge::get_chunk_grid_size);
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
    if (params.has("margin_fraction")) cfg.margin_fraction = (int64_t)((double)params["margin_fraction"] * 65536.0);
    if (params.has("sea_floor_elevation")) cfg.sea_floor_elevation = (int64_t)((double)params["sea_floor_elevation"] * 65536.0);
    if (params.has("oceanic_base_elevation")) cfg.oceanic_base_elevation = (int64_t)((double)params["oceanic_base_elevation"] * 65536.0);
    if (params.has("island_arc_base_elevation")) cfg.island_arc_base_elevation = (int64_t)((double)params["island_arc_base_elevation"] * 65536.0);
    if (params.has("coastal_boundary_elevation")) cfg.coastal_boundary_elevation = (int64_t)((double)params["coastal_boundary_elevation"] * 65536.0);
    if (params.has("shelf_width")) cfg.shelf_width = (int64_t)((double)params["shelf_width"] * 65536.0);
    if (params.has("shelf_depth")) cfg.shelf_depth = (int64_t)((double)params["shelf_depth"] * 65536.0);
    if (params.has("detail_noise_scale")) cfg.detail_noise_scale = (int64_t)((double)params["detail_noise_scale"] * 65536.0);
    if (params.has("noise_octaves")) cfg.noise_octaves = (int)(int64_t)params["noise_octaves"];

    if (params.has("world_gen_mode")) {
        int64_t mode = params["world_gen_mode"];
        _world.set_world_gen_mode((WorldGenMode)mode);
    }
    if (params.has("opentopo_dataset")) {
        godot::String p = params["opentopo_dataset"];
        _world.load_opentopo_dataset(p.utf8().get_data());
    }
    if (params.has("sea_level_offset")) {
        double off = params["sea_level_offset"];
        _world.set_opentopo_sea_level_offset((int64_t)(off * 65536.0));
    }
    if (params.has("sea_buffer_tiles")) {
        int64_t b = params["sea_buffer_tiles"];
        _world.set_opentopo_sea_buffer_tiles((int32_t)b);
    }
    if (params.has("apply_erosion")) {
        bool e = params["apply_erosion"];
        _world.set_opentopo_apply_erosion(e);
    }
    if (params.has("apply_coastal_erosion")) {
        bool ce = params["apply_coastal_erosion"];
        _world.set_apply_coastal_erosion(ce);
    }
    if (params.has("coastal_erosion_iterations")) {
        int64_t ci = params["coastal_erosion_iterations"];
        _world.set_coastal_erosion_iterations((int32_t)ci);
    }
    if (params.has("coastal_erosion_strength")) {
        double cs = params["coastal_erosion_strength"];
        _world.set_coastal_erosion_strength((int64_t)(cs * 65536.0));
    }
    if (params.has("apply_droplet_erosion")) {
        bool ade = params["apply_droplet_erosion"];
        _world.set_apply_droplet_erosion(ade);
    }
    if (params.has("droplet_count")) {
        int64_t dc = params["droplet_count"];
        _world.get_droplet_config_mut().droplet_count = (int32_t)dc;
    }
    if (params.has("droplet_erosion_rate")) {
        double er = params["droplet_erosion_rate"];
        _world.get_droplet_config_mut().erosion_rate = (int64_t)(er * 65536.0);
    }
    if (params.has("droplet_deposition_rate")) {
        double dr = params["droplet_deposition_rate"];
        _world.get_droplet_config_mut().deposition_rate = (int64_t)(dr * 65536.0);
    }
    if (params.has("river_min_area")) {
        int64_t rma = params["river_min_area"];
        _world.set_river_min_area((int32_t)rma);
    }
    if (params.has("river_width_multiplier")) {
        double rwm = params["river_width_multiplier"];
        _world.set_river_width_multiplier((int64_t)(rwm * 65536.0));
    }
    if (params.has("river_width_mult")) {
        double rwm = params["river_width_mult"];
        _world.set_river_width_multiplier((int64_t)(rwm * 65536.0));
    }
    if (params.has("river_carve_depth")) {
        double rcd = params["river_carve_depth"];
        _world.set_river_carve_depth((int64_t)(rcd * 65536.0));
    }
    if (params.has("center_bias")) {
        double val = (double)params["center_bias"];
        if (std::abs(val) > 1.5) {
            cfg.center_bias = (int64_t)((val / 100.0) * 65536.0);
        } else {
            cfg.center_bias = (int64_t)(val * 65536.0);
        }
    }
    if (params.has("velocity_min")) {
        double v = (double)params["velocity_min"];
        cfg.velocity_min = (int64_t)(v * 65536.0);
    }
    if (params.has("velocity_max")) {
        double v = (double)params["velocity_max"];
        cfg.velocity_max = (int64_t)(v * 65536.0);
    }
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
    dict["margin_fraction"] = (double)cfg.margin_fraction / 65536.0;
    dict["sea_floor_elevation"] = (double)cfg.sea_floor_elevation / 65536.0;
    dict["oceanic_base_elevation"] = (double)cfg.oceanic_base_elevation / 65536.0;
    dict["island_arc_base_elevation"] = (double)cfg.island_arc_base_elevation / 65536.0;
    dict["coastal_boundary_elevation"] = (double)cfg.coastal_boundary_elevation / 65536.0;
    dict["shelf_width"] = (double)cfg.shelf_width / 65536.0;
    dict["shelf_depth"] = (double)cfg.shelf_depth / 65536.0;
    dict["detail_noise_scale"] = (double)cfg.detail_noise_scale / 65536.0;
    dict["noise_octaves"] = (int64_t)cfg.noise_octaves;

    dict["world_gen_mode"] = (int64_t)_world.get_world_gen_mode();
    dict["sea_level_offset"] = (double)_world.get_opentopo_sea_level_offset() / 65536.0;
    dict["sea_buffer_tiles"] = (int64_t)_world.get_opentopo_sea_buffer_tiles();
    dict["apply_erosion"] = _world.get_opentopo_apply_erosion();
    dict["apply_coastal_erosion"] = _world.get_apply_coastal_erosion();
    dict["coastal_erosion_iterations"] = (int64_t)_world.get_coastal_erosion_iterations();
    dict["coastal_erosion_strength"] = (double)_world.get_coastal_erosion_strength() / 65536.0;
    dict["apply_droplet_erosion"] = _world.get_apply_droplet_erosion();
    dict["droplet_count"] = (int64_t)_world.get_droplet_config().droplet_count;
    dict["droplet_erosion_rate"] = (double)_world.get_droplet_config().erosion_rate / 65536.0;
    dict["droplet_deposition_rate"] = (double)_world.get_droplet_config().deposition_rate / 65536.0;
    dict["river_min_area"] = (int64_t)_world.get_river_min_area();
    dict["river_width_multiplier"] = (double)_world.get_river_width_multiplier() / 65536.0;
    dict["river_carve_depth"] = (double)_world.get_river_carve_depth() / 65536.0;
    dict["center_bias"] = (double)cfg.center_bias / 65536.0;
    dict["velocity_min"] = (double)cfg.velocity_min / 65536.0;
    dict["velocity_max"] = (double)cfg.velocity_max / 65536.0;

    return dict;
}

void SimBridge::set_tectonic_config(int64_t plates_min, int64_t plates_max, int64_t center_bias, int64_t continent_chance, int64_t vel_min, int64_t vel_max) {
    auto& cfg = _world.get_tectonic_config_mut();
    cfg.num_plates = (int32_t)plates_max;
    cfg.continent_chance_pct = (int32_t)continent_chance;
    if (std::abs(center_bias) > 1 && std::abs(center_bias) <= 100) {
        cfg.center_bias = (center_bias * ONE) / 100;
    } else if (std::abs(center_bias) > 100) {
        cfg.center_bias = center_bias;
    } else {
        cfg.center_bias = center_bias * ONE;
    }
    if (vel_min > 0) cfg.velocity_min = vel_min * 65536 / 1000;
    if (vel_max > 0) cfg.velocity_max = vel_max * 65536 / 1000;
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
    int64_t* ptr = arr.ptrw();
    for (size_t i = 0; i < wt.size(); ++i) {
        ptr[i] = wt[i];
    }
    return arr;
}

godot::PackedByteArray SimBridge::get_aquifers() const {
    godot::PackedByteArray arr;
    const auto& aq = _world.get_aquifers();
    arr.resize(aq.size());
    if (!aq.empty()) {
        memcpy(arr.ptrw(), aq.data(), aq.size());
    }
    return arr;
}

godot::PackedByteArray SimBridge::get_rivers() const {
    godot::PackedByteArray arr;
    const auto& r = _world.get_rivers();
    arr.resize(r.size());
    if (!r.empty()) {
        memcpy(arr.ptrw(), r.data(), r.size());
    }
    return arr;
}

godot::PackedInt32Array SimBridge::get_flow_targets() const {
    godot::PackedInt32Array arr;
    const auto& ft = _world.get_flow_targets();
    arr.resize(ft.size());
    if (!ft.empty()) {
        memcpy(arr.ptrw(), ft.data(), ft.size() * sizeof(int32_t));
    }
    return arr;
}

godot::PackedByteArray SimBridge::get_chunk_tiers() const {
    godot::PackedByteArray arr;
    const auto& t = _world.get_chunk_tiers();
    arr.resize(t.size());
    if (!t.empty()) {
        memcpy(arr.ptrw(), t.data(), t.size());
    }
    return arr;
}

godot::PackedInt64Array SimBridge::get_soil_phosphorus() const {
    godot::PackedInt64Array arr;
    const auto& t = _world.get_soil_phosphorus();
    arr.resize(t.size());
    int64_t* ptr = arr.ptrw();
    for (size_t i = 0; i < t.size(); ++i) {
        ptr[i] = t[i];
    }
    return arr;
}

godot::PackedInt64Array SimBridge::get_soil_potassium() const {
    godot::PackedInt64Array arr;
    const auto& t = _world.get_soil_potassium();
    arr.resize(t.size());
    int64_t* ptr = arr.ptrw();
    for (size_t i = 0; i < t.size(); ++i) {
        ptr[i] = t[i];
    }
    return arr;
}

godot::PackedInt64Array SimBridge::get_soil_calcium() const {
    godot::PackedInt64Array arr;
    const auto& t = _world.get_soil_calcium();
    arr.resize(t.size());
    int64_t* ptr = arr.ptrw();
    for (size_t i = 0; i < t.size(); ++i) {
        ptr[i] = t[i];
    }
    return arr;
}

godot::PackedInt64Array SimBridge::get_soil_magnesium() const {
    godot::PackedInt64Array arr;
    const auto& t = _world.get_soil_magnesium();
    arr.resize(t.size());
    int64_t* ptr = arr.ptrw();
    for (size_t i = 0; i < t.size(); ++i) {
        ptr[i] = t[i];
    }
    return arr;
}

godot::PackedInt64Array SimBridge::get_soil_sulfur() const {
    godot::PackedInt64Array arr;
    const auto& t = _world.get_soil_sulfur();
    arr.resize(t.size());
    int64_t* ptr = arr.ptrw();
    for (size_t i = 0; i < t.size(); ++i) {
        ptr[i] = t[i];
    }
    return arr;
}


godot::PackedInt64Array SimBridge::get_soil_nitrogen() const {
    godot::PackedInt64Array arr;
    const auto& t = _world.get_soil_nitrogen();
    arr.resize(t.size());
    int64_t* ptr = arr.ptrw();
    for (size_t i = 0; i < t.size(); ++i) {
        ptr[i] = t[i];
    }
    return arr;
}

godot::PackedInt64Array SimBridge::get_soil_compactness() const {
    godot::PackedInt64Array arr;
    const auto& t = _world.get_soil_compactness();
    arr.resize(t.size());
    int64_t* ptr = arr.ptrw();
    for (size_t i = 0; i < t.size(); ++i) {
        ptr[i] = t[i];
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
    if (data.size() < 32) return;
    BisectSnapshot snap;
    if (SimWorld::deserialize_snapshot(snap, data.ptr(), (size_t)data.size()) != nullptr) {
        _world.restore(snap);
    }
}

bool SimBridge::till_soil(int64_t x, int64_t y) {
    return _world.till_soil((int32_t)x, (int32_t)y);
}

bool SimBridge::water_soil(int64_t x, int64_t y, double amount) {
    int64_t amt_fixed = (amount > 0.0) ? (int64_t)(amount * 65536.0) : (30LL * ONE);
    return _world.water_soil((int32_t)x, (int32_t)y, amt_fixed);
}

bool SimBridge::plant_crop(int64_t x, int64_t y, int64_t crop_type, int64_t prefix, int64_t suffix, int64_t implicit) {
    return _world.plant_crop((int32_t)x, (int32_t)y, (uint8_t)crop_type, (uint8_t)prefix, (uint8_t)suffix, (uint8_t)implicit);
}

bool SimBridge::harvest_crop(int64_t x, int64_t y) {
    return _world.harvest_crop((int32_t)x, (int32_t)y);
}

bool SimBridge::is_crop_mature(int64_t x, int64_t y) const {
    return _world.is_crop_mature((int32_t)x, (int32_t)y);
}

double SimBridge::get_soil_moisture(int64_t x, int64_t y) const {
    return (double)_world.get_soil_moisture((int32_t)x, (int32_t)y) / 65536.0;
}

int64_t SimBridge::get_soil_moisture_fixed(int64_t x, int64_t y) const {
    return (int64_t)_world.get_soil_moisture((int32_t)x, (int32_t)y);
}

double SimBridge::get_soil_compactness_at(int64_t x, int64_t y) const {
    return (double)_world.get_soil_compactness((int32_t)x, (int32_t)y) / 65536.0;
}

int64_t SimBridge::get_soil_compactness_fixed_at(int64_t x, int64_t y) const {
    return (int64_t)_world.get_soil_compactness((int32_t)x, (int32_t)y);
}

int64_t SimBridge::get_last_dropped_item() const {
    return (int64_t)_world.get_last_dropped_item();
}

godot::Dictionary SimBridge::get_last_generated_item() const {
    return _world.get_last_generated_item().to_dict();
}

godot::PackedByteArray SimBridge::get_crop_types() const {
    godot::PackedByteArray arr;
    const auto& t = _world.get_crop_type();
    arr.resize(t.size());
    if (!t.empty()) {
        memcpy(arr.ptrw(), t.data(), t.size());
    }
    return arr;
}

godot::PackedByteArray SimBridge::get_crop_prefixes() const {
    godot::PackedByteArray arr;
    const auto& t = _world.get_crop_prefix();
    arr.resize(t.size());
    if (!t.empty()) {
        memcpy(arr.ptrw(), t.data(), t.size());
    }
    return arr;
}

godot::PackedByteArray SimBridge::get_crop_suffixes() const {
    godot::PackedByteArray arr;
    const auto& t = _world.get_crop_suffix();
    arr.resize(t.size());
    if (!t.empty()) {
        memcpy(arr.ptrw(), t.data(), t.size());
    }
    return arr;
}

godot::PackedByteArray SimBridge::get_crop_implicits() const {
    godot::PackedByteArray arr;
    const auto& t = _world.get_crop_implicit();
    arr.resize(t.size());
    if (!t.empty()) {
        memcpy(arr.ptrw(), t.data(), t.size());
    }
    return arr;
}

godot::PackedInt64Array SimBridge::get_crop_growth() const {
    godot::PackedInt64Array arr;
    const auto& t = _world.get_crop_growth();
    arr.resize(t.size());
    int64_t* ptr = arr.ptrw();
    for (size_t i = 0; i < t.size(); ++i) {
        ptr[i] = t[i];
    }
    return arr;
}

godot::PackedInt64Array SimBridge::get_crop_health() const {
    godot::PackedInt64Array arr;
    const auto& t = _world.get_crop_health();
    arr.resize(t.size());
    int64_t* ptr = arr.ptrw();
    for (size_t i = 0; i < t.size(); ++i) {
        ptr[i] = t[i];
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
    if (!t.empty()) {
        memcpy(arr.ptrw(), t.data(), t.size());
    }
    return arr;
}

godot::PackedByteArray SimBridge::get_structure_types() const {
    godot::PackedByteArray arr;
    const auto& t = _world.get_structure_type();
    arr.resize(t.size());
    if (!t.empty()) {
        memcpy(arr.ptrw(), t.data(), t.size());
    }
    return arr;
}

godot::PackedInt64Array SimBridge::get_water_pressures() const {
    godot::PackedInt64Array arr;
    const auto& t = _world.get_water_pressure();
    arr.resize(t.size());
    int64_t* ptr = arr.ptrw();
    for (size_t i = 0; i < t.size(); ++i) {
        ptr[i] = t[i];
    }
    return arr;
}

void SimBridge::place_structure(int64_t x, int64_t y, int64_t structure_type) {
    _world.place_structure((int32_t)x, (int32_t)y, (uint8_t)structure_type);
}

void SimBridge::remove_structure(int64_t x, int64_t y) {
    _world.remove_structure((int32_t)x, (int32_t)y);
}

bool SimBridge::modify_terrain(int64_t x, int64_t y, int64_t delta_elev) {
    return _world.modify_terrain((int32_t)x, (int32_t)y, (int32_t)delta_elev);
}

bool SimBridge::can_modify_terrain(int64_t x, int64_t y) const {
    return _world.can_modify_terrain((int32_t)x, (int32_t)y);
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
    if (!t.empty()) {
        memcpy(arr.ptrw(), t.data(), t.size() * sizeof(int32_t));
    }
    return arr;
}

godot::PackedByteArray SimBridge::get_biome() const {
    godot::PackedByteArray arr;
    const auto& t = _world.get_biome();
    arr.resize(t.size());
    if (!t.empty()) {
        memcpy(arr.ptrw(), t.data(), t.size());
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
    if (!t.empty()) {
        memcpy(arr.ptrw(), t.data(), t.size());
    }
    return arr;
}

godot::PackedByteArray SimBridge::get_mineral_types() const {
    godot::PackedByteArray arr;
    const auto& t = _world.get_mineral_type();
    arr.resize(t.size());
    if (!t.empty()) {
        memcpy(arr.ptrw(), t.data(), t.size());
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
    int64_t* ptr = arr.ptrw();
    for (size_t i = 0; i < res.size(); ++i) {
        ptr[i] = res[i].current_volume;
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
    int64_t* ptr = arr.ptrw();
    for (size_t i = 0; i < rivers.size(); ++i) {
        ptr[i] = rivers[i].water_level;
    }
    return arr;
}

godot::PackedInt64Array SimBridge::get_river_flow_rates() const {
    godot::PackedInt64Array arr;
    const auto& rivers = _world.get_hydraulic_graph().get_river_belts();
    arr.resize(rivers.size());
    int64_t* ptr = arr.ptrw();
    for (size_t i = 0; i < rivers.size(); ++i) {
        ptr[i] = rivers[i].flow_rate;
    }
    return arr;
}

godot::PackedInt64Array SimBridge::get_river_max_rates() const {
    godot::PackedInt64Array arr;
    const auto& rivers = _world.get_hydraulic_graph().get_river_belts();
    arr.resize(rivers.size());
    int64_t* ptr = arr.ptrw();
    for (size_t i = 0; i < rivers.size(); ++i) {
        ptr[i] = rivers[i].max_rate;
    }
    return arr;
}

godot::PackedInt64Array SimBridge::get_river_pump_rates() const {
    godot::PackedInt64Array arr;
    const auto& rivers = _world.get_hydraulic_graph().get_river_belts();
    arr.resize(rivers.size());
    int64_t* ptr = arr.ptrw();
    for (size_t i = 0; i < rivers.size(); ++i) {
        ptr[i] = rivers[i].pump_rate;
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

    int64_t sea_level_fixed = static_cast<int64_t>(sea_level * 65536.0);
    int64_t cell_size_fixed = _world.get_cell_size_fixed();
    if (cell_size_fixed <= 0) cell_size_fixed = 2 * ONE;

    std::vector<int64_t> boundary_mask;
    std::vector<Vec2Fixed> plume_vel;
    _world.get_parametric_rivers().compute_estuary_fields(
        elevation, w, h, cell_size_fixed, sea_level_fixed, boundary_mask, plume_vel
    );

    for (int32_t y = 0; y < h; ++y) {
        for (int32_t x = 0; x < w; ++x) {
            size_t idx = static_cast<size_t>(y * w + x);
            float el = static_cast<float>(elevation[idx]) / 65536.0f;
            float b_mask = (idx < boundary_mask.size()) ? (static_cast<float>(boundary_mask[idx]) / 65536.0f) : 0.0f;
            float vx = (idx < plume_vel.size()) ? (static_cast<float>(plume_vel[idx].x) / 65536.0f) : 0.0f;
            float vy = (idx < plume_vel.size()) ? (static_cast<float>(plume_vel[idx].y) / 65536.0f) : 0.0f;

            // If elevation is at or above sea level, always mark as solid land boundary
            if (el >= static_cast<float>(sea_level)) {
                b_mask = 1.0f;
            }

            size_t out_idx = idx * 4;
            ptr[out_idx + 0] = el;       // R: Physical elevation in meters (sea level = 0.0m)
            ptr[out_idx + 1] = b_mask;   // G: Boundary / Estuary Damping Mask (1.0 = solid boundary, 0.0 = ocean)
            ptr[out_idx + 2] = vx;       // B: River outflow plume velocity X (m/s)
            ptr[out_idx + 3] = vy;       // A: River outflow plume velocity Y (m/s)
        }
    }

    return cage;
}

godot::TypedArray<godot::Dictionary> SimBridge::get_estuary_mouths() const {
    godot::TypedArray<godot::Dictionary> arr;
    auto mouths = _world.get_parametric_rivers().get_estuary_mouths(0);
    for (const auto& m : mouths) {
        godot::Dictionary d;
        d["reach_id"] = (int64_t)m.reach_id;
        d["position"] = godot::Vector2((double)m.position.x / 65536.0, (double)m.position.y / 65536.0);
        d["direction"] = godot::Vector2((double)m.outflow_direction.x / 65536.0, (double)m.outflow_direction.y / 65536.0);
        d["width"] = (double)m.width / 65536.0;
        d["velocity"] = (double)m.discharge_velocity / 65536.0;
        arr.push_back(d);
    }
    return arr;
}

void SimBridge::set_world_gen_mode(int64_t mode) {
    _world.set_world_gen_mode((WorldGenMode)mode);
}

int64_t SimBridge::get_world_gen_mode() const {
    return (int64_t)_world.get_world_gen_mode();
}

bool SimBridge::load_opentopo_dataset(godot::String path) {
    return _world.load_opentopo_dataset(path.utf8().get_data());
}

void SimBridge::set_opentopo_sea_level_offset(double offset_meters) {
    _world.set_opentopo_sea_level_offset((int64_t)(offset_meters * 65536.0));
}

double SimBridge::get_opentopo_sea_level_offset() const {
    return (double)_world.get_opentopo_sea_level_offset() / 65536.0;
}

void SimBridge::set_opentopo_sea_buffer_tiles(int64_t tiles) {
    _world.set_opentopo_sea_buffer_tiles((int32_t)tiles);
}

int64_t SimBridge::get_opentopo_sea_buffer_tiles() const {
    return (int64_t)_world.get_opentopo_sea_buffer_tiles();
}

godot::TypedArray<godot::Dictionary> SimBridge::get_parametric_river_reaches() const {
    godot::TypedArray<godot::Dictionary> arr;
    const auto& net = _world.get_parametric_rivers();
    for (const auto& r : net.reaches) {
        godot::Dictionary d;
        d["id"] = (int64_t)r.id;
        d["tier"] = (int64_t)r.tier;
        d["total_length"] = (double)r.total_length / 65536.0;
        d["flow_rate"] = (double)r.flow_rate / 65536.0;
        d["max_capacity"] = (double)r.max_capacity / 65536.0;
        d["water_level"] = (double)r.water_level / 65536.0;
        d["is_dry"] = r.is_dry;

        godot::PackedVector2Array wps;
        for (const auto& pt : r.waypoints) {
            wps.push_back(godot::Vector2((double)pt.x / 65536.0, (double)pt.y / 65536.0));
        }
        d["waypoints"] = wps;

        godot::PackedFloat32Array widths;
        for (int64_t w_val : r.widths) {
            widths.push_back((float)w_val / 65536.0f);
        }
        d["widths"] = widths;

        godot::PackedFloat32Array arc_lengths;
        for (int64_t a_val : r.arc_lengths) {
            arc_lengths.push_back((float)a_val / 65536.0f);
        }
        d["arc_lengths"] = arc_lengths;

        godot::PackedFloat32Array bed_elevs;
        for (int64_t b_val : r.bed_elevations) {
            bed_elevs.push_back((float)b_val / 65536.0f);
        }
        d["bed_elevations"] = bed_elevs;

        godot::PackedFloat32Array water_elevs;
        for (int64_t w_val : r.water_elevations) {
            water_elevs.push_back((float)w_val / 65536.0f);
        }
        d["water_elevations"] = water_elevs;

        arr.push_back(d);
    }
    return arr;
}

godot::Vector2 SimBridge::get_river_flow_vector_at(double x, double y) const {
    int64_t dist = 0, width = 0, depth = 0;
    Vec2Fixed flow_vec;
    uint32_t reach_id = 0;
    Vec2Fixed p((int64_t)(x * 65536.0), (int64_t)(y * 65536.0));
    if (_world.get_parametric_rivers().sample_nearest_river(p, dist, width, flow_vec, depth, reach_id)) {
        if (dist <= width / 2) {
            return godot::Vector2((double)flow_vec.x / 65536.0, (double)flow_vec.y / 65536.0);
        }
    }
    return godot::Vector2(0, 0);
}

void SimBridge::set_apply_coastal_erosion(bool enable) {
    _world.set_apply_coastal_erosion(enable);
}

bool SimBridge::get_apply_coastal_erosion() const {
    return _world.get_apply_coastal_erosion();
}

void SimBridge::set_coastal_erosion_iterations(int64_t iterations) {
    _world.set_coastal_erosion_iterations((int32_t)iterations);
}

int64_t SimBridge::get_coastal_erosion_iterations() const {
    return _world.get_coastal_erosion_iterations();
}

void SimBridge::set_coastal_erosion_strength(double strength) {
    _world.set_coastal_erosion_strength((int64_t)(strength * 65536.0));
}

double SimBridge::get_coastal_erosion_strength() const {
    return (double)_world.get_coastal_erosion_strength() / 65536.0;
}

void SimBridge::set_apply_droplet_erosion(bool enable) {
    _world.set_apply_droplet_erosion(enable);
}

bool SimBridge::get_apply_droplet_erosion() const {
    return _world.get_apply_droplet_erosion();
}

void SimBridge::set_droplet_count(int64_t count) {
    _world.get_droplet_config_mut().droplet_count = (int32_t)count;
}

int64_t SimBridge::get_droplet_count() const {
    return (int64_t)_world.get_droplet_config().droplet_count;
}

void SimBridge::set_droplet_erosion_rate(double rate) {
    _world.get_droplet_config_mut().erosion_rate = (int64_t)(rate * 65536.0);
}

double SimBridge::get_droplet_erosion_rate() const {
    return (double)_world.get_droplet_config().erosion_rate / 65536.0;
}

void SimBridge::set_droplet_deposition_rate(double rate) {
    _world.get_droplet_config_mut().deposition_rate = (int64_t)(rate * 65536.0);
}

double SimBridge::get_droplet_deposition_rate() const {
    return (double)_world.get_droplet_config().deposition_rate / 65536.0;
}

void SimBridge::set_river_min_area(int64_t area) {
    _world.set_river_min_area((int32_t)area);
}

int64_t SimBridge::get_river_min_area() const {
    return (int64_t)_world.get_river_min_area();
}

void SimBridge::set_river_width_multiplier(double mult) {
    _world.set_river_width_multiplier((int64_t)(mult * 65536.0));
}

double SimBridge::get_river_width_multiplier() const {
    return (double)_world.get_river_width_multiplier() / 65536.0;
}

void SimBridge::set_river_carve_depth(double depth) {
    _world.set_river_carve_depth((int64_t)(depth * 65536.0));
}

double SimBridge::get_river_carve_depth() const {
    return (double)_world.get_river_carve_depth() / 65536.0;
}

godot::Dictionary SimBridge::get_nearest_river_sample(double x, double y) const {
    godot::Dictionary d;
    int64_t dist = 0, width = 0, depth = 0;
    Vec2Fixed flow_vec;
    uint32_t reach_id = 0;
    Vec2Fixed p((int64_t)(x * 65536.0), (int64_t)(y * 65536.0));
    bool found = _world.get_parametric_rivers().sample_nearest_river(p, dist, width, flow_vec, depth, reach_id);
    d["found"] = found;
    if (found) {
        d["reach_id"] = (int64_t)reach_id;
        d["dist_to_center"] = (double)dist / 65536.0;
        d["width"] = (double)width / 65536.0;
        d["depth"] = (double)depth / 65536.0;
        d["flow_vector"] = godot::Vector2((double)flow_vec.x / 65536.0, (double)flow_vec.y / 65536.0);
        d["in_channel"] = (dist <= width / 2);

        int64_t tier = 1;
        double flow_rate = 0.0;
        for (const auto& r : _world.get_parametric_rivers().reaches) {
            if (r.id == reach_id) {
                tier = (int64_t)r.tier;
                flow_rate = (double)r.flow_rate / 65536.0;
                break;
            }
        }
        d["tier"] = tier;
        d["flow_rate"] = flow_rate;
    }
    return d;
}

godot::PackedFloat32Array SimBridge::get_water_depth() const {
    godot::PackedFloat32Array arr;
    const auto& wd = _world.get_water_depth();
    arr.resize(wd.size());
    float* ptr = arr.ptrw();
    for (size_t i = 0; i < wd.size(); ++i) {
        ptr[i] = static_cast<float>(wd[i]) / 65536.0f;
    }
    return arr;
}

godot::PackedFloat32Array SimBridge::get_water_depth_data() const {
    godot::PackedFloat32Array arr;
    const auto& wd = _world.get_water_depth();
    const auto& fx = _world.get_water_flow_x();
    const auto& fy = _world.get_water_flow_y();
    const auto& fl = _world.get_water_flux();

    size_t total = wd.size();
    arr.resize(total * 4);
    float* ptr = arr.ptrw();

    for (size_t i = 0; i < total; ++i) {
        ptr[i * 4 + 0] = static_cast<float>(wd[i]) / 65536.0f; // R: water depth in meters
        ptr[i * 4 + 1] = (i < fl.size()) ? (static_cast<float>(fl[i]) / 65536.0f) : 0.0f; // G: water flux
        ptr[i * 4 + 2] = (i < fx.size()) ? (static_cast<float>(fx[i]) / 65536.0f) : 0.0f; // B: flow vector X
        ptr[i * 4 + 3] = (i < fy.size()) ? (static_cast<float>(fy[i]) / 65536.0f) : 0.0f; // A: flow vector Y
    }
    return arr;
}

godot::Ref<godot::ImageTexture> SimBridge::get_water_depth_texture() const {
    int32_t w = _world.get_map_width();
    int32_t h = _world.get_map_height();
    if (w <= 0 || h <= 0) return godot::Ref<godot::ImageTexture>();

    const auto& wd = _world.get_water_depth();
    const auto& fx = _world.get_water_flow_x();
    const auto& fy = _world.get_water_flow_y();
    const auto& fl = _world.get_water_flux();

    size_t total = static_cast<size_t>(w) * h;
    if (wd.size() != total) return godot::Ref<godot::ImageTexture>();

    godot::PackedByteArray byte_data;
    byte_data.resize(total * 4 * sizeof(float));
    float* ptr = reinterpret_cast<float*>(byte_data.ptrw());

    for (size_t i = 0; i < total; ++i) {
        ptr[i * 4 + 0] = static_cast<float>(wd[i]) / 65536.0f;
        ptr[i * 4 + 1] = (i < fl.size()) ? (static_cast<float>(fl[i]) / 65536.0f) : 0.0f;
        ptr[i * 4 + 2] = (i < fx.size()) ? (static_cast<float>(fx[i]) / 65536.0f) : 0.0f;
        ptr[i * 4 + 3] = (i < fy.size()) ? (static_cast<float>(fy[i]) / 65536.0f) : 0.0f;
    }

    godot::Ref<godot::Image> img = godot::Image::create_from_data(w, h, false, godot::Image::FORMAT_RGBAF, byte_data);
    return godot::ImageTexture::create_from_image(img);
}

bool SimBridge::chop_tree(const godot::Vector2 &world_pos, int64_t damage, double max_reach) {
    int64_t fx = (int64_t)(world_pos.x * 65536.0);
    int64_t fz = (int64_t)(world_pos.y * 65536.0);
    int64_t reach_fixed = (max_reach > 0.0) ? (int64_t)(max_reach * 65536.0) : 0;
    return _world.chop_tree(fx, fz, (int32_t)damage, reach_fixed);
}

bool SimBridge::chop_tree_fixed(int64_t world_x, int64_t world_z, int64_t damage, int64_t max_reach) {
    return _world.chop_tree(world_x, world_z, (int32_t)damage, max_reach);
}

int64_t SimBridge::get_tree_health(int64_t tree_id) const {
    return (int64_t)_world.get_tree_health((uint32_t)tree_id);
}

bool SimBridge::is_tree_alive(int64_t tree_id) const {
    return _world.is_tree_alive((uint32_t)tree_id);
}

godot::Dictionary SimBridge::get_nearest_tree(const godot::Vector2 &world_pos, double max_reach) const {
    int64_t fx = (int64_t)(world_pos.x * 65536.0);
    int64_t fz = (int64_t)(world_pos.y * 65536.0);
    int64_t reach_fixed = (max_reach > 0.0) ? (int64_t)(max_reach * 65536.0) : 0;
    const TerrainTree* t = _world.get_nearest_tree(fx, fz, reach_fixed);
    godot::Dictionary d;
    if (t) {
        d["id"] = (int64_t)t->id;
        d["chunk_x"] = (int64_t)t->chunk_x;
        d["chunk_y"] = (int64_t)t->chunk_y;
        d["position"] = godot::Vector3((float)t->x / 65536.0f, (float)t->elevation / 65536.0f, (float)t->z / 65536.0f);
        d["normal"] = godot::Vector3((float)t->normal_x / 65536.0f, (float)t->normal_y / 65536.0f, (float)t->normal_z / 65536.0f);
        d["species"] = (int64_t)t->species;
        d["growth"] = (double)t->age_growth / 65536.0;
        d["scale"] = (double)t->max_scale / 65536.0;
        d["seed"] = (int64_t)t->seed;
        d["last_sim_tick"] = (int64_t)t->last_sim_tick;
        d["flags"] = (int64_t)t->flags;
        d["health"] = (int64_t)t->health;
        d["max_health"] = (int64_t)t->max_health;
        d["is_alive"] = t->is_alive();
        d["is_felled"] = t->is_felled();
    }
    return d;
}

int64_t SimBridge::get_tree_count() const {
    return (int64_t)_world.get_tree_count();
}

godot::TypedArray<godot::Dictionary> SimBridge::get_trees_in_chunk(int64_t cx, int64_t cy) const {
    godot::TypedArray<godot::Dictionary> arr;
    const auto& tree_indices = _world.get_trees_in_chunk((int32_t)cx, (int32_t)cy);
    const auto& all_trees = _world.get_terrain_trees();

    for (uint32_t idx : tree_indices) {
        if (idx >= all_trees.size()) continue;
        const auto& t = all_trees[idx];
        godot::Dictionary d;
        d["id"] = (int64_t)t.id;
        d["chunk_x"] = (int64_t)t.chunk_x;
        d["chunk_y"] = (int64_t)t.chunk_y;
        d["position"] = godot::Vector3((float)t.x / 65536.0f, (float)t.elevation / 65536.0f, (float)t.z / 65536.0f);
        d["normal"] = godot::Vector3((float)t.normal_x / 65536.0f, (float)t.normal_y / 65536.0f, (float)t.normal_z / 65536.0f);
        d["species"] = (int64_t)t.species;
        d["growth"] = (double)t.age_growth / 65536.0;
        d["scale"] = (double)t.max_scale / 65536.0;
        d["seed"] = (int64_t)t.seed;
        d["last_sim_tick"] = (int64_t)t.last_sim_tick;
        d["flags"] = (int64_t)t.flags;
        d["health"] = (int64_t)t.health;
        d["max_health"] = (int64_t)t.max_health;
        d["is_alive"] = t.is_alive();
        d["is_felled"] = t.is_felled();
        arr.push_back(d);
    }
    return arr;
}

godot::TypedArray<godot::Dictionary> SimBridge::get_all_trees() const {
    godot::TypedArray<godot::Dictionary> arr;
    const auto& all_trees = _world.get_terrain_trees();
    for (const auto& t : all_trees) {
        godot::Dictionary d;
        d["id"] = (int64_t)t.id;
        d["chunk_x"] = (int64_t)t.chunk_x;
        d["chunk_y"] = (int64_t)t.chunk_y;
        d["position"] = godot::Vector3((float)t.x / 65536.0f, (float)t.elevation / 65536.0f, (float)t.z / 65536.0f);
        d["normal"] = godot::Vector3((float)t.normal_x / 65536.0f, (float)t.normal_y / 65536.0f, (float)t.normal_z / 65536.0f);
        d["species"] = (int64_t)t.species;
        d["growth"] = (double)t.age_growth / 65536.0;
        d["scale"] = (double)t.max_scale / 65536.0;
        d["seed"] = (int64_t)t.seed;
        d["last_sim_tick"] = (int64_t)t.last_sim_tick;
        d["flags"] = (int64_t)t.flags;
        d["health"] = (int64_t)t.health;
        d["max_health"] = (int64_t)t.max_health;
        d["is_alive"] = t.is_alive();
        d["is_felled"] = t.is_felled();
        arr.push_back(d);
    }
    return arr;
}

godot::PackedFloat32Array SimBridge::get_tree_render_data(int64_t cx, int64_t cy) const {
    godot::PackedFloat32Array arr;
    const auto& all_trees = _world.get_terrain_trees();

    if (cx >= 0 && cy >= 0) {
        const auto& indices = _world.get_trees_in_chunk((int32_t)cx, (int32_t)cy);
        arr.resize(indices.size() * 10);
        float* ptr = arr.ptrw();
        int64_t out_idx = 0;
        for (uint32_t idx : indices) {
            if (idx >= all_trees.size()) continue;
            const auto& t = all_trees[idx];
            ptr[out_idx + 0] = (float)t.x / 65536.0f;
            ptr[out_idx + 1] = (float)t.elevation / 65536.0f;
            ptr[out_idx + 2] = (float)t.z / 65536.0f;
            ptr[out_idx + 3] = (float)t.normal_x / 65536.0f;
            ptr[out_idx + 4] = (float)t.normal_y / 65536.0f;
            ptr[out_idx + 5] = (float)t.normal_z / 65536.0f;
            ptr[out_idx + 6] = (float)t.species;
            ptr[out_idx + 7] = (float)t.max_scale / 65536.0f;
            ptr[out_idx + 8] = (float)t.age_growth / 65536.0f;
            ptr[out_idx + 9] = (float)(t.seed & 0xFFFFFF);
            out_idx += 10;
        }
        if (out_idx < (int64_t)indices.size() * 10) {
            arr.resize(out_idx);
        }
    } else {
        arr.resize(all_trees.size() * 10);
        float* ptr = arr.ptrw();
        int64_t out_idx = 0;
        for (const auto& t : all_trees) {
            ptr[out_idx + 0] = (float)t.x / 65536.0f;
            ptr[out_idx + 1] = (float)t.elevation / 65536.0f;
            ptr[out_idx + 2] = (float)t.z / 65536.0f;
            ptr[out_idx + 3] = (float)t.normal_x / 65536.0f;
            ptr[out_idx + 4] = (float)t.normal_y / 65536.0f;
            ptr[out_idx + 5] = (float)t.normal_z / 65536.0f;
            ptr[out_idx + 6] = (float)t.species;
            ptr[out_idx + 7] = (float)t.max_scale / 65536.0f;
            ptr[out_idx + 8] = (float)t.age_growth / 65536.0f;
            ptr[out_idx + 9] = (float)(t.seed & 0xFFFFFF);
            out_idx += 10;
        }
    }
    return arr;
}

void SimBridge::fast_forward_chunk_trees(int64_t cx, int64_t cy, int64_t target_tick) {
    _world.fast_forward_chunk_trees((int32_t)cx, (int32_t)cy, (uint32_t)target_tick);
}

godot::PackedInt32Array SimBridge::get_chunk_tree_counts() const {
    godot::PackedInt32Array arr;
    int32_t chunk_w = _world.get_chunk_grid_w();
    int32_t chunk_h = _world.get_chunk_grid_h();
    arr.resize(chunk_w * chunk_h);
    for (int32_t cy = 0; cy < chunk_h; ++cy) {
        for (int32_t cx = 0; cx < chunk_w; ++cx) {
            arr[cy * chunk_w + cx] = static_cast<int32_t>(_world.get_trees_in_chunk(cx, cy).size());
        }
    }
    return arr;
}

godot::Vector2i SimBridge::get_chunk_grid_size() const {
    return godot::Vector2i(_world.get_chunk_grid_w(), _world.get_chunk_grid_h());
}
