#pragma once
#include "fixed.h"
#include "shapes.h"
#include "hydrology_graph.h"
#include <vector>
#include <cstdint>

typedef uint32_t EntityID;
constexpr EntityID INVALID_ENTITY = 0;

enum class EntityType : uint8_t {
    PLAYER     = 0,
    ENEMY      = 1,
    ITEM       = 2,
    TRIGGER    = 3,
    PROJECTILE = 4,
    BOT        = 5,
};

enum class EntityFlags : uint8_t {
    NONE    = 0,
    DIRTY   = 1 << 0,   // diverged from map-generated default
    DEAD    = 1 << 1,   // tombstoned
    SOLID   = 1 << 2,
    ACTIVE  = 1 << 3,   // currently participating in simulation
    HIBERNATE = 1 << 4, // checking for active entities in radius
};

enum class CropImplicit : uint8_t {
    NONE = 0,
    WILD = 1,          // -20% growth rate, fixes Nitrogen to soil, higher mutation rate
    DOMESTICATED = 2,  // +20% growth rate, but requires 20% more nutrients
    HEIRLOOM = 3,      // +10% health, double seed/breeding rate
    ORGANIC = 4,       // gets double benefits from water/moisture factors, less decay
    COUNT = 5
};

enum class CropPrefix : uint8_t {
    NONE = 0,
    THICK_STEM = 1,      // +30% crop health/robustness
    BROAD_LEAF = 2,      // +20% growth rate from photosynthesis
    SUCCULENT_FRUIT = 3, // +50% crop yield / harvest value
    THORNY_STEM = 4,     // reduces health decay rate by 30%
    VARIEGATED_LEAF = 5, // higher mutation rate
    COUNT = 6
};

enum class CropSuffix : uint8_t {
    NONE = 0,
    TAP_ROOT = 1,        // completely ignores soil compactness penalties (deep-rooted)
    FIBROUS_ROOT = 2,    // +30% nutrient absorption efficiency (reduces consumption by 30%)
    TUBEROUS_ROOT = 3,   // stores water, drought-resistant (halves low moisture penalty)
    ADVENTITIOUS_ROOT = 4,// flood-resistant (halves high moisture penalty)
    MYCORRHIZAL_ROOT = 5,// symbiote: slowly generates Phosphorus (+1 * ONE / 1000 per tick update)
    COUNT = 6
};

enum class StructureType : uint8_t {
    NONE = 0,
    WELL = 1,
    PUMP = 2,
    CHANNEL = 3,
    SPRINKLER_SMALL = 4,
    SPRINKLER_PIVOT = 5,
    BRIDGE = 6,
};

enum class BotType : uint8_t {
    HUMANOID = 0,
    DRONE    = 1,
    COMBINE  = 2,
    IOT_NODE = 3,
};

struct BotState {
    EntityID entity_id = 0;
    uint8_t  bot_type = 0; // 0=HUMANOID, 1=DRONE, 2=COMBINE, 3=IOT_NODE
    uint8_t  current_job = 0; // 0=IDLE, 1=PLANT, 2=HARVEST, 3=WATER
    int32_t  target_x = 0;
    int32_t  target_y = 0;
    uint8_t  seed_type = 0;
    
    // IoT VM registers and state
    int32_t  registers[4] = {0, 0, 0, 0};
    uint16_t program[32] = {0};
    uint16_t program_size = 0;
    uint16_t pc = 0;
    int32_t  cmp_result = 0;

    int32_t  sensor_x = -1;
    int32_t  sensor_y = -1;
    int32_t  actuator_x = -1;
    int32_t  actuator_y = -1;

    bool operator==(const BotState& o) const {
        if (entity_id != o.entity_id || bot_type != o.bot_type || current_job != o.current_job ||
            target_x != o.target_x || target_y != o.target_y || seed_type != o.seed_type ||
            program_size != o.program_size || pc != o.pc || cmp_result != o.cmp_result ||
            sensor_x != o.sensor_x || sensor_y != o.sensor_y ||
            actuator_x != o.actuator_x || actuator_y != o.actuator_y) {
            return false;
        }
        for (int i = 0; i < 4; ++i) {
            if (registers[i] != o.registers[i]) return false;
        }
        for (int i = 0; i < 32; ++i) {
            if (program[i] != o.program[i]) return false;
        }
        return true;
    }
};

enum class EntityTag : uint32_t {
    NONE       = 0,
    ATTACK     = 1 << 0,
    PROJECTILE = 1 << 1,
    MELEE      = 1 << 2,
    AOE        = 1 << 3,
    MAGIC      = 1 << 4,
    TORNADO    = 1 << 5,
    CHAIN_LIGHTNING = 1 << 6,
    PIERCE_ALL = 1 << 7,

    // Enemy Types
    ENEMY_CHASER = 1 << 8,
    ENEMY_RANGER = 1 << 9,

    CONE         = 1 << 10,
    PLAYER_OWNED = 1 << 11,
    ENEMY_OWNED  = 1 << 12,
};

#pragma pack(push, 1)

struct EntityStats {
    int32_t base_hp = 0;
    int32_t base_damage = 0;
    int64_t base_speed = 0; // fixed point

    int32_t flat_hp = 0;
    int32_t pct_hp = 0; // % e.g., 10 for +10%

    int32_t flat_damage = 0;
    int32_t pct_damage = 0; // %
    int32_t more_damage = 0; // % multiplicative

    int64_t flat_speed = 0; // fixed point
    int32_t pct_speed = 0; // %

    int32_t flat_armor = 0;
    int32_t pct_armor = 0;

    int32_t flat_attack_speed = 0;
    int32_t pct_attack_speed = 0;

    int32_t flat_pierce = 0;

