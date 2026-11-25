#ifndef PLAYER_H
#define PLAYER_H

#include <cstdint>
#include "entity.h"
#include "input.h"

class SimWorld;

/**
 * Player logic class for the simulation.
 * This class contains static methods to transform an Entity (of type PLAYER)
 * based on deterministic InputFrames.
 */
class Player {
public:
    // Constants for player movement (in 16.16 fixed point)
    static constexpr int64_t SPEED = 65536; // 5.0 units per tick
    static constexpr int64_t FRICTION = 58982; // ~0.9 friction multiplier
    static constexpr int64_t BRAKE_FRICTION = 45875; // ~0.70 friction
    static constexpr int64_t MAX_CLIMB_SLOPE = 8LL * ONE; // 8.0 meters max climbable step per cell
    static constexpr int64_t DEFAULT_REACH = 2LL * 64 * ONE;

    static void tick(EntityID id, const InputFrame &input, SimWorld &world);

    // Obstacle and slope collision query
    static bool is_passable(int64_t test_x, int64_t test_y, int64_t cur_elev, const SimWorld &world);

    // Terrain manipulation query and trigger
    static bool try_modify_terrain(int32_t x, int32_t y, int32_t delta_elev, SimWorld &world);

    // Helper to compute target position from player pos, aim coords and reach
    static void compute_target_pos(int64_t px, int64_t py, 
                                   int64_t aim_x, int64_t aim_y,
                                   int32_t move_x, int32_t move_y,
                                   int64_t cell_sz, int64_t max_reach,
                                   int64_t &out_target_x, int64_t &out_target_y);

private:
    static void handle_movement(EntityID id, const InputFrame &input, SimWorld &world);
    static void handle_actions(EntityID id, const InputFrame &input, SimWorld &world);
};

#endif // PLAYER_H
