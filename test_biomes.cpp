#include <iostream>
#include <vector>
#include <map>
#include <string>

#include "src/core/world.h"
#include "src/math/rng.h"

// Included exactly as in test_determinism.cpp
#include "src/core/world.cpp"
#include "src/core/item.cpp"
#include "src/core/collision.cpp"
#include "src/math/cordic.cpp"
#include "src/core/map_generator.cpp"
#include "src/entities/player.cpp"
#include "src/entities/enemy.cpp"
#include "src/entities/wall.cpp"

int main() {
    uint64_t seed = 42;
    SimWorld world_gen;
    world_gen.set_seed(seed);

    int64_t cell_size = 64 * 65536; // 64.0 float units
    int w = 512;
    int h = 512;
    world_gen.generate_map_cellular_automata(seed, w, h, cell_size, 5, 45);

    auto snap = world_gen.snapshot();
    const auto& biome = snap.biome;

    std::map<int, int> counts;
    for (int b : biome) {
        counts[b]++;
    }

    std::map<int, std::string> names = {
        {0, "OCEAN"},
        {1, "TUNDRA / GLACIER"},
        {2, "DESERT"},
        {3, "GRASSLAND"},
        {4, "FOREST"},
        {5, "TROPICAL RAINFOREST"},
        {6, "SWAMP / WETLANDS"},
        {7, "MOUNTAIN"}
    };

    int total_land = 0;
    for (auto const& [k, v] : counts) {
        if (k != 0) total_land += v;
    }

    std::cout << "Biome Distribution:" << std::endl;
    for (auto const& [k, v] : counts) {
        double pct_total = (double)v / (w * h) * 100.0;
        double pct_land = (k != 0) ? (double)v / total_land * 100.0 : 0.0;
        std::cout << names[k] << ": " << v << " (Total: " << pct_total << "%, Land: " << pct_land << "%)" << std::endl;
    }

    return 0;
}
