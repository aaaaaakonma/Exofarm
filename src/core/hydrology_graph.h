#pragma once
#include "fixed.h"
#include <vector>
#include <cstdint>
#include <unordered_map>
#include <algorithm>

namespace Hydrology {

// Flow capacity rate provided per river tile (in 16.16 fixed-point)
constexpr int64_t FLOW_CAPACITY_PER_TILE = 10LL * ONE; // 10.0 units/tick per tile

struct ReservoirNode {
    uint32_t id = 0;
    int64_t  current_volume = 0;    // 16.16 Fixed-Point (in liters/units)
    int64_t  max_capacity = 0;      // 16.16 Fixed-Point
    int64_t  water_level = 0;       // 16.16 Fixed-Point percentage (0 to 100 * ONE)
    int32_t  tile_count = 0;
    int64_t  inflow_rate = 0;       // per tick
    int64_t  outflow_rate = 0;      // per tick
    int64_t  extraction_rate = 0;   // total drawn by active pumps on this reservoir
    int64_t  seepage_rate = 0;
    bool     is_infinite = false;   // Ocean is infinite
    bool     is_dry = false;
    std::vector<int32_t> tile_indices;

    bool operator==(const ReservoirNode& o) const {
        return id == o.id && current_volume == o.current_volume && max_capacity == o.max_capacity &&
               water_level == o.water_level && tile_count == o.tile_count && inflow_rate == o.inflow_rate &&
               outflow_rate == o.outflow_rate && extraction_rate == o.extraction_rate &&
               seepage_rate == o.seepage_rate && is_infinite == o.is_infinite && is_dry == o.is_dry &&
               tile_indices == o.tile_indices;
    }
};

struct RiverBelt {
    uint32_t id = 0;
    uint8_t  tier = 1;               // 1 = Brook (Spring -> Confluence), 2 = River (Confluence -> Confluence / Lake / Ocean), 3 = Trunk
    uint32_t from_node = 0;          // Upstream reservoir ID (0 if headwaters / spring)
    uint32_t to_node = 0;            // Downstream reservoir ID (0 if ocean/sink)
    uint32_t upstream_belt_id = 0;   // Primary upstream river belt ID (0 if none)
    uint32_t downstream_belt_id = 0; // Downstream river belt ID (0 if none)
    std::vector<uint32_t> upstream_belt_ids; // All upstream tributary reaches merging into this reach at its confluence
    int32_t  tile_count = 0;         // Pipe length in tiles
    int64_t  max_rate = 0;           // Flow capacity
    int64_t  pump_rate = 0;          // Total rate drawn by active pumps parasitizing this pipe
    int64_t  flow_rate = 0;          // Effective throughput
    int64_t  water_level = 0;        // Water level percentage: 0 to 100 * ONE (16.16 fixed-point)
    bool     is_dry = false;
    std::vector<int32_t> tile_indices;

    bool operator==(const RiverBelt& o) const {
        return id == o.id && tier == o.tier && from_node == o.from_node && to_node == o.to_node &&
               upstream_belt_id == o.upstream_belt_id && downstream_belt_id == o.downstream_belt_id &&
               upstream_belt_ids == o.upstream_belt_ids &&
               tile_count == o.tile_count && max_rate == o.max_rate && pump_rate == o.pump_rate &&
               flow_rate == o.flow_rate && water_level == o.water_level && is_dry == o.is_dry &&
               tile_indices == o.tile_indices;
    }
};

using RiverPipe = RiverBelt;

class HydraulicGraph {
public:
    HydraulicGraph() = default;

    void set_dimensions(int32_t w, int32_t h) {
        _map_width = w;
        _map_height = h;
    }

    void clear() {
        _reservoirs.clear();
        _river_belts.clear();
        _tile_to_reservoir.clear();
        _tile_to_river_belt.clear();
        _pump_extractions.clear();
    }

