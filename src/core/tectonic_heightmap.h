#pragma once

#include "../math/fixed.h"
#include "../math/noise.h"
#include "../math/rng.h"
#include "../math/fixed_3d.h"
#include <vector>
#include <cstdint>
#include <cmath>
#include <cstdlib>   // std::abs (integer overloads)
#include <algorithm>
#include <climits>   // INT32_MIN / INT32_MAX for clamp guards
#include <cassert>

namespace Tectonics {

// ===== 2D Fixed-Point Vector =====
struct Vec2Fixed {
    int64_t x = 0;
    int64_t y = 0;

    constexpr Vec2Fixed() = default;
    constexpr Vec2Fixed(int64_t _x, int64_t _y) : x(_x), y(_y) {}

    FORCE_INLINE Vec2Fixed operator+(const Vec2Fixed& o) const { return {x + o.x, y + o.y}; }
    FORCE_INLINE Vec2Fixed operator-(const Vec2Fixed& o) const { return {x - o.x, y - o.y}; }
    FORCE_INLINE Vec2Fixed operator*(int64_t scalar) const { return {mul_fixed(x, scalar), mul_fixed(y, scalar)}; }
    FORCE_INLINE Vec2Fixed operator/(int64_t scalar) const { return {div_fixed(x, scalar), div_fixed(y, scalar)}; }

    FORCE_INLINE int64_t dot(const Vec2Fixed& o) const {
        return mul_fixed(x, o.x) + mul_fixed(y, o.y);
    }

    FORCE_INLINE int64_t cross(const Vec2Fixed& o) const {
        return mul_fixed(x, o.y) - mul_fixed(y, o.x);
    }

    FORCE_INLINE int64_t length_sq() const {
        return dot(*this);
    }

    FORCE_INLINE int64_t length() const {
        return sqrt_fixed(length_sq());
    }

    FORCE_INLINE Vec2Fixed normalized() const {
        int64_t len_sq = length_sq();
        if (len_sq <= 16) return {0, 0};
        int64_t inv_len = inv_sqrt_fixed(len_sq);
        return (*this) * inv_len;
    }

    FORCE_INLINE Vec2Fixed perpendicular() const {
        return {-y, x};
    }
};

// ===== Plate Classification & Tectonic Boundaries =====

enum class PlateType : uint8_t {
    OCEANIC = 0,
    CONTINENTAL = 1,
    ISLAND_ARC = 2
};

enum class BoundaryType : uint8_t {
    PASSIVE_INTERIOR = 0,
    CONVERGENT_CONTINENTAL_COLLISION = 1, // High fold mountain ranges (e.g. Himalayas)
    CONVERGENT_SUBDUCTION = 2,           // Trench + Volcanic Cordillera (e.g. Andes)
    CONVERGENT_ISLAND_ARC = 3,            // Ocean-Ocean subduction arc (e.g. Mariana/Aleutian)
    DIVERGENT_CONTINENTAL_RIFT = 4,       // Rift valley + uplifted shoulders (e.g. East Africa)
    DIVERGENT_OCEANIC_SPREADING = 5,      // Mid-ocean ridge + central rift (e.g. Mid-Atlantic)
    TRANSFORM_SHEAR = 6                   // Strike-slip fault / fracture zone (e.g. San Andreas)
};

// ===== Analytical Flexural Slab Profile (Turcotte & Schubert Thin Elastic Plate) =====

// Exact analytical exponential approximation for negative input: e^-u
static FORCE_INLINE int64_t exp_neg_fixed(int64_t u) {
    if (u <= 0) return ONE;
    if (u >= (8 * ONE)) return 0; // e^-8 < 0.00034 < 1/65536

    int64_t t = mul_fixed(u, 94548); // u * log2(e) in 16.16
    int64_t k = t >> 16;
    int64_t f = t & 0xFFFF;

    if (k >= 16) return 0;

    int64_t f2 = mul_fixed(f, f);
    int64_t f3 = mul_fixed(f2, f);
    int64_t poly = ONE - mul_fixed(45426, f) + mul_fixed(15743, f2) - mul_fixed(3638, f3);

    return poly >> k;
}

// Analytical flexural slab deflection: w(x) = w_0 * e^(-x / alpha) * cos(x / alpha)
static FORCE_INLINE int64_t flexural_slab_profile(int64_t x, int64_t alpha, int64_t w0) {
    if (x < 0) x = 0;
    if (alpha <= 0) alpha = ONE;

    int64_t u = div_fixed(x, alpha);
    int64_t exp_val = exp_neg_fixed(u);
    if (exp_val == 0) return 0;

    int64_t cos_val = cos_fixed(u);
    return mul_fixed(w0, mul_fixed(exp_val, cos_val));
}

// ===== Macro Plate Boundary Edge (Physical Fault Trace) =====
struct MacroPlateEdge {
    int32_t id = 0;
    int32_t plate_a = -1;       // Primary plate index in coarse_nodes
    int32_t plate_b = -1;       // Neighbor plate index in coarse_nodes
    uint32_t plate_id_a = 0;    // Unique Plate ID of A
    uint32_t plate_id_b = 0;    // Unique Plate ID of B
    PlateType plate_type_a = PlateType::CONTINENTAL;
    PlateType plate_type_b = PlateType::CONTINENTAL;

    Vec2Fixed v0;               // Start vertex of edge segment (16.16)
    Vec2Fixed v1;               // End vertex of edge segment (16.16)
    int64_t length = 0;         // Segment length in 16.16
    Vec2Fixed normal;           // Unit normal pointing from plate A to plate B (16.16)
    Vec2Fixed tangent;          // Unit tangent from v0 to v1 (16.16)
    int64_t strike_phase = 0;   // Phase offset along strike for mountain massifs

    BoundaryType boundary_type = BoundaryType::PASSIVE_INTERIOR;
    int64_t convergence_rate = 0; // C > 0: Convergent, C < 0: Divergent (16.16)
    int64_t shear_rate = 0;       // Strike-slip shear rate (16.16)
    int64_t impact_force = 0;     // Collisional impact force (16.16)
    int64_t alpha = 24 * ONE;     // Characteristic elastic flexural parameter (16.16)
    int64_t peak_uplift = 0;      // Base peak uplift / orogenic relief (16.16)
    bool a_overrides = false;     // True if plate A is the overriding crustal slab
};

// Distance from point P to line segment AB, returning squared distance and projection t in [0, ONE]
static FORCE_INLINE int64_t dist_to_segment_fixed(
    const Vec2Fixed& p, const Vec2Fixed& a, const Vec2Fixed& b, int64_t& out_t
) {
    Vec2Fixed ab = b - a;
    int64_t ab_len_sq = ab.length_sq();
    if (ab_len_sq <= 16) {
        out_t = 0;
        return (p - a).length_sq();
    }
    Vec2Fixed ap = p - a;
    int64_t t = div_fixed(ap.dot(ab), ab_len_sq);
    out_t = std::clamp<int64_t>(t, 0, ONE);
    Vec2Fixed proj = a + ab * out_t;
    return (p - proj).length_sq();
}

// ===== Tectonic Control Node (Plate Center) =====
struct TectonicNode {
    uint32_t id = 0;
    uint32_t plate_id = 0;           // Macro tectonic plate ID (defaults to id if 0)
    Vec2Fixed position;              // World coordinate position (16.16)
    Vec2Fixed velocity;              // Drift velocity vector (16.16)
    int64_t angular_velocity = 0;    // Rotational velocity (radians/step in 16.16)
    Vec2Fixed tilt_vector = {0, 0};  // 3D planar tilt gradient (slope dZ/dX, dZ/dY in 16.16)
    PlateType plate_type = PlateType::CONTINENTAL;

    int64_t base_elevation  = 30 * ONE;  // Baseline elevation (16.16)
    int64_t crust_thickness = 35 * ONE;  // Crust thickness in km/units (16.16)
    int64_t crust_rigidity  = 5 * ONE;   // Flexural rigidity (affects fold belt width & wavelength)
    int64_t density         = 2700 * ONE;// Crust density (e.g., 2700 for cont, 3000 for oceanic)
    int64_t roughness       = ONE;       // Meso-scale roughness multiplier

    FORCE_INLINE uint32_t get_plate_id() const {
        return plate_id != 0 ? plate_id : id;
    }

    FORCE_INLINE Vec2Fixed velocity_at(const Vec2Fixed& /*p*/) const {
        return velocity;
    }

    FORCE_INLINE int64_t tilt_elevation_at(const Vec2Fixed& /*p*/) const {
        return 0;
    }
};

// ===== Continuous Query Sample =====
struct TectonicSample {
    Vec2Fixed sample_pos;               // Query coordinate
    Vec2Fixed warped_pos;               // Domain-warped position used for organic Voronoi boundaries

