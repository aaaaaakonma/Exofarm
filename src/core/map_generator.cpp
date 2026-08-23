#include "map_generator.h"
#include "world.h"
#include "fixed.h"
#include <vector>
#include <algorithm>
#include <map>

namespace Generation {

bool MapGenerator::generate_map(SimWorld& world, uint64_t seed, int64_t width, int64_t height, int64_t cell_size_fixed) {
    if (width <= 0 || height <= 0) return false;

    int32_t w = static_cast<int32_t>(width);
    int32_t h = static_cast<int32_t>(height);
    size_t total_tiles = static_cast<size_t>(w) * h;

    _micro_generator.set_seed(seed, w, h);

    world.set_map_dimensions(w, h, cell_size_fixed);

    std::vector<uint8_t> surface(total_tiles, static_cast<uint8_t>(SurfaceType::DRY_SOIL));
    std::vector<int32_t> elevation(total_tiles, 30 * ONE);
    std::vector<uint8_t> biome(total_tiles, 3); // 3 = Grassland
    std::vector<int32_t> water_table(total_tiles, 50 * ONE);
    std::vector<uint8_t> aquifers(total_tiles, 0);
    std::vector<uint8_t> rivers(total_tiles, 0);
    std::vector<int32_t> flow_targets(total_tiles, -1);
    std::vector<uint8_t> lakes(total_tiles, 0);
    std::vector<uint8_t> mineral_type(total_tiles, 0);

    std::vector<int32_t> soil_n(total_tiles, 100 * ONE);
    std::vector<int32_t> soil_p(total_tiles, 100 * ONE);
    std::vector<int32_t> soil_k(total_tiles, 100 * ONE);
    std::vector<int32_t> soil_ca(total_tiles, 100 * ONE);
    std::vector<int32_t> soil_mg(total_tiles, 50 * ONE);
    std::vector<int32_t> soil_s(total_tiles, 50 * ONE);
    std::vector<int32_t> soil_compact(total_tiles, 20 * ONE);

    int32_t num_chunks_x = (w + MACRO_CELL_TILES - 1) / MACRO_CELL_TILES;
    int32_t num_chunks_y = (h + MACRO_CELL_TILES - 1) / MACRO_CELL_TILES;

    // 1. Generate all micro chunks and assemble into world arrays
    for (int32_t cy = 0; cy < num_chunks_y; ++cy) {
        for (int32_t cx = 0; cx < num_chunks_x; ++cx) {
            MicroChunkData chunk = _micro_generator.generate_chunk(cx, cy, cell_size_fixed);
            int32_t start_x = cx * MACRO_CELL_TILES;
            int32_t start_y = cy * MACRO_CELL_TILES;

            for (int32_t ly = 0; ly < chunk.height; ++ly) {
                int32_t gy = start_y + ly;
                if (gy >= h) continue;

                for (int32_t lx = 0; lx < chunk.width; ++lx) {
                    int32_t gx = start_x + lx;
                    if (gx >= w) continue;

                    size_t c_idx = static_cast<size_t>(ly) * chunk.width + lx;
                    size_t g_idx = static_cast<size_t>(gy) * w + gx;

                    surface[g_idx] = chunk.surface_types[c_idx];
                    elevation[g_idx] = chunk.elevation[c_idx];
                    biome[g_idx] = chunk.biomes[c_idx];
                    water_table[g_idx] = chunk.water_table[c_idx];
                    rivers[g_idx] = chunk.rivers[c_idx];
                    flow_targets[g_idx] = chunk.flow_targets[c_idx];
                    lakes[g_idx] = chunk.lakes[c_idx];
                    mineral_type[g_idx] = chunk.mineral_types[c_idx];

                    soil_n[g_idx] = chunk.soil_n[c_idx];
                    soil_p[g_idx] = chunk.soil_p[c_idx];
                    soil_k[g_idx] = chunk.soil_k[c_idx];
                    soil_ca[g_idx] = chunk.soil_ca[c_idx];
                    soil_mg[g_idx] = chunk.soil_mg[c_idx];
                    soil_s[g_idx] = chunk.soil_s[c_idx];
                    soil_compact[g_idx] = chunk.soil_compact[c_idx];
                }
            }
        }
    }

    // --- ORGANIC DROPLET RIVER SIMULATION ---
    {
        // BFS ocean distance field
        std::vector<int32_t> ocean_dist(total_tiles, 999999);
        std::queue<int32_t> bfs_q;
        for (int32_t i = 0; i < (int32_t)total_tiles; ++i) {
            int32_t tx = i % w;
            int32_t ty = i / w;
            if (surface[i] == static_cast<uint8_t>(SurfaceType::WATER_DEEP) || 
                tx == 0 || tx == w - 1 || ty == 0 || ty == h - 1) {
                ocean_dist[i] = 0;
                bfs_q.push(i);
            }
        }
        while (!bfs_q.empty()) {
            int32_t cur = bfs_q.front();
            bfs_q.pop();
            int32_t cx = cur % w;
            int32_t cy = cur / w;
            for (int32_t dy = -1; dy <= 1; ++dy) {
                for (int32_t dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    int32_t nx = cx + dx;
                    int32_t ny = cy + dy;
                    if (nx >= 0 && nx < w && ny >= 0 && ny < h) {
                        int32_t ni = ny * w + nx;
                        if (ocean_dist[cur] + 1 < ocean_dist[ni]) {
                            ocean_dist[ni] = ocean_dist[cur] + 1;
                            bfs_q.push(ni);
                        }
                    }
                }
            }
        }
        // ── Coastal Erosion Pass ──
        // The tectonic heightmap often creates sheer cliffs at the land/sea boundary.
        // This pass uses the ocean distance field to gradually taper elevation near
        // the coast, creating natural beach slopes that rivers can flow down into.
        {
            constexpr int32_t BEACH_WIDTH = 6;  // tiles of gentle slope from ocean
            for (int32_t i = 0; i < (int32_t)total_tiles; ++i) {
                if (ocean_dist[i] <= 0 || ocean_dist[i] > BEACH_WIDTH) continue;
                if (surface[i] == static_cast<uint8_t>(SurfaceType::WATER_DEEP)) continue;
                
                // Maximum allowed elevation ramps up linearly with distance from ocean
                // At dist=1: max is just above sea level (gentle beach)
                // At dist=BEACH_WIDTH: max is sea level + ramp (natural slope)
                int64_t ramp_per_tile = ONE * 2; // 2 elevation units per tile of distance
                int64_t max_elev = ELEV_SEA_LEVEL + (static_cast<int64_t>(ocean_dist[i]) * ramp_per_tile);
                
                if (elevation[i] > static_cast<int32_t>(max_elev)) {
                    elevation[i] = static_cast<int32_t>(max_elev);
                    
                    // Close to the shore: mark as mud bank (wet beach)
                    if (ocean_dist[i] <= 2 && surface[i] == static_cast<uint8_t>(SurfaceType::DRY_SOIL)) {
                        surface[i] = static_cast<uint8_t>(SurfaceType::MUD_BANK);
                        water_table[i] = 95 * ONE;
                    }
                }
            }
        }

        std::vector<int32_t> flow(total_tiles, 0);
        std::vector<int32_t> sediment(total_tiles, 0); // sediment deposited at each tile

        // Collect and shuffle all land droplet sources
        std::vector<int32_t> droplets;
        droplets.reserve(total_tiles);
        for (int32_t i = 0; i < (int32_t)total_tiles; ++i) {
            if (surface[i] != static_cast<uint8_t>(SurfaceType::WATER_DEEP)) {
                droplets.push_back(i);
            }
        }
        
        uint32_t lcg = static_cast<uint32_t>(seed + 10101);
        for (size_t i = droplets.size(); i > 1; --i) {
            lcg = lcg * 1664525 + 1013904223;
            size_t j = lcg % i;
            std::swap(droplets[i - 1], droplets[j]);
        }

        std::vector<uint32_t> visited(total_tiles, 0);
        uint32_t trace_id = 0;

        for (int32_t start_idx : droplets) {
            trace_id++;
            int32_t cur = start_idx;
            int32_t max_steps = w + h;
            int64_t carried_sediment = 0; // sediment this droplet is carrying
            
            while (max_steps-- > 0) {
                int32_t cx = cur % w;
                int32_t cy = cur / w;
                
                // Terminal: reached ocean or map edge
                if (surface[cur] == static_cast<uint8_t>(SurfaceType::WATER_DEEP) || ocean_dist[cur] == 0) {
                    // ── Sediment Deposition: dump carried sediment at river mouth ──
                    // This naturally builds deltas and extends coastlines
                    if (carried_sediment > 0) {
                        // Spread sediment in a fan around the terminal tile
                        int32_t deposit_radius = 1 + static_cast<int32_t>(std::min(carried_sediment / (4 * ONE), (int64_t)3));
                        for (int32_t ddy = -deposit_radius; ddy <= deposit_radius; ++ddy) {
                            for (int32_t ddx = -deposit_radius; ddx <= deposit_radius; ++ddx) {
                                int32_t nx = cx + ddx;
                                int32_t ny = cy + ddy;
                                if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;
                                int32_t ni = ny * w + nx;
                                int32_t dist_sq = ddx * ddx + ddy * ddy;
                                if (dist_sq <= deposit_radius * deposit_radius) {
                                    // Amount decreases with distance from mouth
                                    int64_t deposit = carried_sediment / (1 + dist_sq * 2);
                                    sediment[ni] += static_cast<int32_t>(deposit);
                                    // Raise elevation slightly — building land
                                    int64_t raise = deposit / 8;
                                    elevation[ni] = static_cast<int32_t>(std::min(
                                        static_cast<int64_t>(elevation[ni]) + raise,
                                        ELEV_SEA_LEVEL + (2 * ONE)));
                                }
                            }
                        }
                    }
                    break;
                }
                
                flow[cur] += 1;
                visited[cur] = trace_id;
                
                // ── Flatland Meandering ──
                // In flat terrain, add a strong lateral noise force so rivers
                // snake and meander instead of running straight to the ocean.
                int64_t cur_elev = static_cast<int64_t>(elevation[cur]);
                
                // Compute local gradient: max elevation drop to any neighbor
                int64_t max_local_drop = 0;
                for (int32_t dy = -1; dy <= 1; ++dy) {
                    for (int32_t dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dy == 0) continue;
                        int32_t nx = cx + dx;
                        int32_t ny = cy + dy;
                        if (nx >= 0 && nx < w && ny >= 0 && ny < h) {
                            int64_t drop = cur_elev - elevation[ny * w + nx];
                            if (drop > max_local_drop) max_local_drop = drop;
                        }
                    }
                }
                
                // Sample low-frequency noise for meander direction
                // Large scale (>> 7 = /128) gives sweeping bends, not jagged wiggles
                int64_t px_fixed = static_cast<int64_t>(cx) * ONE;
                int64_t py_fixed = static_cast<int64_t>(cy) * ONE;
                int64_t meander_noise = SimMath::fbm2d(px_fixed >> 6, py_fixed >> 6, seed + 7777, 2);
                
                // Meander strength inversely proportional to gradient
                // Steep mountain streams: almost no meander
                // Flat plains: strong lateral wander
                int64_t meander_strength = 0;
                if (max_local_drop < ONE / 4)       meander_strength = 6 * ONE;  // very flat — big meanders
                else if (max_local_drop < ONE)       meander_strength = 3 * ONE;  // gentle hills
                else if (max_local_drop < 3 * ONE)   meander_strength = ONE;      // moderate slope
                // steep terrain: no meander at all
                
                int32_t best = -1;
                int64_t best_score = INT64_MIN;
                
                for (int32_t dy = -1; dy <= 1; ++dy) {
                    for (int32_t dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dy == 0) continue;
                        int32_t nx = cx + dx;
                        int32_t ny = cy + dy;
                        if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;
                        
                        int32_t ni = ny * w + nx;
                        if (visited[ni] == trace_id) continue;
                        
                        int64_t elev_drop = elevation[cur] - elevation[ni];
                        int64_t ocean_drop = static_cast<int64_t>(ocean_dist[cur] - ocean_dist[ni]) * ONE;
                        
                        // Core scoring: elevation dominates, ocean guides
                        int64_t score = elev_drop * 10 + ocean_drop * 2;
                        
                        // Converge towards paths with existing flow (tributary merging)
                        if (flow[ni] > 0) score += (static_cast<int64_t>(flow[ni]) * (ONE / 4)); 
                        
                        // Meander bias: perpendicular push based on noise
                        // The noise value determines whether to veer "left" or "right"
                        // relative to the general flow direction
                        if (meander_strength > 0) {
                            // Use dx,dy to compute a crude perpendicular preference
                            // meander_noise is in [-ONE, ONE] range
                            int64_t perp_score = mul_fixed(meander_noise, meander_strength);
                            // Apply perpendicular: favor dx when noise is positive, dy when negative
                            score += mul_fixed(perp_score, static_cast<int64_t>(dx - dy) * ONE / 2);
                        }
                        
                        lcg = lcg * 1664525 + 1013904223;
                        score += static_cast<int64_t>(lcg % static_cast<uint32_t>(ONE / 10));
                        
                        if (score > best_score) {
                            best_score = score;
                            best = ni;
                        }
                    }
                }
                
                if (best == -1) break;
                
                // ── Erosion & Sediment Pickup ──
                int64_t base_carve = ONE / 20;
                int64_t flow_bonus = (static_cast<int64_t>(flow[cur]) * base_carve) / 100; 
                if (flow_bonus > (ONE / 4)) flow_bonus = ONE / 4;
                int64_t total_carve = base_carve + flow_bonus;
                
                if (elevation[cur] > ELEV_SEA_LEVEL) {
                    int64_t old_elev = static_cast<int64_t>(elevation[cur]);
                    elevation[cur] = static_cast<int32_t>(std::max(ELEV_SEA_LEVEL, old_elev - total_carve));
                    
                    // Track how much material was removed — becomes sediment
                    carried_sediment += (old_elev - elevation[cur]);
                    
                    // Widen valley for strong rivers
                    if (flow[cur] > 75) {
                        int64_t side_carve = total_carve / 4;
                        for (int32_t cdy = -1; cdy <= 1; ++cdy) {
                            for (int32_t cdx = -1; cdx <= 1; ++cdx) {
                                if (cdx == 0 && cdy == 0) continue;
                                int32_t nx = cx + cdx;
                                int32_t ny = cy + cdy;
                                if (nx >= 0 && nx < w && ny >= 0 && ny < h) {
                                    int32_t ni = ny * w + nx;
                                    if (elevation[ni] > elevation[cur]) {
                                        int64_t old_n = static_cast<int64_t>(elevation[ni]);
                                        elevation[ni] = std::max(elevation[cur], static_cast<int32_t>(old_n - side_carve));
                                        carried_sediment += (old_n - elevation[ni]);
                                    }
                                }
                            }
                        }
                    }
                }
                
                // Droplet loses a fraction of sediment along the way (alluvial deposits)
                if (carried_sediment > ONE) {
                    int64_t trail_deposit = carried_sediment / 64;
                    carried_sediment -= trail_deposit;
                    sediment[cur] += static_cast<int32_t>(trail_deposit);
                }
                
                cur = best;
            }
        }

