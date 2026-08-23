#pragma once
#include "macro_world.h"
#include "shapes.h"
#include "rng.h"
#include "hydrology_graph.h"
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>

namespace Generation {

enum class SurfaceType : uint8_t {
    DRY_SOIL        = 0,
    TILLED_SOIL     = 1,
    MUD_BANK        = 2,
    WATER_SHALLOW   = 3, // Wadeable, movement speed reduced
    WATER_DEEP      = 4, // Impassable wall, requires bridge
    ROCK_CLIFF      = 5, // Impassable cliff wall
    MINERAL_N       = 6, // Nitrogen deposit
    MINERAL_P       = 7, // Phosphorus deposit
    MINERAL_K       = 8, // Potassium deposit
};
using MicroSurfaceType = SurfaceType;

struct MicroChunkData {
    int32_t chunk_x = 0;
    int32_t chunk_y = 0;
    int32_t width = MACRO_CELL_TILES;
    int32_t height = MACRO_CELL_TILES;

    std::vector<uint8_t> surface_types;
    std::vector<uint8_t> biomes;
    std::vector<int32_t> elevation;
    std::vector<int32_t> water_table;
    std::vector<uint8_t> rivers;
    std::vector<int32_t> flow_targets;
    std::vector<uint8_t> lakes;
    std::vector<uint8_t> mineral_types;

    std::vector<int32_t> soil_n;
    std::vector<int32_t> soil_p;
    std::vector<int32_t> soil_k;
    std::vector<int32_t> soil_ca;
    std::vector<int32_t> soil_mg;
    std::vector<int32_t> soil_s;
    std::vector<int32_t> soil_compact;

    std::vector<SimAABB> impassable_walls;
};

class MicroChunkGenerator {
public:
    explicit MicroChunkGenerator(uint64_t seed = 42, int32_t world_w = 512, int32_t world_h = 512) 
        : _macro_world(seed, world_w, world_h), _seed(seed) {}

    void set_seed(uint64_t seed, int32_t world_w = 512, int32_t world_h = 512) {
        _seed = seed;
        _macro_world.set_seed(seed, world_w, world_h);
    }

    const MacroWorld& get_macro_world() const { return _macro_world; }

