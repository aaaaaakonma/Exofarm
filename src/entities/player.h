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

    static void tick(EntityID id, const InputFrame &input, SimWorld &world);

private:
    static void handle_movement(EntityID id, const InputFrame &input, SimWorld &world);
    static void handle_actions(EntityID id, const InputFrame &input, SimWorld &world);
};

#endif // PLAYER_H
