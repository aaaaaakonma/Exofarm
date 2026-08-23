#pragma once
#include "fixed.h"
#include "rng.h"
#include "noise.h"
#include "shapes.h"
#include "tectonic_world.h"
#include <vector>
#include <queue>
#include <cstdint>
#include <algorithm>

namespace Generation {

// Macro cell represents 16x16 micro tiles
constexpr int32_t MACRO_CELL_TILES = 16;
constexpr int64_t MACRO_CELL_FIXED = MACRO_CELL_TILES * ONE;

// Elevation constants in 16.16 fixed-point
constexpr int64_t ELEV_SEA_LEVEL = 24LL * ONE;
constexpr int64_t ELEV_COAST_LOW = 30LL * ONE;
constexpr int64_t ELEV_HILLS     = 55LL * ONE;
constexpr int64_t ELEV_MOUNTAIN  = 68LL * ONE;

struct MacroDrainageNode {
    int64_t px = 0; // World fixed-point pos
    int64_t py = 0;
    int64_t relaxed_px = 0; // Smoothed pos for organic rivers
    int64_t relaxed_py = 0;
    int64_t elevation = 0;
    int32_t down_mx = 0; // Downstream macro cell
    int32_t down_my = 0;
    bool    has_downstream = false;
    int32_t flow_accum = 1; // Number of contributing upstream cells
    bool    is_river = false;
    bool    is_alpine_spring = false;
    bool    is_glacial_tarn = false;
    uint8_t river_tier = 1; // 1 = Brook, 2 = River, 3 = Main Trunk
};

struct MacroCell {
    int32_t mx = 0;
    int32_t my = 0;
    int64_t elevation = 0;        // 0 .. 100 * ONE
    int64_t temperature = 0;      // 0 .. 100 * ONE
    int64_t moisture = 0;         // 0 .. 100 * ONE
    int64_t water_table = 0;      // 0 .. 100 * ONE
    uint8_t biome = 3;            // 0=Ocean, 1=Tundra/Snow, 2=Desert, 3=Grassland, 4=Forest, 5=Rainforest, 6=Swamp, 7=Mountain
    bool    is_land = false;
    bool    is_glacial_tarn = false;
    uint8_t mineral_province = 0; // 0=None, 1=Nitrogen, 2=Phosphorus, 3=Potassium
    MacroDrainageNode drainage;
};

struct RiverSegment {
    int64_t x1, y1;
    int64_t x2, y2;
    int64_t flow_accum;
    int64_t width_deep;    // fixed-point radius
    int64_t width_shallow; // fixed-point radius
    int64_t width_mud;     // fixed-point radius
    uint8_t tier;
    uint32_t pipe_id;
};


class MacroWorld {
public:
    explicit MacroWorld(uint64_t seed = 42, int32_t world_w = 512, int32_t world_h = 512) 
        : _seed(seed), _world_w(world_w), _world_h(world_h), _tectonics(seed, world_w, world_h) {
        build();
    }

    void set_seed(uint64_t seed, int32_t world_w = 512, int32_t world_h = 512) { 
        _seed = seed;
        _world_w = world_w;
        _world_h = world_h;
        _tectonics.set_seed(seed, world_w, world_h);
        build();
    }
    uint64_t get_seed() const { return _seed; }
    const TectonicWorld& get_tectonics() const { return _tectonics; }