    // Generates a single chunk of size MACRO_CELL_TILES x MACRO_CELL_TILES (16x16) on demand
    MicroChunkData generate_chunk(int32_t cx, int32_t cy, int64_t cell_size_fixed = 64 * ONE) const {
        MicroChunkData chunk;
        chunk.chunk_x = cx;
        chunk.chunk_y = cy;
        chunk.width = MACRO_CELL_TILES;
        chunk.height = MACRO_CELL_TILES;
        size_t total_tiles = static_cast<size_t>(chunk.width) * chunk.height;

        chunk.surface_types.resize(total_tiles, static_cast<uint8_t>(SurfaceType::DRY_SOIL));
        chunk.biomes.resize(total_tiles, 3);
        chunk.elevation.resize(total_tiles, 30 * ONE);
        chunk.water_table.resize(total_tiles, 50 * ONE);
        chunk.rivers.resize(total_tiles, 0);
        chunk.flow_targets.resize(total_tiles, -1);
        chunk.lakes.resize(total_tiles, 0);
        chunk.mineral_types.resize(total_tiles, 0);

        chunk.soil_n.resize(total_tiles, 100 * ONE);
        chunk.soil_p.resize(total_tiles, 100 * ONE);
        chunk.soil_k.resize(total_tiles, 100 * ONE);
        chunk.soil_ca.resize(total_tiles, 100 * ONE);
        chunk.soil_mg.resize(total_tiles, 50 * ONE);
        chunk.soil_s.resize(total_tiles, 50 * ONE);
        chunk.soil_compact.resize(total_tiles, 20 * ONE);

        int32_t start_x = cx * MACRO_CELL_TILES;
        int32_t start_y = cy * MACRO_CELL_TILES;

        std::vector<RiverSegment> river_segs = _macro_world.get_nearby_river_segments(cx, cy);

        // Direct per-tile tectonic sampling for full-resolution terrain fidelity
        const TectonicWorld& tec_world = _macro_world.get_tectonics();

        for (int32_t ly = 0; ly < chunk.height; ++ly) {
            for (int32_t lx = 0; lx < chunk.width; ++lx) {
                int32_t gx = start_x + lx;
                int32_t gy = start_y + ly;
                size_t idx = static_cast<size_t>(ly) * chunk.width + lx;

                int64_t px_fixed = static_cast<int64_t>(gx) * ONE;
                int64_t py_fixed = static_cast<int64_t>(gy) * ONE;

                // Full-resolution tectonic sample at this tile
                TectonicSample ts = tec_world.sample(px_fixed, py_fixed);

                // Micro terrain perturbation (unique seed — tectonic coast uses _seed+101)
                int64_t micro_noise = SimMath::fbm2d(px_fixed / 4, py_fixed / 4, _seed + 5101, 3);
                int64_t tile_elev = std::clamp(ts.elevation + mul_fixed(micro_noise, 2 * ONE), 0LL, 100 * ONE);

                // Per-tile water table from tectonic fields
                int64_t base_wt = (100 * ONE) - (std::max(0LL, tile_elev - ELEV_SEA_LEVEL) / 2) + mul_fixed(ts.moisture, 32768);
                if (ts.biome == 0) base_wt = 100 * ONE;
                else if (ts.biome == 6 || ts.is_glacial_tarn) base_wt = 98 * ONE;
                base_wt = std::clamp(base_wt, 10 * ONE, 100 * ONE);

                chunk.elevation[idx] = static_cast<int32_t>(tile_elev);
                chunk.biomes[idx] = ts.biome;

                // 1. Ocean Water Check
                if (!ts.is_land || tile_elev < ELEV_SEA_LEVEL) {
                    chunk.surface_types[idx] = static_cast<uint8_t>(SurfaceType::WATER_DEEP);
                    chunk.biomes[idx] = 0; // Ocean
                    chunk.water_table[idx] = 100 * ONE;
                    
                    SimAABB wall;
                    wall.x = static_cast<int64_t>(gx) * cell_size_fixed;
                    wall.y = static_cast<int64_t>(gy) * cell_size_fixed;
                    wall.width = cell_size_fixed;
                    wall.height = cell_size_fixed;
                    wall.user_data = static_cast<uint32_t>(SurfaceType::WATER_DEEP);
                    chunk.impassable_walls.push_back(wall);
                    continue;
                }

                // 2. Disable old mathematical rivers
                bool is_river_tile = false;

                // 4. Mountain Rock Peak Cliffs
                if (!is_river_tile && ts.biome == 7) {
                    uint32_t rh = SimMath::hash2d(gx / 3, gy / 3, _seed + 303);
                    if ((rh % 5) != 0) { // Natural ramp gaps
                        chunk.surface_types[idx] = static_cast<uint8_t>(SurfaceType::ROCK_CLIFF);
                        SimAABB wall;
                        wall.x = static_cast<int64_t>(gx) * cell_size_fixed;
                        wall.y = static_cast<int64_t>(gy) * cell_size_fixed;
                        wall.width = cell_size_fixed;
                        wall.height = cell_size_fixed;
                        wall.user_data = static_cast<uint32_t>(SurfaceType::ROCK_CLIFF);
                        chunk.impassable_walls.push_back(wall);
                    }
                }

                // 5. Mineral Veins
                if (chunk.surface_types[idx] == static_cast<uint8_t>(SurfaceType::DRY_SOIL) && ts.mineral_province > 0) {
                    SimMath::VoronoiResult vor = SimMath::voronoi2d(px_fixed / 4, py_fixed / 4, _seed + 404);
                    if (vor.dist_sq < (32000)) {
                        if (ts.mineral_province == 1) {
                            chunk.surface_types[idx] = static_cast<uint8_t>(SurfaceType::MINERAL_N);
                            chunk.mineral_types[idx] = 1;
                        } else if (ts.mineral_province == 2) {
                            chunk.surface_types[idx] = static_cast<uint8_t>(SurfaceType::MINERAL_P);
                            chunk.mineral_types[idx] = 2;
                        } else if (ts.mineral_province == 3) {
                            chunk.surface_types[idx] = static_cast<uint8_t>(SurfaceType::MINERAL_K);
                            chunk.mineral_types[idx] = 3;
                        }
                    }
                }

                // 6. Soil Chemistry Pedogenesis
                uint8_t st = chunk.surface_types[idx];
                uint8_t bm = chunk.biomes[idx];
                int64_t sn = 100 * ONE, sp = 100 * ONE, sk = 100 * ONE;
                int64_t sca = 100 * ONE, smg = 50 * ONE, ss = 50 * ONE, scomp = 20 * ONE;

                if (st == static_cast<uint8_t>(SurfaceType::MUD_BANK)) {
                    sn = 180 * ONE; sp = 120 * ONE; sk = 110 * ONE;
                    sca = 150 * ONE; smg = 40 * ONE; ss = 30 * ONE; scomp = 10 * ONE;
                } else if (bm == 6) { // Swamp
                    sn = 220 * ONE; sp = 90 * ONE; sk = 50 * ONE;
                    sca = 40 * ONE; smg = 60 * ONE; ss = 180 * ONE; scomp = 5 * ONE;
                } else if (bm == 4 || bm == 5) { // Forest
                    sn = 150 * ONE; sp = 130 * ONE; sk = 90 * ONE;
                    sca = 90 * ONE; smg = 50 * ONE; ss = 40 * ONE; scomp = 15 * ONE;
                } else if (bm == 2) { // Desert
                    sn = 20 * ONE; sp = 30 * ONE; sk = 150 * ONE;
                    sca = 180 * ONE; smg = 140 * ONE; ss = 70 * ONE; scomp = 60 * ONE;
                } else if (bm == 1 || bm == 7) { // Snow / Mountain
                    sn = 40 * ONE; sp = 50 * ONE; sk = 120 * ONE;
                    sca = 80 * ONE; smg = 100 * ONE; ss = 50 * ONE; scomp = 50 * ONE;
                }

                int64_t p_noise = SimMath::fbm2d(px_fixed / 3, py_fixed / 3, _seed + 505, 2);
                sn = std::clamp(sn + mul_fixed(p_noise, 15 * ONE), 5 * ONE, 300 * ONE);
                sp = std::clamp(sp + mul_fixed(p_noise, 15 * ONE), 5 * ONE, 300 * ONE);
                sk = std::clamp(sk + mul_fixed(p_noise, 15 * ONE), 5 * ONE, 300 * ONE);

                chunk.soil_n[idx] = static_cast<int32_t>(sn);
                chunk.soil_p[idx] = static_cast<int32_t>(sp);
                chunk.soil_k[idx] = static_cast<int32_t>(sk);
                chunk.soil_ca[idx] = static_cast<int32_t>(sca);
                chunk.soil_mg[idx] = static_cast<int32_t>(smg);
                chunk.soil_s[idx] = static_cast<int32_t>(ss);
                chunk.soil_compact[idx] = static_cast<int32_t>(scomp);
                chunk.water_table[idx] = static_cast<int32_t>(base_wt);
            }
        }

        return chunk;
    }

private:
    MacroWorld _macro_world;
    uint64_t   _seed = 42;
};

} // namespace Generation