    uint32_t add_reservoir(int64_t initial_vol, int64_t max_cap, bool infinite, const std::vector<int32_t>& tiles) {
        uint32_t id = static_cast<uint32_t>(_reservoirs.size() + 1);
        ReservoirNode node;
        node.id = id;
        node.current_volume = initial_vol;
        node.max_capacity = max_cap;
        node.is_infinite = infinite;
        node.is_dry = (!infinite && initial_vol <= 0);
        node.tile_count = static_cast<int32_t>(tiles.size());
        if (infinite) {
            node.water_level = 100LL * ONE;
        } else if (max_cap > 0) {
            node.water_level = std::clamp((initial_vol * 100LL * ONE) / max_cap, 0LL, 100LL * ONE);
        } else {
            node.water_level = 0;
        }
        node.tile_indices = tiles;
        for (int32_t idx : tiles) {
            _tile_to_reservoir[idx] = id;
        }
        _reservoirs.push_back(node);
        return id;
    }

    uint32_t add_river_belt(uint32_t from_n, uint32_t to_n, int64_t max_q, const std::vector<int32_t>& tiles, uint32_t up_belt = 0, uint32_t down_belt = 0, uint8_t tier_val = 1, const std::vector<uint32_t>& up_belts = {}) {
        uint32_t id = static_cast<uint32_t>(_river_belts.size() + 1);
        RiverBelt belt;
        belt.id = id;
        belt.tier = tier_val;
        belt.from_node = from_n;
        belt.to_node = to_n;
        belt.upstream_belt_id = up_belt;
        belt.downstream_belt_id = down_belt;
        belt.upstream_belt_ids = up_belts;
        if (up_belt > 0 && std::find(belt.upstream_belt_ids.begin(), belt.upstream_belt_ids.end(), up_belt) == belt.upstream_belt_ids.end()) {
            belt.upstream_belt_ids.push_back(up_belt);
        }
        belt.tile_count = static_cast<int32_t>(tiles.size());
        belt.max_rate = (max_q > 0) ? max_q : (static_cast<int64_t>(belt.tile_count) * FLOW_CAPACITY_PER_TILE);
        if (belt.max_rate <= 0) belt.max_rate = FLOW_CAPACITY_PER_TILE;
        belt.pump_rate = 0;
        belt.flow_rate = belt.max_rate;
        belt.water_level = 100LL * ONE; // 100%
        belt.is_dry = false;
        belt.tile_indices = tiles;
        for (int32_t idx : tiles) {
            _tile_to_river_belt[idx] = id;
        }
        _river_belts.push_back(belt);
        return id;
    }

    void add_pump(int32_t tile_idx, int64_t rate) {
        _pump_extractions[tile_idx] = rate;
        _recalculate_extractions();
    }

    void remove_pump(int32_t tile_idx) {
        _pump_extractions.erase(tile_idx);
        _recalculate_extractions();
    }