        // ── Apply rivers based on flow accumulation ──
        for (int32_t i = 0; i < (int32_t)total_tiles; ++i) {
            if (surface[i] == static_cast<uint8_t>(SurfaceType::WATER_DEEP)) continue;
            
            if (flow[i] > 1000) {
                surface[i] = static_cast<uint8_t>(SurfaceType::WATER_DEEP);
                rivers[i] = 1;
                water_table[i] = 100 * ONE;
                
                int32_t cx = i % w;
                int32_t cy = i / w;
                for (int32_t dy = -1; dy <= 1; ++dy) {
                    for (int32_t dx = -1; dx <= 1; ++dx) {
                        int32_t nx = cx + dx;
                        int32_t ny = cy + dy;
                        if (nx >= 0 && nx < w && ny >= 0 && ny < h) {
                            int32_t ni = ny * w + nx;
                            if (surface[ni] == static_cast<uint8_t>(SurfaceType::DRY_SOIL) || surface[ni] == static_cast<uint8_t>(SurfaceType::MUD_BANK)) {
                                surface[ni] = static_cast<uint8_t>(SurfaceType::WATER_SHALLOW);
                                rivers[ni] = 1;
                                water_table[ni] = 100 * ONE;
                            }
                        }
                    }
                }
            } else if (flow[i] > 240) {
                surface[i] = static_cast<uint8_t>(SurfaceType::WATER_SHALLOW);
                rivers[i] = 1;
                water_table[i] = 100 * ONE;
            } else if (flow[i] > 75) {
                surface[i] = static_cast<uint8_t>(SurfaceType::MUD_BANK);
                water_table[i] = 90 * ONE;
            }
        }