    uint32_t primary_plate_id = 0;      // Closest plate node
    uint32_t secondary_plate_id = 0;    // Adjacent neighboring plate node
    PlateType primary_type = PlateType::CONTINENTAL;
    PlateType secondary_type = PlateType::CONTINENTAL;

    int64_t dist_to_primary = 0;        // Distance to closest node
    int64_t dist_to_boundary = 0;       // Perpendicular distance to Voronoi bisector hyperplane
    int64_t strike_coord = 0;           // Longitudinal coordinate along tectonic boundary strike (16.16)

    int64_t convergence_rate = 0;       // C > 0: Convergent, C < 0: Divergent
    int64_t shear_rate = 0;             // S: Lateral strike-slip rate
    BoundaryType boundary_type = BoundaryType::PASSIVE_INTERIOR;

    int64_t elevation_base = 0;         // Baseline crust elevation (16.16)
    int64_t elevation_tectonic = 0;     // Uplift / subsidence from plate boundary stress (16.16)
    int64_t elevation_noise = 0;        // Multi-frequency detail / ridge noise (16.16)
    int64_t elevation_final = 0;        // Composite final height (16.16)

    // Convenient float conversion helpers
    float final_elevation_float() const { return static_cast<float>(elevation_final) / 65536.0f; }
    float base_elevation_float() const { return static_cast<float>(elevation_base) / 65536.0f; }
    float tectonic_elevation_float() const { return static_cast<float>(elevation_tectonic) / 65536.0f; }
};

// ===== Continuous Tectonic Heightmap Generator =====
class ContinuousTectonicHeightmap {
public:
    struct Config {
        uint64_t seed = 1337;

        // World Bounds / Control Node Spacing
        int64_t world_width  = 512 * ONE;
        int64_t world_height = 512 * ONE;
        int32_t num_plates   = 16;
        int32_t continent_chance_pct = 70; // 70% continental, 30% oceanic for interior plates
        int32_t island_arc_chance_pct = 25; // % of oceanic plates that become ISLAND_ARC instead

        // Domain Warping (organic boundaries vs straight Voronoi lines)
        // Kept at ~32 to ensure continuous boundaries without caustic coordinate folding or cliff steepening
        int64_t warp_scale     = 32 * ONE;
        int64_t warp_frequency = 800;        // Broader, smoother warping

        // Convergent Mountain / Fold Belt Parameters
        // These are used directly in calculate_tectonic_deformation().
        int64_t collision_uplift_scale  = 54 * ONE; // Peak orogenic uplift per c_factor unit (dramatic high relief)
        int64_t subduction_arc_scale    = 32 * ONE; // Volcanic arc peak per c_factor unit
        int64_t subduction_trench_scale = 24 * ONE; // Oceanic trench depth per c_factor unit
        int64_t fold_belt_base_width    = 44 * ONE; // Base deformation width

        // Divergent Rifting Parameters
        int64_t rift_graben_scale   = 22 * ONE; // Central graben depression depth
        int64_t rift_shoulder_scale = 12 * ONE; // Flanking rift shoulder height
        int64_t mid_ocean_ridge_scale = 6 * ONE; // Bathymetric ocean ridge height
        int64_t mid_ocean_axial_scale = 8 * ONE; // Central axial rift depression

        // Transform / Shear Fault Parameters
        int64_t shear_scarp_continental = 8 * ONE; // Scarp height on continental transform
        int64_t shear_scarp_oceanic     = 3 * ONE; // Scarp height on oceanic transform

        // Baseline Elevations
        int64_t continental_base_elevation = 8 * ONE;  // Gentle continental lowlands above sea level (8m)
        int64_t oceanic_base_elevation     = -36 * ONE; // Deep abyssal ocean floor
        int64_t island_arc_base_elevation  = -10 * ONE; // Raised: allows volcanic islands to emerge above sea

        // Detail Noise (removed for smooth, clean geodynamic terrain)
        int noise_octaves           = 4;
        int64_t noise_persistence   = 32768;     // 0.5
        int64_t noise_lacunarity    = 131072;    // 2.0
        int64_t detail_noise_scale  = 0;         // 0: noise removed, clean terrain geometry

        // Continental Shelf & Coastal Margin (shallow ocean and beach near continents)
        int64_t shelf_width         = 36 * ONE;  // How far from plate interior boundary the shelf tapers
        int64_t shelf_depth         = -8 * ONE;  // Target shelf elevation (shallow sea ~-8m)
        // coastal_boundary_elevation: Crest height of gentle beach sand above sea level (16.16).
        int64_t coastal_boundary_elevation = (5 * ONE) / 2; // +2.5m gentle beach berm crest

        // Best-candidate sampling: separate k values for coarse and fine passes.
        // Higher k = better spatial coverage, slower generation.
        int32_t best_candidate_k_coarse = 10; // k tried per coarse plate (fewer nodes → moderate k sufficient)
        int32_t best_candidate_k_fine   = 20; // k tried per fine node (more nodes → higher k needed for coverage)

        // Two-pass tessellation
        // num_fine_plates: number of fine retessellation nodes (0 = disabled: direct plate modeling)
        int32_t num_fine_plates        = 0;
        // border_oceanic_margin: nodes that lie within this distance of a canvas
        // edge are classified as OCEANIC so the map has an ocean rim.
        // (0 = auto: world_width / 10, in 16.16 fixed-point units)
        int64_t border_oceanic_margin  = 0;

        // Baseline elevation blend range: how far from a plate boundary the elevation smoothly
        // interpolates from the midpoint value to the interior value. Expressed as a fraction of
        // (world_width / num_fine_plates), so it automatically scales with node density.
        // Default 1.5 means blend_range = 1.5 × average fine-cell radius.
        // (0 = auto using default multiplier)
        int64_t blend_range_scale = 0; // 16.16 multiplier; 0 = auto (default 98304 ≈ 1.5)
    };

private:
    Config _config;
    std::vector<TectonicNode> _coarse_nodes;
    std::vector<MacroPlateEdge> _macro_edges;
    std::vector<TectonicNode> _nodes;
    uint64_t _seed = 0;

    // ---------------------------------------------------------------
    // Spatial Grid for O(1) nearest-neighbor lookups during sample().
    //
    // At init time, all _nodes are bucketed into a fixed grid of cells.
    // At query time, only the 3×3 neighbourhood of cells around the
    // query position is scanned, reducing the search from O(N) to O(k)
    // where k is the average number of nodes per grid cell (~1-2).
    // ---------------------------------------------------------------
    struct SpatialGrid {
        int32_t cols = 0;
        int32_t rows = 0;
        int64_t cell_w = ONE; // cell width  (16.16)
        int64_t cell_h = ONE; // cell height (16.16)

        // Flattened grid: each entry is a list of node indices in that cell
        std::vector<std::vector<int32_t>> cells;

        void build(const std::vector<TectonicNode>& nodes,
                   int64_t world_w, int64_t world_h,
                   int32_t target_cols, int32_t target_rows) {
            cols = std::max(1, target_cols);
            rows = std::max(1, target_rows);
            cell_w = world_w / cols;
            cell_h = world_h / rows;
            if (cell_w <= 0) cell_w = ONE;
            if (cell_h <= 0) cell_h = ONE;

            cells.assign(static_cast<size_t>(cols) * rows, {});

            for (int32_t i = 0; i < (int32_t)nodes.size(); ++i) {
                // Clamp node position into grid — boundary guard nodes sit outside
                // the world bounds, so we clamp them to the edge cells.
                int32_t cx = static_cast<int32_t>(std::clamp(
                    nodes[i].position.x / cell_w, 0LL, (int64_t)(cols - 1)));
                int32_t cy = static_cast<int32_t>(std::clamp(
                    nodes[i].position.y / cell_h, 0LL, (int64_t)(rows - 1)));
                cells[static_cast<size_t>(cy) * cols + cx].push_back(i);
            }
        }

