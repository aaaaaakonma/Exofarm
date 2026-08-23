#pragma once
#include "shapes.h"
#include "rng.h"
#include "hydrology_graph.h"
#include "macro_world.h"
#include "micro_chunk_generator.h"
#include <vector>
#include <cstdint>
#include <memory>

// Forward declaration
class SimWorld;

namespace Generation {

struct StarterSiteInfo {
    int32_t spawn_x = 0;
    int32_t spawn_y = 0;
    int32_t pond_x = 0;
    int32_t pond_y = 0;
    int32_t patch_n_x = 0;
    int32_t patch_n_y = 0;
    int32_t patch_p_x = 0;
    int32_t patch_p_y = 0;
    int32_t patch_k_x = 0;
    int32_t patch_k_y = 0;
    uint32_t pond_reservoir_id = 0;
};

class MapGenerator {
public:
    MapGenerator() = default;

    // Holistic map generation entry point
    bool generate_map(SimWorld& world, uint64_t seed, int64_t width, int64_t height, int64_t cell_size_fixed);

    const StarterSiteInfo& get_starter_site() const { return _starter_site; }
    const MacroWorld& get_macro_world() const { return _micro_generator.get_macro_world(); }
    const MicroChunkGenerator& get_micro_generator() const { return _micro_generator; }

private:
    StarterSiteInfo     _starter_site;
    MicroChunkGenerator _micro_generator;
};

} // namespace Generation
