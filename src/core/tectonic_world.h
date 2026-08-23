#pragma once
#include "fixed.h"
#include "rng.h"
#include "noise.h"
#include "cordic.h"
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <string>

namespace Generation {

struct GlacialTarn {
    int64_t px = 0; // World fixed-point pos
    int64_t py = 0;
    int64_t radius = 0;
    uint32_t id = 0;
};

struct TectonicSample {
    int64_t elevation = 0;        // 0 .. 100 * ONE (16.16 fixed point)
    int64_t temperature = 0;      // 0 .. 100 * ONE
    int64_t moisture = 0;         // 0 .. 100 * ONE
    uint8_t biome = 3;            // 0=Ocean, 1=Tundra/Snow, 2=Desert, 3=Grassland, 4=Forest, 5=Rainforest, 6=Swamp, 7=Mountain
    bool    is_land = false;
    bool    is_peak = false;      // High bare grey rock peak
    bool    is_snow = false;      // White snow / glacier
    bool    is_glacial_tarn = false; // Cirque lake nested in alpine zone
    bool    is_rain_shadow = false; // Protected arid basin
    uint8_t mineral_province = 0; // 1=N, 2=P, 3=K
    uint32_t plate_id = 0;        // Dominant plate ID at this location
};

struct TectonicPlate {
    uint32_t id = 0;
    int64_t  cx = 0;              // Center X (16.16 fixed-point world coords)
    int64_t  cy = 0;              // Center Y (16.16 fixed-point world coords)
    int64_t  vx = 0;              // Velocity X (16.16 fixed-point, tiles per time unit)
    int64_t  vy = 0;              // Velocity Y (16.16 fixed-point, tiles per time unit)
    int64_t  heading = 0;         // Angle in radians (16.16 fixed-point)
    int64_t  speed = 0;           // Speed in 16.16 fixed-point
    int64_t  thickness = 35 * ONE;// Crustal thickness (15 to 50 * ONE)
    int64_t  rigidity = 4 * ONE; // Flexural rigidity (1 to 10 * ONE)
    int64_t  base_elevation = 32 * ONE; // Base elevation for continental crust
    bool     is_continental = true;     // true = land/continental, false = oceanic
    uint8_t  mineral_bias = 0;    // 1=N, 2=P, 3=K
};

struct CollisionZone {
    uint32_t plate_a_id = 0;
    uint32_t plate_b_id = 0;
    int32_t  plate_a_idx = 0;
    int32_t  plate_b_idx = 0;

    // Midpoint between plate centers
    int64_t  mid_x = 0;
    int64_t  mid_y = 0;

    // Unit normal vector directed from Plate A to Plate B
    int64_t  normal_x = 0;
    int64_t  normal_y = 0;

    // Unit tangent vector along the fault boundary
    int64_t  tangent_x = 0;
    int64_t  tangent_y = 0;

    int64_t  distance_ab = 0;

    // Kinematics
    int64_t  convergence_rate = 0; // Positive = colliding/crashing, negative = rifting/diverging
    int64_t  shear_rate = 0;       // Strike-slip transform shear magnitude

    // Biot Buckling Fold Physics Parameters
    int64_t  wavelength_1 = 0;     // Primary fold wavelength (anticline-syncline pairs)
    int64_t  wavelength_2 = 0;     // Secondary parasitic folds (Pumpelly's rule: lambda_1 / 3)
    int64_t  wavelength_3 = 0;     // Tertiary fine ridges / crenulation (lambda_1 / 7)
    int64_t  max_amplitude = 0;    // Peak mountain uplift from crustal shortening
    int64_t  belt_width = 0;       // Total fold-and-thrust belt width

    // Vergence & Overthrust Asymmetry
    int64_t  vergence = 0;         // [-ONE, ONE] Fold lean direction towards underthrusting plate
    int64_t  thrust_offset = 0;    // Lateral shift of the structural crest due to overthrusting
    int64_t  decay_overriding = 0; // Gentle backlimb / plateau slope width
    int64_t  decay_underthrust = 0;// Steep frontal forelimb escarpment width

    bool     is_convergent = false;
    bool     is_subduction = false;// Oceanic-continental collision
};

class TectonicWorld {
public:
    inline static int32_t config_num_plates_min = 4;
    inline static int32_t config_num_plates_max = 7;
    inline static int32_t config_center_bias = 85;
    inline static int32_t config_continent_chance = 70;
    inline static int32_t config_vel_min = 50;  // 0.50 speed in hundredths
    inline static int32_t config_vel_max = 300; // 3.00 speed in hundredths

