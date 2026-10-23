#pragma once
#include "entity.h"
#include "shapes.h"
#include "input.h"
#include "ecs.h"
#include "entity_grid.h"
#include "static_wall_grid.h"
#include "rng.h"
#include "tile_registry.h"
#include "map_generator.h"
#include "hydrology_graph.h"
#include <vector>
#include <unordered_set>
#include <cstdint>
#include <memory>

enum class WeatherState : uint8_t {
    CLEAR = 0,
    RAIN = 1,
    ACID_RAIN = 2,
    STORM = 3,
    FLOOD = 4,
};

struct GenHitSet {
    struct Entry {
        uint64_t pair;
        uint32_t gen;
    };
    std::vector<Entry> table;
    uint32_t current_gen = 1;
    std::vector<uint64_t> items;

    void clear() {
        current_gen++;
        if (current_gen == 0) { // overflow
            std::memset(table.data(), 0, table.size() * sizeof(Entry));
            current_gen = 1;
        }
        items.clear();
    }

    void reserve(size_t capacity) {
        size_t cap = 256;
        while (cap < capacity * 2) cap <<= 1;
        table.resize(cap, {0, 0});
        items.reserve(capacity);
    }

    bool contains(uint64_t pair) const {
        if (table.empty()) return false;
        size_t mask = table.size() - 1;
        // Simple fast hash
        uint64_t h = pair * 11400714819323198485ULL;
        size_t idx = (h ^ (h >> 32)) & mask;
        while (table[idx].gen == current_gen) {
            if (table[idx].pair == pair) return true;
            idx = (idx + 1) & mask;
        }
        return false;
    }

    void insert(uint64_t pair) {
        if (items.size() * 2 >= table.size()) {
            size_t new_cap = table.empty() ? 256 : table.size() * 2;
            std::vector<Entry> new_table(new_cap, {0, 0});
            size_t mask = new_cap - 1;
            for (uint64_t k : items) {
                uint64_t h = k * 11400714819323198485ULL;
                size_t idx = (h ^ (h >> 32)) & mask;
                while (new_table[idx].gen == current_gen) idx = (idx + 1) & mask;
                new_table[idx] = {k, current_gen};
            }
            table = std::move(new_table);
        }
        size_t mask = table.size() - 1;
        uint64_t h = pair * 11400714819323198485ULL;
        size_t idx = (h ^ (h >> 32)) & mask;
        while (table[idx].gen == current_gen) {
            if (table[idx].pair == pair) return;
            idx = (idx + 1) & mask;
        }
        table[idx] = {pair, current_gen};
        items.push_back(pair);
    }
};

class SimWorld {
public:
    SimWorld();
    
    void load_tile_registry(const std::string& path) { _tile_registry.load_from_file(path); }
    const TileRegistry& get_tile_registry() const { return _tile_registry; }
    const Generation::MapGenerator* get_last_generator() const { return _last_generator.get(); }


    void set_seed(uint64_t seed);
    void tick(const InputFrame &input);

    BisectSnapshot snapshot() const;
    void restore(const BisectSnapshot &snap);

    EntityID spawn_entity(EntityType type, int64_t x, int64_t y, int64_t radius, int32_t health, uint32_t tags = 0);

    void kill_entity(EntityID id);

    void equip_item(EntityID entity_id, int slot, uint64_t item_id);
    void unequip_item(EntityID entity_id, int slot);

    void add_wall(int64_t x, int64_t y, int64_t w, int64_t h);
    void generate_map_cellular_automata(uint64_t seed, int64_t width, int64_t height, int64_t cell_size_fixed, int32_t iterations = 5, int32_t fill_prob = 45);
    void generate_player_map(uint64_t seed, int64_t width, int64_t height, int64_t cell_size_fixed);
    void clear_walls();
    void finalize_walls();
    const std::vector<SimAABB>& get_walls() const { return _walls; }

