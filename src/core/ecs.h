#pragma once
#include "entity.h"
#include "shapes.h"
#include <vector>
#include <cstdint>
#include <algorithm>

#ifdef _MSC_VER
#define FORCE_INLINE __forceinline
#else
#define FORCE_INLINE inline __attribute__((always_inline))
#endif

class EntityContainer {
public:
    EntityContainer() {}

    // Pre-allocate all parallel arrays to avoid repeated reallocations during spawn
    // bursts. Because the level entity count is fully deterministic from the
    // SplitMix64 Level Seed, callers should always call reserve() before spawning.
    void reserve(size_t capacity) {
        _chaos.dense_ids.reserve(capacity);
        _chaos.node_ids.reserve(capacity);
        _chaos.tree_min_x.reserve(capacity);
        _chaos.tree_min_y.reserve(capacity);
        _chaos.tree_max_x.reserve(capacity);
        _chaos.tree_max_y.reserve(capacity);
        _chaos.positions_x.reserve(capacity);
        _chaos.positions_y.reserve(capacity);
        _chaos.velocities_x.reserve(capacity);
        _chaos.velocities_y.reserve(capacity);
        _chaos.radii.reserve(capacity);
        _chaos.frictions.reserve(capacity);
        _chaos.healths.reserve(capacity);
        _chaos.pierces.reserve(capacity);
        _chaos.types.reserve(capacity);
        _chaos.tags.reserve(capacity);
        _chaos.flags.reserve(capacity);
        _heavy_stats.reserve(capacity);
        _heavy_equip.reserve(capacity);
    }

    // Optional companion to reserve(): call when the maximum EntityID to be
    // issued this level is known upfront.  Without this, _id_to_chaos_idx grows
    // reactively in spawn() via amortised doubling (still correct, just slightly
    // more work on the first handful of spawns when IDs are large).
    void reserve_id_map(EntityID max_id) {
        if (max_id >= _id_to_chaos_idx.size()) {
            _id_to_chaos_idx.resize(max_id + 1, 0xFFFFFFFF);
        }
    }

    FORCE_INLINE void clear() {
        _chaos.clear();
        _chaos.tree_min_x.clear();
        _chaos.tree_min_y.clear();
        _chaos.tree_max_x.clear();
        _chaos.tree_max_y.clear();
        _heavy_stats.clear();
        _heavy_equip.clear();
        _id_to_chaos_idx.clear();
    }

    uint32_t spawn(EntityID id, EntityType type, int64_t x, int64_t y, int64_t radius, int32_t health, uint32_t tags = 0) {
        // EntityIDs are never reused, so the sparse array only ever grows.
        // Amortised doubling mirrors std::vector's own growth strategy, reducing
        // allocations from O(n) to O(log n) for sequential ID issuance.
        if (id >= _id_to_chaos_idx.size()) {
            size_t new_cap = std::max<size_t>(id + 1, _id_to_chaos_idx.size() * 2);
            _id_to_chaos_idx.resize(new_cap, 0xFFFFFFFF);
        }

        uint32_t idx = (uint32_t)_chaos.size();
        _id_to_chaos_idx[id] = idx;

        _chaos.dense_ids.push_back(id);
        _chaos.node_ids.push_back(-1); // To be set by SimWorld
        _chaos.tree_min_x.push_back(0);
        _chaos.tree_min_y.push_back(0);
        _chaos.tree_max_x.push_back(0);
        _chaos.tree_max_y.push_back(0);
        _chaos.positions_x.push_back(x);
        _chaos.positions_y.push_back(y);
        _chaos.velocities_x.push_back(0);
        _chaos.velocities_y.push_back(0);
        _chaos.radii.push_back(radius);
        _chaos.frictions.push_back(65536);
        _chaos.healths.push_back(health);
        _chaos.pierces.push_back(0);
        _chaos.types.push_back(type);
        _chaos.tags.push_back(tags);
        _chaos.flags.push_back((uint8_t)EntityFlags::DIRTY);

        // emplace_back constructs EntityStats in-place, avoiding a copy.
        _heavy_stats.push_back({health, 10, 327680});

        // emplace_back constructs Equipment in-place, avoiding a copy.
        _heavy_equip.emplace_back();

        return idx;
    }

