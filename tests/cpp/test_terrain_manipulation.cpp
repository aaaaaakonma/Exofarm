#include <iostream>
#include <vector>
#include <cassert>
#include <iomanip>
#include <string>

#include "src/core/world.h"
#include "src/math/rng.h"
#include "src/core/input.h"

// Implementations for standalone headless test
#include "src/core/world.cpp"
#include "src/core/item.cpp"
#include "src/entities/player.cpp"

void test_empty_terrain_modification() {
    std::cout << "[Test 1] Testing empty terrain modification..." << std::endl;
    SimWorld world;
    const int32_t width = 32;
    const int32_t height = 32;
    const int64_t cell_size = 2 * 65536; // 2.0 fixed
    world.set_map_dimensions(width, height, cell_size);

    std::vector<int32_t> initial_elevation(width * height, 100 * 65536);
    world.set_elevation(initial_elevation);

    // Verify out of bounds
    assert(!world.can_modify_terrain(-1, 0));
    assert(!world.can_modify_terrain(0, -1));
    assert(!world.can_modify_terrain(width, 0));
    assert(!world.can_modify_terrain(0, height));
    assert(!world.modify_terrain(-1, 0, 10 * 65536));
    assert(!world.modify_terrain(width, 10, 10 * 65536));

    // Verify modifying empty terrain inside bounds
    int32_t tx = 5, ty = 5;
    size_t idx = (size_t)ty * width + tx;
    int32_t orig_elev = world.get_elevation()[idx];
    assert(orig_elev == 100 * 65536);

    assert(world.can_modify_terrain(tx, ty));
    int32_t delta1 = 15 * 65536;
    bool ok1 = world.modify_terrain(tx, ty, delta1);
    assert(ok1);
    assert(world.get_elevation()[idx] == orig_elev + delta1);

    // Lower elevation (negative delta)
    int32_t delta2 = -25 * 65536;
    bool ok2 = world.modify_terrain(tx, ty, delta2);
    assert(ok2);
    assert(world.get_elevation()[idx] == orig_elev + delta1 + delta2);

    std::cout << "  [PASS] Empty terrain modification alters elevation correctly." << std::endl;
}

void test_structure_obstacle_validation() {
    std::cout << "[Test 2] Testing structure obstacle validation..." << std::endl;
    SimWorld world;
    const int32_t width = 32;
    const int32_t height = 32;
    const int64_t cell_size = 2 * 65536;
    world.set_map_dimensions(width, height, cell_size);

    std::vector<int32_t> initial_elevation(width * height, 50 * 65536);
    world.set_elevation(initial_elevation);

    int32_t sx = 10, sy = 12;
    size_t idx = (size_t)sy * width + sx;
    int32_t initial_val = world.get_elevation()[idx];

    // Initially modifiable
    assert(world.can_modify_terrain(sx, sy));

    // 1. Place a structure (StructureType::WELL = 1)
    world.place_structure(sx, sy, 1);
    assert(world.get_structure_type()[idx] == 1);

    // Verify modification is blocked
    assert(!world.can_modify_terrain(sx, sy));
    bool mod_blocked = world.modify_terrain(sx, sy, 20 * 65536);
    assert(!mod_blocked);
    assert(world.get_elevation()[idx] == initial_val); // Elevation unchanged

    // 2. Remove structure
    world.remove_structure(sx, sy);
    assert(world.get_structure_type()[idx] == 0);

    // Verify modification is allowed again
    assert(world.can_modify_terrain(sx, sy));
    int32_t new_delta = 30 * 65536;
    bool mod_ok = world.modify_terrain(sx, sy, new_delta);
    assert(mod_ok);
    assert(world.get_elevation()[idx] == initial_val + new_delta);

    std::cout << "  [PASS] Structure prevents modification when placed and allows modification when removed." << std::endl;
}

void test_tree_obstacle_validation() {
    std::cout << "[Test 3] Testing tree obstacle validation..." << std::endl;
    SimWorld world;
    const int32_t width = 32;
    const int32_t height = 32;
    const int64_t cell_size = 2 * 65536;
    world.set_map_dimensions(width, height, cell_size);

    std::vector<int32_t> initial_elevation(width * height, 75 * 65536);
    world.set_elevation(initial_elevation);

    int32_t tx = 8, ty = 8;
    size_t idx = (size_t)ty * width + tx;
    int32_t initial_val = world.get_elevation()[idx];

    // Ensure cell is currently clear
    assert(world.can_modify_terrain(tx, ty));

    // Place an active tree at cell (tx, ty)
    BisectSnapshot snap = world.snapshot();
    TerrainTree tree;
    tree.id = 1;
    tree.chunk_x = tx / SimWorld::CHUNK_SIZE;
    tree.chunk_y = ty / SimWorld::CHUNK_SIZE;
    // Tree world coordinates within cell (tx, ty)
    tree.x = tx * cell_size + (cell_size / 2);
    tree.z = ty * cell_size + (cell_size / 2);
    tree.elevation = initial_val;
    tree.flags = 1; // Active / Alive (flags & 1)
    snap.terrain_trees.push_back(tree);
    world.restore(snap);

    // Verify terrain modification is blocked on tree tile
    assert(!world.can_modify_terrain(tx, ty));
    bool mod_res = world.modify_terrain(tx, ty, 40 * 65536);
    assert(!mod_res);
    assert(world.get_elevation()[idx] == initial_val); // Elevation remains unchanged

    // Test with inactive / dead tree (flags = 0)
    snap = world.snapshot();
    snap.terrain_trees[0].flags = 0; // Cleared / felled tree
    world.restore(snap);

    assert(world.can_modify_terrain(tx, ty));
    bool mod_dead_ok = world.modify_terrain(tx, ty, 40 * 65536);
    assert(mod_dead_ok);
    assert(world.get_elevation()[idx] == initial_val + 40 * 65536);

    std::cout << "  [PASS] Active tree prevents terrain modification; cleared tree allows modification." << std::endl;
}