    void add_polygon(int64_t x, int64_t y, const std::vector<int64_t>& vx, const std::vector<int64_t>& vy) {
        SimPolygon poly;
        poly.x = x; poly.y = y; poly.vx = vx; poly.vy = vy; poly.user_data = 0;
        int64_t min_x = 0x7FFFFFFFFFFFFFFF; int64_t min_y = 0x7FFFFFFFFFFFFFFF;
        int64_t max_x = -0x7FFFFFFFFFFFFFFF; int64_t max_y = -0x7FFFFFFFFFFFFFFF;
        for(size_t i=0; i<vx.size(); ++i) {
            if(vx[i] < min_x) min_x = vx[i]; if(vx[i] > max_x) max_x = vx[i];
            if(vy[i] < min_y) min_y = vy[i]; if(vy[i] > max_y) max_y = vy[i];
        }
        poly.bounds.x = x + min_x; poly.bounds.y = y + min_y;
        poly.bounds.width = max_x - min_x; poly.bounds.height = max_y - min_y;
        poly.bounds.user_data = 0;
        _polygons.push_back(poly);
    }
    void clear_polygons() { _polygons.clear(); }
    const std::vector<SimPolygon>& get_polygons() const { return _polygons; }
    void finalize_polygons() { _poly_grid.build_polygons(_polygons); }

    void clear_entities();

    uint32_t current_tick() const { return _tick; }

    const ChaosState& get_chaos() const { return _entities.get_chaos(); }
    ChaosState& get_chaos_mut() { return _entities.get_chaos(); }

    bool has_entity(EntityID id) const;
    uint32_t get_dense_idx(EntityID id) const;

    const EntityStats& get_stats(EntityID id) const;
    EntityStats& get_stats_mut(EntityID id);

    const Equipment& get_equipment(EntityID id) const;
    Equipment& get_equipment_mut(EntityID id);

    uint64_t get_state_hash() const;

    // --- Water Table & Aquifers ---
    void set_map_dimensions(int32_t width, int32_t height, int64_t cell_size_fixed = 64 * 65536) {
        _map_width = width;
        _map_height = height;
        _cell_size_fixed = cell_size_fixed;

        size_t total_cells = (size_t)width * height;
        _crop_type.assign(total_cells, 0);
        _crop_growth.assign(total_cells, 0);
        _crop_health.assign(total_cells, 0);
        _crop_prefix.assign(total_cells, 0);
        _crop_suffix.assign(total_cells, 0);
        _crop_implicit.assign(total_cells, 0);
        _soil_nitrogen.assign(total_cells, 0);
        _soil_phosphorus.assign(total_cells, 0);
        _soil_potassium.assign(total_cells, 0);
        _soil_calcium.assign(total_cells, 0);
        _soil_magnesium.assign(total_cells, 0);
        _soil_sulfur.assign(total_cells, 0);
        _soil_compactness.assign(total_cells, 0);
        _lakes.assign(total_cells, 0);
        _structure_type.assign(total_cells, 0);
        _water_pressure.assign(total_cells, 0);
        _actuator_activated.assign(total_cells, 0);
        _elevation.assign(total_cells, 0);
        _biome.assign(total_cells, 0);
        _surface_type.assign(total_cells, 0);
        _mineral_type.assign(total_cells, 0);
        _planted_tile_indices.clear();
    }
    void set_water_table(const std::vector<int32_t>& wt) { _water_table = wt; }
    void set_aquifers(const std::vector<uint8_t>& aq) { _aquifers = aq; }
    void set_rivers(const std::vector<uint8_t>& r) { _rivers = r; }
    void set_flow_targets(const std::vector<int32_t>& ft) { _flow_targets = ft; }
    void set_lakes(const std::vector<uint8_t>& l) { _lakes = l; }
    void set_structure_type(const std::vector<uint8_t>& st) { _structure_type = st; }
    void set_water_pressure(const std::vector<int32_t>& wp) { _water_pressure = wp; }
    void set_actuator_activated(const std::vector<uint8_t>& aa) { _actuator_activated = aa; }
    void set_weather_state(uint8_t ws) { _weather_state = ws; }
    uint8_t get_weather_state() const { return _weather_state; }
    void set_weather_timer(int32_t t) { _weather_timer = t; }
    int32_t get_weather_timer() const { return _weather_timer; }
    void set_co2_level(int32_t co2) { _co2_level = co2; }
    int32_t get_co2_level() const { return _co2_level; }
    void set_elevation(const std::vector<int32_t>& el) { _elevation = el; }
    void set_biome(const std::vector<uint8_t>& b) { _biome = b; }
    void set_surface_type(const std::vector<uint8_t>& s) { _surface_type = s; }
    void set_mineral_type(const std::vector<uint8_t>& m) { _mineral_type = m; }
    void set_starter_spawn(int32_t x, int32_t y) { _starter_spawn_x = x; _starter_spawn_y = y; }
    void set_starter_pond(int64_t cur, int64_t max) { _starter_pond_volume = cur; _starter_pond_max = max; }
    void set_hydraulic_graph(const Hydrology::HydraulicGraph& hg) { _hydraulic_graph = hg; }
    void set_chunk_tiers(const std::vector<uint8_t>& t) { _chunk_tiers = t; }
    void set_soil_nitrogen(const std::vector<int32_t>& n) { _soil_nitrogen = n; }
    void set_soil_phosphorus(const std::vector<int32_t>& p) { _soil_phosphorus = p; }
    void set_soil_potassium(const std::vector<int32_t>& k) { _soil_potassium = k; }
    void set_soil_calcium(const std::vector<int32_t>& ca) { _soil_calcium = ca; }
    void set_soil_magnesium(const std::vector<int32_t>& mg) { _soil_magnesium = mg; }
    void set_soil_sulfur(const std::vector<int32_t>& s) { _soil_sulfur = s; }
    void set_soil_compactness(const std::vector<int32_t>& c) { _soil_compactness = c; }
    void set_crop_type(const std::vector<uint8_t>& v) { _crop_type = v; }
    void set_crop_growth(const std::vector<int32_t>& v) { _crop_growth = v; }
    void set_crop_health(const std::vector<int32_t>& v) { _crop_health = v; }
    void set_crop_prefix(const std::vector<uint8_t>& v) { _crop_prefix = v; }
    void set_crop_suffix(const std::vector<uint8_t>& v) { _crop_suffix = v; }
    void set_crop_implicit(const std::vector<uint8_t>& v) { _crop_implicit = v; }
    void set_planted_tile_indices(const std::vector<int32_t>& v) { _planted_tile_indices = v; }