    void kill(EntityID id, std::vector<EntityID>& entity_recycle_bin, std::vector<uint64_t>& hit_pairs, std::vector<EntityID>& attack_recycle_bin) {
        uint32_t idx = get_dense_idx(id);
        if (idx == 0xFFFFFFFF) return;

        // Compute last_idx once; reused for all swap-and-pop operations below.
        const uint32_t last_idx = (uint32_t)(_chaos.size() - 1);

        if (idx != last_idx) {
            EntityID last_id = _chaos.dense_ids[last_idx];

            // Swap-and-pop across all parallel SoA arrays to maintain contiguity.
            _chaos.dense_ids[idx]    = _chaos.dense_ids[last_idx];
            _chaos.node_ids[idx]     = _chaos.node_ids[last_idx];
            _chaos.tree_min_x[idx]   = _chaos.tree_min_x[last_idx];
            _chaos.tree_min_y[idx]   = _chaos.tree_min_y[last_idx];
            _chaos.tree_max_x[idx]   = _chaos.tree_max_x[last_idx];
            _chaos.tree_max_y[idx]   = _chaos.tree_max_y[last_idx];
            _chaos.positions_x[idx]  = _chaos.positions_x[last_idx];
            _chaos.positions_y[idx]  = _chaos.positions_y[last_idx];
            _chaos.velocities_x[idx] = _chaos.velocities_x[last_idx];
            _chaos.velocities_y[idx] = _chaos.velocities_y[last_idx];
            _chaos.radii[idx]        = _chaos.radii[last_idx];
            _chaos.frictions[idx]    = _chaos.frictions[last_idx];
            _chaos.healths[idx]      = _chaos.healths[last_idx];
            _chaos.pierces[idx]      = _chaos.pierces[last_idx];
            _chaos.types[idx]        = _chaos.types[last_idx];
            _chaos.tags[idx]         = _chaos.tags[last_idx];
            _chaos.flags[idx]        = _chaos.flags[last_idx];

            // Move instead of copy
            _heavy_stats[idx] = std::move(_heavy_stats[last_idx]);
            _heavy_equip[idx] = std::move(_heavy_equip[last_idx]);

            _id_to_chaos_idx[last_id] = idx;
        }

        _chaos.dense_ids.pop_back();
        _chaos.node_ids.pop_back();
        _chaos.tree_min_x.pop_back();
        _chaos.tree_min_y.pop_back();
        _chaos.tree_max_x.pop_back();
        _chaos.tree_max_y.pop_back();
        _chaos.positions_x.pop_back();
        _chaos.positions_y.pop_back();
        _chaos.velocities_x.pop_back();
        _chaos.velocities_y.pop_back();
        _chaos.radii.pop_back();
        _chaos.frictions.pop_back();
        _chaos.healths.pop_back();
        _chaos.pierces.pop_back();
        _chaos.types.pop_back();
        _chaos.tags.pop_back();
        _chaos.flags.pop_back();

        _heavy_stats.pop_back();
        _heavy_equip.pop_back();

        // Invalidate before returning.
        _id_to_chaos_idx[id] = 0xFFFFFFFF;
    }

    FORCE_INLINE uint32_t get_dense_idx(EntityID id) const {
        if (id >= _id_to_chaos_idx.size()) return 0xFFFFFFFF;
        return _id_to_chaos_idx[id];
    }

    FORCE_INLINE bool has_entity(EntityID id) const {
        return get_dense_idx(id) != 0xFFFFFFFF;
    }

    void rebuild_id_map() {
        std::fill(_id_to_chaos_idx.begin(), _id_to_chaos_idx.end(), 0xFFFFFFFF);
        for (size_t i = 0; i < _chaos.dense_ids.size(); ++i) {
            EntityID id = _chaos.dense_ids[i];
            if (id >= _id_to_chaos_idx.size()) {
                _id_to_chaos_idx.resize(id + 1, 0xFFFFFFFF);
            }
            _id_to_chaos_idx[id] = (uint32_t)i;
        }
    }

    FORCE_INLINE ChaosState& get_chaos() { return _chaos; }
    FORCE_INLINE const ChaosState& get_chaos() const { return _chaos; }

    FORCE_INLINE std::vector<EntityStats>& get_heavy_stats() { return _heavy_stats; }
    FORCE_INLINE const std::vector<EntityStats>& get_heavy_stats() const { return _heavy_stats; }

    FORCE_INLINE std::vector<Equipment>& get_heavy_equip() { return _heavy_equip; }
    FORCE_INLINE const std::vector<Equipment>& get_heavy_equip() const { return _heavy_equip; }

    // Copy overload: used when the caller must retain the source snapshot
    // (e.g. keeping it on the replay tape while also restoring to it).
    void set_state(const ChaosState& chaos, const std::vector<EntityStats>& stats, const std::vector<Equipment>& equip) {
        _chaos       = chaos;
        _heavy_stats = stats;
        _heavy_equip = equip;
        rebuild_id_map();
    }

    // Move overload: preferred when restoring a BisectSnapshot passed to a cold
    // Lambda. The snapshot vectors are large (all ChaosState hot data + heavy arrays);
    // moving them is O(1) vs O(n). The caller's snapshot is left empty after this
    // call, which is fine since the Lambda owns it exclusively.
    void set_state(ChaosState&& chaos, std::vector<EntityStats>&& stats, std::vector<Equipment>&& equip) {
        _chaos       = std::move(chaos);
        _heavy_stats = std::move(stats);
        _heavy_equip = std::move(equip);
        rebuild_id_map();
    }

private:
    ChaosState _chaos;
    std::vector<EntityStats> _heavy_stats;
    std::vector<Equipment>   _heavy_equip;
    std::vector<uint32_t>    _id_to_chaos_idx;
};