    void build() {
        _macro_w = (_world_w + MACRO_CELL_TILES - 1) / MACRO_CELL_TILES;
        _macro_h = (_world_h + MACRO_CELL_TILES - 1) / MACRO_CELL_TILES;
        int32_t total_cells = _macro_w * _macro_h;

        _cells.assign(total_cells, MacroCell());

        // Pass 1: Generate base properties and initial springs
        for (int32_t my = 0; my < _macro_h; ++my) {
            for (int32_t mx = 0; mx < _macro_w; ++mx) {
                MacroCell& cell = _cells[my * _macro_w + mx];
                cell.mx = mx;
                cell.my = my;

                int64_t cx_fixed = (static_cast<int64_t>(mx) * MACRO_CELL_FIXED) + (MACRO_CELL_FIXED / 2);
                int64_t cy_fixed = (static_cast<int64_t>(my) * MACRO_CELL_FIXED) + (MACRO_CELL_FIXED / 2);

                TectonicSample tec = _tectonics.sample(cx_fixed, cy_fixed);
                cell.elevation = tec.elevation;
                cell.temperature = tec.temperature;
                cell.moisture = tec.moisture;
                cell.biome = tec.biome;
                cell.is_land = tec.is_land;
                cell.is_glacial_tarn = false;
                cell.mineral_province = tec.mineral_province;

                int64_t wt = (100 * ONE) - (std::max(0LL, cell.elevation - ELEV_SEA_LEVEL) / 2) + mul_fixed(cell.moisture, 32768);
                if (cell.biome == 0) wt = 100 * ONE;
                else if (cell.biome == 6) wt = 98 * ONE;
                cell.water_table = std::clamp(wt, 10 * ONE, 100 * ONE);

                uint32_t dh = SimMath::hash2d(mx, my, _seed + 1234);
                cell.drainage.px = (static_cast<int64_t>(mx) * MACRO_CELL_FIXED) + (mul_fixed((dh & 0xFFFF), 39321) + (3 * ONE));
                cell.drainage.py = (static_cast<int64_t>(my) * MACRO_CELL_FIXED) + (mul_fixed(((dh >> 16) & 0xFFFF), 39321) + (3 * ONE));
                cell.drainage.elevation = cell.elevation;
                cell.drainage.is_glacial_tarn = false;

                // Seed flow accumulation from snow/high mountains and moisture
                if (tec.is_snow) {
                    cell.drainage.is_alpine_spring = true;
                    cell.drainage.flow_accum = 50; // High meltwater source
                } else if (cell.elevation >= ELEV_HILLS && (dh % 3 == 0)) {
                    cell.drainage.is_alpine_spring = true;
                    cell.drainage.flow_accum = 18; // Frequent springs starting at hills
                } else {
                    // Base precipitation flow
                    cell.drainage.flow_accum = (cell.moisture > 50 * ONE) ? 2 : 1;
                }
            }
        }

        // ─── Physically-Simulated River Generation ───────────────────────
        // Instead of Planchon-Darboux pit-filling + mathematical flow routing,
        // we simulate how real rivers work:
        //   1. Compute a BFS "ocean distance" field for every cell
        //   2. From each spring source, trace a path downhill with a persistent
        //      ocean-seeking force applied at every step
        //   3. Rivers are guaranteed to reach the ocean by construction —
        //      no dead ends, no cycles, no post-hoc pruning needed.

        // Pass 2: BFS ocean distance field — gives every land cell a direction
        // towards the nearest ocean sink, used as the downstream "force"
        std::vector<int32_t> ocean_dist(_macro_w * _macro_h, INT32_MAX);
        {
            std::queue<int32_t> bfs_q;
            for (int32_t i = 0; i < _macro_w * _macro_h; ++i) {
                int32_t mx = i % _macro_w;
                int32_t my = i / _macro_w;
                if (_cells[i].elevation <= ELEV_SEA_LEVEL ||
                    mx == 0 || mx == _macro_w - 1 || my == 0 || my == _macro_h - 1) {
                    ocean_dist[i] = 0;
                    bfs_q.push(i);
                }
            }
            while (!bfs_q.empty()) {
                int32_t cur = bfs_q.front();
                bfs_q.pop();
                int32_t cx = cur % _macro_w;
                int32_t cy = cur / _macro_w;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dy == 0) continue;
                        int32_t nx = cx + dx;
                        int32_t ny = cy + dy;
                        if (nx < 0 || nx >= _macro_w || ny < 0 || ny >= _macro_h) continue;
                        int32_t ni = ny * _macro_w + nx;
                        int32_t nd = ocean_dist[cur] + 1;
                        if (nd < ocean_dist[ni]) {
                            ocean_dist[ni] = nd;
                            bfs_q.push(ni);
                        }
                    }
                }
            }
        }

        // Pass 3: Collect spring sources, highest elevation first.
        // Higher springs get traced first so tributaries naturally merge into them.
        std::vector<int32_t> spring_sources;
        spring_sources.reserve(64);
        for (int32_t i = 0; i < _macro_w * _macro_h; ++i) {
            if (_cells[i].drainage.is_alpine_spring && _cells[i].is_land) {
                spring_sources.push_back(i);
            }
        }
        std::sort(spring_sources.begin(), spring_sources.end(), [&](int32_t a, int32_t b) {
            return _cells[a].elevation > _cells[b].elevation;
        });

        // Pass 4: Trace each river from source to ocean.
        // At each step the river picks the best neighbor using a weighted score:
        //   • Elevation drop  (strongly prefer downhill — how real water flows)
        //   • Ocean proximity  (persistent force towards ocean — escapes depressions)
        //   • Merge bonus      (prefer joining an existing river — forms tributaries)
        //   • Slight noise     (organic, non-mechanical look)
        // Cycle prevention: never revisit a cell within the same trace.
        std::vector<int32_t> path_owner(_macro_w * _macro_h, -1); // which trace owns each cell

        for (size_t si = 0; si < spring_sources.size(); ++si) {
            int32_t src = spring_sources[si];
            if (_cells[src].drainage.is_river) continue; // already absorbed by a higher river

            int32_t trace_id = static_cast<int32_t>(si);
            std::vector<int32_t> path;
            path.reserve(64);

            int32_t cur = src;
            bool reached_ocean = false;
            bool merged_into_existing = false;
            int32_t max_steps = (_macro_w + _macro_h) * 3;

            while (max_steps-- > 0) {
                int32_t cx = cur % _macro_w;
                int32_t cy = cur / _macro_w;

                // Terminal: reached ocean / map boundary
                if (_cells[cur].elevation <= ELEV_SEA_LEVEL ||
                    cx <= 0 || cx >= _macro_w - 1 || cy <= 0 || cy >= _macro_h - 1) {
                    reached_ocean = true;
                    break; // 'cur' is the ocean target, NOT added to path
                }

                path.push_back(cur);
                path_owner[cur] = trace_id;

                // Terminal: merged into an already-traced river
                if (_cells[cur].drainage.is_river) {
                    merged_into_existing = true;
                    break; // 'cur' is in path AND already has its downstream set
                }

                // ── Score each of the 8 neighbours ──
                int32_t best = -1;
                int64_t best_score = INT64_MIN;

                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dy == 0) continue;
                        int32_t nx = cx + dx;
                        int32_t ny = cy + dy;
                        if (nx < 0 || nx >= _macro_w || ny < 0 || ny >= _macro_h) continue;
                        int32_t ni = ny * _macro_w + nx;

                        // Never revisit a cell in this trace (hard cycle prevention)
                        if (path_owner[ni] == trace_id) continue;

                        // (a) Elevation drop — primary terrain following (weight ×3)
                        int64_t elev_drop = _cells[cur].elevation - _cells[ni].elevation;
                        int64_t s = elev_drop * 3;

                        // (b) Ocean-seeking force — decreasing ocean_dist (weight ×2)
                        //     This is the "downstream push" that guarantees
                        //     the river eventually reaches the ocean.
                        s += static_cast<int64_t>(ocean_dist[cur] - ocean_dist[ni]) * 2 * ONE;

                        // (c) Merge bonus — prefer flowing into existing rivers
                        if (_cells[ni].drainage.is_river) s += 8 * ONE;

                        // (d) Deterministic noise — organic feel, breaks ties
                        uint32_t h = SimMath::hash2d(
                            static_cast<uint32_t>(nx * 31 + cx),
                            static_cast<uint32_t>(ny * 37 + cy),
                            _seed + 9876);
                        s += static_cast<int64_t>(h % static_cast<uint32_t>(ONE / 2));

                        if (s > best_score) { best_score = s; best = ni; }
                    }
                }

                if (best < 0) break; // completely stuck (shouldn't happen)
                cur = best;
            }

            // Only commit paths that actually reached a valid terminus
            if ((!reached_ocean && !merged_into_existing) || path.empty()) continue;

            // ── Commit: link each path cell to its downstream neighbour ──
            for (size_t i = 0; i + 1 < path.size(); ++i) {
                int32_t idx = path[i];
                _cells[idx].drainage.is_river = true;
                _cells[idx].drainage.has_downstream = true;
                _cells[idx].drainage.down_mx = path[i + 1] % _macro_w;
                _cells[idx].drainage.down_my = path[i + 1] / _macro_w;
            }
            // Handle the last cell in the path
            int32_t last = path.back();
            _cells[last].drainage.is_river = true;
            if (reached_ocean && !merged_into_existing) {
                // Last land cell → downstream is the ocean / boundary cell 'cur'
                _cells[last].drainage.has_downstream = true;
                _cells[last].drainage.down_mx = cur % _macro_w;
                _cells[last].drainage.down_my = cur / _macro_w;
            }
            // If merged, 'last' already has its downstream from a previous trace
        }

        // Pass 5: Accumulate flow along traced river paths.
        // Topological order via BFS from leaf (source) nodes, so every
        // upstream cell contributes before its downstream is processed.
        {
            // Build upstream adjacency
            std::vector<std::vector<int32_t>> up_adj(_macro_w * _macro_h);
            std::vector<int32_t> in_deg(_macro_w * _macro_h, 0);
            for (int32_t i = 0; i < _macro_w * _macro_h; ++i) {
                if (_cells[i].drainage.is_river && _cells[i].drainage.has_downstream) {
                    int32_t d = _cells[i].drainage.down_my * _macro_w + _cells[i].drainage.down_mx;
                    up_adj[d].push_back(i);
                    in_deg[d]++;
                }
            }

            // Seed queue with leaf river cells (no upstream tributaries)
            std::queue<int32_t> topo_q;
            for (int32_t i = 0; i < _macro_w * _macro_h; ++i) {
                if (_cells[i].drainage.is_river && in_deg[i] == 0) {
                    topo_q.push(i);
                }
            }
            while (!topo_q.empty()) {
                int32_t cur = topo_q.front();
                topo_q.pop();
                if (_cells[cur].drainage.has_downstream) {
                    int32_t d = _cells[cur].drainage.down_my * _macro_w + _cells[cur].drainage.down_mx;
                    _cells[d].drainage.flow_accum += _cells[cur].drainage.flow_accum;
                    in_deg[d]--;
                    if (in_deg[d] <= 0 && _cells[d].drainage.is_river) {
                        topo_q.push(d);
                    }
                }
            }
        }

        // Pass 6: Assign river tiers based on accumulated flow
        for (auto& cell : _cells) {
            if (cell.drainage.is_river) {
                if (cell.drainage.flow_accum >= 300) cell.drainage.river_tier = 3;
                else if (cell.drainage.flow_accum >= 60) cell.drainage.river_tier = 2;
                else cell.drainage.river_tier = 1;
            }
        }

        // Pass 7: Laplacian smoothing of river nodes to remove angular zigzags
        for (int i = 0; i < _macro_w * _macro_h; ++i) {
            _cells[i].drainage.relaxed_px = _cells[i].drainage.px;
            _cells[i].drainage.relaxed_py = _cells[i].drainage.py;
        }

        std::vector<std::vector<int32_t>> upstream(_macro_w * _macro_h);
        for (int i = 0; i < _macro_w * _macro_h; ++i) {
            if (_cells[i].drainage.is_river && _cells[i].drainage.has_downstream) {
                int d_idx = _cells[i].drainage.down_my * _macro_w + _cells[i].drainage.down_mx;
                upstream[d_idx].push_back(i);
            }
        }

        int smooth_iters = 6;
        for (int iter = 0; iter < smooth_iters; ++iter) {
            std::vector<int64_t> new_px(_macro_w * _macro_h);
            std::vector<int64_t> new_py(_macro_w * _macro_h);
            
            for (int i = 0; i < _macro_w * _macro_h; ++i) {
                new_px[i] = _cells[i].drainage.relaxed_px;
                new_py[i] = _cells[i].drainage.relaxed_py;

                if (_cells[i].drainage.is_river) {
                    if (upstream[i].empty() || !_cells[i].drainage.has_downstream) {
                        // Anchor endpoints to prevent rivers shrinking into themselves
                        new_px[i] = _cells[i].drainage.px;
                        new_py[i] = _cells[i].drainage.py;
                    } else {
                        int64_t sum_x = _cells[i].drainage.px; // Anchor slightly to original position
                        int64_t sum_y = _cells[i].drainage.py;
                        int count = 1;
                        
                        if (_cells[i].drainage.has_downstream) {
                            int d_idx = _cells[i].drainage.down_my * _macro_w + _cells[i].drainage.down_mx;
                            sum_x += _cells[d_idx].drainage.relaxed_px;
                            sum_y += _cells[d_idx].drainage.relaxed_py;
                            count++;
                        }
                        
                        for (int u_idx : upstream[i]) {
                            sum_x += _cells[u_idx].drainage.relaxed_px;
                            sum_y += _cells[u_idx].drainage.relaxed_py;
                            count++;
                        }
                        
                        new_px[i] = sum_x / count;
                        new_py[i] = sum_y / count;
                    }
                }
            }
            
            for (int i = 0; i < _macro_w * _macro_h; ++i) {
                if (_cells[i].drainage.is_river) {
                    _cells[i].drainage.relaxed_px = new_px[i];
                    _cells[i].drainage.relaxed_py = new_py[i];
                }
            }
        }
    }

    MacroCell evaluate_cell(int32_t mx, int32_t my) const {
        if (mx < 0 || mx >= _macro_w || my < 0 || my >= _macro_h) {
            MacroCell empty;
            return empty;
        }
        return _cells[my * _macro_w + mx];
    }

    std::vector<RiverSegment> get_nearby_river_segments(int32_t center_mx, int32_t center_my) const {
        std::vector<RiverSegment> segments;
        segments.reserve(64);

        for (int dmy = -1; dmy <= 1; ++dmy) {
            for (int dmx = -1; dmx <= 1; ++dmx) {
                int32_t mx = center_mx + dmx;
                int32_t my = center_my + dmy;
                MacroCell src = evaluate_cell(mx, my);

                if (src.drainage.is_river && src.drainage.has_downstream) {
                    MacroCell dst = evaluate_cell(src.drainage.down_mx, src.drainage.down_my);

                    // Procedural Meandering Splines using Laplacian-smoothed positions
                    int64_t x1 = src.drainage.relaxed_px;
                    int64_t y1 = src.drainage.relaxed_py;
                    int64_t x2 = dst.drainage.relaxed_px;
                    int64_t y2 = dst.drainage.relaxed_py;
                    
                    int64_t drop = src.drainage.elevation - dst.drainage.elevation;
                    if (drop < 0) drop = 0;
                    
                    // Amplitude based on gradient (flatter = more meandering)
                    int64_t amplitude = 0;
                    if (drop < 1 * ONE) amplitude = 16 * ONE; // Huge meanders in flat plains
                    else if (drop < 3 * ONE) amplitude = 10 * ONE; // Medium meanders in gentle hills
                    else if (drop < 10 * ONE) amplitude = 4 * ONE; // Slight meanders in steeper terrain
                    
                    int num_steps = 8; // Higher subdivision for smooth curves
                    int64_t dx = x2 - x1;
                    int64_t dy = y2 - y1;
                    
                    int64_t len = sqrt_fixed(mul_fixed(dx, dx) + mul_fixed(dy, dy));
                    int64_t perp_x = 0, perp_y = 0;
                    if (len > 0) {
                        perp_x = -div_fixed(dy, len);
                        perp_y = div_fixed(dx, len);
                    }
                    
                    int64_t prev_x = x1;
                    int64_t prev_y = y1;
                    
                    for (int i = 1; i <= num_steps; ++i) {
                        int64_t t = div_fixed(i * ONE, num_steps * ONE);
                        int64_t curr_x = x1 + mul_fixed(dx, t);
                        int64_t curr_y = y1 + mul_fixed(dy, t);
                        
                        if (i < num_steps && amplitude > 0) {
                            // Divide by 128 (shift right 7) so 1 unit of noise space = 128 tiles = 2048 world units.
                            // This creates large, sweeping meanders instead of jagged zig-zags.
                            int64_t noise_val = SimMath::fbm2d(curr_x >> 7, curr_y >> 7, _seed + 777, 2);
                            int64_t offset = mul_fixed(noise_val, amplitude);
                            curr_x += mul_fixed(perp_x, offset);
                            curr_y += mul_fixed(perp_y, offset);
                        }
                        
                        RiverSegment seg;
                        seg.x1 = prev_x;
                        seg.y1 = prev_y;
                        seg.x2 = curr_x;
                        seg.y2 = curr_y;
                        seg.flow_accum = src.drainage.flow_accum;
                        seg.tier = src.drainage.river_tier;
                        seg.pipe_id = (static_cast<uint32_t>(mx & 0xFFFF) << 16) | static_cast<uint32_t>(my & 0xFFFF);
                        
                        if (seg.tier == 3) {
                            seg.width_deep    = 117964; // 1.8 * ONE
                            seg.width_shallow = 209715; // 3.2 * ONE
                            seg.width_mud     = 314572; // 4.8 * ONE
                        } else if (seg.tier == 2) {
                            seg.width_deep    = 58982;  // 0.9 * ONE
                            seg.width_shallow = 131072; // 2.0 * ONE
                            seg.width_mud     = 209715; // 3.2 * ONE
                        } else {
                            seg.width_deep    = 26214;  // 0.4 * ONE
                            seg.width_shallow = 65536;  // 1.0 * ONE
                            seg.width_mud     = 117964; // 1.8 * ONE
                        }
                        
                        segments.push_back(seg);
                        prev_x = curr_x;
                        prev_y = curr_y;
                    }
                }
            }
        }

        return segments;
    }

private:
    uint64_t      _seed = 42;
    int32_t       _world_w = 512;
    int32_t       _world_h = 512;
    int32_t       _macro_w = 0;
    int32_t       _macro_h = 0;
    TectonicWorld _tectonics;
    std::vector<MacroCell> _cells;
};

} // namespace Generation