        // ── Biome Feedback: River Moisture Bleed ──
        // Rivers moisten the surrounding landscape, creating floodplains and oases.
        // BFS outward from river tiles, bleeding moisture into dry biomes.
        {
            std::vector<int32_t> river_moisture(total_tiles, 0);
            std::queue<int32_t> moisture_q;
            
            // Seed BFS from all river/water tiles with their flow strength
            for (int32_t i = 0; i < (int32_t)total_tiles; ++i) {
                if (rivers[i] || surface[i] == static_cast<uint8_t>(SurfaceType::WATER_SHALLOW) ||
                    surface[i] == static_cast<uint8_t>(SurfaceType::WATER_DEEP)) {
                    // Moisture intensity based on river size
                    int32_t intensity = std::min(flow[i] / 10, (int32_t)20) + 5;
                    river_moisture[i] = intensity;
                    moisture_q.push(i);
                }
            }
            
            while (!moisture_q.empty()) {
                int32_t cur = moisture_q.front();
                moisture_q.pop();
                if (river_moisture[cur] <= 1) continue;
                
                int32_t cx = cur % w;
                int32_t cy = cur / w;
                int32_t spread = river_moisture[cur] - 1;
                
                for (int32_t dy = -1; dy <= 1; ++dy) {
                    for (int32_t dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dy == 0) continue;
                        int32_t nx = cx + dx;
                        int32_t ny = cy + dy;
                        if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;
                        int32_t ni = ny * w + nx;
                        
                        if (spread > river_moisture[ni]) {
                            river_moisture[ni] = spread;
                            moisture_q.push(ni);
                        }
                    }
                }
            }
            