    int32_t get_map_width() const { return _map_width; }
    int32_t get_map_height() const { return _map_height; }
    int64_t get_cell_size_fixed() const { return _cell_size_fixed; }
    const std::vector<int32_t>& get_water_table() const { return _water_table; }
    const std::vector<uint8_t>& get_aquifers() const { return _aquifers; }
    const std::vector<uint8_t>& get_rivers() const { return _rivers; }
    const std::vector<int32_t>& get_flow_targets() const { return _flow_targets; }
    const std::vector<uint8_t>& get_lakes() const { return _lakes; }
    const std::vector<uint8_t>& get_structure_type() const { return _structure_type; }
    const std::vector<int32_t>& get_water_pressure() const { return _water_pressure; }
    const std::vector<uint8_t>& get_actuator_activated() const { return _actuator_activated; }
    const std::vector<int32_t>& get_elevation() const { return _elevation; }
    const std::vector<uint8_t>& get_biome() const { return _biome; }
    const std::vector<uint8_t>& get_surface_type() const { return _surface_type; }
    std::vector<uint8_t>& get_surface_type_mut() { return _surface_type; }
    const std::vector<uint8_t>& get_mineral_type() const { return _mineral_type; }
    int32_t get_starter_spawn_x() const { return _starter_spawn_x; }
    int32_t get_starter_spawn_y() const { return _starter_spawn_y; }
    int64_t get_starter_pond_volume() const { return _starter_pond_volume; }
    int64_t get_starter_pond_max() const { return _starter_pond_max; }
    const Hydrology::HydraulicGraph& get_hydraulic_graph() const { return _hydraulic_graph; }
    Hydrology::HydraulicGraph& get_hydraulic_graph_mut() { return _hydraulic_graph; }
    const std::vector<uint8_t>& get_chunk_tiers() const { return _chunk_tiers; }
    const std::vector<int32_t>& get_soil_nitrogen() const { return _soil_nitrogen; }
    const std::vector<int32_t>& get_soil_phosphorus() const { return _soil_phosphorus; }
    const std::vector<int32_t>& get_soil_potassium() const { return _soil_potassium; }
    const std::vector<int32_t>& get_soil_calcium() const { return _soil_calcium; }
    const std::vector<int32_t>& get_soil_magnesium() const { return _soil_magnesium; }
    const std::vector<int32_t>& get_soil_sulfur() const { return _soil_sulfur; }
    const std::vector<int32_t>& get_soil_compactness() const { return _soil_compactness; }
    const std::vector<uint8_t>& get_crop_type() const { return _crop_type; }
    const std::vector<int32_t>& get_crop_growth() const { return _crop_growth; }
    const std::vector<int32_t>& get_crop_health() const { return _crop_health; }
    const std::vector<uint8_t>& get_crop_prefix() const { return _crop_prefix; }
    const std::vector<uint8_t>& get_crop_suffix() const { return _crop_suffix; }
    const std::vector<uint8_t>& get_crop_implicit() const { return _crop_implicit; }
    const std::vector<int32_t>& get_planted_tile_indices() const { return _planted_tile_indices; }