    int32_t crit_chance = 5; // 5% base
    int32_t crit_multiplier = 150; // 150% base (1.5x)

    int32_t get_max_hp() const {
        int32_t base = base_hp + flat_hp;
        return base + (base * pct_hp) / 100;
    }

    int32_t get_damage() const {
        int32_t base = base_damage + flat_damage;
        int32_t increased = base + (base * pct_damage) / 100;
        return increased + (increased * more_damage) / 100;
    }

    int64_t get_speed() const {
        int64_t base = base_speed + flat_speed;
        return base + (base * pct_speed) / 100;
    }
};

struct Equipment {
    uint64_t slots[3] = {0, 0, 0}; // [0]=Weapon, [1]=Armor, [2]=Accessory. item IDs (seed)
};

#pragma pack(pop)

// The Hot Path SoA for active/dirty entities
struct ChaosState {
    std::vector<EntityID>   dense_ids;
    std::vector<int32_t>    node_ids;       // Dynamic AABB Tree node handles
    std::vector<int64_t>    tree_min_x;
    std::vector<int64_t>    tree_min_y;
    std::vector<int64_t>    tree_max_x;
    std::vector<int64_t>    tree_max_y;
    
    // Physics & Movement
    std::vector<int64_t>    positions_x;
    std::vector<int64_t>    positions_y;
    std::vector<int64_t>    velocities_x;
    std::vector<int64_t>    velocities_y;
    std::vector<int64_t>    radii;
    std::vector<int64_t>    frictions;
    
    // Combat State
    std::vector<int32_t>    healths;
    std::vector<int32_t>    pierces;
    
    // Identity & Flags
    std::vector<EntityType> types;
    std::vector<uint32_t>   tags;
    std::vector<uint8_t>    flags;

    void clear() {
        dense_ids.clear();
        node_ids.clear();
        positions_x.clear(); positions_y.clear();
        velocities_x.clear(); velocities_y.clear();
        radii.clear(); frictions.clear();
        healths.clear(); pierces.clear();
        types.clear(); tags.clear(); flags.clear();
    }
    
    size_t size() const { return dense_ids.size(); }

    struct RestrictedView {
        EntityID* __restrict dense_ids;
        int32_t* __restrict node_ids;
        int64_t* __restrict positions_x;
        int64_t* __restrict positions_y;
        int64_t* __restrict velocities_x;
        int64_t* __restrict velocities_y;
        int64_t* __restrict radii;
        int64_t* __restrict frictions;
        int32_t* __restrict healths;
        int32_t* __restrict pierces;
        EntityType* __restrict types;
        uint32_t* __restrict tags;
        uint8_t* __restrict flags;
    };

    RestrictedView get_restricted_view() {
        return {
            dense_ids.data(),
            node_ids.data(),
            positions_x.data(), positions_y.data(),
            velocities_x.data(), velocities_y.data(),
            radii.data(), frictions.data(),
            healths.data(), pierces.data(),
            types.data(), tags.data(), flags.data()
        };
    }
};

// Snapshot of dirty + tombstoned entities at a bisection boundary
struct BisectSnapshot {
    uint32_t              tick = 0;
    EntityID              next_id = 1;
    
    // Master RNG Seeds
    uint64_t              shotgun_seed = 0;
    uint64_t              tornado_seed = 0;
    uint64_t              crit_seed = 0;

    ChaosState            chaos;            // Contiguous hot data
    std::vector<EntityStats> heavy_stats;   // Dense heavy data
    std::vector<Equipment>   heavy_equip;   // Dense equipment data
    std::vector<EntityID> entity_recycle_bin; // Non-projectile IDs available for reuse
    std::vector<EntityID> attack_recycle_bin;
    std::vector<uint64_t> hit_pairs;        // Collision history
    std::vector<SimAABB>  walls;            // Static environment
    std::vector<SimPolygon> polygons;

    // Map state for farming game
    int32_t               map_width = 0;
    int32_t               map_height = 0;
    int64_t               cell_size_fixed = 0;
    std::vector<int32_t>  water_table;
    std::vector<uint8_t>  aquifers;
    std::vector<uint8_t>  rivers;
    std::vector<uint8_t>  lakes;
    std::vector<uint8_t>  structure_type;
    std::vector<int32_t>  water_pressure;
    std::vector<uint8_t>  chunk_tiers;

    std::vector<int32_t>  soil_nitrogen;
    std::vector<int32_t>  soil_phosphorus;
    std::vector<int32_t>  soil_potassium;
    std::vector<int32_t>  soil_calcium;
    std::vector<int32_t>  soil_magnesium;
    std::vector<int32_t>  soil_sulfur;
    std::vector<int32_t>  soil_compactness;
    std::vector<uint8_t>  crop_type;
    std::vector<int32_t>  crop_growth;
    std::vector<int32_t>  crop_health;
    std::vector<uint8_t>  crop_prefix;
    std::vector<uint8_t>  crop_suffix;
    std::vector<uint8_t>  crop_implicit;
    std::vector<int32_t>  planted_tile_indices;
    std::vector<BotState> bot_states;
    std::vector<uint8_t>  actuator_activated;
    uint8_t               weather_state;
    int32_t               weather_timer;
    int32_t               co2_level;
    std::vector<int32_t>  elevation;
    std::vector<uint8_t>  biome;
    std::vector<uint8_t>  surface_type;
    std::vector<uint8_t>  mineral_type;
    int32_t               starter_spawn_x = 0;
    int32_t               starter_spawn_y = 0;
    int64_t               starter_pond_volume = 0;
    int64_t               starter_pond_max = 0;
    std::vector<Hydrology::ReservoirNode> hydraulic_reservoirs;
    std::vector<Hydrology::RiverBelt>     hydraulic_river_belts;
};
