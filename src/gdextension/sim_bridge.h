#pragma once
// gdextension/sim_bridge.h
//
// Single GDExtension node that exposes SimWorld to Godot.
// Translation layer only — no logic lives here.
// All types converted at the boundary: Godot → sim_core structs on the way in,
// sim_core structs → Godot types on the way out.

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector2i.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include "world.h"

class SimBridge : public godot::Node {
    GDCLASS(SimBridge, godot::Node)

public:
    SimBridge();
    ~SimBridge();

    static SimBridge *get_singleton();
    static void _bind_methods();

    void              set_seed(int64_t seed);
    void              set_config(const godot::Dictionary &params);
    void              set_params(const godot::Dictionary &params);
    godot::Dictionary get_config() const;
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

    // World Map Generation
    void              generate_player_map(int64_t seed, int64_t width, int64_t height, int64_t cell_size_fixed);

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
    
    // Farming Pipeline & Soil Actions
    bool                   till_soil(int64_t x, int64_t y);
    bool                   water_soil(int64_t x, int64_t y, double amount = 30.0);
    bool                   plant_crop(int64_t x, int64_t y, int64_t crop_type, int64_t prefix = 0, int64_t suffix = 0, int64_t implicit = 0);
    bool                   harvest_crop(int64_t x, int64_t y);
    bool                   is_crop_mature(int64_t x, int64_t y) const;
    double                 get_soil_moisture(int64_t x, int64_t y) const;
    int64_t                get_soil_moisture_fixed(int64_t x, int64_t y) const;
    double                 get_soil_compactness_at(int64_t x, int64_t y) const;
    int64_t                get_soil_compactness_fixed_at(int64_t x, int64_t y) const;
    int64_t                get_last_dropped_item() const;
    godot::Dictionary      get_last_generated_item() const;

    void                   place_structure(int64_t x, int64_t y, int64_t structure_type);
    void                   remove_structure(int64_t x, int64_t y);
    bool                   modify_terrain(int64_t x, int64_t y, int64_t delta_elev);
    bool                   can_modify_terrain(int64_t x, int64_t y) const;
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

    // Tectonic Heightmap & Wave Cage APIs
    godot::Dictionary            get_tectonic_sample(double x, double y) const;
    godot::TypedArray<godot::Dictionary> get_tectonic_nodes() const;
    godot::TypedArray<godot::Dictionary> get_macro_edges() const;
    godot::TypedArray<godot::Dictionary> get_macro_plates() const;
    godot::PackedFloat32Array    get_wave_cage_data(double sea_level = 0.0) const;

    // OpenTopography & Parameterized River APIs
    void                         set_world_gen_mode(int64_t mode);
    int64_t                      get_world_gen_mode() const;
    bool                         load_opentopo_dataset(godot::String path);
    void                         set_opentopo_sea_level_offset(double offset_meters);
    double                       get_opentopo_sea_level_offset() const;
    void                         set_opentopo_sea_buffer_tiles(int64_t tiles);
    int64_t                      get_opentopo_sea_buffer_tiles() const;
    void                         set_apply_coastal_erosion(bool enable);
    bool                         get_apply_coastal_erosion() const;
    void                         set_coastal_erosion_iterations(int64_t iterations);
    int64_t                      get_coastal_erosion_iterations() const;
    void                         set_coastal_erosion_strength(double strength);
    double                       get_coastal_erosion_strength() const;

    // Droplet Hydraulic Erosion & River Network Parameterization
    void                         set_apply_droplet_erosion(bool enable);
    bool                         get_apply_droplet_erosion() const;
    void                         set_droplet_count(int64_t count);
    int64_t                      get_droplet_count() const;
    void                         set_droplet_erosion_rate(double rate);
    double                       get_droplet_erosion_rate() const;
    void                         set_droplet_deposition_rate(double rate);
    double                       get_droplet_deposition_rate() const;

    void                         set_river_min_area(int64_t area);
    int64_t                      get_river_min_area() const;
    void                         set_river_width_multiplier(double mult);
    double                       get_river_width_multiplier() const;
    void                         set_river_carve_depth(double depth);
    double                       get_river_carve_depth() const;
    godot::Dictionary            get_nearest_river_sample(double x, double y) const;

    // Continuous Lagrangian Water Depth & Flux Field (Zero Z-fighting, zero polygon clipping)
    godot::PackedFloat32Array    get_water_depth() const;
    godot::PackedFloat32Array    get_water_depth_data() const;
    godot::Ref<godot::ImageTexture> get_water_depth_texture() const;

    godot::TypedArray<godot::Dictionary> get_parametric_river_reaches() const;
    godot::TypedArray<godot::Dictionary> get_estuary_mouths() const;
    godot::Vector2               get_river_flow_vector_at(double x, double y) const;

    // Terrain Tree / Chunked Vegetation APIs
    bool                         chop_tree(const godot::Vector2 &world_pos, int64_t damage = 25, double max_reach = 0.0);
    bool                         chop_tree_fixed(int64_t world_x, int64_t world_z, int64_t damage = 25, int64_t max_reach = 0);
    int64_t                      get_tree_health(int64_t tree_id) const;
    bool                         is_tree_alive(int64_t tree_id) const;
    godot::Dictionary            get_nearest_tree(const godot::Vector2 &world_pos, double max_reach = 0.0) const;
    int64_t                      get_tree_count() const;
    godot::TypedArray<godot::Dictionary> get_trees_in_chunk(int64_t cx, int64_t cy) const;
    godot::TypedArray<godot::Dictionary> get_all_trees() const;
    godot::PackedFloat32Array    get_tree_render_data(int64_t cx = -1, int64_t cy = -1) const;
    void                         fast_forward_chunk_trees(int64_t cx, int64_t cy, int64_t target_tick);
    godot::PackedInt32Array      get_chunk_tree_counts() const;
    godot::Vector2i              get_chunk_grid_size() const;

private:
    static SimBridge *singleton;
    SimWorld          _world;
};
