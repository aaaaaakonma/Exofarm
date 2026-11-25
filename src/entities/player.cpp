#include "player.h"
#include "world.h"
#include "fixed.h"

void Player::tick(EntityID id, const InputFrame &input, SimWorld &world) {
    uint32_t idx = world.get_dense_idx(id);
    if (idx == 0xFFFFFFFF) return;
    
    ChaosState &chaos = world.get_chaos_mut();
    if (chaos.types[idx] != EntityType::PLAYER || (chaos.flags[idx] & (uint8_t)EntityFlags::DEAD)) {
        return;
    }

    handle_movement(id, input, world);
    
    uint32_t move_idx = world.get_dense_idx(id);
    if (move_idx != 0xFFFFFFFF) {
        world.sync_node_aabb(move_idx);
    }

    handle_actions(id, input, world);
    
    // Always mark as dirty if it's an active player to ensure state is tracked
    // Re-fetch chaos — handle_actions may have spawned entities, invalidating prior refs
    uint32_t act_idx = world.get_dense_idx(id);
    if (act_idx != 0xFFFFFFFF) {
        world.get_chaos_mut().flags[act_idx] |= (uint8_t)EntityFlags::DIRTY;
    }
}

bool Player::is_passable(int64_t test_x, int64_t test_y, int64_t cur_elev, const SimWorld &world) {
    int32_t map_w = world.get_map_width();
    int32_t map_h = world.get_map_height();
    int64_t cell_sz = world.get_cell_size_fixed();

    if (map_w <= 0 || map_h <= 0 || cell_sz <= 0) {
        return true;
    }

    int32_t cell_x = static_cast<int32_t>(test_x / cell_sz);
    if (test_x < 0 && test_x % cell_sz != 0) cell_x--;
    int32_t cell_y = static_cast<int32_t>(test_y / cell_sz);
    if (test_y < 0 && test_y % cell_sz != 0) cell_y--;

    // World boundary check
    if (cell_x < 0 || cell_x >= map_w || cell_y < 0 || cell_y >= map_h) {
        return false;
    }

    int32_t t_idx = cell_y * map_w + cell_x;

    // Bridges allow traversal over deep water and ravines
    const auto &structs = world.get_structure_type();
    if (!structs.empty() && structs[t_idx] == static_cast<uint8_t>(StructureType::BRIDGE)) {
        return true;
    }

    // Impassable deep water channels (rivers and lakes level >= 2)
    const auto &rivers = world.get_rivers();
    if (!rivers.empty() && rivers[t_idx] >= 2) {
        return false;
    }

    const auto &lakes = world.get_lakes();
    if (!lakes.empty() && lakes[t_idx] >= 2) {
        return false;
    }

    // Elevation checks
    const auto &elevs = world.get_elevation();
    if (!elevs.empty()) {
        int64_t next_elev = elevs[t_idx];
        // Ocean / sea-level is impassable water without a bridge
        if (next_elev <= 0) {
            return false;
        }
        // Sheer climb cliff check
        if (cur_elev > 0 && (next_elev - cur_elev) > MAX_CLIMB_SLOPE) {
            return false;
        }
    }

    return true;
}