        // Find the three closest node indices to query position (wx, wy).
        // Expands the search radius until at least 3 nodes are found (or all available nodes).
        void query_three_nearest(
            const std::vector<TectonicNode>& nodes,
            int64_t wx, int64_t wy,
            int& out_a, int& out_b, int& out_c,
            int64_t& out_d2a, int64_t& out_d2b, int64_t& out_d2c
        ) const {
            out_a  = -1; out_b  = -1; out_c  = -1;
            out_d2a = 0x7FFFFFFFFFFFFFFF;
            out_d2b = 0x7FFFFFFFFFFFFFFF;
            out_d2c = 0x7FFFFFFFFFFFFFFF;

            if (nodes.empty()) return;

            // Centre cell of the query (clamped into grid)
            int32_t qcx = static_cast<int32_t>(std::clamp(wx / cell_w, 0LL, (int64_t)(cols - 1)));
            int32_t qcy = static_cast<int32_t>(std::clamp(wy / cell_h, 0LL, (int64_t)(rows - 1)));

            for (int32_t r = 0; r <= std::max(cols, rows); ++r) {
                int64_t ring_min_d2 = 0;
                if (r > 1) {
                    int64_t ring_dist = static_cast<int64_t>(r - 1) * std::min(cell_w, cell_h);
                    ring_min_d2 = mul_fixed(ring_dist, ring_dist);
                }
                // If we already have 3 candidates and the nearest possible node in the
                // next ring is farther than our third-best, we can stop.
                if (out_c >= 0 && ring_min_d2 > out_d2c) break;

                int32_t x0 = std::max(0, qcx - r);
                int32_t x1 = std::min(cols - 1, qcx + r);
                int32_t y0 = std::max(0, qcy - r);
                int32_t y1 = std::min(rows - 1, qcy + r);

                for (int32_t cy = y0; cy <= y1; ++cy) {
                    for (int32_t cx = x0; cx <= x1; ++cx) {
                        // Only visit the border ring (avoid re-scanning inner cells)
                        if (r > 0 && cx > x0 && cx < x1 && cy > y0 && cy < y1) continue;

                        for (int32_t ni : cells[static_cast<size_t>(cy) * cols + cx]) {
                            if (ni == out_a || ni == out_b || ni == out_c) continue;
                            Vec2Fixed diff = {wx - nodes[ni].position.x,
                                              wy - nodes[ni].position.y};
                            int64_t d2 = diff.length_sq();
                            if (d2 < out_d2a) {
                                out_d2c = out_d2b; out_c = out_b;
                                out_d2b = out_d2a; out_b = out_a;
                                out_d2a = d2;      out_a = ni;
                            } else if (d2 < out_d2b) {
                                out_d2c = out_d2b; out_c = out_b;
                                out_d2b = d2;      out_b = ni;
                            } else if (d2 < out_d2c) {
                                out_d2c = d2;      out_c = ni;
                            }
                        }
                    }
                }
            }
        }

        // Find the two closest node indices to query position (wx, wy).
        void query_two_nearest(
            const std::vector<TectonicNode>& nodes,
            int64_t wx, int64_t wy,
            int& out_a, int& out_b,
            int64_t& out_d2a, int64_t& out_d2b
        ) const {
            int out_c = -1;
            int64_t out_d2c = 0;
            query_three_nearest(nodes, wx, wy, out_a, out_b, out_c, out_d2a, out_d2b, out_d2c);
        }
    };

    SpatialGrid _grid;

public:
    ContinuousTectonicHeightmap() = default;

    explicit ContinuousTectonicHeightmap(const Config& config) {
        init(config);
    }

    void init(const Config& config) {
        _config = config;
        _seed = config.seed;
        generate_control_nodes();
    }

    void set_seed(uint64_t seed) {
        _config.seed = seed;
        _seed = seed;
        generate_control_nodes();
    }

    const Config& get_config() const { return _config; }
    const std::vector<TectonicNode>& get_nodes() const { return _nodes; }
    std::vector<TectonicNode>& get_nodes_mut() { return _nodes; }
    const std::vector<TectonicNode>& get_macro_plates() const { return _coarse_nodes; }
    const std::vector<MacroPlateEdge>& get_macro_edges() const { return _macro_edges; }

    void build_macro_edges(const std::vector<TectonicNode>& plates) {
        _macro_edges.clear();
        int n = static_cast<int>(plates.size());
        if (n < 2) return;

        int64_t world_w = _config.world_width;
        int64_t world_h = _config.world_height;
        if (world_w <= 0) world_w = 512 * ONE;
        if (world_h <= 0) world_h = 512 * ONE;
        int64_t BIG_T = 4 * std::max(world_w, world_h);

        for (int i = 0; i < n; ++i) {
            for (int j = i + 1; j < n; ++j) {
                Vec2Fixed mid = {(plates[i].position.x + plates[j].position.x) / 2,
                                 (plates[i].position.y + plates[j].position.y) / 2};
                Vec2Fixed sep = plates[j].position - plates[i].position;
                int64_t sep_len = sep.length();
                if (sep_len <= 16) continue;

                Vec2Fixed norm = sep / sep_len; // points from i to j
                Vec2Fixed tang = norm.perpendicular();

                int64_t t_min = -BIG_T, t_max = BIG_T;
                bool valid = true;
                int64_t d2_mid_i = (mid - plates[i].position).length_sq();

                for (int k = 0; k < n; ++k) {
                    if (k == i || k == j) continue;
                    Vec2Fixed diff_ki = plates[k].position - plates[i].position;
                    int64_t A = 2 * tang.dot(diff_ki);
                    int64_t d2_mid_k = (mid - plates[k].position).length_sq();
                    int64_t B = d2_mid_k - d2_mid_i;

                    if (abs_fixed(A) <= 16) {
                        if (B < 0) { valid = false; break; }
                    } else if (A > 0) {
                        int64_t t_bound = div_fixed(B, A);
                        if (t_bound < t_max) t_max = t_bound;
                    } else {
                        int64_t t_bound = div_fixed(B, A);
                        if (t_bound > t_min) t_min = t_bound;
                    }
                }
                if (!valid || t_min >= t_max) continue;

                auto clip_axis = [&](int64_t mid_c, int64_t tang_c, int64_t max_c) {
                    if (abs_fixed(tang_c) > 16) {
                        int64_t t0 = div_fixed(0 - mid_c, tang_c);
                        int64_t t1 = div_fixed(max_c - mid_c, tang_c);
                        if (tang_c > 0) {
                            t_min = std::max(t_min, t0);
                            t_max = std::min(t_max, t1);
                        } else {
                            t_min = std::max(t_min, t1);
                            t_max = std::min(t_max, t0);
                        }
                    } else {
                        if (mid_c < 0 || mid_c > max_c) valid = false;
                    }
                };
                clip_axis(mid.x, tang.x, world_w);
                clip_axis(mid.y, tang.y, world_h);

                if (valid && (t_max - t_min) > (2 * ONE)) {
                    MacroPlateEdge e;
                    e.id = static_cast<int32_t>(_macro_edges.size());
                    e.plate_a = i;
                    e.plate_b = j;
                    e.plate_id_a = plates[i].get_plate_id();
                    e.plate_id_b = plates[j].get_plate_id();
                    e.plate_type_a = plates[i].plate_type;
                    e.plate_type_b = plates[j].plate_type;
                    e.v0 = mid + tang * t_min;
                    e.v1 = mid + tang * t_max;
                    e.length = (e.v1 - e.v0).length();
                    if (e.length <= 16) continue;
                    e.normal = norm;
                    e.tangent = (e.v1 - e.v0) / e.length;
                    e.strike_phase = mul_fixed(static_cast<int64_t>((e.id * 179424) % 65536), ONE);

                    // Decomposed pure directional push projection:
                    // c = (v_a - v_b) . norm = (v_a . norm) - (v_b . norm)
                    int64_t u_a = plates[i].velocity.dot(norm);
                    int64_t u_b = plates[j].velocity.dot(norm);
                    e.convergence_rate = u_a - u_b;

                    // Decomposed tangential shear projection:
                    // s = |(v_a - v_b) . tang| = |(v_a . tang) - (v_b . tang)|
                    int64_t w_a = plates[i].velocity.dot(e.tangent);
                    int64_t w_b = plates[j].velocity.dot(e.tangent);
                    e.shear_rate = abs_fixed(w_a - w_b);

                    int64_t h_eff = (plates[i].crust_thickness + plates[j].crust_thickness) / 2;
                    int64_t r_eff = (plates[i].crust_rigidity + plates[j].crust_rigidity) / 2;

                    e.alpha = (24 * ONE) + mul_fixed(h_eff, 16384) + mul_fixed(r_eff, 8192);
                    e.a_overrides = (plates[i].plate_type == PlateType::CONTINENTAL && plates[j].plate_type != PlateType::CONTINENTAL) ||
                                    (plates[i].crust_rigidity > plates[j].crust_rigidity) ||
                                    (plates[i].crust_rigidity == plates[j].crust_rigidity && plates[i].id <= plates[j].id);

                    if (e.convergence_rate > 2000) {
                        if (plates[i].plate_type == PlateType::CONTINENTAL && plates[j].plate_type == PlateType::CONTINENTAL) {
                            e.boundary_type = BoundaryType::CONVERGENT_CONTINENTAL_COLLISION;
                            int64_t c_factor = std::clamp(e.convergence_rate * 3, 0LL, 2 * ONE);
                            e.peak_uplift = mul_fixed(_config.collision_uplift_scale, c_factor);
                        } else if (plates[i].plate_type != plates[j].plate_type) {
                            e.boundary_type = BoundaryType::CONVERGENT_SUBDUCTION;
                            int64_t c_factor = std::clamp(e.convergence_rate * 3, 0LL, 2 * ONE);
                            e.peak_uplift = mul_fixed(_config.subduction_arc_scale, c_factor);
                        } else {
                            e.boundary_type = BoundaryType::CONVERGENT_ISLAND_ARC;
                            int64_t c_factor = std::clamp(e.convergence_rate * 3, 0LL, 2 * ONE);
                            e.peak_uplift = mul_fixed(_config.subduction_arc_scale / 2, c_factor);
                        }
                        e.impact_force = mul_fixed(e.convergence_rate, 40 * ONE + h_eff);
                    } else if (e.convergence_rate < -2000) {
                        if (plates[i].plate_type == PlateType::CONTINENTAL && plates[j].plate_type == PlateType::CONTINENTAL) {
                            e.boundary_type = BoundaryType::DIVERGENT_CONTINENTAL_RIFT;
                        } else {
                            e.boundary_type = BoundaryType::DIVERGENT_OCEANIC_SPREADING;
                        }
                    } else if (e.shear_rate > 3000) {
                        e.boundary_type = BoundaryType::TRANSFORM_SHEAR;
                    } else {
                        e.boundary_type = BoundaryType::PASSIVE_INTERIOR;
                    }

                    _macro_edges.push_back(e);
                }
            }
        }
    }

