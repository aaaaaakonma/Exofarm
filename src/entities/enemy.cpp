#include "enemy.h"
#include "world.h"
#include "fixed.h"
#include "cordic.h"

void Enemy::tick(uint32_t idx, int64_t target_x, int64_t target_y, SimWorld &world, ChaosState::RestrictedView& v) {
    handle_ai(idx, target_x, target_y, world, v);
    handle_movement(idx, v);
    v.flags[idx] |= (uint8_t)EntityFlags::DIRTY;
}

void Enemy::handle_ai(uint32_t idx, int64_t target_x, int64_t target_y, SimWorld &world, ChaosState::RestrictedView& v) {
    int64_t pos_x = v.positions_x[idx];
    int64_t pos_y = v.positions_y[idx];

    int32_t map_w = world.get_map_width();
    int32_t map_h = world.get_map_height();
    int64_t cell_sz = world.get_cell_size_fixed();

    if (map_w <= 0 || map_h <= 0 || cell_sz <= 0) return;

    int64_t final_target_x = target_x;
    int64_t final_target_y = target_y;
    bool targeting_crop = false;
    int32_t target_crop_idx = -1;

    // Find nearest planted crop
    const auto& planted = world.get_planted_tile_indices();
    int64_t min_dist_sq = 0x7FFFFFFFFFFFFFFF;
    for (int32_t c_idx : planted) {
        if (world.get_crop_type()[c_idx] == 0) continue;
        int32_t tx = c_idx % map_w;
        int32_t ty = c_idx / map_w;
        int64_t wx = tx * cell_sz + cell_sz / 2;
        int64_t wy = ty * cell_sz + cell_sz / 2;
        int64_t dx = wx - pos_x;
        int64_t dy = wy - pos_y;
        int64_t dist_sq = mul_fixed(dx, dx) + mul_fixed(dy, dy);
        if (dist_sq < min_dist_sq) {
            min_dist_sq = dist_sq;
            final_target_x = wx;
            final_target_y = wy;
            target_crop_idx = c_idx;
            targeting_crop = true;
        }
    }

    // Direction vector towards final target
    int64_t dx = final_target_x - pos_x;
    int64_t dy = final_target_y - pos_y;
    
    int64_t dist_sq = mul_fixed(dx, dx) + mul_fixed(dy, dy);
    int64_t dist = sqrt_fixed(dist_sq);
    if (dist == 0) dist = 1;

    int64_t dir_x = div_fixed(dx, dist);
    int64_t dir_y = div_fixed(dy, dist);

    // Separation Force (swarm dynamics)
    int64_t sep_x = 0;
    int64_t sep_y = 0;
    int sep_count = 0;
    size_t total_entities = world.get_chaos().size();
    for (size_t i = 0; i < total_entities; ++i) {
        if (i == idx) continue;
        if (v.types[i] == EntityType::ENEMY && !(v.flags[i] & (uint8_t)EntityFlags::DEAD)) {
            int64_t odx = pos_x - v.positions_x[i];
            int64_t ody = pos_y - v.positions_y[i];
            int64_t odist_sq = mul_fixed(odx, odx) + mul_fixed(ody, ody);
            int64_t thresh_sq = 25LL * 65536 * 25LL * 65536; // 25.0 float units squared
            if (odist_sq > 0 && odist_sq < thresh_sq) {
                int64_t odist = sqrt_fixed(odist_sq);
                if (odist > 0) {
                    sep_x += div_fixed(odx, odist);
                    sep_y += div_fixed(ody, odist);
                    sep_count++;
                }
            }
        }
    }
    if (sep_count > 0) {
        dir_x += sep_x / sep_count;
        dir_y += sep_y / sep_count;
        int64_t len_sq = mul_fixed(dir_x, dir_x) + mul_fixed(dir_y, dir_y);
        int64_t len = sqrt_fixed(len_sq);
        if (len > 0) {
            dir_x = div_fixed(dir_x, len);
            dir_y = div_fixed(dir_y, len);
        }
    }

    // Apply acceleration force
    int64_t speed = 2LL * 65536; // Base speed: 2.0
    EntityStats& stats = world.get_stats_mut(v.dense_ids[idx]);
    if (stats.base_speed > 0) {
        speed = stats.get_speed();
    }
    int64_t accel = speed / 5;
    v.velocities_x[idx] += mul_fixed(dir_x, accel);
    v.velocities_y[idx] += mul_fixed(dir_y, accel);

    // Limit speed
    int64_t vel_len_sq = mul_fixed(v.velocities_x[idx], v.velocities_x[idx]) + mul_fixed(v.velocities_y[idx], v.velocities_y[idx]);
    int64_t vel_len = sqrt_fixed(vel_len_sq);
    if (vel_len > speed) {
        v.velocities_x[idx] = mul_fixed(div_fixed(v.velocities_x[idx], vel_len), speed);
        v.velocities_y[idx] = mul_fixed(div_fixed(v.velocities_y[idx], vel_len), speed);
    }

    // Melee attack behaviors
    if (targeting_crop && target_crop_idx != -1) {
        if (dist <= cell_sz) {
            // Damage the crop (approx 5% crop health per tick)
            world.damage_crop(target_crop_idx, static_cast<int32_t>(5LL * 65536));
        }
    } else {
        // Player attack if adjacent
        if (dist <= (v.radii[idx] + 32LL * 65536)) {
            for (size_t i = 0; i < total_entities; ++i) {
                if (v.types[i] == EntityType::PLAYER && !(v.flags[i] & (uint8_t)EntityFlags::DEAD)) {
                    int32_t dmg = 1;
                    if (v.healths[i] > dmg) {
                        v.healths[i] -= dmg;
                    } else {
                        v.healths[i] = 0;
                        v.flags[i] |= (uint8_t)EntityFlags::DEAD;
                    }
                    break;
                }
            }
        }
    }
}

void Enemy::handle_movement(uint32_t idx, ChaosState::RestrictedView& v) {
    v.velocities_x[idx] = mul_fixed(v.velocities_x[idx], FRICTION);
    v.velocities_y[idx] = mul_fixed(v.velocities_y[idx], FRICTION);
    v.positions_x[idx] += v.velocities_x[idx];
    v.positions_y[idx] += v.velocities_y[idx];
}