    explicit TectonicWorld(uint64_t seed = 42, int32_t world_w = 512, int32_t world_h = 512)
        : _seed(seed), _world_w(world_w), _world_h(world_h) {
        _build_default_plates();
        build();
    }

    void set_seed(uint64_t seed, int32_t world_w = 512, int32_t world_h = 512) {
        _seed = seed;
        _world_w = world_w;
        _world_h = world_h;
        _build_default_plates();
        build();
    }

    // --- Plate Configuration API ---

    void clear_plates() {
        _plates.clear();
        _collision_zones.clear();
        _glacial_tarns.clear();
    }

    // Add plate with direct Cartesian velocity vector (vx, vy)
    void add_plate(uint32_t id, int64_t cx, int64_t cy,
                   int64_t vx, int64_t vy,
                   int64_t thickness = 35 * ONE, int64_t rigidity = 4 * ONE,
                   bool is_continental = true, int64_t base_elevation = 32 * ONE) {
        TectonicPlate p;
        p.id = id;
        p.cx = cx;
        p.cy = cy;
        p.vx = vx;
        p.vy = vy;
        p.thickness = std::clamp(thickness, 10 * ONE, 80 * ONE);
        p.rigidity = std::clamp(rigidity, ONE, 15 * ONE);
        p.is_continental = is_continental;
        p.base_elevation = base_elevation;

        Cordic::Polar pol = Cordic::vectorize(vx, vy);
        p.heading = pol.angle;
        p.speed = pol.magnitude;

        _plates.push_back(p);
    }

    // Add plate using polar coordinates: angle in fixed-point radians [-PI, PI] and speed in fixed-point
    void add_plate_polar(uint32_t id, int64_t cx, int64_t cy,
                         int64_t angle_rad_fixed, int64_t speed_fixed,
                         int64_t thickness = 35 * ONE, int64_t rigidity = 4 * ONE,
                         bool is_continental = true, int64_t base_elevation = 32 * ONE) {
        Cordic::Vec2 dir = Cordic::from_angle(angle_rad_fixed);
        int64_t vx = mul_fixed(dir.x, speed_fixed);
        int64_t vy = mul_fixed(dir.y, speed_fixed);

        TectonicPlate p;
        p.id = id;
        p.cx = cx;
        p.cy = cy;
        p.vx = vx;
        p.vy = vy;
        p.heading = angle_rad_fixed;
        p.speed = speed_fixed;
        p.thickness = std::clamp(thickness, 10 * ONE, 80 * ONE);
        p.rigidity = std::clamp(rigidity, ONE, 15 * ONE);
        p.is_continental = is_continental;
        p.base_elevation = base_elevation;

        _plates.push_back(p);
    }

    // Add plate with angle specified in integer degrees [0 .. 360]
    void add_plate_deg(uint32_t id, int64_t cx, int64_t cy,
                       int32_t angle_deg, int64_t speed_fixed,
                       int64_t thickness = 35 * ONE, int64_t rigidity = 4 * ONE,
                       bool is_continental = true, int64_t base_elevation = 32 * ONE) {
        int64_t angle_rad = div_fixed(static_cast<int64_t>(angle_deg) * PI_FIXED, 180 * ONE);
        add_plate_polar(id, cx, cy, angle_rad, speed_fixed, thickness, rigidity, is_continental, base_elevation);
    }

    // --- Preset Generators ---

    // 2-Plate Continental Orogeny (e.g. India-Eurasia style Himalayan collision)
    void setup_preset_collision_2plates(int64_t speed_a = 2 * ONE, int64_t speed_b = 3 * ONE,
                                        int64_t angle_a_deg = 15, int64_t angle_b_deg = 195,
                                        int64_t thickness_a = 38 * ONE, int64_t thickness_b = 45 * ONE,
                                        int64_t rigidity_a = 4 * ONE, int64_t rigidity_b = 6 * ONE) {
        clear_plates();
        int64_t w_fixed = static_cast<int64_t>(_world_w) * ONE;
        int64_t h_fixed = static_cast<int64_t>(_world_h) * ONE;

        // Plate 1: Western Continent
        add_plate_deg(1, mul_fixed(w_fixed, 24000), mul_fixed(h_fixed, 32768),
                      static_cast<int32_t>(angle_a_deg), speed_a, thickness_a, rigidity_a, true, 30 * ONE);

        // Plate 2: Eastern Continent
        add_plate_deg(2, mul_fixed(w_fixed, 42000), mul_fixed(h_fixed, 32768),
                      static_cast<int32_t>(angle_b_deg), speed_b, thickness_b, rigidity_b, true, 34 * ONE);

        // Surrounding Oceans
        add_plate(3, mul_fixed(w_fixed, 6000), mul_fixed(h_fixed, 32768), ONE, 0, 15 * ONE, 2 * ONE, false, 12 * ONE);
        add_plate(4, mul_fixed(w_fixed, 58000), mul_fixed(h_fixed, 32768), -ONE, 0, 15 * ONE, 2 * ONE, false, 12 * ONE);
        add_plate(5, mul_fixed(w_fixed, 32768), mul_fixed(h_fixed, 6000), 0, ONE, 15 * ONE, 2 * ONE, false, 12 * ONE);
        add_plate(6, mul_fixed(w_fixed, 32768), mul_fixed(h_fixed, 58000), 0, -ONE, 15 * ONE, 2 * ONE, false, 12 * ONE);

        build();
    }