    void add_node(const TectonicNode& node) {
        _nodes.push_back(node);
        _coarse_nodes = _nodes;
        build_macro_edges(_coarse_nodes);
        rebuild_grid((int32_t)_nodes.size());
    }

    void clear_nodes() {
        _nodes.clear();
        _coarse_nodes.clear();
        _macro_edges.clear();
        _grid = SpatialGrid{}; // reset grid — empty node set
    }

    // --- Procedural Generation of Control Nodes (Two-Pass Tessellation) ---
    //
    // Pass 1 – Coarse Partition:
    //   Places `num_plates` seed nodes via Mitchell's Best-Candidate sampling.
    //   Assigns each a PlateType (CONTINENTAL / OCEANIC / ISLAND_ARC) via random roll.
    //   Plate 0 is always CONTINENTAL (anchors the spawn-friendly region).
    //   No border override is applied at this stage — coarse nodes are classification
    //   prototypes only and are never pushed into _nodes.
    //
    // Pass 2 – Fine Retessellation:
    //   Generates `num_fine_plates` denser nodes using an independent RNG stream
    //   (_seed + 99999). Each fine node inherits its PlateType from the nearest
    //   coarse Voronoi cell (plain Euclidean, no domain warp — structural step).
    //   After type inheritance, any fine node within `border_oceanic_margin` of a
    //   canvas edge is reclassified as OCEANIC, so the world terminates in ocean
    //   on all sides without requiring external guard nodes.
    //   Velocity and crustal properties are assigned using fine_rng throughout.
    //   Fine nodes are pushed into _nodes.
    //
    // Spatial Grid:
    //   After all nodes are placed, a SpatialGrid is built to accelerate the O(N)
    //   nearest-neighbour scan in sample() to near O(1).
    void generate_control_nodes() {
        _nodes.clear();
        SplitMix64 rng(_seed);

        int count = _config.num_plates;
        if (count < 2) count = 2;

        int64_t w = _config.world_width;
        int64_t h = _config.world_height;
        int64_t half_w = w / 2;
        int64_t half_h = h / 2;
        int k_coarse = _config.best_candidate_k_coarse;
        if (k_coarse < 4) k_coarse = 10;

        int64_t border_margin = (_config.border_oceanic_margin > 0)
                                    ? _config.border_oceanic_margin
                                    : (w / 10); // 10% outer ocean margin

        // ---------------------------------------------------------------
        // PASS 1 – Macro Tectonic Plates Placement & Physics Setup
        //
        // Mitchell's Best-Candidate Sampling for even plate distribution.
        // Plate 0 is placed near the world center and guaranteed CONTINENTAL.
        // Outer border plates are classified as OCEANIC so the map is naturally
        // encircled by ocean. Interior plates roll continental / island arc.
        // Each plate receives its own coherent drift velocity vector and crustal
        // thickness/rigidity/elevation parameters.
        // ---------------------------------------------------------------
        std::vector<TectonicNode> coarse_nodes;
        coarse_nodes.reserve(count);

        {
            std::vector<Vec2Fixed> coarse_placed;
            coarse_placed.reserve(count);

            for (int i = 0; i < count; ++i) {
                TectonicNode coarse;
                coarse.id = static_cast<uint32_t>(i + 1);
                coarse.plate_id = coarse.id;

                Vec2Fixed best_pos = {
                    mul_fixed(w, rng.next_range(3000, 62536)),
                    mul_fixed(h, rng.next_range(3000, 62536))
                };
                int64_t best_min_dist_sq = -1;

                for (int c = 0; c < k_coarse; ++c) {
                    Vec2Fixed cand;
                    if (i == 0) {
                        cand.x = half_w + mul_fixed(w, rng.next_range(-6000, 6000));
                        cand.y = half_h + mul_fixed(h, rng.next_range(-6000, 6000));
                    } else {
                        cand.x = mul_fixed(w, rng.next_range(3000, 62536));
                        cand.y = mul_fixed(h, rng.next_range(3000, 62536));
                    }

                    int64_t min_d2 = 0x7FFFFFFFFFFFFFFF;
                    for (const Vec2Fixed& placed : coarse_placed) {
                        Vec2Fixed diff = cand - placed;
                        int64_t d2 = diff.length_sq();
                        if (d2 < min_d2) min_d2 = d2;
                    }
                    if (coarse_placed.empty()) min_d2 = 0x7FFFFFFFFFFFFFFF;

                    if (min_d2 > best_min_dist_sq) {
                        best_min_dist_sq = min_d2;
                        best_pos = cand;
                    }
                }

                coarse.position = best_pos;
                coarse_placed.push_back(best_pos);

                // Plate type classification
                if (i == 0) {
                    coarse.plate_type = PlateType::CONTINENTAL;
                } else {
                    bool near_border = (
                        best_pos.x < border_margin ||
                        best_pos.x > (w - border_margin) ||
                        best_pos.y < border_margin ||
                        best_pos.y > (h - border_margin)
                    );
                    if (near_border) {
                        coarse.plate_type = PlateType::OCEANIC;
                    } else {
                        int32_t roll = rng.next_range(0, 100);
                        if (roll < _config.continent_chance_pct) {
                            coarse.plate_type = PlateType::CONTINENTAL;
                        } else {
                            int32_t arc_roll = rng.next_range(0, 100);
                            coarse.plate_type = (arc_roll < _config.island_arc_chance_pct)
                                                    ? PlateType::ISLAND_ARC
                                                    : PlateType::OCEANIC;
                        }
                    }
                }

                // Coherent plate drift velocity
                int64_t speed = rng.next_range(6000, 24000); // 0.09 to 0.36 units/step
                int64_t angle = rng.next_range(-PI_FIXED, PI_FIXED);
                coarse.velocity.x = mul_fixed(cos_fixed(angle), speed);
                coarse.velocity.y = mul_fixed(sin_fixed(angle), speed);

                // Crustal properties
                switch (coarse.plate_type) {
                    case PlateType::CONTINENTAL:
                        coarse.base_elevation  = _config.continental_base_elevation + rng.next_range(-2 * ONE, 3 * ONE);
                        coarse.crust_thickness = rng.next_range(30 * ONE, 52 * ONE);
                        coarse.crust_rigidity  = rng.next_range(3 * ONE, 8 * ONE);
                        coarse.density         = 2700 * ONE;
                        coarse.roughness       = ONE;
                        break;
                    case PlateType::ISLAND_ARC:
                        coarse.base_elevation  = _config.island_arc_base_elevation + rng.next_range(-2 * ONE, 4 * ONE);
                        coarse.crust_thickness = rng.next_range(10 * ONE, 22 * ONE);
                        coarse.crust_rigidity  = rng.next_range(2 * ONE, 5 * ONE);
                        coarse.density         = 2850 * ONE;
                        coarse.roughness       = ONE;
                        break;
                    case PlateType::OCEANIC:
                    default:
                        coarse.base_elevation  = _config.oceanic_base_elevation + rng.next_range(-8 * ONE, 6 * ONE);
                        coarse.crust_thickness = rng.next_range(6 * ONE, 18 * ONE);
                        coarse.crust_rigidity  = rng.next_range(1 * ONE, 4 * ONE);
                        coarse.density         = 3000 * ONE;
                        coarse.roughness       = ONE / 2 + rng.next_range(0, 8192);
                        break;
                }

                coarse_nodes.push_back(coarse);
            }
        } // end Pass 1

        // ---------------------------------------------------------------
        // MACRO PLATE BOUNDARY EDGES
        //
        // Generates explicit macro boundary edges via 16.16 bisector clipping.
        // Direct edge kinematics are evaluated from pure directional push
        // projection: c = u_a - u_b.
        // ---------------------------------------------------------------
        _coarse_nodes = coarse_nodes;
        build_macro_edges(_coarse_nodes);

        // ---------------------------------------------------------------
        // PASS 2 – Finer Node Tessellation with Plate Grouping
        //
        // Generates finer nodes that inherit their parent plate's identity,
        // bulk drift velocity, angular velocity, and crustal properties.
        // Near the canvas border, finer nodes are reclassified as OCEANIC
        // to ensure an organic ocean rim encircles the world.
        // ---------------------------------------------------------------
        int fine_count = (_config.num_fine_plates > 0) ? _config.num_fine_plates : (count * 4);
        int k_fine = _config.best_candidate_k_fine;
        SplitMix64 fine_rng(_seed + 99999);
        _nodes.reserve(fine_count);

        std::vector<Vec2Fixed> fine_placed;
        fine_placed.reserve(fine_count);

        for (int i = 0; i < fine_count; ++i) {
            TectonicNode fine;
            fine.id = static_cast<uint32_t>(i + 1);

            Vec2Fixed best_pos = {
                mul_fixed(w, fine_rng.next_range(3000, 62536)),
                mul_fixed(h, fine_rng.next_range(3000, 62536))
            };
            int64_t best_min_dist_sq = -1;

            for (int c = 0; c < k_fine; ++c) {
                Vec2Fixed cand;
                if (i == 0) {
                    cand.x = half_w + mul_fixed(w, fine_rng.next_range(-6000, 6000));
                    cand.y = half_h + mul_fixed(h, fine_rng.next_range(-6000, 6000));
                } else {
                    cand.x = mul_fixed(w, fine_rng.next_range(3000, 62536));
                    cand.y = mul_fixed(h, fine_rng.next_range(3000, 62536));
                }

                int64_t min_d2 = 0x7FFFFFFFFFFFFFFF;
                for (const Vec2Fixed& placed : fine_placed) {
                    Vec2Fixed diff = cand - placed;
                    int64_t d2 = diff.length_sq();
                    if (d2 < min_d2) min_d2 = d2;
                }
                if (fine_placed.empty()) min_d2 = 0x7FFFFFFFFFFFFFFF;

                if (min_d2 > best_min_dist_sq) {
                    best_min_dist_sq = min_d2;
                    best_pos = cand;
                }
            }

            fine.position = best_pos;
            fine_placed.push_back(best_pos);

            // Inherit from nearest coarse macro plate (maintains coherent plate grouping)
            int nearest_coarse = 0;
            int64_t nearest_d2 = 0x7FFFFFFFFFFFFFFF;
            for (int ci = 0; ci < (int)_coarse_nodes.size(); ++ci) {
                Vec2Fixed diff = best_pos - _coarse_nodes[ci].position;
                int64_t d2 = diff.length_sq();
                if (d2 < nearest_d2) {
                    nearest_d2 = d2;
                    nearest_coarse = ci;
                }
            }
            const TectonicNode& parent = _coarse_nodes[nearest_coarse];
            fine.plate_id         = parent.plate_id;
            fine.plate_type       = parent.plate_type;
            fine.velocity         = parent.velocity;
            fine.crust_thickness  = parent.crust_thickness;
            fine.crust_rigidity   = parent.crust_rigidity;
            fine.density          = parent.density;
            fine.roughness        = parent.roughness;

            fine.base_elevation = parent.base_elevation;

            // Border override: finer nodes near any canvas edge are reclassified
            // to OCEANIC so the continent naturally transitions into a surrounding ocean
            bool near_border = (
                best_pos.x < border_margin ||
                best_pos.x > (w - border_margin) ||
                best_pos.y < border_margin ||
                best_pos.y > (h - border_margin)
            );
            if (near_border) {
                fine.plate_type      = PlateType::OCEANIC;
                fine.base_elevation  = _config.oceanic_base_elevation + fine_rng.next_range(-6 * ONE, 4 * ONE);
                fine.crust_thickness = fine_rng.next_range(6 * ONE, 16 * ONE);
                fine.crust_rigidity  = fine_rng.next_range(1 * ONE, 3 * ONE);
                fine.density         = 3000 * ONE;
                fine.roughness       = ONE / 2 + fine_rng.next_range(0, 8192);
            }

            _nodes.push_back(fine);
        }

        assert(_nodes.size() >= 2 && "ContinuousTectonicHeightmap: must have at least 2 nodes");
        rebuild_grid((int32_t)_nodes.size());
    }