    // Steady-state O(nodes) simulation tick
    void tick_hydrology(int32_t dt, uint8_t weather_state, int64_t rain_rate) {
        if (dt <= 0) return;

        // 1. Process River Pipes: transfer water from upstream reservoir/tributary reaches to downstream reaches
        for (auto& belt : _river_belts) {
            int64_t upstream_inflow = 0;
            if (!belt.upstream_belt_ids.empty()) {
                for (uint32_t up_id : belt.upstream_belt_ids) {
                    if (up_id > 0 && up_id <= _river_belts.size()) {
                        const auto& up_belt = _river_belts[up_id - 1];
                        if (!up_belt.is_dry) {
                            upstream_inflow += up_belt.flow_rate;
                        }
                    }
                }
            } else if (belt.upstream_belt_id > 0 && belt.upstream_belt_id <= _river_belts.size()) {
                const auto& up_belt = _river_belts[belt.upstream_belt_id - 1];
                if (!up_belt.is_dry) {
                    upstream_inflow = up_belt.flow_rate;
                }
            } else {
                // Headwater spring origin - natural spring base yield
                upstream_inflow = belt.max_rate;
            }

            int64_t available_flow = std::min(belt.max_rate, upstream_inflow);

            // Check upstream reservoir supply if connected (e.g. lake outlet)
            if (belt.from_node > 0 && belt.from_node <= _reservoirs.size()) {
                auto& src = _reservoirs[belt.from_node - 1];
                if (src.is_dry) {
                    available_flow = 0;
                } else if (!src.is_infinite) {
                    int64_t transfer = std::min(src.current_volume, available_flow * dt);
                    src.current_volume -= transfer;
                    if (src.current_volume <= 0) {
                        src.current_volume = 0;
                        src.is_dry = true;
                    }
                    available_flow = (dt > 0) ? (transfer / dt) : 0;
                }
            }

            // Apply pump extraction
            int64_t effective = available_flow - belt.pump_rate;
            if (effective <= 0) {
                belt.flow_rate = 0;
                belt.water_level = 0;
                belt.is_dry = true;
            } else {
                belt.flow_rate = effective;
                belt.water_level = (effective * 100LL * ONE) / belt.max_rate;
                if (belt.water_level > 100LL * ONE) belt.water_level = 100LL * ONE;
                belt.is_dry = false;
            }

            // Transfer to downstream reservoir if connected (lake or ocean)
            if (belt.to_node > 0 && belt.to_node <= _reservoirs.size() && !belt.is_dry) {
                auto& dst = _reservoirs[belt.to_node - 1];
                if (!dst.is_infinite) {
                    int64_t transfer = belt.flow_rate * dt;
                    dst.current_volume += transfer;
                    if (dst.current_volume > dst.max_capacity) {
                        dst.current_volume = dst.max_capacity;
                    }
                    if (dst.current_volume > 0) {
                        dst.is_dry = false;
                    }
                }
            }
        }

        // 2. Process Reservoirs: Inflows, Outflows, Pumps, Weather, Evaporation
        for (auto& r : _reservoirs) {
            if (r.is_infinite) {
                r.water_level = 100LL * ONE;
                r.is_dry = false;
                continue;
            }

            // Rainfall recharge
            if (rain_rate > 0 && r.tile_count > 0) {
                int64_t rain_in = (rain_rate * r.tile_count * dt) / 10;
                r.current_volume += rain_in;
            }

            // Evaporation during clear dry weather
            if (weather_state == 0) { // CLEAR
                int64_t evap = (2LL * ONE / 100) * r.tile_count * dt;
                r.current_volume -= evap;
            }

            // Extraction from active pumps on this reservoir
            if (r.extraction_rate > 0) {
                r.current_volume -= r.extraction_rate * dt;
            }

            // Clamping
            if (r.current_volume <= 0) {
                r.current_volume = 0;
                r.is_dry = true;
                r.water_level = 0;
            } else {
                r.is_dry = false;
                if (r.current_volume > r.max_capacity) {
                    r.current_volume = r.max_capacity;
                }
                if (r.max_capacity > 0) {
                    r.water_level = (r.current_volume * 100LL * ONE) / r.max_capacity;
                } else {
                    r.water_level = 100LL * ONE;
                }
            }
        }
    }

    int32_t get_reservoir_id_for_tile(int32_t tile_idx) const {
        auto it = _tile_to_reservoir.find(tile_idx);
        if (it != _tile_to_reservoir.end()) return it->second;
        return 0;
    }

    int32_t get_river_belt_id_for_tile(int32_t tile_idx) const {
        auto it = _tile_to_river_belt.find(tile_idx);
        if (it != _tile_to_river_belt.end()) return it->second;
        return 0;
    }

    const ReservoirNode* get_reservoir(uint32_t id) const {
        if (id > 0 && id <= _reservoirs.size()) {
            return &_reservoirs[id - 1];
        }
        return nullptr;
    }

    ReservoirNode* get_reservoir_mut(uint32_t id) {
        if (id > 0 && id <= _reservoirs.size()) {
            return &_reservoirs[id - 1];
        }
        return nullptr;
    }

    const RiverBelt* get_river_belt(uint32_t id) const {
        if (id > 0 && id <= _river_belts.size()) {
            return &_river_belts[id - 1];
        }
        return nullptr;
    }

    RiverBelt* get_river_belt_mut(uint32_t id) {
        if (id > 0 && id <= _river_belts.size()) {
            return &_river_belts[id - 1];
        }
        return nullptr;
    }

    const std::vector<ReservoirNode>& get_reservoirs() const { return _reservoirs; }
    void set_reservoirs(const std::vector<ReservoirNode>& r) {
        _reservoirs = r;
        _tile_to_reservoir.clear();
        for (const auto& res : _reservoirs) {
            for (int32_t idx : res.tile_indices) {
                _tile_to_reservoir[idx] = res.id;
            }
        }
    }