    // 3-Plate Triple Junction (e.g. Afar/Pacific-Nazca-Cocos style)
    void setup_preset_triple_junction(int64_t speed = 2 * ONE) {
        clear_plates();
        int64_t w_fixed = static_cast<int64_t>(_world_w) * ONE;
        int64_t h_fixed = static_cast<int64_t>(_world_h) * ONE;

        int64_t cx = w_fixed / 2;
        int64_t cy = h_fixed / 2;

        add_plate_deg(1, cx - mul_fixed(w_fixed, 9000), cy - mul_fixed(h_fixed, 7000), 45, speed, 40 * ONE, 5 * ONE, true, 32 * ONE);
        add_plate_deg(2, cx + mul_fixed(w_fixed, 9000), cy - mul_fixed(h_fixed, 7000), 135, speed, 36 * ONE, 4 * ONE, true, 30 * ONE);
        add_plate_deg(3, cx, cy + mul_fixed(h_fixed, 11000), 270, speed + (ONE / 2), 44 * ONE, 6 * ONE, true, 34 * ONE);

        // Surrounding Oceans
        add_plate(4, mul_fixed(w_fixed, 6000), mul_fixed(h_fixed, 32768), ONE, 0, 15 * ONE, 2 * ONE, false, 12 * ONE);
        add_plate(5, mul_fixed(w_fixed, 58000), mul_fixed(h_fixed, 32768), -ONE, 0, 15 * ONE, 2 * ONE, false, 12 * ONE);
        add_plate(6, mul_fixed(w_fixed, 32768), mul_fixed(h_fixed, 6000), 0, ONE, 15 * ONE, 2 * ONE, false, 12 * ONE);
        add_plate(7, mul_fixed(w_fixed, 32768), mul_fixed(h_fixed, 58000), 0, -ONE, 15 * ONE, 2 * ONE, false, 12 * ONE);

        build();
    }

    const std::vector<TectonicPlate>& get_plates() const { return _plates; }
    const std::vector<CollisionZone>& get_collision_zones() const { return _collision_zones; }
    const std::vector<GlacialTarn>& get_glacial_tarns() const { return _glacial_tarns; }

    // Run collision analysis and compute all fold physics structures
    void build() {
        _collision_zones.clear();
        _glacial_tarns.clear();

        if (_plates.empty()) {
            _build_default_plates();
        }

        size_t n = _plates.size();
        for (size_t i = 0; i < n; ++i) {
            for (size_t j = i + 1; j < n; ++j) {
                _analyze_plate_pair(static_cast<int32_t>(i), static_cast<int32_t>(j));
            }
        }
    }

