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
    handle_actions(id, input, world);
    
    // Always mark as dirty if it's an active player to ensure state is tracked
    // Re-fetch chaos — handle_actions may have spawned entities, invalidating prior refs
    world.get_chaos_mut().flags[world.get_dense_idx(id)] |= (uint8_t)EntityFlags::DIRTY;
}

#include "map_generator.h"

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

    if (map_w > 0 && map_h > 0 && cell_sz > 0) {
        int32_t cell_x = static_cast<int32_t>(px / cell_sz);
        if (px < 0 && px % cell_sz != 0) cell_x--;
        int32_t cell_y = static_cast<int32_t>(py / cell_sz);
        if (py < 0 && py % cell_sz != 0) cell_y--;

        if (cell_x >= 0 && cell_x < map_w && cell_y >= 0 && cell_y < map_h) {
            int32_t t_idx = cell_y * map_w + cell_x;
            const auto& structs = world.get_structure_type();
            const auto& surfs = world.get_surface_type();

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

    int64_t base_speed = stats.get_speed();
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
        constexpr int64_t BRAKE_FRICTION = 45875; // ~0.70 friction
        chaos.velocities_x[idx] = mul_fixed(chaos.velocities_x[idx], BRAKE_FRICTION);
        chaos.velocities_y[idx] = mul_fixed(chaos.velocities_y[idx], BRAKE_FRICTION);
        if (std::abs(chaos.velocities_x[idx]) < 256) chaos.velocities_x[idx] = 0;
        if (std::abs(chaos.velocities_y[idx]) < 256) chaos.velocities_y[idx] = 0;
    }

    // Apply velocity to position
    chaos.positions_x[idx] += chaos.velocities_x[idx];
    chaos.positions_y[idx] += chaos.velocities_y[idx];
}

void Player::handle_actions(EntityID id, const InputFrame &input, SimWorld &world) {
    // ARPG action handling removed.
}