void Player::handle_movement(EntityID id, const InputFrame &input, SimWorld &world) {
    uint32_t idx = world.get_dense_idx(id);
    ChaosState &chaos = world.get_chaos_mut();
    EntityStats &stats = world.get_stats_mut(id);

    int64_t px = chaos.positions_x[idx];
    int64_t py = chaos.positions_y[idx];

    // Check terrain speed modifier based on current surface & structures
    int64_t terrain_mult = ONE; // 1.0 in 16.16
    int32_t map_w = world.get_map_width();
    int32_t map_h = world.get_map_height();
    int64_t cell_sz = world.get_cell_size_fixed();
    int64_t cur_elev = 0;

    if (map_w > 0 && map_h > 0 && cell_sz > 0) {
        int32_t cell_x = static_cast<int32_t>(px / cell_sz);
        if (px < 0 && px % cell_sz != 0) cell_x--;
        int32_t cell_y = static_cast<int32_t>(py / cell_sz);
        if (py < 0 && py % cell_sz != 0) cell_y--;

        if (cell_x >= 0 && cell_x < map_w && cell_y >= 0 && cell_y < map_h) {
            int32_t t_idx = cell_y * map_w + cell_x;
            const auto& structs = world.get_structure_type();
            const auto& surfs = world.get_surface_type();
            const auto& elevs = world.get_elevation();

            if (!elevs.empty()) {
                cur_elev = elevs[t_idx];
            }

            if (!structs.empty() && structs[t_idx] == static_cast<uint8_t>(StructureType::BRIDGE)) {
                terrain_mult = 72089; // 1.10x speed on wooden bridges
            } else if (!surfs.empty()) {
                uint8_t st = surfs[t_idx];
                if (st == static_cast<uint8_t>(Generation::MicroSurfaceType::WATER_SHALLOW)) {
                    terrain_mult = 42598; // 0.65x speed (wading through shallow water)
                } else if (st == static_cast<uint8_t>(Generation::MicroSurfaceType::MUD_BANK)) {
                    terrain_mult = 52428; // 0.80x speed (mud drag)
                }
            }
        }
    }

    int64_t raw_speed = stats.get_speed();
    int64_t base_speed = (cell_sz > 0) ? (raw_speed * cell_sz) / (64LL * ONE) : raw_speed;
    int64_t target_speed = mul_fixed(base_speed, terrain_mult);

    int32_t in_x = input.payload.player.move_x;
    int32_t in_y = input.payload.player.move_y;

    if (in_x != 0 || in_y != 0) {
        // Apply input acceleration
        int64_t acc_x = mul_fixed(in_x, target_speed);
        int64_t acc_y = mul_fixed(in_y, target_speed);

        chaos.velocities_x[idx] += acc_x;
        chaos.velocities_y[idx] += acc_y;

        // Apply standard friction
        chaos.velocities_x[idx] = mul_fixed(chaos.velocities_x[idx], FRICTION);
        chaos.velocities_y[idx] = mul_fixed(chaos.velocities_y[idx], FRICTION);

        // Clamp maximum velocity magnitude to target_speed
        int64_t vel_sq = mul_fixed(chaos.velocities_x[idx], chaos.velocities_x[idx]) + 
                         mul_fixed(chaos.velocities_y[idx], chaos.velocities_y[idx]);
        int64_t max_speed_sq = mul_fixed(target_speed, target_speed);
        if (vel_sq > max_speed_sq) {
            int64_t vel_len = sqrt_fixed(vel_sq);
            if (vel_len > 0) {
                chaos.velocities_x[idx] = mul_fixed(div_fixed(chaos.velocities_x[idx], vel_len), target_speed);
                chaos.velocities_y[idx] = mul_fixed(div_fixed(chaos.velocities_y[idx], vel_len), target_speed);
            }
        }
    } else {
        // Crisp braking friction when releasing controls
        chaos.velocities_x[idx] = mul_fixed(chaos.velocities_x[idx], BRAKE_FRICTION);
        chaos.velocities_y[idx] = mul_fixed(chaos.velocities_y[idx], BRAKE_FRICTION);
        if (std::abs(chaos.velocities_x[idx]) < 256) chaos.velocities_x[idx] = 0;
        if (std::abs(chaos.velocities_y[idx]) < 256) chaos.velocities_y[idx] = 0;
    }

    int64_t vx = chaos.velocities_x[idx];
    int64_t vy = chaos.velocities_y[idx];

    if (vx == 0 && vy == 0) {
        return;
    }

    int64_t target_px = px + vx;
    int64_t target_py = py + vy;

    // Deterministic axis-decoupled tangent sliding
    bool can_move_x = (vx == 0) || is_passable(target_px, py, cur_elev, world);
    bool can_move_y = (vy == 0) || is_passable(px, target_py, cur_elev, world);

    if (can_move_x && can_move_y) {
        if (is_passable(target_px, target_py, cur_elev, world)) {
            chaos.positions_x[idx] = target_px;
            chaos.positions_y[idx] = target_py;
        } else {
            // Diagonal corner blocked: slide along the dominant velocity axis
            if (std::abs(vx) >= std::abs(vy)) {
                chaos.positions_x[idx] = target_px;
                chaos.velocities_y[idx] = 0;
            } else {
                chaos.positions_y[idx] = target_py;
                chaos.velocities_x[idx] = 0;
            }
        }
    } else if (can_move_x) {
        // Slide along X tangent
        chaos.positions_x[idx] = target_px;
        chaos.velocities_y[idx] = 0;
    } else if (can_move_y) {
        // Slide along Y tangent
        chaos.positions_y[idx] = target_py;
        chaos.velocities_x[idx] = 0;
    } else {
        // Direct head-on obstacle: stop
        chaos.velocities_x[idx] = 0;
        chaos.velocities_y[idx] = 0;
    }
}