    // Evaluates tectonic elevation, alpine snowcaps, rock peaks, glacial tarns, and rain-shadow climate
    TectonicSample sample(int64_t x_fixed, int64_t y_fixed) const {
        if (_plates.empty()) {
            TectonicSample s;
            s.elevation = 20 * ONE;
            s.biome = 0;
            return s;
        }

        // 1. Organic domain-warped fracture coordinates for realistic fault geometry
        int64_t warp_x = SimMath::fbm2d(x_fixed / 28, y_fixed / 28, _seed + 101, 3);
        int64_t warp_y = SimMath::fbm2d((x_fixed + 50 * ONE) / 28, (y_fixed + 50 * ONE) / 28, _seed + 202, 3);
        int64_t px_warped = x_fixed + mul_fixed(warp_x, 14 * ONE);
        int64_t py_warped = y_fixed + mul_fixed(warp_y, 14 * ONE);

        // 2. Identify nearest (primary) and 2nd nearest (neighboring) plates
        int32_t p1_idx = 0;
        int32_t p2_idx = -1;
        int64_t min_d1_sq = 0x7FFFFFFFFFFFFFFF;
        int64_t min_d2_sq = 0x7FFFFFFFFFFFFFFF;

        for (size_t i = 0; i < _plates.size(); ++i) {
            int64_t dx = px_warped - _plates[i].cx;
            int64_t dy = py_warped - _plates[i].cy;
            int64_t d2 = mul_fixed(dx, dx) + mul_fixed(dy, dy);

            if (d2 < min_d1_sq) {
                min_d2_sq = min_d1_sq;
                p2_idx = p1_idx;
                min_d1_sq = d2;
                p1_idx = static_cast<int32_t>(i);
            } else if (d2 < min_d2_sq) {
                min_d2_sq = d2;
                p2_idx = static_cast<int32_t>(i);
            }
        }

        const TectonicPlate& p1 = _plates[p1_idx];
        bool is_p2_valid = (p2_idx >= 0 && p2_idx < static_cast<int32_t>(_plates.size()));

        // 3. Baseline Elevation based on Crustal Type
        int64_t base_elev = 0;
        bool is_land = false;

        if (!p1.is_continental) {
            // Primary plate is Oceanic
            int64_t ocean_depth_noise = SimMath::fbm2d(x_fixed / 32, y_fixed / 32, _seed + 303, 2);
            base_elev = (14 * ONE) + mul_fixed(ocean_depth_noise, 6 * ONE); // Ocean depth 8..20
            is_land = false;
        } else {
            // Primary plate is Continental
            is_land = true;
            // Interior elevation rises with distance from oceanic margins
            int64_t interior_rise = 0;
            if (is_p2_valid && !_plates[p2_idx].is_continental) {
                // Near oceanic plate: gradual coastal shelf
                int64_t dist_1 = sqrt_fixed(min_d1_sq);
                int64_t dist_2 = sqrt_fixed(min_d2_sq);
                int64_t margin_dist = dist_2 - dist_1; // Distance from continental-oceanic boundary
                int64_t shelf_factor = std::clamp(div_fixed(margin_dist, 16 * ONE), 0LL, ONE);
                interior_rise = mul_fixed(shelf_factor, 14 * ONE);
            } else {
                interior_rise = 12 * ONE;
            }
            base_elev = p1.base_elevation + interior_rise;
        }

        // 4. Biot Buckling Fold Orogeny & Collision Zone Dynamics
        int64_t total_fold_uplift = 0;
        int64_t total_trench_depression = 0;
        int64_t rain_shadow_intensity = 0;

        for (const auto& zone : _collision_zones) {
            bool affects = (zone.plate_a_idx == p1_idx && zone.plate_b_idx == p2_idx) ||
                           (zone.plate_b_idx == p1_idx && zone.plate_a_idx == p2_idx);
            if (!affects && zone.belt_width > 0) {
                // Check if point is physically within boundary belt distance
                int64_t to_mid_x = px_warped - zone.mid_x;
                int64_t to_mid_y = py_warped - zone.mid_y;
                int64_t d_mid_sq = mul_fixed(to_mid_x, to_mid_x) + mul_fixed(to_mid_y, to_mid_y);
                if (d_mid_sq > mul_fixed(zone.belt_width * 2, zone.belt_width * 2)) {
                    continue;
                }
            }

            // Signed perpendicular distance to fault line (positive towards Plate B)
            int64_t px_rel = px_warped - zone.mid_x;
            int64_t py_rel = py_warped - zone.mid_y;
            int64_t d_perp = mul_fixed(px_rel, zone.normal_x) + mul_fixed(py_rel, zone.normal_y);
            int64_t d_para = mul_fixed(px_rel, zone.tangent_x) + mul_fixed(py_rel, zone.tangent_y);

            if (zone.is_convergent) {
                // --- Fold & Thrust Belt Buckling ---
                int64_t d_shifted = d_perp - zone.thrust_offset;
                int64_t decay_width = (d_shifted >= 0) ? zone.decay_underthrust : zone.decay_overriding;
                if (decay_width <= 0) decay_width = zone.belt_width;

                int64_t abs_d = abs_fixed(d_shifted);
                if (abs_d < decay_width) {
                    // Quintic Bell Envelope
                    int64_t norm_d = div_fixed(abs_d, decay_width);
                    int64_t env = SimMath::quintic_smooth(ONE - norm_d);

                    // Multi-harmonic fold train (anticline / syncline wave series)
                    // Primary harmonic
                    int64_t phase1 = div_fixed(mul_fixed(d_shifted, TWO_PI), zone.wavelength_1);
                    int64_t cos1 = Cordic::cos(phase1);
                    int64_t sin2 = Cordic::sin(phase1 * 2);
                    // Skewed vergent waveform (steep forelimb, gentle backlimb)
                    int64_t wave1 = cos1 + mul_fixed(zone.vergence, mul_fixed(sin2, 16384));

                    // Secondary parasitic folds (S/Z/M folds on the limbs)
                    int64_t phase2 = div_fixed(mul_fixed(d_shifted, TWO_PI), zone.wavelength_2) + mul_fixed(zone.vergence, 51471);
                    int64_t wave2 = mul_fixed(Cordic::cos(phase2), 22937); // 0.35 weight

                    // Tertiary crenulation / schistosity ridges
                    int64_t phase3 = div_fixed(mul_fixed(d_shifted, TWO_PI), zone.wavelength_3);
                    int64_t wave3 = mul_fixed(Cordic::cos(phase3), 9830);  // 0.15 weight

                    // Along-strike structural modulation (en echelon plunging folds, saddles, culminations)
                    int64_t strike_noise = SimMath::fbm2d(d_para / 32, 0, _seed + 404, 2);
                    int64_t strike_mod = ONE + mul_fixed(strike_noise, 18000);

                    int64_t total_wave = wave1 + wave2 + wave3;
                    int64_t fold_val = mul_fixed(env, mul_fixed(zone.max_amplitude, total_wave));
                    fold_val = mul_fixed(fold_val, strike_mod);

                    if (fold_val > total_fold_uplift) {
                        total_fold_uplift = fold_val;
                    }

                    // Rain-shadow calculation: leeward basin behind high mountain belts
                    if (zone.max_amplitude >= 20 * ONE && d_perp < -zone.wavelength_1 / 2) {
                        int64_t shadow_env = SimMath::hermite_smooth(ONE - std::min(ONE, div_fixed(abs_fixed(d_perp), zone.belt_width * 2)));
                        int64_t shadow = mul_fixed(shadow_env, 28 * ONE);
                        if (shadow > rain_shadow_intensity) rain_shadow_intensity = shadow;
                    }
                } else if (abs_d < decay_width + (16 * ONE)) {
                    // Foreland Basin flexural depression / moat
                    int64_t moat_norm = div_fixed(abs_d - decay_width, 16 * ONE);
                    int64_t moat_env = mul_fixed(SimMath::hermite_smooth(moat_norm), SimMath::hermite_smooth(ONE - moat_norm));
                    total_trench_depression += mul_fixed(moat_env, 4 * ONE);
                }

                // Subduction Trench on oceanic side
                if (zone.is_subduction) {
                    bool on_oceanic_side = (zone.plate_a_idx == p1_idx) ? !_plates[zone.plate_a_idx].is_continental : !_plates[zone.plate_b_idx].is_continental;
                    if (on_oceanic_side && abs_d < (14 * ONE)) {
                        int64_t tr_env = SimMath::hermite_smooth(ONE - div_fixed(abs_d, 14 * ONE));
                        total_trench_depression += mul_fixed(tr_env, 12 * ONE);
                    }
                }
            } else {
                // --- Divergent / Rift Valley Graben Dynamics ---
                int64_t abs_d = abs_fixed(d_perp);
                if (abs_d < (18 * ONE)) {
                    int64_t rift_env = SimMath::quintic_smooth(ONE - div_fixed(abs_d, 18 * ONE));
                    int64_t rift_depth = mul_fixed(abs_fixed(zone.convergence_rate), 12 * ONE);
                    total_trench_depression += mul_fixed(rift_env, rift_depth);
                }
            }
        }

        // 5. Alpine Ridge Sharpening & Lowland Rolling Topography
        int64_t combined_elev = base_elev + total_fold_uplift - total_trench_depression;

        if (total_fold_uplift > 14 * ONE) {
            // Jagged alpine arêtes and horn peaks
            int64_t alpine_ridge = SimMath::ridge_noise2d(x_fixed / 14, y_fixed / 14, _seed + 777, 3);
            combined_elev += mul_fixed(alpine_ridge, 16 * ONE);
        } else if (is_land) {
            // Lowland rolling hills
            int64_t lowland_fbm = SimMath::fbm2d(x_fixed / 24, y_fixed / 24, _seed + 555, 2);
            combined_elev += mul_fixed(lowland_fbm, 4 * ONE);
        }

        int64_t final_elevation = std::clamp(combined_elev, 2 * ONE, 95 * ONE);
        if (final_elevation < 22 * ONE) {
            is_land = false;
        }

        // 6. Climate, Temperature, Moisture & Rain-Shadow Model
        int64_t temperature = 65 * ONE;
        int64_t moisture = 55 * ONE;

        if (!is_land) {
            // Ocean climate
            temperature = 50 * ONE;
            moisture = 100 * ONE;
        } else if (final_elevation >= 58 * ONE) {
            // High altitude orographic precipitation & adiabatic lapse rate cooling
            int64_t alt_cooling = mul_fixed(final_elevation - (50 * ONE), 65536);
            temperature = std::max(10 * ONE, (65 * ONE) - alt_cooling);
            moisture = 75 * ONE; // Orographic snow/rain on mountains
        } else if (rain_shadow_intensity > 15 * ONE && final_elevation < 50 * ONE) {
            // Enclosed Rain-Shadow Desert Basin!
            temperature = 78 * ONE;
            moisture = std::max(12 * ONE, (45 * ONE) - rain_shadow_intensity);
        } else {
            // Coastal & lowland humidity
            int64_t m_noise = SimMath::fbm2d(x_fixed / 28, y_fixed / 28, _seed + 888, 2);
            moisture = (50 * ONE) + mul_fixed(m_noise, 22 * ONE);
            if (!is_p2_valid || !_plates[p2_idx].is_continental) {
                moisture += 15 * ONE; // Coastal humidity boost
            }
        }

        // 8. Biome Classification
        uint8_t biome_id = 3; // Grassland
        bool is_peak = false;
        bool is_snow = false;

        if (!is_land) {
            biome_id = 0; // Ocean
        } else if (final_elevation >= 74 * ONE) {
            biome_id = 7; // Mountain Peak (Grey bare rock)
            is_peak = true;
        } else if (final_elevation >= 58 * ONE || temperature <= 25 * ONE) {
            biome_id = 1; // Snow / Glacier / Tundra (White)
            is_snow = true;
        } else if ((rain_shadow_intensity > 18 * ONE && final_elevation < 48 * ONE) || moisture < 24 * ONE) {
            biome_id = 2; // Desert / Arid Basin (Tan/Sand)
        } else if (moisture >= 68 * ONE && final_elevation <= 32 * ONE) {
            biome_id = 6; // Swamp / Coastal Wetland (Moss Green)
        } else if (moisture >= 58 * ONE) {
            biome_id = 4; // Temperate Forest (Emerald Green)
        } else {
            biome_id = 3; // Grassland (Fresh Green)
        }

        // 9. Mineral Province Determination from Structural Geology
        uint8_t min_prov = 0;
        if (biome_id == 2) {
            min_prov = 1; // Nitrogen saltpetre deposits in dry basins
        } else if (is_peak || is_snow || total_fold_uplift > 30 * ONE) {
            min_prov = 3; // Potassium / Heavy metals in volcanic/orogenic mountain roots
        } else if (biome_id == 6 || (!is_p2_valid || !_plates[p2_idx].is_continental)) {
            min_prov = 2; // Phosphorus in wetland and marine sedimentary deposits
        }

        TectonicSample s;
        s.elevation = final_elevation;
        s.temperature = temperature;
        s.moisture = moisture;
        s.biome = biome_id;
        s.is_land = is_land;
        s.is_peak = is_peak;
        s.is_snow = is_snow;
        s.is_glacial_tarn = false;
        s.is_rain_shadow = (rain_shadow_intensity > 18 * ONE);
        s.mineral_province = min_prov;
        s.plate_id = p1.id;

        return s;
    }

private:
    uint64_t _seed = 42;
    int32_t  _world_w = 512;
    int32_t  _world_h = 512;

