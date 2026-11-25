#pragma once

#include <vector>
#include <cstdint>
#include <string>
#include <cmath>
#include <algorithm>
#include "../math/fixed.h"
#include "../math/fixed_3d.h"
#include "../math/rng.h"

namespace exofarm {

enum class BotanicalSpecies : int {
    CONIFER = 0,
    BROADLEAF_OAK = 1,
    SLENDER_BIRCH = 2,
    DEAD_SNAG = 3
};

struct BotanicalParams {
    std::string type_name = "Conifer";
    int64_t max_height = 13 * ONE + 32768;      // 13.5m
    int64_t base_radius = (28 * ONE) / 100;     // 0.28m
    int64_t trunk_taper = (85 * ONE) / 100;     // 0.85
    int num_tiers = 8;
    int branches_per_tier = 5;
    int total_spiral_branches = 0;
    bool helical_branching = false;
    bool deliquescent_fork = false;
    int64_t fork_height_ratio = (58 * ONE) / 100;
    int64_t crown_start_ratio = (18 * ONE) / 100;
    int64_t branch_angle_deg = 68 * ONE;
    int64_t max_branch_len = (45 * ONE) / 10;   // 4.5m
    int64_t crown_shape_exp = (85 * ONE) / 100; // 0.85
    int64_t droop_strength = (50 * ONE) / 100;
    int64_t tip_hook_strength = (40 * ONE) / 100;
    int64_t trunk_crookedness = (8 * ONE) / 100;
    bool secondary_twigs = true;
    bool weeping_twigs = false;
    int64_t buttress_flare = (15 * ONE) / 10;
    int radial_segments_trunk = 8;
    int radial_segments_branch = 5;
    int64_t card_length = (21 * ONE) / 10;      // 2.1m
    int64_t card_width = (15 * ONE) / 10;       // 1.5m
    int64_t cladoptosis_threshold = (22 * ONE) / 100;
    int64_t phototropism_vertical = (75 * ONE) / 100;
    int64_t phototropism_lateral = (25 * ONE) / 100;
    int64_t wander_tortuosity = (8 * ONE) / 100;
    int64_t shoot_prune_threshold = (32 * ONE) / 100;
    
    // Colors for vertex tinting
    float stem_r = 0.32f, stem_g = 0.22f, stem_b = 0.14f;
    float leaf_r = 0.16f, leaf_g = 0.40f, leaf_b = 0.16f;

    static BotanicalParams get_preset(BotanicalSpecies species);
};

// 3x3 Deterministic Rotation Basis in 16.16 Fixed Point
struct BasisFixed {
    Vec3Fixed side = {ONE, 0, 0};
    Vec3Fixed up = {0, ONE, 0};
    Vec3Fixed forward = {0, 0, ONE};

    BasisFixed() = default;
    BasisFixed(const Vec3Fixed& s, const Vec3Fixed& u, const Vec3Fixed& f)
        : side(s), up(u), forward(f) {}

    static BasisFixed from_forward_up(const Vec3Fixed& fwd, const Vec3Fixed& ref_up = {0, ONE, 0}) {
        Vec3Fixed f = fwd.normalized();
        Vec3Fixed u = ref_up;
        if (abs_fixed(f.dot(u)) > 58982) { // > 0.90
            u = (abs_fixed(f.x) < 58982) ? Vec3Fixed(ONE, 0, 0) : Vec3Fixed(0, 0, ONE);
        }
        Vec3Fixed s = u.cross(f).normalized();
        u = f.cross(s).normalized();
        return BasisFixed(s, u, f);
    }

    BasisFixed rotated(const Vec3Fixed& axis, int64_t angle) const {
        QuatFixed q = QuatFixed::from_axis_angle(axis, angle);
        return BasisFixed(
            q.rotate_vector(side).normalized(),
            q.rotate_vector(up).normalized(),
            q.rotate_vector(forward).normalized()
        );
    }