    const std::vector<RiverBelt>& get_river_belts() const { return _river_belts; }
    void set_river_belts(const std::vector<RiverBelt>& b) {
        _river_belts = b;
        _tile_to_river_belt.clear();
        for (const auto& belt : _river_belts) {
            for (int32_t idx : belt.tile_indices) {
                _tile_to_river_belt[idx] = belt.id;
            }
        }
    }

    const std::unordered_map<int32_t, int64_t>& get_pump_extractions() const { return _pump_extractions; }
    void set_pump_extractions(const std::unordered_map<int32_t, int64_t>& pe) {
        _pump_extractions = pe;
        _recalculate_extractions();
    }

private:
    void _recalculate_extractions() {
        for (auto& r : _reservoirs) {
            r.extraction_rate = 0;
        }
        for (auto& b : _river_belts) {
            b.pump_rate = 0;
        }
        for (const auto& pair : _pump_extractions) {
            int32_t tile_idx = pair.first;
            int64_t rate = pair.second;

            // Direct match
            int32_t r_id = get_reservoir_id_for_tile(tile_idx);
            if (r_id > 0 && r_id <= (int32_t)_reservoirs.size()) {
                _reservoirs[r_id - 1].extraction_rate += rate;
                continue;
            }

            int32_t b_id = get_river_belt_id_for_tile(tile_idx);
            if (b_id > 0 && b_id <= (int32_t)_river_belts.size()) {
                _river_belts[b_id - 1].pump_rate += rate;
                continue;
            }

            // If tile itself is not water, check 8-neighborhood for adjacent water body
            if (_map_width > 0 && _map_height > 0) {
                int32_t cx = tile_idx % _map_width;
                int32_t cy = tile_idx / _map_width;
                int dxs[] = {1, -1, 0, 0, 1, -1, 1, -1};
                int dys[] = {0, 0, 1, -1, 1, 1, -1, -1};
                bool found = false;
                for (int n = 0; n < 8; ++n) {
                    int32_t nx = cx + dxs[n];
                    int32_t ny = cy + dys[n];
                    if (nx >= 0 && nx < _map_width && ny >= 0 && ny < _map_height) {
                        int32_t n_idx = ny * _map_width + nx;
                        int32_t adj_r = get_reservoir_id_for_tile(n_idx);
                        if (adj_r > 0 && adj_r <= (int32_t)_reservoirs.size()) {
                            _reservoirs[adj_r - 1].extraction_rate += rate;
                            found = true;
                            break;
                        }
                        int32_t adj_b = get_river_belt_id_for_tile(n_idx);
                        if (adj_b > 0 && adj_b <= (int32_t)_river_belts.size()) {
                            _river_belts[adj_b - 1].pump_rate += rate;
                            found = true;
                            break;
                        }
                    }
                }
                if (found) continue;
            }
        }

        // Recalculate pipe flow rates and water levels (as percentage: 0 to 100 * ONE)
        for (auto& b : _river_belts) {
            if (b.max_rate <= 0) {
                b.max_rate = static_cast<int64_t>(b.tile_count) * FLOW_CAPACITY_PER_TILE;
                if (b.max_rate <= 0) b.max_rate = FLOW_CAPACITY_PER_TILE;
            }
            int64_t effective = b.max_rate - b.pump_rate;
            if (effective <= 0) {
                b.flow_rate = 0;
                b.water_level = 0;
                b.is_dry = true;
            } else {
                b.flow_rate = effective;
                b.water_level = (effective * 100LL * ONE) / b.max_rate;
                if (b.water_level > 100LL * ONE) b.water_level = 100LL * ONE;
                b.is_dry = false;
            }
        }
    }

    int32_t _map_width = 0;
    int32_t _map_height = 0;
    std::vector<ReservoirNode> _reservoirs;
    std::vector<RiverBelt>     _river_belts;
    std::unordered_map<int32_t, uint32_t> _tile_to_reservoir;
    std::unordered_map<int32_t, uint32_t> _tile_to_river_belt;
    std::unordered_map<int32_t, int64_t>  _pump_extractions;
};

} // namespace Hydrology