    // --- Domain Warping Helper ---

    // Warps coordinate space organically using multi-frequency noise
    FORCE_INLINE Vec2Fixed warp_coordinate(int64_t x_fixed, int64_t y_fixed) const {
        if (_config.warp_scale <= 0) return {x_fixed, y_fixed};

        // Multi-octave organic warp: 3 octaves prevents high-frequency gradient spikes
        int64_t qx = SimMath::fbm2d(
            mul_fixed(x_fixed, _config.warp_frequency) + (123 * ONE),
            mul_fixed(y_fixed, _config.warp_frequency) + (456 * ONE),
            _seed + 101, 3
        );
        int64_t qy = SimMath::fbm2d(
            mul_fixed(x_fixed, _config.warp_frequency) + (789 * ONE),
            mul_fixed(y_fixed, _config.warp_frequency) + (321 * ONE),
            _seed + 202, 3
        );

        int64_t wx = x_fixed + mul_fixed(qx, _config.warp_scale);
        int64_t wy = y_fixed + mul_fixed(qy, _config.warp_scale);
        return {wx, wy};
    }

    struct BoundaryInteraction {
        int64_t dist_to_boundary = 0;
        int64_t strike_coord = 0;
        int64_t convergence_rate = 0;
        int64_t shear_rate = 0;
        int64_t base_elevation = 0;
        int64_t tectonic_delta = 0;
        BoundaryType boundary_type = BoundaryType::PASSIVE_INTERIOR;
    };