    EntityGrid& get_grid_mut() { return _entity_grid; }
    const EntityGrid& get_grid() const { return _entity_grid; }

    static std::vector<uint8_t> serialize_snapshot(const BisectSnapshot &snap);
    static const uint8_t*       deserialize_snapshot(BisectSnapshot &snap, const uint8_t *ptr);

    static constexpr int64_t ACTIVATION_RADIUS = 500 * 65536; // 500 units (Aggro 400 + Buffer 100)

    SplitMix64 get_rng_shotgun() const { return _rng_shotgun; }
    SplitMix64 get_rng_tornado() const { return _rng_tornado; }
    SplitMix64 get_rng_crit() const { return _rng_crit; }

    std::vector<EntityStats>& get_stats_array() { return _entities.get_heavy_stats(); }
    const std::vector<EntityStats>& get_stats_array() const { return _entities.get_heavy_stats(); }

    bool has_hit(EntityID attacker_id, EntityID target_id) const;
    void add_hit(EntityID attacker_id, EntityID target_id);

    public:
    static constexpr int32_t CHUNK_SIZE = 16;
    static constexpr uint8_t TIER_ACTIVE = 0;
    static constexpr uint8_t TIER_SLOW = 1;
    static constexpr uint8_t TIER_FROZEN = 2;
    void update_chunk_tiers();
    void tick_environment();
    void tick_crops();

    void plant_crop(int32_t x, int32_t y, uint8_t crop_type, uint8_t prefix = 0, uint8_t suffix = 0, uint8_t implicit = 0);
    void harvest_crop(int32_t x, int32_t y);
    void apply_fertilizer(int32_t x, int32_t y, int64_t n, int64_t p, int64_t k, int64_t ca, int64_t mg, int64_t s);
    void damage_crop(int32_t idx, int32_t amount);
    
    void place_structure(int32_t x, int32_t y, uint8_t structure_type);
    void remove_structure(int32_t x, int32_t y);

    EntityID spawn_bot(uint8_t bot_type, int64_t x, int64_t y);
    void set_bot_job(EntityID bot_id, uint8_t job, int32_t tx, int32_t ty, uint8_t seed_type);
    void upload_bot_program(EntityID bot_id, const std::vector<uint16_t>& opcodes);
    void wire_bot_ports(EntityID bot_id, int32_t sx, int32_t sy, int32_t ax, int32_t ay);
    const std::vector<BotState>& get_bot_states() const { return _bot_states; }
    void set_bot_states(const std::vector<BotState>& s) { _bot_states = s; }