            // Apply biome upgrades based on moisture bleed
            for (int32_t i = 0; i < (int32_t)total_tiles; ++i) {
                if (river_moisture[i] <= 0) continue;
                if (surface[i] == static_cast<uint8_t>(SurfaceType::WATER_DEEP) ||
                    surface[i] == static_cast<uint8_t>(SurfaceType::WATER_SHALLOW)) continue;
                
                // Boost water table near rivers
                int64_t wt_boost = static_cast<int64_t>(river_moisture[i]) * (ONE * 2);
                water_table[i] = static_cast<int32_t>(std::min(
                    static_cast<int64_t>(water_table[i]) + wt_boost, 100LL * ONE));
                
                // Boost soil nutrients in floodplains (alluvial fertility)
                if (river_moisture[i] >= 3) {
                    int64_t nutrient_boost = static_cast<int64_t>(river_moisture[i]) * (ONE * 3);
                    soil_n[i] = static_cast<int32_t>(std::min(static_cast<int64_t>(soil_n[i]) + nutrient_boost, 300LL * ONE));
                    soil_p[i] = static_cast<int32_t>(std::min(static_cast<int64_t>(soil_p[i]) + nutrient_boost / 2, 300LL * ONE));
                    soil_k[i] = static_cast<int32_t>(std::min(static_cast<int64_t>(soil_k[i]) + nutrient_boost / 3, 300LL * ONE));
                }
                
                // Biome upgrade: Desert near big rivers → Grassland (Nile effect)
                if (biome[i] == 2 && river_moisture[i] >= 5) {
                    biome[i] = 3; // Desert → Grassland oasis
                }
                // Grassland near big rivers → Forest (lush floodplain)
                else if (biome[i] == 3 && river_moisture[i] >= 10) {
                    biome[i] = 4; // Grassland → Forest
                }
                // Tundra near rivers with enough moisture → Grassland
                else if (biome[i] == 1 && river_moisture[i] >= 8) {
                    biome[i] = 3; // Tundra → Grassland
                }
            }
        }

        // ── Sediment Deposition Surface Effects ──
        // Heavy sediment deposits create muddy, fertile floodplains
        for (int32_t i = 0; i < (int32_t)total_tiles; ++i) {
            if (sediment[i] > (2 * ONE) && 
                surface[i] == static_cast<uint8_t>(SurfaceType::DRY_SOIL)) {
                surface[i] = static_cast<uint8_t>(SurfaceType::MUD_BANK);
                // Sediment-rich soil is extremely fertile
                soil_n[i] = static_cast<int32_t>(std::min(static_cast<int64_t>(soil_n[i]) + 80LL * ONE, 300LL * ONE));
                soil_p[i] = static_cast<int32_t>(std::min(static_cast<int64_t>(soil_p[i]) + 60LL * ONE, 300LL * ONE));
                soil_compact[i] = static_cast<int32_t>(std::max(static_cast<int64_t>(soil_compact[i]) - 10LL * ONE, 5LL * ONE));
            }
        }
    }
    // --- END ORGANIC DROPLET RIVER SIMULATION ---

    // 2. Discover Starter Site
    int32_t center_x = w / 2;
    int32_t center_y = h / 2;
    int32_t spawn_x = center_x;
    int32_t spawn_y = center_y;
    bool found_spawn = false;

    // Search outwards in spiral
    for (int32_t r = 0; r < std::max(w, h) / 2 && !found_spawn; ++r) {
        for (int32_t dy = -r; dy <= r && !found_spawn; ++dy) {
            for (int32_t dx = -r; dx <= r && !found_spawn; ++dx) {
                if (std::abs(dx) != r && std::abs(dy) != r) continue;
                int32_t tx = center_x + dx;
                int32_t ty = center_y + dy;
                if (tx < 10 || tx >= w - 10 || ty < 10 || ty >= h - 10) continue;

                size_t idx = static_cast<size_t>(ty) * w + tx;
                uint8_t st = surface[idx];
                uint8_t bm = biome[idx];

                if (st == static_cast<uint8_t>(SurfaceType::DRY_SOIL) && (bm == 3 || bm == 4 || bm == 5)) {
                    // Verify a 5x5 build area is clear of deep water / cliffs
                    bool flat_area = true;
                    for (int32_t cy_i = -2; cy_i <= 2 && flat_area; ++cy_i) {
                        for (int32_t cx_i = -2; cx_i <= 2 && flat_area; ++cx_i) {
                            size_t n_idx = static_cast<size_t>(ty + cy_i) * w + (tx + cx_i);
                            uint8_t n_st = surface[n_idx];
                            if (n_st == static_cast<uint8_t>(SurfaceType::WATER_DEEP) || n_st == static_cast<uint8_t>(SurfaceType::ROCK_CLIFF)) {
                                flat_area = false;
                            }
                        }
                    }
                    if (flat_area) {
                        spawn_x = tx;
                        spawn_y = ty;
                        found_spawn = true;
                    }
                }
            }
        }
    }

    // Carve clean buildable clearing around spawn
    for (int32_t cy_i = -2; cy_i <= 2; ++cy_i) {
        for (int32_t cx_i = -2; cx_i <= 2; ++cx_i) {
            int32_t tx = spawn_x + cx_i;
            int32_t ty = spawn_y + cy_i;
            if (tx >= 0 && tx < w && ty >= 0 && ty < h) {
                size_t idx = static_cast<size_t>(ty) * w + tx;
                surface[idx] = static_cast<uint8_t>(SurfaceType::DRY_SOIL);
                rivers[idx] = 0;
                lakes[idx] = 0;
            }
        }
    }

    // 3. Guarantee Starter Pond (Reservoir)
    int32_t pond_cx = spawn_x + 5;
    int32_t pond_cy = spawn_y;
    if (pond_cx >= w - 4) pond_cx = spawn_x - 5;

    std::vector<int32_t> starter_pond_tiles;
    for (int32_t dy = -2; dy <= 2; ++dy) {
        for (int32_t dx = -2; dx <= 2; ++dx) {
            int32_t px = pond_cx + dx;
            int32_t py = pond_cy + dy;
            if (px >= 0 && px < w && py >= 0 && py < h) {
                size_t idx = static_cast<size_t>(py) * w + px;
                int32_t dist_sq = dx * dx + dy * dy;
                if (dist_sq <= 4) {
                    surface[idx] = static_cast<uint8_t>(SurfaceType::WATER_SHALLOW);
                    lakes[idx] = 1;
                    water_table[idx] = 100 * ONE;
                    starter_pond_tiles.push_back(static_cast<int32_t>(idx));
                } else if (dist_sq <= 6) {
                    surface[idx] = static_cast<uint8_t>(SurfaceType::MUD_BANK);
                    water_table[idx] = 90 * ONE;
                }
            }
        }
    }

    // 4. Guarantee Starter N-P-K mineral deposits
    int32_t patch_n_x = spawn_x - 4, patch_n_y = spawn_y - 3;
    int32_t patch_p_x = spawn_x - 4, patch_p_y = spawn_y + 3;
    int32_t patch_k_x = spawn_x,     patch_k_y = spawn_y + 5;

    auto place_patch = [&](int32_t px, int32_t py, SurfaceType st, uint8_t m_type) {
        for (int32_t dy = -1; dy <= 1; ++dy) {
            for (int32_t dx = -1; dx <= 1; ++dx) {
                int32_t tx = px + dx;
                int32_t ty = py + dy;
                if (tx >= 0 && tx < w && ty >= 0 && ty < h) {
                    size_t idx = static_cast<size_t>(ty) * w + tx;
                    surface[idx] = static_cast<uint8_t>(st);
                    mineral_type[idx] = m_type;
                }
            }
        }
    };

    place_patch(patch_n_x, patch_n_y, SurfaceType::MINERAL_N, 1);
    place_patch(patch_p_x, patch_p_y, SurfaceType::MINERAL_P, 2);
    place_patch(patch_k_x, patch_k_y, SurfaceType::MINERAL_K, 3);

    // Save Starter Site info
    _starter_site.spawn_x = spawn_x;
    _starter_site.spawn_y = spawn_y;
    _starter_site.pond_x = pond_cx;
    _starter_site.pond_y = pond_cy;
    _starter_site.patch_n_x = patch_n_x;
    _starter_site.patch_n_y = patch_n_y;
    _starter_site.patch_p_x = patch_p_x;
    _starter_site.patch_p_y = patch_p_y;
    _starter_site.patch_k_x = patch_k_x;
    _starter_site.patch_k_y = patch_k_y;

    // Apply arrays to SimWorld
    world.set_surface_type(surface);
    world.set_elevation(elevation);
    world.set_biome(biome);
    world.set_water_table(water_table);
    world.set_aquifers(aquifers);
    world.set_rivers(rivers);
    world.set_flow_targets(flow_targets);
    world.set_lakes(lakes);
    world.set_mineral_type(mineral_type);

    world.set_soil_nitrogen(soil_n);
    world.set_soil_phosphorus(soil_p);
    world.set_soil_potassium(soil_k);
    world.set_soil_calcium(soil_ca);
    world.set_soil_magnesium(soil_mg);
    world.set_soil_sulfur(soil_s);
    world.set_soil_compactness(soil_compact);

    world.set_starter_spawn(spawn_x, spawn_y);
    world.set_starter_pond(30000LL * ONE, 30000LL * ONE);

    // 5. Build Hydraulic Graph
    Hydrology::HydraulicGraph hydro_graph;
    hydro_graph.set_dimensions(w, h);

    // Register Starter Pond as Reservoir #1
    uint32_t pond_res_id = hydro_graph.add_reservoir(30000LL * ONE, 30000LL * ONE, false, starter_pond_tiles);
    _starter_site.pond_reservoir_id = pond_res_id;

    // Group river tiles into RiverBelt reaches
    std::vector<int32_t> river_tile_indices;
    for (size_t i = 0; i < total_tiles; ++i) {
        if (rivers[i]) {
            river_tile_indices.push_back(static_cast<int32_t>(i));
        }
    }

    if (!river_tile_indices.empty()) {
        int64_t max_rate = static_cast<int64_t>(river_tile_indices.size()) * Hydrology::FLOW_CAPACITY_PER_TILE;
        hydro_graph.add_river_belt(0, 0, max_rate, river_tile_indices, 0, 0, 2);
    }

    world.set_hydraulic_graph(hydro_graph);

    // 6. Build Static Wall Collisions
    world.clear_walls();
    for (int32_t y = 0; y < h; ++y) {
        for (int32_t x = 0; x < w; ++x) {
            size_t idx = static_cast<size_t>(y) * w + x;
            uint8_t st = surface[idx];
            if (st == static_cast<uint8_t>(SurfaceType::WATER_DEEP) || st == static_cast<uint8_t>(SurfaceType::ROCK_CLIFF)) {
                world.add_wall(static_cast<int64_t>(x) * cell_size_fixed, 
                               static_cast<int64_t>(y) * cell_size_fixed, 
                               cell_size_fixed, 
                               cell_size_fixed);
            }
        }
    }
    world.finalize_walls();

    return true;
}

} // namespace Generation