    // --- Core Continuous Query Function ---
    // Samples the continuous heightfield, returning full tectonic diagnostic data
    TectonicSample sample(int64_t x_fixed, int64_t y_fixed) const {
        TectonicSample result;
        result.sample_pos = {x_fixed, y_fixed};

        if (_nodes.empty() && _coarse_nodes.empty()) {
            return result;
        }

        // 1. Organic Domain Warping
        Vec2Fixed p_warped = warp_coordinate(x_fixed, y_fixed);
        result.warped_pos = p_warped;

        const auto& coarse_list = !_coarse_nodes.empty() ? _coarse_nodes : _nodes;

        // If no macro edges exist, return baseline of closest node
        if (_macro_edges.empty()) {
            if (!_nodes.empty()) {
                const auto& n = _nodes[0];
                result.primary_plate_id = n.id;
                result.primary_type = n.plate_type;
                result.elevation_base = n.base_elevation;
                result.elevation_final = result.elevation_base;
            }
            return result;
        }

        // 2. Query Closest (E1) and Second-Closest (E2) Macro Plate Edges
        int best_edge_idx = -1;
        int second_edge_idx = -1;
        int64_t min_d2_1 = 0x7FFFFFFFFFFFFFFF;
        int64_t min_d2_2 = 0x7FFFFFFFFFFFFFFF;
        int64_t best_t_1 = 0;
        int64_t best_t_2 = 0;

        for (size_t ei = 0; ei < _macro_edges.size(); ++ei) {
            int64_t t_proj = 0;
            int64_t d2 = dist_to_segment_fixed(p_warped, _macro_edges[ei].v0, _macro_edges[ei].v1, t_proj);
            if (d2 < min_d2_1) {
                min_d2_2 = min_d2_1;
                second_edge_idx = best_edge_idx;
                best_t_2 = best_t_1;

                min_d2_1 = d2;
                best_edge_idx = static_cast<int>(ei);
                best_t_1 = t_proj;
            } else if (d2 < min_d2_2) {
                min_d2_2 = d2;
                second_edge_idx = static_cast<int>(ei);
                best_t_2 = t_proj;
            }
        }

        const auto& edge1 = _macro_edges[best_edge_idx];
        int64_t dist1 = sqrt_fixed(min_d2_1);
        int64_t strike1 = mul_fixed(best_t_1, edge1.length) - (edge1.length / 2) + edge1.strike_phase;

        int pa = std::clamp(edge1.plate_a, 0, (int)coarse_list.size() - 1);
        int pb = std::clamp(edge1.plate_b, 0, (int)coarse_list.size() - 1);
        const TectonicNode& plate_a = coarse_list[pa];
        const TectonicNode& plate_b = coarse_list[pb];

        int64_t d2_pa = (p_warped - plate_a.position).length_sq();
        int64_t d2_pb = (p_warped - plate_b.position).length_sq();
        bool on_side_a = (d2_pa <= d2_pb);

        const TectonicNode& local_plate = on_side_a ? plate_a : plate_b;
        const TectonicNode& neighbor_plate = on_side_a ? plate_b : plate_a;

        // Query nearest fine node in _nodes for local micro-block roughness/ID
        int fine_idx = -1;
        int64_t min_fine_d2 = 0x7FFFFFFFFFFFFFFF;
        if (!_nodes.empty()) {
            int d_b = -1, d_c = -1;
            int64_t d2_b = 0, d2_c = 0;
            _grid.query_three_nearest(_nodes, p_warped.x, p_warped.y, fine_idx, d_b, d_c, min_fine_d2, d2_b, d2_c);
        }
        const TectonicNode& fine_node = (fine_idx >= 0 && fine_idx < (int)_nodes.size()) ? _nodes[fine_idx] : local_plate;

        result.primary_plate_id = fine_node.id;
        result.secondary_plate_id = neighbor_plate.id;
        result.primary_type = fine_node.plate_type;
        result.secondary_type = neighbor_plate.plate_type;
        result.dist_to_primary = sqrt_fixed(min_fine_d2 < 0x7FFFFFFFFFFFFFFF ? min_fine_d2 : d2_pa);
        result.dist_to_boundary = dist1;
        result.strike_coord = strike1;
        result.convergence_rate = edge1.convergence_rate;
        result.shear_rate = edge1.shear_rate;
        result.boundary_type = edge1.boundary_type;

        int node_count = static_cast<int>(_nodes.empty() ? _coarse_nodes.size() : _nodes.size());
        int64_t avg_cell_radius = _config.world_width / static_cast<int64_t>(std::max(1, (int32_t)std::sqrt((double)node_count)));
        int64_t blend_scale = (_config.blend_range_scale > 0) ? _config.blend_range_scale : 98304; // default 1.5
        int64_t blend_range = mul_fixed(avg_cell_radius, blend_scale);
        if (blend_range < 24 * ONE) blend_range = 24 * ONE;

        // 3. Compute Primary Boundary Interaction with Edge 1
        BoundaryInteraction inter1 = compute_edge_interaction(
            local_plate, neighbor_plate, edge1, dist1, strike1, p_warped, blend_range
        );

        int64_t base_elev = inter1.base_elevation;
        int64_t tectonic_delta = inter1.tectonic_delta;
        BoundaryType btype = inter1.boundary_type;

        // 4. Smooth Triple-Junction Blending with Edge 2
        if (second_edge_idx >= 0 && second_edge_idx != best_edge_idx) {
            int64_t dist2 = sqrt_fixed(min_d2_2);
            int64_t delta_dist = dist2 - dist1;
            if (delta_dist < 0) delta_dist = 0;

            int64_t w_blend = avg_cell_radius / 2;
            if (w_blend < 8 * ONE) w_blend = 8 * ONE;

            if (delta_dist < w_blend) {
                const auto& edge2 = _macro_edges[second_edge_idx];
                int64_t strike2 = mul_fixed(best_t_2, edge2.length) - (edge2.length / 2) + edge2.strike_phase;

                int pa2 = std::clamp(edge2.plate_a, 0, (int)coarse_list.size() - 1);
                int pb2 = std::clamp(edge2.plate_b, 0, (int)coarse_list.size() - 1);
                int64_t d2_pa2 = (p_warped - coarse_list[pa2].position).length_sq();
                int64_t d2_pb2 = (p_warped - coarse_list[pb2].position).length_sq();
                bool on_side_a2 = (d2_pa2 <= d2_pb2);
                const TectonicNode& local_plate2 = on_side_a2 ? coarse_list[pa2] : coarse_list[pb2];
                const TectonicNode& neighbor_plate2 = on_side_a2 ? coarse_list[pb2] : coarse_list[pa2];

                BoundaryInteraction inter2 = compute_edge_interaction(
                    local_plate2, neighbor_plate2, edge2, dist2, strike2, p_warped, blend_range
                );

                int64_t t = div_fixed(delta_dist, w_blend);
                int64_t s = SimMath::quintic_smooth(std::clamp(t, 0LL, ONE));
                int64_t w2 = (ONE - s) / 2;
                int64_t w1 = ONE - w2;

                base_elev = mul_fixed(w1, inter1.base_elevation) + mul_fixed(w2, inter2.base_elevation);
                tectonic_delta = mul_fixed(w1, inter1.tectonic_delta) + mul_fixed(w2, inter2.tectonic_delta);
                if (w2 > ONE / 2) {
                    btype = inter2.boundary_type;
                }
            }
        }

        result.elevation_base = base_elev;
        result.elevation_tectonic = tectonic_delta;
        result.boundary_type = btype;

        // 5. Multi-Frequency Procedural Details (fBm + Ridge Noise + Littoral Attenuation)
        int64_t strain_rate = sqrt_fixed(mul_fixed(result.convergence_rate, result.convergence_rate) +
                                         mul_fixed(result.shear_rate, result.shear_rate));
        int64_t macro_elev = base_elev + tectonic_delta;
        int64_t noise_delta = calculate_detail_noise(
            x_fixed, y_fixed, fine_node, strain_rate, btype,
            tectonic_delta, macro_elev, strike1, dist1
        );
        result.elevation_noise = noise_delta;

        // 6. Final Composite Elevation
        result.elevation_final = macro_elev + noise_delta;

        return result;
    }

    // --- Fast Elevation-Only Query ---
    FORCE_INLINE int64_t get_elevation(int64_t x_fixed, int64_t y_fixed) const {
        return sample(x_fixed, y_fixed).elevation_final;
    }

    FORCE_INLINE float get_elevation_float(float x, float y) const {
        int64_t xf = static_cast<int64_t>(x * 65536.0f);
        int64_t yf = static_cast<int64_t>(y * 65536.0f);
        return get_elevation(xf, yf) / 65536.0f;
    }

    // --- Grid Population Helper ---
    // out_plates stores the primary_plate_id for each cell as a uint16_t, supporting
    // up to 65 535 fine nodes without ID truncation (previous uint8_t silently corrupted
    // the seam mask for fine_count > 255).
    void generate_grid(
        int32_t width, int32_t height, int64_t cell_size_fixed,
        std::vector<int32_t>& out_elevation,
        std::vector<uint16_t>* out_plates = nullptr
    ) const {
        size_t total = static_cast<size_t>(width) * height;
        out_elevation.resize(total);
        if (out_plates) out_plates->resize(total);

        for (int32_t y = 0; y < height; ++y) {
            int64_t py = static_cast<int64_t>(y) * cell_size_fixed;
            for (int32_t x = 0; x < width; ++x) {
                int64_t px = static_cast<int64_t>(x) * cell_size_fixed;
                TectonicSample s = sample(px, py);
                size_t idx = static_cast<size_t>(y) * width + x;
                // M5: clamp before narrowing cast — extreme config values can push
                // elevation_final outside int32 range, causing silent UB otherwise.
                out_elevation[idx] = static_cast<int32_t>(
                    std::clamp<int64_t>(s.elevation_final, INT32_MIN, INT32_MAX));
                if (out_plates)
                    (*out_plates)[idx] = static_cast<uint16_t>(
                        std::min<uint32_t>(s.primary_plate_id, 0xFFFFu));
            }
        }
    }

private:
    // --- Spatial Grid Rebuild Helper ---
    // Centralises the grid sizing formula so generate_control_nodes() and add_node()
    // always produce the same cell layout for a given node count.  Q2 FIX: previously
    // add_node() used a divergent formula (based on _nodes.size()) while generate_control_nodes()
    // used fine_count (the target count), producing inconsistent query behaviour after manual
    // node additions.
    void rebuild_grid(int32_t node_count) {
        const int64_t w = _config.world_width;
        const int64_t h = _config.world_height;
        int32_t grid_cols = std::max(4, (int32_t)std::sqrt((double)node_count));
        int32_t grid_rows = std::max(4, (int32_t)(std::sqrt((double)node_count) * (double)h / (double)w));
        _grid.build(_nodes, w, h, grid_cols, grid_rows);
    }