    private:
    void tick_movement(const InputFrame &input, int64_t px, int64_t py, bool player_found);
    void tick_collision(int64_t px, int64_t py, bool player_found, int64_t act_sq);
    void tick_cleanup();
    void recalculate_stats(EntityID id);
    void _do_equip(EntityID entity_id, int slot, uint64_t item_id);
    void _do_place_structure(int32_t x, int32_t y, uint8_t structure_type);
    void _do_remove_structure(int32_t x, int32_t y);
    void _do_spawn_bot(EntityID bot_id, uint8_t bot_type, int64_t x, int64_t y);
    void _do_set_bot_job(EntityID bot_id, uint8_t job, int32_t tx, int32_t ty, uint8_t seed_type);
    void _do_upload_bot_program(EntityID bot_id, const uint16_t* opcodes, uint16_t size);
    void _do_wire_bot_ports(EntityID bot_id, int32_t sx, int32_t sy, int32_t ax, int32_t ay);
    void tick_bots();
    void _sync_node_aabb(uint32_t idx);

    // ---- SYSTEMS ----
    EntityContainer _entities;
    EntityGrid _entity_grid;
    StaticWallGrid  _wall_grid;
    StaticWallGrid  _poly_grid;

    int32_t _last_player_cx = -999;
    int32_t _last_player_cy = -999;

    // ---- DATA ----
    std::vector<EntityID> _entity_recycle_bin; // IDs available for reuse
    std::vector<EntityID> _attack_recycle_bin;
    std::vector<SimAABB> _walls;
    std::vector<SimPolygon> _polygons;
    
    TileRegistry _tile_registry;
    std::unique_ptr<Generation::MapGenerator> _last_generator;

    uint64_t _shotgun_seed = 0;
    uint64_t _tornado_seed = 0;
    uint64_t _crit_seed = 0;

    SplitMix64 _rng_shotgun;
    SplitMix64 _rng_tornado;
    SplitMix64 _rng_crit;

    std::vector<uint64_t> _hit_pairs;
    GenHitSet _tick_hit_pairs;
    std::vector<uint64_t> _hit_pairs_scratch;
    std::vector<uint64_t> _tick_hit_pairs_sorted;
    uint32_t             _tick          = 0;
    EntityID             _next_id       = 1;

    // Map properties for farming game
    int32_t              _map_width     = 0;
    int32_t              _map_height    = 0;
    int64_t              _cell_size_fixed = 0;
    std::vector<int32_t> _water_table;
    std::vector<uint8_t> _aquifers;
    std::vector<uint8_t> _rivers;
    std::vector<int32_t> _flow_targets;
    std::vector<uint8_t> _lakes;
    std::vector<uint8_t> _structure_type;
    std::vector<int32_t> _water_pressure;
    std::vector<uint8_t> _actuator_activated;
    std::vector<BotState> _bot_states;
    std::vector<uint8_t> _chunk_tiers;
    std::vector<int32_t> _soil_nitrogen;
    std::vector<int32_t> _soil_phosphorus;
    std::vector<int32_t> _soil_potassium;
    std::vector<int32_t> _soil_calcium;
    std::vector<int32_t> _soil_magnesium;
    std::vector<int32_t> _soil_sulfur;
    std::vector<int32_t> _soil_compactness;
    std::vector<uint8_t> _crop_type;
    std::vector<int32_t> _crop_growth;
    std::vector<int32_t> _crop_health;
    std::vector<uint8_t> _crop_prefix;
    std::vector<uint8_t> _crop_suffix;
    std::vector<uint8_t> _crop_implicit;
    std::vector<int32_t> _planted_tile_indices;
    
    uint8_t              _weather_state = 0;
    int32_t              _weather_timer = 0;
    int32_t              _co2_level     = 300 * 65536; // 300 ppm

    std::vector<int32_t> _elevation;
    std::vector<uint8_t> _biome;
    std::vector<uint8_t> _surface_type;
    std::vector<uint8_t> _mineral_type;
    int32_t              _starter_spawn_x = 0;
    int32_t              _starter_spawn_y = 0;
    int64_t              _starter_pond_volume = 0;
    int64_t              _starter_pond_max = 0;
    Hydrology::HydraulicGraph _hydraulic_graph;
};