    std::vector<TectonicPlate> _plates;
    std::vector<CollisionZone>  _collision_zones;
    std::vector<GlacialTarn>   _glacial_tarns;

    void _build_default_plates() {
        _plates.clear();
        int64_t w_fixed = static_cast<int64_t>(_world_w) * ONE;
        int64_t h_fixed = static_cast<int64_t>(_world_h) * ONE;
        SplitMix64 rng(_seed);

        // Randomize plates based on configuration for a more natural, varied world
        int num_plates = rng.next_range(config_num_plates_min, config_num_plates_max);
        
        // Calculate center cluster spread based on center bias
        int64_t spread_ratio = div_fixed((120 - std::clamp(config_center_bias, 0, 100)) * ONE, 100 * ONE);
        int64_t max_offset_x = std::clamp(mul_fixed(w_fixed / 2, spread_ratio), mul_fixed(w_fixed, 12000), mul_fixed(w_fixed, 28000));
        int64_t max_offset_y = std::clamp(mul_fixed(h_fixed / 2, spread_ratio), mul_fixed(h_fixed, 12000), mul_fixed(h_fixed, 28000));

        for (int i = 0; i < num_plates; ++i) {
            // First plate is guaranteed continental and near center for a playable main continent
            bool is_cont = (i == 0) ? true : (rng.next_range(0, 100) < config_continent_chance); 
            
            int64_t cx, cy;
            if (i == 0) {
                cx = (w_fixed / 2) + mul_fixed(w_fixed, rng.next_range(-6000, 6000));
                cy = (h_fixed / 2) + mul_fixed(h_fixed, rng.next_range(-6000, 6000));
            } else {
                int64_t off_x = mul_fixed(max_offset_x, (rng.next_range(-100, 100) * ONE) / 100);
                int64_t off_y = mul_fixed(max_offset_y, (rng.next_range(-100, 100) * ONE) / 100);
                cx = std::clamp((w_fixed / 2) + off_x, mul_fixed(w_fixed, 6000), mul_fixed(w_fixed, 59536));
                cy = std::clamp((h_fixed / 2) + off_y, mul_fixed(h_fixed, 6000), mul_fixed(h_fixed, 59536));
            }
            
            // Calculate unit vector pointing to center
            int64_t dx = (w_fixed / 2) - cx;
            int64_t dy = (h_fixed / 2) - cy;
            int64_t dist_sq = mul_fixed(dx, dx) + mul_fixed(dy, dy);
            int64_t dist = sqrt_fixed(dist_sq);
            
            int64_t dir_x = 0;
            int64_t dir_y = 0;
            if (dist > 0) {
                dir_x = div_fixed(dx, dist);
                dir_y = div_fixed(dy, dist);
            }
            
            // Random noise vector for variance
            int64_t noise_x = (rng.next_range(-100, 100) * ONE) / 100;
            int64_t noise_y = (rng.next_range(-100, 100) * ONE) / 100;
            
            int64_t noise_bias = std::max(0, 100 - config_center_bias);
            
            // Blend: mostly towards center, some random drift
            int64_t blend_x = mul_fixed(dir_x, (config_center_bias * ONE) / 100) + mul_fixed(noise_x, (noise_bias * ONE) / 100);
            int64_t blend_y = mul_fixed(dir_y, (config_center_bias * ONE) / 100) + mul_fixed(noise_y, (noise_bias * ONE) / 100);
                                  
            int64_t v_min = std::max(1, config_vel_min);
            int64_t v_max = std::max(v_min, static_cast<int64_t>(config_vel_max));
            int64_t speed = (rng.next_range(v_min, v_max) * ONE) / 100;
            int64_t vx = mul_fixed(blend_x, speed);
            int64_t vy = mul_fixed(blend_y, speed);
            
            int64_t thickness = is_cont ? rng.next_range(35, 52) * ONE : rng.next_range(12, 18) * ONE;
            int64_t rigidity = is_cont ? rng.next_range(4, 8) * ONE : rng.next_range(1, 3) * ONE;
            int64_t base_elev = is_cont ? rng.next_range(32, 42) * ONE : rng.next_range(10, 15) * ONE;
            
            add_plate(i + 1, cx, cy, vx, vy, thickness, rigidity, is_cont, base_elev);
        }
        
        // Add 4 static oceanic boundary plates outside edges to frame the world with ocean
        int64_t pad_x = mul_fixed(w_fixed, 6000);
        int64_t pad_y = mul_fixed(h_fixed, 6000);
        add_plate(num_plates + 1, -pad_x, h_fixed / 2, ONE / 2, 0, 15 * ONE, 2 * ONE, false, 10 * ONE);
        add_plate(num_plates + 2, w_fixed + pad_x, h_fixed / 2, -ONE / 2, 0, 15 * ONE, 2 * ONE, false, 10 * ONE);
        add_plate(num_plates + 3, w_fixed / 2, -pad_y, 0, ONE / 2, 15 * ONE, 2 * ONE, false, 10 * ONE);
        add_plate(num_plates + 4, w_fixed / 2, h_fixed + pad_y, 0, -ONE / 2, 15 * ONE, 2 * ONE, false, 10 * ONE);
    }