    // --- Physics of Boundary Deformation ---
    // All scale parameters are read from _config so that Config fields are live
    // tuning controls rather than dead documentation.
    int64_t calculate_tectonic_deformation(
        const TectonicNode& node_a,
        const TectonicNode& node_b,
        int64_t dist_to_boundary,
        int64_t strike_coord,
        int64_t convergence_rate,
        int64_t shear_rate,
        BoundaryType& out_type
    ) const {
        int64_t deform = 0;

        int64_t h_eff = (node_a.crust_thickness + node_b.crust_thickness) / 2;
        int64_t r_eff = (node_a.crust_rigidity + node_b.crust_rigidity) / 2;

        int64_t belt_width = _config.fold_belt_base_width + mul_fixed(h_eff, 16384) + mul_fixed(r_eff, 8192);
        if (belt_width <= 12 * ONE) belt_width = 12 * ONE;

        int64_t conv_threshold = 2000;

        // ==========================================
        // 1. CONVERGENT TECTONICS (C > Threshold)
        // ==========================================
        if (convergence_rate > conv_threshold) {
            int64_t c_factor = std::clamp(convergence_rate * 3, 0LL, 2 * ONE);

            // A. Continental-Continental Collision (Analytical Flexural Slab w(x) = w_0 * e^(-x/alpha) * cos(x/alpha))
            if (node_a.plate_type == PlateType::CONTINENTAL && node_b.plate_type == PlateType::CONTINENTAL) {
                out_type = BoundaryType::CONVERGENT_CONTINENTAL_COLLISION;

                // Determine overriding vs under-riding plate for asymmetric rotational tilt
                bool a_is_overriding = (node_a.crust_rigidity > node_b.crust_rigidity) ||
                                       (node_a.crust_rigidity == node_b.crust_rigidity && node_a.id <= node_b.id);

                int64_t peak_uplift = mul_fixed(_config.collision_uplift_scale, c_factor);

                // Along-strike modulation: creates alternating high mountain massifs and lower passes/valleys
                int64_t wave1 = cos_fixed(div_fixed(mul_fixed(strike_coord, TWO_PI), 56 * ONE));
                int64_t wave2 = sin_fixed(div_fixed(mul_fixed(strike_coord, TWO_PI), 20 * ONE));
                int64_t strike_mod = (90 * ONE / 100) + (wave1 * 30 / 100) + (wave2 * 15 / 100);
                if (strike_mod < (40 * ONE / 100)) strike_mod = 40 * ONE / 100;
                peak_uplift = mul_fixed(peak_uplift, strike_mod);

                // Characteristic elastic flexural parameter: alpha = (24m + h_eff/4 + r_eff/8)
                int64_t alpha = (24 * ONE) + mul_fixed(h_eff, 16384) + mul_fixed(r_eff, 8192);

                // Analytical thin elastic plate flexural slab deflection: w(x) = w_0 * e^(-x/alpha) * cos(x/alpha)
                int64_t slab_w = flexural_slab_profile(dist_to_boundary, alpha, peak_uplift);

                if (a_is_overriding) {
                    // Overriding plate: flexural mountain ridge + elevated plateau tilt ramp
                    int64_t plateau_reach = alpha * 3;
                    int64_t plateau_ramp = 0;
                    if (dist_to_boundary < plateau_reach) {
                        int64_t u_plat = div_fixed(dist_to_boundary, plateau_reach);
                        plateau_ramp = mul_fixed(peak_uplift * 38 / 100, ONE - SimMath::quintic_smooth(u_plat));
                    }
                    deform = std::max(slab_w, plateau_ramp);
                } else {
                    // Under-riding plate: frontal thrust peak at collision seam, then dips into flexural foreland basin
                    deform = slab_w;
                }
            }
            // B. Subduction (Continental vs. Oceanic, or ISLAND_ARC involved)
            else if (node_a.plate_type != node_b.plate_type) {
                out_type = BoundaryType::CONVERGENT_SUBDUCTION;

                bool a_overrides = (node_a.plate_type == PlateType::CONTINENTAL) ||
                                   (node_a.plate_type == PlateType::ISLAND_ARC &&
                                    node_b.plate_type == PlateType::OCEANIC);

                int64_t alpha = (20 * ONE) + mul_fixed(h_eff, 16384);

                if (a_overrides) {
                    // Continental overriding plate: volcanic cordillera arc peaks at flexural offset alpha
                    int64_t arc_dist = abs_fixed(dist_to_boundary - alpha);
                    int64_t arc_peak = mul_fixed(_config.subduction_arc_scale, c_factor);

                    int64_t volc_wave = cos_fixed(div_fixed(mul_fixed(strike_coord, TWO_PI), 36 * ONE));
                    int64_t volc_mod = (85 * ONE / 100) + mul_fixed(45 * ONE / 100, volc_wave);
                    arc_peak = mul_fixed(arc_peak, volc_mod);

                    deform += flexural_slab_profile(arc_dist, alpha, arc_peak);
                } else {
                    // Subducting oceanic plate: analytical trench depression and outer rise
                    int64_t trench_d = mul_fixed(_config.subduction_trench_scale, c_factor);
                    deform -= flexural_slab_profile(dist_to_boundary, alpha * 2 / 3, trench_d);
                }
            }
            // C. Oceanic-Oceanic (or ISLAND_ARC vs ISLAND_ARC) Subduction
            else {
                out_type = BoundaryType::CONVERGENT_ISLAND_ARC;

                int64_t trench_center = std::max(12 * ONE, belt_width / 3);
                int64_t trench_span   = trench_center;
                int64_t trench_dist   = abs_fixed(dist_to_boundary - trench_center);
                if (trench_dist < trench_span) {
                    int64_t u = div_fixed(trench_dist, trench_span);
                    int64_t tr_env = ONE - SimMath::quintic_smooth(u);
                    bool has_arc = (node_a.plate_type == PlateType::ISLAND_ARC || node_b.plate_type == PlateType::ISLAND_ARC);
                    int64_t trench_d = has_arc
                                        ? mul_fixed(_config.subduction_trench_scale, 32768) // 0.5x for arcs
                                        : _config.subduction_trench_scale;
                    deform -= mul_fixed(trench_d, tr_env);
                }
            }
        }
        // ==========================================
        // 2. DIVERGENT TECTONICS (C < -Threshold)
        // ==========================================
        else if (convergence_rate < -conv_threshold) {
            int64_t r_factor = std::clamp((-convergence_rate) * 3, 0LL, 2 * ONE);
            int64_t rift_width = belt_width * 2 / 3;

            if (node_a.plate_type == PlateType::CONTINENTAL && node_b.plate_type == PlateType::CONTINENTAL) {
                out_type = BoundaryType::DIVERGENT_CONTINENTAL_RIFT;

                // Central graben depression
                if (dist_to_boundary < rift_width / 2) {
                    int64_t u = div_fixed(dist_to_boundary, rift_width / 2);
                    int64_t graben_env = ONE - SimMath::quintic_smooth(u);
                    deform -= mul_fixed(_config.rift_graben_scale, mul_fixed(r_factor, graben_env));
                }
                // Flanking rift shoulders
                int64_t shoulder_dist = mul_fixed(rift_width, 39321); // 0.60 * rift_width
                int64_t shoulder_span = rift_width / 2;
                int64_t diff_sh = abs_fixed(dist_to_boundary - shoulder_dist);
                if (diff_sh < shoulder_span) {
                    int64_t u = div_fixed(diff_sh, shoulder_span);
                    int64_t sh_env = ONE - SimMath::quintic_smooth(u);
                    deform += mul_fixed(_config.rift_shoulder_scale, mul_fixed(r_factor, sh_env));
                }
            } else {
                out_type = BoundaryType::DIVERGENT_OCEANIC_SPREADING;

                // Submerged spreading ridge (stays below sea level)
                if (dist_to_boundary < rift_width) {
                    int64_t u = div_fixed(dist_to_boundary, rift_width);
                    int64_t ridge_env = ONE - SimMath::hermite_smooth(u);
                    deform += mul_fixed(_config.mid_ocean_ridge_scale, mul_fixed(r_factor, ridge_env));

                    if (dist_to_boundary < rift_width / 4) {
                        int64_t axial_u = div_fixed(dist_to_boundary, rift_width / 4);
                        int64_t axial_env = ONE - SimMath::quintic_smooth(axial_u);
                        deform -= mul_fixed(_config.mid_ocean_axial_scale, axial_env);
                    }
                }
            }
        }
        // ==========================================
        // 3. TRANSFORM / SHEAR FAULTING
        // ==========================================
        else if (shear_rate > 3000) {
            out_type = BoundaryType::TRANSFORM_SHEAR;
            int64_t fault_width = belt_width / 2;

            if (dist_to_boundary < fault_width) {
                int64_t u = div_fixed(dist_to_boundary, fault_width);
                int64_t scarp_env = ONE - SimMath::quintic_smooth(u);
                int64_t scarp_scale = (node_a.plate_type == PlateType::CONTINENTAL)
                                        ? _config.shear_scarp_continental
                                        : _config.shear_scarp_oceanic;
                deform += mul_fixed(scarp_scale, scarp_env);
            }
        }

        return deform;
    }

