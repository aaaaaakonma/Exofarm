#pragma once
// gdextension/sim_bridge.h
//
// Single GDExtension node that exposes SimWorld to Godot.
// Translation layer only — no logic lives here.
// All types converted at the boundary: Godot → sim_core structs on the way in,
// sim_core structs → Godot types on the way out.

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include "world.h"

class SimBridge : public godot::Node {
    GDCLASS(SimBridge, godot::Node)

public:
    SimBridge();
    ~SimBridge();

    static SimBridge *get_singleton();
    static void _bind_methods();

    void              set_seed(int64_t seed);
    void              set_tectonic_config(int64_t plates_min, int64_t plates_max, int64_t center_bias, int64_t continent_chance, int64_t vel_min = 50, int64_t vel_max = 300);

    // Called every physics tick from GDScript with the current InputFrame
    void              step(int64_t move_x, int64_t move_y, int64_t aim_x, int64_t aim_y, int64_t actions);

    // Entity management
    int64_t           spawn_entity(int64_t type, int64_t x, int64_t y, int64_t radius, int64_t health, int64_t tags = 0);
    void              kill_entity(int64_t id);
    void              clear_entities();

    bool              load_tile_registry(godot::String path);
    godot::Dictionary get_room_type_info() const;


    void              equip_item(int64_t entity_id, int64_t slot, int64_t item_id);
    void              unequip_item(int64_t entity_id, int64_t slot);

    // Wall management
    void              add_wall(int64_t x, int64_t y, int64_t w, int64_t h);
    void              add_polygon(int64_t x, int64_t y, godot::PackedInt64Array vx, godot::PackedInt64Array vy);
    void              clear_polygons();
    godot::TypedArray<godot::Dictionary> get_polygons() const;
    void              generate_map_cellular_automata(int64_t seed, int64_t width, int64_t height, int64_t cell_size_fixed, int64_t iterations, int64_t fill_prob);
    void              generate_player_map(int64_t seed, int64_t width, int64_t height, int64_t cell_size_fixed);

    void              clear_walls();
    godot::TypedArray<godot::Dictionary> get_walls() const;

    // Read entity state for renderer — returns [x, y, vel_x, vel_y, health, flags, radius, tags]
    godot::TypedArray<int64_t> get_entity_state(int64_t id) const;

    // Get all active entities (id -> type)
    godot::Dictionary get_active_entities() const;
    godot::PackedFloat64Array get_render_states() const;

    // Snapshot — returns serialized dirty+tombstone state as PackedByteArray
    godot::PackedByteArray take_snapshot() const;
    void                   restore_snapshot(godot::PackedByteArray data);

    // Item generation
    godot::Dictionary      generate_item(int64_t id) const;

    int64_t                get_state_hash() const;

    godot::Dictionary      get_spatial_info() const;
    int64_t                get_cell_occupancy(int64_t x, int64_t y) const;

    int64_t                get_map_width() const;
    int64_t                get_map_height() const;
    godot::PackedInt64Array get_water_table() const;
    godot::PackedByteArray get_aquifers() const;
    godot::PackedByteArray get_rivers() const;
    godot::PackedInt32Array get_flow_targets() const;
    godot::PackedByteArray get_lakes() const;
    godot::PackedByteArray get_structure_types() const;
    godot::PackedInt64Array get_water_pressures() const;
    godot::PackedByteArray get_chunk_tiers() const;
    godot::PackedInt64Array get_soil_nitrogen() const;
    godot::PackedInt64Array get_soil_phosphorus() const;
    godot::PackedInt64Array get_soil_potassium() const;
    godot::PackedInt64Array get_soil_calcium() const;
    godot::PackedInt64Array get_soil_magnesium() const;
    godot::PackedInt64Array get_soil_sulfur() const;
    godot::PackedInt64Array get_soil_compactness() const;
    
    void                   plant_crop(int64_t x, int64_t y, int64_t crop_type, int64_t prefix = 0, int64_t suffix = 0, int64_t implicit = 0);
    void                   harvest_crop(int64_t x, int64_t y);
    void                   place_structure(int64_t x, int64_t y, int64_t structure_type);
    void                   remove_structure(int64_t x, int64_t y);
    void                   apply_fertilizer(int64_t x, int64_t y, int64_t n, int64_t p, int64_t k, int64_t ca, int64_t mg, int64_t s);
    godot::PackedByteArray get_crop_types() const;
    godot::PackedByteArray get_crop_prefixes() const;
    godot::PackedByteArray get_crop_suffixes() const;
    godot::PackedByteArray get_crop_implicits() const;
    godot::PackedInt64Array get_crop_growth() const;
    godot::PackedInt64Array get_crop_health() const;
    godot::PackedInt32Array get_elevation() const;
    godot::PackedByteArray get_biome() const;
    godot::PackedByteArray get_surface_types() const;
    godot::PackedByteArray get_mineral_types() const;
    godot::Vector2i        get_starter_spawn() const;
    int64_t                get_starter_pond_volume() const;
    int64_t                get_starter_pond_max() const;
    double                 get_starter_pond_percent() const;
    godot::PackedInt64Array get_reservoir_volumes() const;
    int64_t                get_river_count() const;
    godot::PackedInt64Array get_river_water_levels() const;
    godot::PackedInt64Array get_river_flow_rates() const;
    godot::PackedInt64Array get_river_max_rates() const;
    godot::PackedInt64Array get_river_pump_rates() const;
    godot::Dictionary      get_river_pipe_info(int64_t index) const;

    int64_t current_tick() const;

    int64_t                spawn_bot(int64_t bot_type, int64_t x, int64_t y);
    void                   set_bot_job(int64_t bot_id, int64_t job, int64_t tx, int64_t ty, int64_t seed_type);
    void                   upload_bot_program(int64_t bot_id, godot::PackedInt64Array opcodes);
    void                   wire_bot_ports(int64_t bot_id, int64_t sx, int64_t sy, int64_t ax, int64_t ay);
    godot::Dictionary      get_bot_state(int64_t bot_id) const;
    godot::Array           get_all_bots() const;
    godot::PackedByteArray get_actuator_activated() const;
    int64_t                get_weather_state() const;
    int64_t                get_weather_timer() const;
    int64_t                get_co2_level() const;

private:
    static SimBridge *singleton;
    SimWorld          _world;
};