    void _analyze_plate_pair(int32_t idx_a, int32_t idx_b) {
        const TectonicPlate& a = _plates[idx_a];
        const TectonicPlate& b = _plates[idx_b];

        int64_t dx = b.cx - a.cx;
        int64_t dy = b.cy - a.cy;
        int64_t dist_sq = mul_fixed(dx, dx) + mul_fixed(dy, dy);
        if (dist_sq <= 0) return;

        int64_t dist = sqrt_fixed(dist_sq);
        if (dist <= 0) return;

        CollisionZone zone;
        zone.plate_a_id = a.id;
        zone.plate_b_id = b.id;
        zone.plate_a_idx = idx_a;
        zone.plate_b_idx = idx_b;
        zone.distance_ab = dist;

        zone.mid_x = (a.cx + b.cx) / 2;
        zone.mid_y = (a.cy + b.cy) / 2;

        // Normal unit vector from A to B
        zone.normal_x = div_fixed(dx, dist);
        zone.normal_y = div_fixed(dy, dist);

        // Tangent unit vector along the boundary
        zone.tangent_x = -zone.normal_y;
        zone.tangent_y = zone.normal_x;

        // Relative velocity: v_rel = v_a - v_b
        int64_t rel_vx = a.vx - b.vx;
        int64_t rel_vy = a.vy - b.vy;

        // Convergence rate = v_rel . normal (Positive = crashing together)
        zone.convergence_rate = mul_fixed(rel_vx, zone.normal_x) + mul_fixed(rel_vy, zone.normal_y);
        zone.shear_rate = abs_fixed(mul_fixed(rel_vx, zone.tangent_x) + mul_fixed(rel_vy, zone.tangent_y));

        zone.is_convergent = (zone.convergence_rate > (ONE / 8));
        zone.is_subduction = (a.is_continental != b.is_continental) && zone.is_convergent;

        if (zone.is_convergent) {
            // --- Biot Buckling Folding Physics ---
            int64_t h_eff = (a.thickness + b.thickness) / 2;
            int64_t r_eff = (a.rigidity + b.rigidity) / 2;

            // Biot Buckling Dominant Wavelength: lambda = 2*pi * h * (R / R_0)^(1/3)
            // Fixed point approximation: lambda_1 scales with h_eff and r_eff
            int64_t lambda_1 = mul_fixed(h_eff, (2 * ONE) + mul_fixed(r_eff, 16384));
            lambda_1 = std::clamp(lambda_1, 14 * ONE, 64 * ONE);
            zone.wavelength_1 = lambda_1;

            // Parasitic harmonic folds (Pumpelly's Rule: lambda_2 = lambda_1 / 3, lambda_3 = lambda_1 / 7)
            zone.wavelength_2 = std::max(4 * ONE, lambda_1 / 3);
            zone.wavelength_3 = std::max(2 * ONE, lambda_1 / 7);

            // Orogenic Amplitude: scales with convergence rate and crustal thickness
            int64_t crust_scale = (a.is_continental && b.is_continental) ? (32 * ONE) : (22 * ONE);
            int64_t amp = mul_fixed(zone.convergence_rate, mul_fixed(h_eff, 18000));
            zone.max_amplitude = std::clamp(amp, 8 * ONE, 52 * ONE);

            // Belt Width: total deformation span across the orogen
            zone.belt_width = mul_fixed(lambda_1, (2 * ONE) + mul_fixed(zone.convergence_rate, 8192));

            // Vergence & Overthrust Asymmetry
            // v_a_proj = v_a . normal, v_b_proj = -v_b . normal
            int64_t va_proj = mul_fixed(a.vx, zone.normal_x) + mul_fixed(a.vy, zone.normal_y);
            int64_t vb_proj = -(mul_fixed(b.vx, zone.normal_x) + mul_fixed(b.vy, zone.normal_y));
            int64_t push_sum = abs_fixed(va_proj) + abs_fixed(vb_proj) + 1;
            int64_t verg = div_fixed(va_proj - vb_proj, push_sum);
            zone.vergence = std::clamp(verg, -ONE, ONE);

            // Lateral crest offset from overthrusting
            zone.thrust_offset = mul_fixed(zone.wavelength_1 / 4, zone.vergence);

            // Asymmetric fold envelope slopes
            int64_t abs_verg = abs_fixed(zone.vergence);
            zone.decay_overriding = mul_fixed(zone.belt_width, ONE + mul_fixed(abs_verg, 28000)); // Broad plateau
            zone.decay_underthrust = mul_fixed(zone.belt_width, ONE - mul_fixed(abs_verg, 18000));// Steep front
        }

        _collision_zones.push_back(zone);
    }

    void _place_glacial_cirques_and_oases() {
        _glacial_tarns.clear();
    }
};

} // namespace Generation