void test_snapshot_rollback_determinism() {
    std::cout << "[Test 4] Testing snapshot, rollback, and lockstep determinism with terrain modification..." << std::endl;
    SimWorld world1;
    const int32_t width = 32;
    const int32_t height = 32;
    const int64_t cell_size = 2 * 65536;
    world1.set_seed(42);
    world1.set_map_dimensions(width, height, cell_size);

    std::vector<int32_t> elev(width * height, 20 * 65536);
    world1.set_elevation(elev);

    // Spawn player entity
    EntityID player = world1.spawn_entity(EntityType::PLAYER, 16 * cell_size, 16 * cell_size, 32768, 100);
    assert(player != INVALID_ENTITY);

    // Modify terrain at several coordinates
    assert(world1.modify_terrain(2, 2, 10 * 65536));
    assert(world1.modify_terrain(3, 4, -5 * 65536));
    assert(world1.modify_terrain(10, 10, 50 * 65536));

    // Capture snapshot after modification
    BisectSnapshot snap_post_mod = world1.snapshot();
    assert(snap_post_mod.elevation[2 * width + 2] == 30 * 65536);
    assert(snap_post_mod.elevation[4 * width + 3] == 15 * 65536);
    assert(snap_post_mod.elevation[10 * width + 10] == 70 * 65536);

    // Run world1 forward for 100 ticks with inputs
    std::vector<InputFrame> inputs(100);
    for (size_t t = 0; t < 100; ++t) {
        inputs[t].kind = InputFrame::Kind::PlayerInput;
        inputs[t].tick = static_cast<uint32_t>(t);
        inputs[t].payload.player.move_x = (t % 2 == 0) ? 65536 : -65536;
        inputs[t].payload.player.move_y = (t % 3 == 0) ? 65536 : 0;
        inputs[t].payload.player.aim_x = 16 * cell_size;
        inputs[t].payload.player.aim_y = 16 * cell_size;
        inputs[t].payload.player.actions = 0;

        // Also test InputFrame::Kind::ModifyTerrain during simulation at tick 30 and 60
        if (t == 30) {
            inputs[t].kind = InputFrame::Kind::ModifyTerrain;
            inputs[t].payload.terrain.x = 15;
            inputs[t].payload.terrain.y = 15;
            inputs[t].payload.terrain.delta_elev = 22 * 65536;
        } else if (t == 60) {
            inputs[t].kind = InputFrame::Kind::ModifyTerrain;
            inputs[t].payload.terrain.x = 20;
            inputs[t].payload.terrain.y = 22;
            inputs[t].payload.terrain.delta_elev = -8 * 65536;
        }

        world1.tick(inputs[t]);
    }

    uint64_t hash_after_100 = world1.get_state_hash();
    std::vector<int32_t> final_elev_world1 = world1.get_elevation();
    assert(final_elev_world1[15 * width + 15] == 42 * 65536); // 20 + 22
    assert(final_elev_world1[22 * width + 20] == 12 * 65536); // 20 - 8

    // Rollback world1 to snap_post_mod
    world1.restore(snap_post_mod);
    assert(world1.get_elevation()[2 * width + 2] == 30 * 65536);
    assert(world1.get_elevation()[4 * width + 3] == 15 * 65536);
    assert(world1.get_elevation()[10 * width + 10] == 70 * 65536);
    assert(world1.get_elevation()[15 * width + 15] == 20 * 65536); // Reverted before tick 30 mod

    // Replay exact 100 ticks from restored state
    for (size_t t = 0; t < 100; ++t) {
        world1.tick(inputs[t]);
    }

    uint64_t hash_replay = world1.get_state_hash();
    assert(hash_replay == hash_after_100);
    assert(world1.get_elevation() == final_elev_world1);

    // Create independent world2 restored from snap_post_mod and run
    SimWorld world2;
    world2.restore(snap_post_mod);
    for (size_t t = 0; t < 100; ++t) {
        world2.tick(inputs[t]);
    }
    assert(world2.get_state_hash() == hash_after_100);
    assert(world2.get_elevation() == final_elev_world1);

    std::cout << "  [PASS] Snapshot rollback and lockstep simulation are 100% deterministic." << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << " ExoFarm Terrain Manipulation Test Suite " << std::endl;
    std::cout << "========================================" << std::endl;

    test_empty_terrain_modification();
    test_structure_obstacle_validation();
    test_tree_obstacle_validation();
    test_snapshot_rollback_determinism();

    std::cout << "========================================" << std::endl;
    std::cout << " ALL TERRAIN MANIPULATION TESTS PASSED! " << std::endl;
    std::cout << "========================================" << std::endl;
    return 0;
}