    // --- Continental Margin Baseline Elevation & Littoral Shoreline Profile ---
    int64_t evaluate_baseline_elevation(
        const TectonicNode& node_a,
        const TectonicNode& node_b,
        int64_t dist_to_boundary,
        int64_t blend_range,
        int64_t local_base,
        int64_t neighbor_base
    ) const {
        bool is_oceanic_a = (node_a.plate_type == PlateType::OCEANIC);
        bool is_oceanic_b = (node_b.plate_type == PlateType::OCEANIC);

        if (is_oceanic_a != is_oceanic_b) {
            // Continental-Oceanic Boundary (Coastline)
            // At dist_to_boundary = 0 (the coastline itself), elevation is exactly 0.0 (sea level) on BOTH sides.
            if (!is_oceanic_a) {
                // Land (Continental / Island Arc) side:
                // Gentle sandy beach profile from 0.0m up to beach_h (+2.5m) over beach_w (20m),
                // then smoothly rising inland to continental interior base elevation over blend_range.
                int64_t beach_w = 20 * ONE; // 20m wide gentle sandy beach (10 tiles at 2m)
                int64_t beach_h = (_config.coastal_boundary_elevation > 0)
                                      ? _config.coastal_boundary_elevation
                                      : ((5 * ONE) / 2); // +2.5m beach berm crest

                if (dist_to_boundary < beach_w) {
                    int64_t u = div_fixed(dist_to_boundary, beach_w);
                    return mul_fixed(beach_h, SimMath::quintic_smooth(u));
                } else if (dist_to_boundary < blend_range) {
                    int64_t inland_span = blend_range - beach_w;
                    if (inland_span <= 0) inland_span = ONE;
                    int64_t u = div_fixed(dist_to_boundary - beach_w, inland_span);
                    int64_t t_blend = SimMath::quintic_smooth(std::clamp(u, 0LL, ONE));
                    return beach_h + mul_fixed(t_blend, local_base - beach_h);
                } else {
                    return local_base;
                }
            } else {
                // Oceanic side:
                // Gentle wading surf from 0.0m down to -2.0m over surf_w,
                // then sloping along the continental shelf to shelf_depth (-8m),
                // then descending continental slope down to abyssal floor (-36m).
                int64_t surf_w  = 20 * ONE; // 20m shallow wading surf
                int64_t surf_d  = -2 * ONE; // -2.0m outer surf depth
                int64_t shelf_w = _config.shelf_width > 0 ? _config.shelf_width : (36 * ONE);
                if (shelf_w <= surf_w) shelf_w = surf_w + 8 * ONE;
                int64_t shelf_d = _config.shelf_depth; // default -8.0m

                if (dist_to_boundary < surf_w) {
                    int64_t u = div_fixed(dist_to_boundary, surf_w);
                    return mul_fixed(surf_d, SimMath::quintic_smooth(u));
                } else if (dist_to_boundary < shelf_w) {
                    int64_t u = div_fixed(dist_to_boundary - surf_w, shelf_w - surf_w);
                    int64_t t_blend = SimMath::quintic_smooth(std::clamp(u, 0LL, ONE));
                    return surf_d + mul_fixed(t_blend, shelf_d - surf_d);
                } else if (dist_to_boundary < shelf_w * 2) {
                    int64_t u = div_fixed(dist_to_boundary - shelf_w, shelf_w);
                    int64_t t_blend = SimMath::quintic_smooth(std::clamp(u, 0LL, ONE));
                    return shelf_d + mul_fixed(t_blend, local_base - shelf_d);
                } else {
                    return local_base;
                }
            }
        } else {
            // Interior Boundary (Land-Land or Ocean-Ocean):
            // Smooth midpoint transition between plates
            int64_t h_boundary = (local_base + neighbor_base) / 2;
            int64_t safe_blend = blend_range > 0 ? blend_range : ONE;
            if (dist_to_boundary < safe_blend) {
                int64_t u = div_fixed(dist_to_boundary, safe_blend);
                int64_t t_blend = SimMath::quintic_smooth(std::clamp(u, 0LL, ONE));
                return h_boundary + mul_fixed(t_blend, local_base - h_boundary);
            }
            return local_base;
        }
    }

    // --- Compute Edge Boundary Interaction ---
    BoundaryInteraction compute_edge_interaction(
        const TectonicNode& local_plate,
        const TectonicNode& neighbor_plate,
        const MacroPlateEdge& edge,
        int64_t dist_to_boundary,
        int64_t strike_coord,
        const Vec2Fixed& p_warped,
        int64_t blend_range
    ) const {
        BoundaryInteraction res;
        res.dist_to_boundary = dist_to_boundary;
        res.strike_coord = strike_coord;
        res.convergence_rate = edge.convergence_rate;
        res.shear_rate = edge.shear_rate;
        res.boundary_type = edge.boundary_type;

        int64_t local_base = local_plate.base_elevation;
        int64_t neighbor_base = neighbor_plate.base_elevation;

        res.base_elevation = evaluate_baseline_elevation(
            local_plate, neighbor_plate, dist_to_boundary, blend_range, local_base, neighbor_base
        );

        res.tectonic_delta = calculate_tectonic_deformation(
            local_plate, neighbor_plate, dist_to_boundary, strike_coord,
            edge.convergence_rate, edge.shear_rate, res.boundary_type
        );

        return res;
    }


    // --- Procedural Meso & Micro Detail Noise ---
    int64_t calculate_detail_noise(
        int64_t x_fixed, int64_t y_fixed,
        const TectonicNode& node,
        int64_t strain_rate,
        BoundaryType btype,
        int64_t tectonic_delta = 0,
        int64_t macro_elev = 0,
        int64_t strike_coord = 0,
        int64_t dist_to_boundary = 0
    ) const {
        if (_config.detail_noise_scale <= 0) {
            return 0;
        }

        // Multi-frequency Fractal Brownian Motion (fBm)
        int64_t fbm_val = SimMath::fbm2d(
            mul_fixed(x_fixed, 1800),
            mul_fixed(y_fixed, 1800),
            _seed + 303,
            _config.noise_octaves,
            _config.noise_persistence,
            _config.noise_lacunarity
        );

        // Ridge Noise for sharp alpine peaks and rugged highlands:
        // Modulated continuously across continental highlands and active deformation zones.
        int64_t ridge_val = 0;
        bool is_continental_or_arc = (node.plate_type == PlateType::CONTINENTAL ||
                                      node.plate_type == PlateType::ISLAND_ARC);
        if (is_continental_or_arc) {
            // Uplift boost: active convergent deformation
            int64_t uplift_boost = (tectonic_delta > 0)
                ? std::clamp(div_fixed(tectonic_delta, 10 * ONE), 0LL, ONE)
                : 0;

            // Elevation fade: continental highlands and plateaus above ~20m
            int64_t elev_fade = 0;
            if (macro_elev > 20 * ONE) {
                elev_fade = std::clamp(div_fixed(macro_elev - 20 * ONE, 24 * ONE), 0LL, ONE);
            }

            // Strain fade: active shearing or deformation belts
            int64_t strain_fade = std::clamp(div_fixed(strain_rate, 8000), 0LL, ONE);

            int64_t ridge_factor = std::max(uplift_boost, std::max(elev_fade, strain_fade));
            if (ridge_factor > 0) {
                // In active mountain belts, align ridge noise along the tectonic strike
                int64_t rx = (tectonic_delta > 0 && abs_fixed(strike_coord) > 0)
                    ? mul_fixed(strike_coord, 1100)
                    : mul_fixed(x_fixed, 2400);
                int64_t ry = (tectonic_delta > 0 && dist_to_boundary > 0)
                    ? mul_fixed(dist_to_boundary, 2800)
                    : mul_fixed(y_fixed, 2400);

                int64_t raw_ridge = SimMath::ridge_noise2d(rx, ry, _seed + 404, 4);
                int64_t smooth_factor = SimMath::quintic_smooth(ridge_factor);
                ridge_val = mul_fixed(raw_ridge, smooth_factor);
            }
        }

        // Modulate roughness by plate roughness & tectonic strain
        int64_t strain_boost = std::clamp(mul_fixed(strain_rate, 4 * ONE), 0LL, 2 * ONE);
        int64_t total_detail = mul_fixed(fbm_val, _config.detail_noise_scale);

        if (ridge_val > 0) {
            total_detail += mul_fixed(ridge_val, mul_fixed(_config.detail_noise_scale, ONE + strain_boost));
        }

        // Littoral Noise Attenuation:
        // As elevation approaches sea level (waterline = 0), attenuate high-amplitude
        // rugged noise, but preserve gentle sand dune relief (~40%) so beaches are natural and organic
        // rather than flat featureless tables.
        int64_t dist_to_sea = abs_fixed(macro_elev);
        int64_t littoral_range = 6 * ONE; // 6m transition zone
        if (dist_to_sea < littoral_range) {
            int64_t u = div_fixed(dist_to_sea, littoral_range);
            // Attenuate to 15% at shoreline, scaling up smoothly to 100% at +/- 6m
            int64_t littoral_att = (15 * ONE / 100) + mul_fixed(85 * ONE / 100, SimMath::quintic_smooth(u));
            total_detail = mul_fixed(total_detail, littoral_att);
        }

        if (node.plate_type == PlateType::OCEANIC) {
            // Keep oceanic abyssal plains smooth and deeply submerged
            total_detail = total_detail / 3;
        }

        return mul_fixed(total_detail, node.roughness);
    }

};

} // namespace Tectonics
