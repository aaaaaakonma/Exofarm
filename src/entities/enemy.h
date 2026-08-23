#ifndef ENEMY_H
#define ENEMY_H

#include <cstdint>
#include "entity.h"

class SimWorld;

/**
 * Enemy logic class for the simulation.
 * Handles AI behavior, pathing (if any), and movement for ENEMY type entities.
 */
class Enemy {
public:
    static constexpr int64_t SPEED = 131072; // 2.0 units per tick
    static constexpr int64_t FRICTION = 52428; // ~0.8 friction multiplier
    static constexpr int64_t DETECTION_RANGE = 26614400; // 400 units

    // Tick the enemy AI. 
    static void tick(uint32_t idx, int64_t target_x, int64_t target_y, SimWorld &world, ChaosState::RestrictedView& v);

private:
    static void handle_ai(uint32_t idx, int64_t target_x, int64_t target_y, SimWorld &world, ChaosState::RestrictedView& v);
    static void handle_movement(uint32_t idx, ChaosState::RestrictedView& v);
};

#endif // ENEMY_H