bool Player::try_modify_terrain(int32_t x, int32_t y, int32_t delta_elev, SimWorld &world) {
    return world.modify_terrain(x, y, delta_elev);
}

void Player::compute_target_pos(int64_t px, int64_t py, 
                               int64_t aim_x, int64_t aim_y,
                               int32_t move_x, int32_t move_y,
                               int64_t cell_sz, int64_t max_reach,
                               int64_t &out_target_x, int64_t &out_target_y) {
    int64_t reach = (max_reach > 0) ? max_reach : DEFAULT_REACH;
    int64_t dx = aim_x - px;
    int64_t dy = aim_y - py;

    if (aim_x == 0 && aim_y == 0 && (move_x != 0 || move_y != 0)) {
        dx = static_cast<int64_t>(move_x);
        dy = static_cast<int64_t>(move_y);
        int64_t dist = sqrt_fixed(mul_fixed(dx, dx) + mul_fixed(dy, dy));
        if (dist > 0) {
            out_target_x = px + mul_fixed(div_fixed(dx, dist), reach);
            out_target_y = py + mul_fixed(div_fixed(dy, dist), reach);
        } else {
            out_target_x = px;
            out_target_y = py;
        }
        return;
    }

    int64_t dist = sqrt_fixed(mul_fixed(dx, dx) + mul_fixed(dy, dy));
    if (dist > reach && dist > 0) {
        out_target_x = px + mul_fixed(div_fixed(dx, dist), reach);
        out_target_y = py + mul_fixed(div_fixed(dy, dist), reach);
    } else {
        out_target_x = aim_x;
        out_target_y = aim_y;
    }
}

void Player::handle_actions(EntityID id, const InputFrame &input, SimWorld &world) {
    uint32_t acts = input.payload.player.actions;
    if (acts == 0) return;

    uint32_t idx = world.get_dense_idx(id);
    if (idx == 0xFFFFFFFF) return;

    const auto &chaos = world.get_chaos();
    int64_t px = chaos.positions_x[idx];
    int64_t py = chaos.positions_y[idx];

    int64_t cell_sz = world.get_cell_size_fixed();
    if (cell_sz <= 0) cell_sz = 64LL * ONE;

    int64_t target_x = 0;
    int64_t target_y = 0;
    compute_target_pos(px, py, 
                       input.payload.player.aim_x, input.payload.player.aim_y,
                       input.payload.player.move_x, input.payload.player.move_y,
                       cell_sz, DEFAULT_REACH,
                       target_x, target_y);

    int32_t tx = static_cast<int32_t>(target_x / cell_sz);
    if (target_x < 0 && target_x % cell_sz != 0) tx--;
    int32_t ty = static_cast<int32_t>(target_y / cell_sz);
    if (target_y < 0 && target_y % cell_sz != 0) ty--;

    if (acts & (ACTION_TERRAFORM_RAISE | ACTION_TERRAFORM_LOWER)) {
        int32_t delta = (acts & ACTION_TERRAFORM_RAISE) ? ONE : -ONE;
        world.modify_terrain(tx, ty, delta);
    }
    if (acts & ACTION_CHOP) {
        world.chop_tree(target_x, target_y, 25, DEFAULT_REACH);
    }
    if (acts & ACTION_TILL) {
        world.till_soil(tx, ty);
    }
    if (acts & ACTION_WATER) {
        world.water_soil(tx, ty);
    }
    if (acts & ACTION_PLANT) {
        uint8_t c_type = input.payload.player.crop_type;
        if (c_type == 0) c_type = 1;
        world.plant_crop(tx, ty, c_type,
                         input.payload.player.crop_prefix,
                         input.payload.player.crop_suffix,
                         input.payload.player.crop_implicit);
    }
    if (acts & ACTION_HARVEST) {
        world.harvest_crop(tx, ty);
    }
}