    Vec3Fixed xform(const Vec3Fixed& v) const {
        return side * v.x + up * v.y + forward * v.z;
    }
};

struct BotanicalNode {
    Vec3Fixed pos;
    Vec3Fixed dir = {0, ONE, 0};
    int64_t radius = 3276; // 0.05m
    int branch_order = 0;  // 0 = trunk, 1 = bough, 2 = twig
    int tier_idx = 0;
    int64_t uv_v = 0;
    int64_t light_exposure = ONE; // [0..ONE]
    bool is_pruned = false;
    bool is_terminal = false;
    int64_t phloem_flux = 0;
};

struct BotanicalCard {
    BasisFixed basis;
    Vec3Fixed pos;
    int cluster_id = 0;
    Vec3Fixed cluster_center;
    int64_t cluster_radius = ONE;
    int64_t size_scale = ONE;
    int card_variant = 0; // 0 = apical bud, 1 = bushy mid, 2 = weeping fan
    int64_t light_exposure = ONE;
    bool is_pruned = false;
    BotanicalSpecies species_type = BotanicalSpecies::CONIFER;
};

struct BotanicalShoot {
    int id = -1;
    int branch_id = -1;
    int segment_idx = -1;
    Vec3Fixed origin;
    Vec3Fixed direction = {0, ONE, 0};
    int64_t length = (35 * ONE) / 100;
    int64_t radius = 196; // ~0.003m
    std::vector<BotanicalCard> cards;
    int64_t light_exposure = ONE;
    bool is_pruned = false;
    int cluster_id = -1;
};

struct TreeVertex {
    float x, y, z;
    float nx, ny, nz;
    float u, v;
    float r, g, b, a;
};

struct TreeMeshData {
    std::vector<TreeVertex> vertices;
    std::vector<uint32_t> indices;
};

// Data layout matching GLSL compute shader SSBO (std430, 32 bytes)
struct TreeClusterData {
    float x, y, z, radius;
    float leaf_count, tree_id, gamma, eff_r_sq;
};

struct TreeObstacle {
    Vec3Fixed center;
    int64_t radius = 26214; // ~0.40m
    int64_t height = 917504; // ~14.0m
};

struct CandidateBranchData {
    int branch_id = 0;
    int tier = 0;
    std::vector<BotanicalNode> chain;
    std::vector<BotanicalShoot> shoots;
    double branch_card_scale = 1.0;
    double sun_align = 0.5;
    double b_vigor = 1.0;
    double tier_elongation = 1.0;
    struct TwigCandidate {
        std::vector<BotanicalNode> chain;
        std::vector<BotanicalShoot> shoots;
        int junction_seg_idx = -1;
        int cluster_id = -1;
        double avg_light = 1.0;
        bool is_alive = true;
    };
    std::vector<TwigCandidate> twigs;
};

struct BotanicalCandidates {
    std::vector<BotanicalNode> trunk_chain;
    std::vector<CandidateBranchData> candidate_branches;
    std::vector<TreeClusterData> candidate_clusters;
    BotanicalSpecies species = BotanicalSpecies::CONIFER;
    BotanicalParams cfg;
    int64_t tree_height = 0;
    int64_t base_radius = 0;
    int64_t max_spread_sq = 0;
    int collisions_avoided = 0;
    Vec3Fixed ground_pos = {0, 0, 0};
    Vec3Fixed ground_normal = {0, ONE, 0};
    Vec3Fixed sun_norm = {32768, 46341, 32768};
    double g = 1.0;
    int64_t seed_val = 0;
};

struct BotanicalTreeStats {
    float height = 0.0f;
    float crown_spread = 0.0f;
    float base_radius = 0.0f;
    int total_branches = 0;
    int living_branches = 0;
    int pruned_branches = 0;
    int active_cards_count = 0;
    int pruned_cards_count = 0;
    int collisions_avoided = 0;
    int total_nodes = 0;
    int triangle_count = 0;
    int foliage_triangle_count = 0;
    float avg_light_exposure = 1.0f;
    uint64_t state_hash = 0;
};

struct BotanicalTreeResult {
    TreeMeshData bark_mesh;
    TreeMeshData foliage_mesh;
    std::vector<TreeClusterData> clusters;
    BotanicalTreeStats stats;
};

class BotanicalTreeGenerator {
public:
    // Pass 1: Architecture generation and candidate cluster placement
    static BotanicalCandidates generate_candidates(
        int64_t seed_val,
        BotanicalSpecies species,
        int64_t growth_fixed, // [0..ONE]
        const Vec3Fixed& ground_pos = {0, 0, 0},
        const Vec3Fixed& ground_normal = {0, ONE, 0},
        const Vec3Fixed& sun_dir = {32768, 46341, 32768},
        const std::vector<TreeObstacle>& obstacles = {},
        int64_t cladoptosis_override = -1,
        int64_t lateral_bias_override = -1,
        int64_t vertical_bias_override = -1
    );

    // Pass 2/3: Light Exposure, Biological Cladoptosis, and Final Mesh Assembly
    static BotanicalTreeResult assemble_tree(
        BotanicalCandidates& cands,
        const std::vector<float>& cluster_exposures,
        bool enable_self_pruning = true,
        int64_t cladoptosis_override = -1
    );

    // High-level single-tree generation (runs candidates -> shadow eval -> assemble)
    static BotanicalTreeResult generate(
        int64_t seed_val,
        BotanicalSpecies species,
        int64_t growth_fixed, // [0..ONE]
        const Vec3Fixed& ground_pos = {0, 0, 0},
        const Vec3Fixed& ground_normal = {0, ONE, 0},
        const Vec3Fixed& sun_dir = {32768, 46341, 32768},
        bool enable_self_pruning = true,
        int64_t cladoptosis_override = -1,
        int64_t lateral_bias_override = -1,
        int64_t vertical_bias_override = -1
    );

    // Indexed RMF bark extrusion with strictly outward Godot winding
    static TreeMeshData build_bark_mesh(
        const std::vector<BotanicalNode>& trunk,
        const std::vector<std::vector<BotanicalNode>>& branches,
        int radial_trunk,
        int radial_branch
    );

    // SpeedTree-like crossed-quad foliage card mesh generator with two-scale volumetric normals
    static TreeMeshData build_foliage_mesh(
        const std::vector<BotanicalCard>& cards,
        BotanicalSpecies species,
        int64_t tree_height,
        const Vec3Fixed& ground_pos,
        int64_t base_card_len,
        int64_t base_card_wid,
        const Vec3Fixed& ground_normal
    );

    // Fast CPU-based hemispherical cluster lighting evaluation (matches GLSL compute shader)
    static std::vector<float> evaluate_canopy_shadows_cpu(
        const std::vector<TreeClusterData>& clusters,
        const Vec3Fixed& sun_dir
    );

private:
    static void extrude_indexed_chain(
        TreeMeshData& mesh,
        const std::vector<BotanicalNode>& chain,
        int sides
    );

    static int64_t dist_segment_to_segment(
        const Vec3Fixed& p1, const Vec3Fixed& q1,
        const Vec3Fixed& p2, const Vec3Fixed& q2
    );

    static int64_t get_ground_y(
        const Vec3Fixed& pos,
        const Vec3Fixed& g_pos,
        const Vec3Fixed& g_norm
    );
};

} // namespace exofarm
