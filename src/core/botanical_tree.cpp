#include "botanical_tree.h"
#include <cmath>
#include <algorithm>

namespace exofarm {

// ==============================================================================
// Species Presets
// ==============================================================================
BotanicalParams BotanicalParams::get_preset(BotanicalSpecies species) {
    BotanicalParams p;
    switch (species) {
        case BotanicalSpecies::CONIFER:
            p.type_name = "Conifer (Pine / Spruce)";
            p.max_height = 13 * ONE + 32768;      // 13.5m
            p.base_radius = (28 * ONE) / 100;     // 0.28m
            p.trunk_taper = (85 * ONE) / 100;     // 0.85
            p.num_tiers = 8;
            p.branches_per_tier = 5;
            p.helical_branching = false;
            p.deliquescent_fork = false;
            p.crown_start_ratio = (18 * ONE) / 100;
            p.branch_angle_deg = 68 * ONE;
            p.max_branch_len = (45 * ONE) / 10;   // 4.5m
            p.crown_shape_exp = (85 * ONE) / 100; // Conical
            p.droop_strength = (50 * ONE) / 100;
            p.tip_hook_strength = (40 * ONE) / 100;
            p.trunk_crookedness = (8 * ONE) / 100;
            p.secondary_twigs = true;
            p.weeping_twigs = false;
            p.buttress_flare = (15 * ONE) / 10;
            p.radial_segments_trunk = 8;
            p.radial_segments_branch = 5;
            p.card_length = (21 * ONE) / 10;      // 2.1m
            p.card_width = (15 * ONE) / 10;       // 1.5m
            p.cladoptosis_threshold = (22 * ONE) / 100;
            p.phototropism_vertical = (75 * ONE) / 100;
            p.phototropism_lateral = (25 * ONE) / 100;
            p.wander_tortuosity = (8 * ONE) / 100;
            p.shoot_prune_threshold = (32 * ONE) / 100;
            p.stem_r = 0.32f; p.stem_g = 0.22f; p.stem_b = 0.14f;
            p.leaf_r = 0.16f; p.leaf_g = 0.40f; p.leaf_b = 0.16f;
            break;

        case BotanicalSpecies::BROADLEAF_OAK:
            p.type_name = "Broadleaf Oak";
            p.max_height = 11 * ONE + 32768;      // 11.5m
            p.base_radius = (40 * ONE) / 100;     // 0.40m
            p.trunk_taper = (75 * ONE) / 100;     // 0.75
            p.num_tiers = 7;
            p.branches_per_tier = 4;
            p.helical_branching = false;
            p.deliquescent_fork = false;
            p.crown_start_ratio = (30 * ONE) / 100;
            p.branch_angle_deg = 48 * ONE;
            p.max_branch_len = (52 * ONE) / 10;   // 5.2m
            p.crown_shape_exp = (45 * ONE) / 100; // Spreading / Domed
            p.droop_strength = (35 * ONE) / 100;
            p.tip_hook_strength = (30 * ONE) / 100;
            p.trunk_crookedness = (28 * ONE) / 100;
            p.secondary_twigs = true;
            p.weeping_twigs = false;
            p.buttress_flare = (18 * ONE) / 10;
            p.radial_segments_trunk = 8;
            p.radial_segments_branch = 5;
            p.card_length = (24 * ONE) / 10;      // 2.4m
            p.card_width = (18 * ONE) / 10;       // 1.8m
            p.cladoptosis_threshold = (20 * ONE) / 100;
            p.phototropism_vertical = (28 * ONE) / 100;
            p.phototropism_lateral = (85 * ONE) / 100;
            p.wander_tortuosity = (52 * ONE) / 100;
            p.shoot_prune_threshold = (42 * ONE) / 100;
            p.stem_r = 0.38f; p.stem_g = 0.25f; p.stem_b = 0.15f;
            p.leaf_r = 0.24f; p.leaf_g = 0.54f; p.leaf_b = 0.16f;
            break;

        case BotanicalSpecies::SLENDER_BIRCH:
            p.type_name = "Slender Birch";
            p.max_height = 12 * ONE + 52428;      // 12.8m
            p.base_radius = (18 * ONE) / 100;     // 0.18m
            p.trunk_taper = (82 * ONE) / 100;     // 0.82
            p.num_tiers = 7;
            p.branches_per_tier = 3;
            p.total_spiral_branches = 26;
            p.helical_branching = true;
            p.deliquescent_fork = true;
            p.fork_height_ratio = (58 * ONE) / 100;
            p.crown_start_ratio = (20 * ONE) / 100;
            p.branch_angle_deg = 38 * ONE;
            p.max_branch_len = (34 * ONE) / 10;   // 3.4m
            p.crown_shape_exp = (65 * ONE) / 100;
            p.droop_strength = (70 * ONE) / 100;
            p.tip_hook_strength = 0;
            p.trunk_crookedness = (24 * ONE) / 100;
            p.secondary_twigs = true;
            p.weeping_twigs = true;
            p.buttress_flare = (135 * ONE) / 100;
            p.radial_segments_trunk = 8;
            p.radial_segments_branch = 5;
            p.card_length = (145 * ONE) / 100;    // 1.45m
            p.card_width = (110 * ONE) / 100;     // 1.10m
            p.cladoptosis_threshold = (22 * ONE) / 100;
            p.phototropism_vertical = (70 * ONE) / 100;
            p.phototropism_lateral = (40 * ONE) / 100;
            p.wander_tortuosity = (45 * ONE) / 100;
            p.shoot_prune_threshold = (35 * ONE) / 100;
            p.stem_r = 0.42f; p.stem_g = 0.35f; p.stem_b = 0.28f;
            p.leaf_r = 0.32f; p.leaf_g = 0.62f; p.leaf_b = 0.20f;
            break;

        case BotanicalSpecies::DEAD_SNAG:
            p.type_name = "Dead Snag";
            p.max_height = (85 * ONE) / 10;       // 8.5m
            p.base_radius = (30 * ONE) / 100;     // 0.30m
            p.trunk_taper = (90 * ONE) / 100;     // 0.90
            p.num_tiers = 5;
            p.branches_per_tier = 3;
            p.helical_branching = false;
            p.deliquescent_fork = false;
            p.crown_start_ratio = (25 * ONE) / 100;
            p.branch_angle_deg = 65 * ONE;
            p.max_branch_len = (26 * ONE) / 10;   // 2.6m
            p.crown_shape_exp = (50 * ONE) / 100;
            p.droop_strength = (20 * ONE) / 100;
            p.tip_hook_strength = 0;
            p.trunk_crookedness = (22 * ONE) / 100;
            p.secondary_twigs = false;
            p.weeping_twigs = false;
            p.buttress_flare = (14 * ONE) / 10;
            p.radial_segments_trunk = 8;
            p.radial_segments_branch = 5;
            p.card_length = (120 * ONE) / 100;    // 1.2m
            p.card_width = (90 * ONE) / 100;      // 0.9m
            p.cladoptosis_threshold = (45 * ONE) / 100;
            p.phototropism_vertical = (40 * ONE) / 100;
            p.phototropism_lateral = (60 * ONE) / 100;
            p.wander_tortuosity = (22 * ONE) / 100;
            p.shoot_prune_threshold = (50 * ONE) / 100;
            p.stem_r = 0.45f; p.stem_g = 0.38f; p.stem_b = 0.30f;
            p.leaf_r = 0.48f; p.leaf_g = 0.40f; p.leaf_b = 0.28f;
            break;
    }
    return p;
}

// ==============================================================================
// Math Helpers
// ==============================================================================
int64_t BotanicalTreeGenerator::get_ground_y(const Vec3Fixed& pos, const Vec3Fixed& g_pos, const Vec3Fixed& g_norm) {
    if (abs_fixed(g_norm.y) < 65) return g_pos.y;
    int64_t num = mul_fixed(pos.x - g_pos.x, g_norm.x) + mul_fixed(pos.z - g_pos.z, g_norm.z);
    return g_pos.y - div_fixed(num, g_norm.y);
}

int64_t BotanicalTreeGenerator::dist_segment_to_segment(
    const Vec3Fixed& p1, const Vec3Fixed& q1,
    const Vec3Fixed& p2, const Vec3Fixed& q2
) {
    Vec3Fixed u = q1 - p1;
    Vec3Fixed v = q2 - p2;
    Vec3Fixed w = p1 - p2;
    int64_t a = u.dot(u);
    int64_t b = u.dot(v);
    int64_t c = v.dot(v);
    int64_t d = u.dot(w);
    int64_t e = v.dot(w);
    int64_t D = mul_fixed(a, c) - mul_fixed(b, b);
    int64_t sc = 0, sN = 0, sD = D;
    int64_t tc = 0, tN = 0, tD = D;

    if (a < 16 && c < 16) return (p1 - p2).length();
    if (a < 16) {
        int64_t t_val = div_fixed(e, std::max<int64_t>(16, c));
        t_val = std::clamp<int64_t>(t_val, 0, ONE);
        return (p1 - (p2 + v * t_val)).length();
    }
    if (c < 16) {
        int64_t s_val = div_fixed(-d, std::max<int64_t>(16, a));
        s_val = std::clamp<int64_t>(s_val, 0, ONE);
        return ((p1 + u * s_val) - p2).length();
    }

    if (D < 64) {
        sN = 0; sD = ONE; tN = e; tD = c;
    } else {
        sN = mul_fixed(b, e) - mul_fixed(c, d);
        tN = mul_fixed(a, e) - mul_fixed(b, d);
        if (sN < 0) {
            sN = 0; tN = e; tD = c;
        } else if (sN > sD) {
            sN = sD; tN = e + b; tD = c;
        }
    }

    if (tN < 0) {
        tc = 0;
        if (-d < 0) sc = 0;
        else if (-d > a) sc = ONE;
        else sc = div_fixed(-d, std::max<int64_t>(16, a));
    } else if (tN > tD) {
        tc = ONE;
        if ((-d + b) < 0) sc = 0;
        else if ((-d + b) > a) sc = ONE;
        else sc = div_fixed(-d + b, std::max<int64_t>(16, a));
    } else {
        tc = div_fixed(tN, std::max<int64_t>(16, tD));
        sc = div_fixed(sN, std::max<int64_t>(16, sD));
    }

    Vec3Fixed dP = w + (u * sc) - (v * tc);
    return dP.length();
}

static BotanicalNode sample_chain_at_height(const std::vector<BotanicalNode>& chain, int64_t target_h) {
    BotanicalNode def_node;
    if (chain.empty()) return def_node;
    if (chain.size() == 1 || target_h <= chain[0].pos.y) return chain[0];
    if (target_h >= chain.back().pos.y) return chain.back();

    for (size_t i = 0; i < chain.size() - 1; ++i) {
        int64_t y0 = chain[i].pos.y;
        int64_t y1 = chain[i + 1].pos.y;
        if ((target_h >= y0 && target_h <= y1) || (target_h >= y1 && target_h <= y0)) {
            int64_t dy = y1 - y0;
            int64_t t = (abs_fixed(dy) > 16) ? div_fixed(target_h - y0, dy) : 32768;
            t = std::clamp<int64_t>(t, 0, ONE);
            BotanicalNode res;
            res.pos = chain[i].pos + (chain[i + 1].pos - chain[i].pos) * t;
            res.radius = chain[i].radius + mul_fixed(chain[i + 1].radius - chain[i].radius, t);
            res.dir = (chain[i].dir + (chain[i + 1].dir - chain[i].dir) * t).normalized();
            return res;
        }
    }
    return chain.back();
}

// ==============================================================================
// Pass 1: Architecture Generation & Candidate Cluster Placement
// ==============================================================================
BotanicalCandidates BotanicalTreeGenerator::generate_candidates(
    int64_t seed_val,
    BotanicalSpecies species,
    int64_t growth_fixed,
    const Vec3Fixed& ground_pos,
    const Vec3Fixed& ground_normal,
    const Vec3Fixed& sun_dir,
    const std::vector<TreeObstacle>& obstacles,
    int64_t cladoptosis_override,
    int64_t lateral_bias_override,
    int64_t vertical_bias_override
) {
    BotanicalCandidates cands;
    if (growth_fixed <= 0) return cands;

    cands.species = species;
    cands.seed_val = seed_val;
    cands.ground_pos = ground_pos;
    cands.ground_normal = ground_normal;
    cands.cfg = BotanicalParams::get_preset(species);
    if (cladoptosis_override >= 0) {
        cands.cfg.cladoptosis_threshold = cladoptosis_override;
        cands.cfg.shoot_prune_threshold = mul_fixed(cladoptosis_override, double_to_fixed(1.5));
    }
    if (lateral_bias_override >= 0) cands.cfg.phototropism_lateral = lateral_bias_override;
    if (vertical_bias_override >= 0) cands.cfg.phototropism_vertical = vertical_bias_override;
    cands.g = fixed_to_double(growth_fixed);

    Vec3Fixed sun_norm = sun_dir.normalized();
    if (sun_norm.length_sq() < 16) sun_norm = Vec3Fixed(32768, 46341, 32768).normalized();
    cands.sun_norm = sun_norm;
    Vec3Fixed sun_horiz(sun_norm.x, 0, sun_norm.z);
    sun_horiz = (sun_horiz.length_sq() > 16) ? sun_horiz.normalized() : Vec3Fixed(46341, 0, 46341).normalized();

    double surge_norm = (1.0 - std::exp(-3.2 * cands.g)) / (1.0 - std::exp(-3.2));
    double tree_h_d = std::max(0.25, fixed_to_double(cands.cfg.max_height) * (0.10 + 0.90 * surge_norm));
    cands.tree_height = double_to_fixed(tree_h_d);

    double caliper_growth = std::pow(cands.g, 0.75) * (0.18 + 0.82 * std::pow(cands.g, 1.25));
    double base_r_d = std::max(0.008, fixed_to_double(cands.cfg.base_radius) * caliper_growth);
    cands.base_radius = double_to_fixed(base_r_d);

    // 1. Trunk Spline
    int trunk_segs = 10;
    cands.trunk_chain.reserve(trunk_segs + 1);

    Vec3Fixed cur_pos = ground_pos;
    Vec3Fixed cur_dir = (ground_normal * double_to_fixed(0.35) + Vec3Fixed(0, ONE, 0) * double_to_fixed(0.65)).normalized();

    int64_t max_spread_sq = 0;
    int collisions_avoided = 0;

    int fork_s = std::clamp((int)((double)trunk_segs * fixed_to_double(cands.cfg.fork_height_ratio)), 4, trunk_segs - 2);
    Vec3Fixed fork_attach_pos = ground_pos;
    int64_t fork_limb_radius = 0;
    Vec3Fixed fork_horiz_dir(ONE, 0, 0);
    Vec3Fixed fork_limb_dir(0, ONE, 0);

    BotanicalNode t_root;
    t_root.pos = cur_pos;
    t_root.radius = mul_fixed(cands.base_radius, cands.cfg.buttress_flare);
    t_root.dir = cur_dir;
    t_root.branch_order = 0;
    t_root.tier_idx = 0;
    t_root.uv_v = 0;
    t_root.light_exposure = ONE;
    cands.trunk_chain.push_back(t_root);

    double h_step = tree_h_d / (double)trunk_segs;

    for (int s = 1; s <= trunk_segs; ++s) {
        double s_frac = (double)s / (double)trunk_segs;

        // Organic trunk curvature, helical spiral & wander with continuous sinuous sweep
        double s_curve_phase = s_frac * 6.2831853 * 0.85 + (double)(seed_val % 47);
        double s_curve_az = det_rand_float(seed_val, 77, 1) * 6.2831853;
        Vec3Fixed s_curve_dir = Vec3Fixed(double_to_fixed(std::cos(s_curve_az)), 0, double_to_fixed(std::sin(s_curve_az))).normalized();
        Vec3Fixed s_curve_offset = s_curve_dir * double_to_fixed(std::sin(s_curve_phase) * fixed_to_double(cands.cfg.trunk_crookedness) * 0.55);

        double wx = (det_rand_float(seed_val, s, 10, 1) - 0.5) * fixed_to_double(cands.cfg.trunk_crookedness) * 0.75;
        double wz = (det_rand_float(seed_val, s, 10, 2) - 0.5) * fixed_to_double(cands.cfg.trunk_crookedness) * 0.75;
        Vec3Fixed wander_jitter = Vec3Fixed(double_to_fixed(wx), 0, double_to_fixed(wz));

        // Steer trunk towards sun + slight negative gravitropism with expressive organic lean
        double up_weight = std::lerp(1.45, 1.05, s_frac);
        Vec3Fixed steer = (Vec3Fixed(0, ONE, 0) * double_to_fixed(up_weight) + sun_horiz * double_to_fixed(0.22) + s_curve_offset + wander_jitter).normalized();

        // Deliquescent fork recoil: trunk leans slightly away from the fork limb
        if (cands.cfg.deliquescent_fork && s > fork_s) {
            steer = (steer - fork_horiz_dir * double_to_fixed(0.22)).normalized();
        }

        Vec3Fixed next_pos = cur_pos + steer * double_to_fixed(h_step);

        int64_t gy = get_ground_y(next_pos, ground_pos, ground_normal);
        if (next_pos.y < gy + 5243) {
            next_pos.y = gy + 5243;
            steer = (next_pos - cur_pos).normalized();
        }

        double r_taper = (1.0 - s_frac * fixed_to_double(cands.cfg.trunk_taper));
        double r_d = base_r_d * std::max(0.06, r_taper);

        if (s_frac < 0.20) {
            double flare_norm = (0.20 - s_frac) / 0.20;
            r_d *= (1.0 + flare_norm * flare_norm * (fixed_to_double(cands.cfg.buttress_flare) - 1.0));
        }

        if (cands.cfg.deliquescent_fork && s >= fork_s) {
            r_d *= 0.74;
        }

        BotanicalNode t_node;
        t_node.pos = next_pos;
        t_node.dir = steer;
        t_node.radius = double_to_fixed(r_d);
        t_node.branch_order = 0;
        t_node.tier_idx = s;
        t_node.uv_v = double_to_fixed((double)s * h_step);
        t_node.is_terminal = (s == trunk_segs);
        cands.trunk_chain.push_back(t_node);

        if (cands.cfg.deliquescent_fork && s == fork_s) {
            fork_attach_pos = next_pos;
            fork_limb_radius = double_to_fixed((r_d / 0.74) * 0.65);
            double f_az = det_rand_float(seed_val, 99, 1) * 6.2831853;
            fork_horiz_dir = Vec3Fixed(double_to_fixed(std::cos(f_az)), 0, double_to_fixed(std::sin(f_az))).normalized();
            fork_limb_dir = (fork_horiz_dir * 32768 + Vec3Fixed(0, ONE, 0) * 55705).normalized(); // 0.50 h + 0.85 v
        }

        cur_pos = next_pos;
        cur_dir = steer;

        int64_t dx = next_pos.x - ground_pos.x;
        int64_t dz = next_pos.z - ground_pos.z;
        int64_t r_sq = mul_fixed(dx, dx) + mul_fixed(dz, dz);
        if (r_sq > max_spread_sq) max_spread_sq = r_sq;
    }

    // Update directions along trunk
    for (size_t s = 0; s < cands.trunk_chain.size(); ++s) {
        if (s + 1 < cands.trunk_chain.size()) cands.trunk_chain[s].dir = (cands.trunk_chain[s + 1].pos - cands.trunk_chain[s].pos).normalized();
        else if (s > 0) cands.trunk_chain[s].dir = cands.trunk_chain[s - 1].dir;
    }

    // Committed segment ledger for branch-to-branch collision detection
    struct SegmentLine {
        Vec3Fixed start;
        Vec3Fixed end;
        int64_t radius = 0;
        int branch_id = 0;
        int tier_idx = 0;
    };
    std::vector<SegmentLine> committed_segments;
    committed_segments.reserve(128);

    for (size_t i = 0; i + 1 < cands.trunk_chain.size(); ++i) {
        committed_segments.push_back({
            cands.trunk_chain[i].pos,
            cands.trunk_chain[i + 1].pos,
            cands.trunk_chain[i].radius,
            0,
            0
        });
    }

    int current_branch_id = 1;
    int next_cluster_id = 1;

    double crown_start_h = tree_h_d * fixed_to_double(cands.cfg.crown_start_ratio);
    double crown_avail_h = tree_h_d * 0.94 - crown_start_h;

    // Deliquescent Codominant Fork Limb
    std::vector<BotanicalNode> fork_limb_chain;
    std::vector<BotanicalShoot> fork_limb_shoots;
    int fork_branch_id = 999;

    if (cands.cfg.deliquescent_fork && cands.g >= 0.28) {
        int fork_segs = 4;
        double remaining_h = tree_h_d - fixed_to_double(fork_attach_pos.y);
        double fork_growth = std::clamp((cands.g - 0.25) / 0.75, 0.0, 1.0);
        double fork_elong = 0.35 + 0.65 * ((1.0 - std::exp(-3.0 * fork_growth)) / (1.0 - std::exp(-3.0)));
        double fork_limb_len = remaining_h * 0.88 * fork_elong;
        double f_seg_len = fork_limb_len / (double)fork_segs;

        Vec3Fixed cur_f_p = fork_attach_pos;
        Vec3Fixed cur_f_d = fork_limb_dir;

        BotanicalNode f_root;
        f_root.pos = fork_attach_pos;
        f_root.dir = fork_limb_dir;
        f_root.radius = fork_limb_radius;
        f_root.branch_order = 1;
        f_root.tier_idx = fork_s;
        fork_limb_chain.push_back(f_root);

        double f_wander_phase = det_rand_float(seed_val, 99, 3) * 6.2831853;
        for (int fs = 1; fs <= fork_segs; ++fs) {
            double fs_norm = (double)fs / (double)fork_segs;
            double f_ang = f_wander_phase + fs_norm * 3.14159265 * 1.8;
            Vec3Fixed f_side = cur_f_d.cross(Vec3Fixed(0, ONE, 0)).normalized();
            if (f_side.length_sq() < 16) f_side = cur_f_d.cross(Vec3Fixed(ONE, 0, 0)).normalized();
            Vec3Fixed f_binorm = cur_f_d.cross(f_side).normalized();

            Vec3Fixed f_wander = (f_side * double_to_fixed(std::cos(f_ang)) + f_binorm * double_to_fixed(std::sin(f_ang) * 0.75)) * double_to_fixed(0.42);
            Vec3Fixed f_steer = (cur_f_d * double_to_fixed(0.65) + Vec3Fixed(0, ONE, 0) * double_to_fixed(0.35) + sun_horiz * double_to_fixed(0.15) + f_wander).normalized();
            Vec3Fixed next_f_p = cur_f_p + f_steer * double_to_fixed(f_seg_len);

            BotanicalNode f_n;
            f_n.pos = next_f_p;
            f_n.dir = f_steer;
            f_n.radius = std::max<int64_t>(327, double_to_fixed(fixed_to_double(fork_limb_radius) * (1.0 - fs_norm * 0.65)));
            f_n.branch_order = 1;
            f_n.tier_idx = fork_s + fs;
            f_n.is_terminal = (fs == fork_segs);
            fork_limb_chain.push_back(f_n);

            committed_segments.push_back({ cur_f_p, next_f_p, f_n.radius, fork_branch_id, fork_s });

            cur_f_p = next_f_p;
            cur_f_d = f_steer;
        }

        // Intermediate micro-twigs along the fork limb
        for (int fs = 1; fs < fork_segs; ++fs) {
            const auto& f_mid_n = fork_limb_chain[fs];
            BasisFixed fm_basis = BasisFixed::from_forward_up(f_mid_n.dir);

            int fm_cl_id = next_cluster_id++;
            double fm_cl_r = fixed_to_double(cands.cfg.card_length) * 0.45;

            TreeClusterData cd;
            cd.x = fixed_to_float(f_mid_n.pos.x); cd.y = fixed_to_float(f_mid_n.pos.y); cd.z = fixed_to_float(f_mid_n.pos.z);
            cd.radius = (float)fm_cl_r; cd.leaf_count = 6.0f; cd.tree_id = (float)fork_branch_id;
            cd.gamma = (float)std::pow(0.55, fm_cl_r / 0.65);
            cd.eff_r_sq = (float)((fm_cl_r * 1.35) * (fm_cl_r * 1.35));
            cands.candidate_clusters.push_back(cd);

            BotanicalShoot fm_shoot;
            fm_shoot.id = (int)cands.candidate_clusters.size();
            fm_shoot.branch_id = fork_branch_id;
            fm_shoot.segment_idx = fs;
            fm_shoot.origin = f_mid_n.pos;
            fm_shoot.direction = f_mid_n.dir;
            fm_shoot.cluster_id = fm_cl_id;

            double fm_side_sign = (fs % 2 == 0) ? 1.0 : -1.0;

            BotanicalCard fm_c1;
            fm_c1.basis = fm_basis.rotated(fm_basis.up, double_to_fixed(0.5934 * fm_side_sign))
                                  .rotated(fm_basis.side, double_to_fixed(-0.2792));
            fm_c1.pos = f_mid_n.pos + fm_basis.side * double_to_fixed(0.15 * fm_side_sign);
            fm_c1.cluster_id = fm_cl_id;
            fm_c1.cluster_center = f_mid_n.pos + f_mid_n.dir * double_to_fixed(fm_cl_r);
            fm_c1.cluster_radius = double_to_fixed(fixed_to_double(cands.cfg.card_length));
            fm_c1.card_variant = 1;
            fm_c1.species_type = species;
            fm_shoot.cards.push_back(fm_c1);

            BotanicalCard fm_c2;
            fm_c2.basis = fm_basis.rotated(fm_basis.up, double_to_fixed(-0.5934 * fm_side_sign))
                                  .rotated(fm_basis.side, double_to_fixed(-0.3141));
            fm_c2.pos = f_mid_n.pos - fm_basis.side * double_to_fixed(0.15 * fm_side_sign);
            fm_c2.cluster_id = fm_cl_id;
            fm_c2.cluster_center = fm_c1.cluster_center;
            fm_c2.cluster_radius = fm_c1.cluster_radius;
            fm_c2.card_variant = 2;
            fm_c2.species_type = species;
            fm_shoot.cards.push_back(fm_c2);

            fork_limb_shoots.push_back(fm_shoot);
        }

        // Terminal shoot for fork limb (3-way fan)
        const auto& f_tip_n = fork_limb_chain.back();
        BasisFixed f_tip_basis = BasisFixed::from_forward_up(f_tip_n.dir);

        int f_tip_cl_id = next_cluster_id++;
        double f_cl_r = 0.65 * 0.95;

        TreeClusterData cd_tip;
        cd_tip.x = fixed_to_float(f_tip_n.pos.x); cd_tip.y = fixed_to_float(f_tip_n.pos.y); cd_tip.z = fixed_to_float(f_tip_n.pos.z);
        cd_tip.radius = (float)f_cl_r; cd_tip.leaf_count = 9.0f; cd_tip.tree_id = (float)fork_branch_id;
        cd_tip.gamma = (float)std::pow(0.55, f_cl_r / 0.65);
        cd_tip.eff_r_sq = (float)((f_cl_r * 1.35) * (f_cl_r * 1.35));
        cands.candidate_clusters.push_back(cd_tip);

        BotanicalShoot f_term_shoot;
        f_term_shoot.id = (int)cands.candidate_clusters.size();
        f_term_shoot.branch_id = fork_branch_id;
        f_term_shoot.segment_idx = (int)fork_limb_chain.size() - 1;
        f_term_shoot.origin = f_tip_n.pos;
        f_term_shoot.direction = f_tip_n.dir;
        f_term_shoot.cluster_id = f_tip_cl_id;

        // Card 1: center
        BotanicalCard fc1;
        fc1.basis = f_tip_basis;
        fc1.pos = f_tip_n.pos;
        fc1.cluster_id = f_tip_cl_id;
        fc1.cluster_center = f_tip_n.pos;
        fc1.cluster_radius = double_to_fixed(fixed_to_double(cands.cfg.card_length));
        fc1.card_variant = 0;
        fc1.species_type = species;
        f_term_shoot.cards.push_back(fc1);

        // Card 2: left wing
        BotanicalCard fc2;
        fc2.basis = f_tip_basis.rotated(f_tip_basis.up, double_to_fixed(-0.6632))
                               .rotated(f_tip_basis.side, double_to_fixed(-0.3490));
        fc2.pos = f_tip_n.pos - f_tip_basis.side * double_to_fixed(0.15);
        fc2.cluster_id = f_tip_cl_id;
        fc2.cluster_center = f_tip_n.pos;
        fc2.cluster_radius = fc1.cluster_radius;
        fc2.card_variant = 2;
        fc2.species_type = species;
        f_term_shoot.cards.push_back(fc2);

        // Card 3: right wing
        BotanicalCard fc3;
        fc3.basis = f_tip_basis.rotated(f_tip_basis.up, double_to_fixed(0.6632))
                               .rotated(f_tip_basis.side, double_to_fixed(-0.3490));
        fc3.pos = f_tip_n.pos + f_tip_basis.side * double_to_fixed(0.15);
        fc3.cluster_id = f_tip_cl_id;
        fc3.cluster_center = f_tip_n.pos;
        fc3.cluster_radius = fc1.cluster_radius;
        fc3.card_variant = 1;
        fc3.species_type = species;
        f_term_shoot.cards.push_back(fc3);

        fork_limb_shoots.push_back(f_term_shoot);

        CandidateBranchData fork_data;
        fork_data.branch_id = fork_branch_id;
        fork_data.tier = fork_s;
        fork_data.chain = fork_limb_chain;
        fork_data.shoots = fork_limb_shoots;
        fork_data.branch_card_scale = 1.0;
        fork_data.sun_align = 0.5;
        fork_data.b_vigor = 1.0;
        fork_data.tier_elongation = 1.0;
        cands.candidate_branches.push_back(fork_data);
    }

    // 3. Primary Branch Distribution Planning
    struct BranchSpec {
        double target_h = 0.0;
        double norm = 0.0;
        double azimuth = 0.0;
        int index = 0;
        int tier_idx = 0;
        bool is_helical = false;
    };
    std::vector<BranchSpec> branch_specs;

    if (cands.cfg.helical_branching) {
        for (int b_i = 0; b_i < cands.cfg.total_spiral_branches; ++b_i) {
            double norm = (double)b_i / (double)cands.cfg.total_spiral_branches;
            double b_target_h = crown_start_h + std::pow(norm, 0.88) * crown_avail_h;
            double golden_angle = (double)b_i * 2.3999632 + (det_rand_float(seed_val, b_i, 7) - 0.5) * 0.25;
            BranchSpec bs;
            bs.target_h = b_target_h;
            bs.norm = norm;
            bs.azimuth = golden_angle;
            bs.index = b_i;
            bs.tier_idx = (int)(norm * cands.cfg.num_tiers);
            bs.is_helical = true;
            branch_specs.push_back(bs);
        }
    } else {
        for (int tier = 0; tier < cands.cfg.num_tiers; ++tier) {
            double tier_norm = (double)tier / (double)cands.cfg.num_tiers;
            double tier_target_h = crown_start_h + tier_norm * crown_avail_h;
            double golden_angle = (double)tier * 2.39996;
            int count = (tier == cands.cfg.num_tiers - 1) ? std::max(2, cands.cfg.branches_per_tier - 1) : cands.cfg.branches_per_tier;
            for (int b = 0; b < count; ++b) {
                double az_jitter = (det_rand_float(seed_val, tier, b, 7) - 0.5) * 0.35;
                double azimuth = golden_angle + ((double)b / (double)count) * 6.2831853 + az_jitter;
                BranchSpec bs;
                bs.target_h = tier_target_h;
                bs.norm = tier_norm;
                bs.azimuth = azimuth;
                bs.index = b;
                bs.tier_idx = tier;
                bs.is_helical = false;
                branch_specs.push_back(bs);
            }
        }
    }

    // Pass 1: Build Primary Boughs, Sinuous Wander, and Micro-Twigs
    for (const auto& b_spec : branch_specs) {
        double tier_norm = b_spec.norm;
        double tier_target_h = b_spec.target_h;
        int tier = b_spec.tier_idx;
        double azimuth = b_spec.azimuth;
        int b_i = b_spec.index;

        double tier_bud_start_g = 0.04 + 0.60 * tier_norm;
        if (cands.g < tier_bud_start_g) continue;

        double branch_lifespan_span = std::max(0.12, 1.0 - tier_bud_start_g);
        double branch_age = std::clamp((cands.g - tier_bud_start_g) / branch_lifespan_span, 0.0, 1.0);
        double juvenile_surge = (1.0 - std::exp(-3.2 * branch_age)) / (1.0 - std::exp(-3.2));
        double tier_elongation = 0.60 * juvenile_surge + 0.40 * branch_age;
        if (tier_elongation < 0.02) continue;

        Vec3Fixed attach_pos = ground_pos;
        int64_t trunk_r_at_tier = double_to_fixed(base_r_d * 0.5);
        bool attaches_to_fork = false;
        Vec3Fixed horiz_dir(double_to_fixed(std::cos(azimuth)), 0, double_to_fixed(std::sin(azimuth)));
        horiz_dir = horiz_dir.normalized();

        if (cands.cfg.deliquescent_fork && fork_limb_chain.size() >= 2 && tier_norm > 0.38 && (b_i % 2 == 1)) {
            if (fork_horiz_dir.dot(horiz_dir) > 0) {
                BotanicalNode fork_sample = sample_chain_at_height(fork_limb_chain, double_to_fixed(tier_target_h));
                attach_pos = fork_sample.pos;
                trunk_r_at_tier = fork_sample.radius;
                attaches_to_fork = true;
            }
        }
        if (!attaches_to_fork) {
            BotanicalNode trunk_sample = sample_chain_at_height(cands.trunk_chain, double_to_fixed(tier_target_h));
            attach_pos = trunk_sample.pos;
            trunk_r_at_tier = trunk_sample.radius;
        }

        // Crown silhouette profile factor
        double profile_factor = 1.0;
        if (cands.cfg.crown_shape_exp >= (70 * ONE) / 100) {
            profile_factor = std::pow(1.0 - tier_norm, fixed_to_double(cands.cfg.crown_shape_exp));
        } else if (cands.cfg.helical_branching && species == BotanicalSpecies::SLENDER_BIRCH) {
            profile_factor = 0.50 + 0.60 * std::sin(std::lerp(0.15, 0.90, tier_norm) * 3.14159265);
        } else {
            profile_factor = 0.45 + 0.55 * std::sin(tier_norm * 3.14159265);
        }

        double pitch_rad = fixed_to_double(cands.cfg.branch_angle_deg) * (3.14159265 / 180.0);
        double local_pitch = pitch_rad + (1.0 - tier_norm) * 0.18 - tier_norm * 0.22;
        if (species == BotanicalSpecies::SLENDER_BIRCH) {
            local_pitch = pitch_rad + (1.0 - tier_norm) * 0.12 - tier_norm * 0.15;
        }

        current_branch_id++;
        int this_b_id = current_branch_id;

        Vec3Fixed branch_dir = (horiz_dir * double_to_fixed(std::sin(local_pitch)) + Vec3Fixed(0, ONE, 0) * double_to_fixed(std::cos(local_pitch))).normalized();
        double sun_align = fixed_to_double(horiz_dir.dot(sun_horiz));
        double sun_multiplier = 1.0 + sun_align * 0.28;
        double b_vigor = 1.0 + (det_rand_float(seed_val, tier, b_i, 13) - 0.5) * 0.30;

        uint64_t b_seed = (uint64_t)seed_val ^ ((uint64_t)this_b_id * 1013904223ULL) ^ ((uint64_t)tier * 668265263ULL);
        double b_wander_freq = 1.6 + (det_rand_float(b_seed, 1) - 0.5) * 0.8;
        double b_wander_phase = det_rand_float(b_seed, 2) * 6.2831853;
        double b_tortuosity = fixed_to_double(cands.cfg.wander_tortuosity);
        double b_wander_amp = b_tortuosity * (1.1 + 0.5 * det_rand_float(b_seed, 3));

        double bias_lat = fixed_to_double(cands.cfg.phototropism_lateral);
        double bias_vert = fixed_to_double(cands.cfg.phototropism_vertical);
        double silhouette_reach_mult = 1.0 + (bias_lat - bias_vert) * 0.25;

        double branch_max_reach = fixed_to_double(cands.cfg.max_branch_len) * profile_factor * tier_elongation * sun_multiplier * b_vigor * silhouette_reach_mult;
        if (branch_max_reach < 0.12) continue;

        int num_segs = (species != BotanicalSpecies::CONIFER && branch_max_reach > 2.6) ? 5 : (branch_max_reach > 1.2 ? 4 : (branch_max_reach > 0.5 ? 3 : 2));
        double seg_len = branch_max_reach / (double)num_segs;

        Vec3Fixed b_start_pos = attach_pos + horiz_dir * mul_fixed(trunk_r_at_tier, (60 * ONE) / 100);
        Vec3Fixed cur_b_pos = b_start_pos;
        Vec3Fixed cur_b_dir = branch_dir;

        double b_base_r_d = std::clamp(fixed_to_double(trunk_r_at_tier) * 0.42 * std::pow(tier_elongation, 0.6), 0.006, fixed_to_double(trunk_r_at_tier) * 0.65);
        int64_t b_base_r = double_to_fixed(b_base_r_d);

        std::vector<BotanicalNode> b_chain;
        BotanicalNode first_b_node;
        first_b_node.pos = b_start_pos;
        first_b_node.dir = branch_dir;
        first_b_node.radius = b_base_r;
        first_b_node.branch_order = 1;
        first_b_node.tier_idx = tier;
        b_chain.push_back(first_b_node);

        bool branch_terminated_by_collision = false;

        for (int bs = 1; bs <= num_segs; ++bs) {
            if (branch_terminated_by_collision) break;

            double bs_norm = (double)bs / (double)num_segs;
            double w_stiff = std::lerp(0.85, 0.35, bs_norm);
            Vec3Fixed sag(0, 0, 0);
            Vec3Fixed tip_hook(0, 0, 0);

            if (species == BotanicalSpecies::SLENDER_BIRCH) {
                double weep_curve = std::pow(bs_norm, 1.5);
                sag = Vec3Fixed(0, -double_to_fixed(fixed_to_double(cands.cfg.droop_strength) * 0.82 * weep_curve * tier_elongation), 0);
            } else {
                sag = Vec3Fixed(0, -double_to_fixed(fixed_to_double(cands.cfg.droop_strength) * 0.45 * std::pow(bs_norm, 1.8) * tier_elongation), 0);
                if (bs >= num_segs - 1) {
                    tip_hook = (Vec3Fixed(0, ONE, 0) * double_to_fixed(0.7) + sun_horiz * double_to_fixed(0.3)).normalized() * double_to_fixed(fixed_to_double(cands.cfg.tip_hook_strength) * 0.35 * bs_norm);
                }
            }

            Vec3Fixed b_side = cur_b_dir.cross(Vec3Fixed(0, ONE, 0)).normalized();
            if (b_side.length_sq() < 16) b_side = cur_b_dir.cross(Vec3Fixed(ONE, 0, 0)).normalized();
            Vec3Fixed b_binorm = cur_b_dir.cross(b_side).normalized();

            double w_ang = b_wander_phase + bs_norm * b_wander_freq * 3.14159265;
            Vec3Fixed b_wander = (b_side * double_to_fixed(std::cos(w_ang)) + b_binorm * double_to_fixed(std::sin(w_ang * 1.3) * 0.82)) * double_to_fixed(b_wander_amp);

            Vec3Fixed v_lat(cur_b_dir.x, 0, cur_b_dir.z);
            v_lat = (v_lat.length_sq() > 16) ? v_lat.normalized() : sun_horiz;
            Vec3Fixed phototropism_vector = (v_lat * cands.cfg.phototropism_lateral + Vec3Fixed(0, ONE, 0) * cands.cfg.phototropism_vertical + sun_horiz * double_to_fixed(0.25 + sun_align * 0.15)).normalized();

            double photo_steer_weight = 0.25 * (1.0 - w_stiff);
            double wander_steer_weight = std::clamp(1.0 - w_stiff * 0.50, 0.35, 0.90);
            Vec3Fixed target_heading = (cur_b_dir * double_to_fixed(1.0 - photo_steer_weight) + phototropism_vector * double_to_fixed(photo_steer_weight) + sag + tip_hook + b_wander * double_to_fixed(wander_steer_weight)).normalized();
            double bend_weight = std::clamp(1.0 - w_stiff * 0.60, 0.30, 0.82);
            Vec3Fixed seg_dir = (cur_b_dir * double_to_fixed(1.0 - bend_weight) + target_heading * double_to_fixed(bend_weight)).normalized();
            Vec3Fixed next_b_pos = cur_b_pos + seg_dir * double_to_fixed(seg_len);

            int64_t gy = get_ground_y(next_b_pos, ground_pos, ground_normal);
            if (next_b_pos.y < gy + (18 * ONE) / 100) {
                next_b_pos.y = gy + (18 * ONE) / 100;
                seg_dir = (next_b_pos - cur_b_pos).normalized();
                collisions_avoided++;
            }

            int64_t branch_test_r = double_to_fixed(std::max(0.004, b_base_r_d * (1.0 - bs_norm * 0.70)));

            // External Obstacles Avoidance (Cylinders)
            if (!obstacles.empty()) {
                for (const auto& obs : obstacles) {
                    int64_t h_dx = next_b_pos.x - obs.center.x;
                    int64_t h_dz = next_b_pos.z - obs.center.z;
                    int64_t h_dist_sq = mul_fixed(h_dx, h_dx) + mul_fixed(h_dz, h_dz);
                    int64_t min_clr = obs.radius + branch_test_r + 3276;
                    int64_t min_clr_sq = mul_fixed(min_clr, min_clr);
                    if (next_b_pos.y >= obs.center.y && next_b_pos.y <= obs.center.y + obs.height && h_dist_sq < min_clr_sq) {
                        Vec3Fixed repulse(h_dx, 0, h_dz);
                        repulse = (repulse.length_sq() > 16) ? repulse.normalized() : Vec3Fixed(ONE, 0, 0);
                        seg_dir = (seg_dir + repulse * 42598).normalized();
                        next_b_pos = cur_b_pos + seg_dir * double_to_fixed(seg_len);
                        collisions_avoided++;
                        break;
                    }
                }
            }

            // Branch-to-Branch Collision (Dan Sunday line segment distance)
            bool col_hit = false;
            Vec3Fixed col_repulse(0, 0, 0);

            for (const auto& seg_info : committed_segments) {
                if (seg_info.branch_id == this_b_id) continue;
                if (seg_info.branch_id == 0 || seg_info.branch_id == fork_branch_id) {
                    if (bs >= 2) {
                        int64_t h_dx = next_b_pos.x - ground_pos.x;
                        int64_t h_dz = next_b_pos.z - ground_pos.z;
                        int64_t trunk_h_dist_sq = mul_fixed(h_dx, h_dx) + mul_fixed(h_dz, h_dz);
                        int64_t min_t_r = trunk_r_at_tier + branch_test_r + 1311;
                        if (trunk_h_dist_sq < mul_fixed(min_t_r, min_t_r)) {
                            col_hit = true;
                            Vec3Fixed outward(h_dx, 0, h_dz);
                            col_repulse = (outward.length_sq() > 16) ? outward.normalized() : Vec3Fixed(0, ONE, 0);
                            break;
                        }
                    }
                    continue;
                }

                if (abs_fixed(cur_b_pos.y - seg_info.start.y) > (144179)) continue; // > 2.2m
                if (seg_info.tier_idx == tier && bs <= 1) continue;

                int64_t d = dist_segment_to_segment(cur_b_pos, next_b_pos, seg_info.start, seg_info.end);
                int64_t min_dist = branch_test_r + seg_info.radius + 3276;
                if (d < min_dist) {
                    col_hit = true;
                    Vec3Fixed diff = (cur_b_pos + next_b_pos) - (seg_info.start + seg_info.end);
                    col_repulse = (diff.length_sq() > 16) ? diff.normalized() : Vec3Fixed(0, ONE, 0);
                    break;
                }
            }

            if (col_hit) {
                collisions_avoided++;
                seg_dir = (seg_dir + col_repulse * 42598).normalized();
                next_b_pos = cur_b_pos + seg_dir * double_to_fixed(seg_len);
                if (bs >= 2) {
                    for (const auto& seg_info : committed_segments) {
                        if (seg_info.branch_id == this_b_id || seg_info.branch_id == 0 || seg_info.branch_id == fork_branch_id) continue;
                        int64_t d2 = dist_segment_to_segment(cur_b_pos, next_b_pos, seg_info.start, seg_info.end);
                        if (d2 < (branch_test_r + mul_fixed(seg_info.radius, 26214))) { // < r + 0.4*r_obs
                            branch_terminated_by_collision = true;
                            break;
                        }
                    }
                }
                if (branch_terminated_by_collision) break;
            }

            committed_segments.push_back({ cur_b_pos, next_b_pos, branch_test_r, this_b_id, tier });

            BotanicalNode b_node;
            b_node.pos = next_b_pos;
            b_node.dir = seg_dir;
            b_node.radius = branch_test_r;
            b_node.branch_order = 1;
            b_node.tier_idx = tier;
            b_node.uv_v = double_to_fixed((double)bs * seg_len);
            b_node.is_terminal = (bs == num_segs);
            b_chain.push_back(b_node);

            cur_b_pos = next_b_pos;
            cur_b_dir = seg_dir;

            int64_t b_dx = cur_b_pos.x - ground_pos.x;
            int64_t b_dz = cur_b_pos.z - ground_pos.z;
            int64_t br_sq = mul_fixed(b_dx, b_dx) + mul_fixed(b_dz, b_dz);
            if (br_sq > max_spread_sq) max_spread_sq = br_sq;
        }

        if (b_chain.size() < 2) continue;

        double branch_card_scale = std::clamp(tier_elongation * b_vigor * (0.8 + 0.2 * sun_align), 0.25, 1.15);
        std::vector<BotanicalShoot> b_shoots_for_this;
        std::vector<CandidateBranchData::TwigCandidate> twig_cands;

        if (species == BotanicalSpecies::SLENDER_BIRCH) {
            // Secondary Downstream Branches (Order 2) from intermediate nodes
            if (b_chain.size() >= 3 && branch_max_reach >= 0.70) {
                for (size_t dw_i = 1; dw_i < b_chain.size() - 1; ++dw_i) {
                    const auto& p_node = b_chain[dw_i];
                    double side_sign = ((b_i + tier + (int)dw_i) % 2 == 0) ? 1.0 : -1.0;
                    Vec3Fixed p_dir = p_node.dir.normalized();
                    Vec3Fixed p_side = p_dir.cross(Vec3Fixed(0, ONE, 0)).normalized();
                    if (p_side.length_sq() < 16) p_side = p_dir.cross(Vec3Fixed(ONE, 0, 0)).normalized();

                    double dw_reach = branch_max_reach * (dw_i == 1 ? 0.50 : 0.40) * (0.85 + 0.30 * det_rand_float(seed_val, b_i, (int64_t)dw_i, 101));
                    if (dw_reach < 0.25) continue;

                    Vec3Fixed dw_init_dir = (p_dir * double_to_fixed(0.70) + p_side * double_to_fixed(side_sign * 0.65) + Vec3Fixed(0, ONE, 0) * double_to_fixed(0.14)).normalized();
                    int dw_segs = (dw_reach > 0.85) ? 3 : 2;
                    double dw_seg_len = dw_reach / (double)dw_segs;

                    std::vector<BotanicalNode> dw_chain;
                    BotanicalNode dw_root;
                    dw_root.pos = p_node.pos;
                    dw_root.dir = dw_init_dir;
                    dw_root.radius = std::max<int64_t>(262, mul_fixed(p_node.radius, double_to_fixed(0.58)));
                    dw_root.branch_order = 2;
                    dw_root.tier_idx = tier;
                    dw_chain.push_back(dw_root);

                    Vec3Fixed cur_dw_p = p_node.pos;
                    Vec3Fixed cur_dw_d = dw_init_dir;

                    for (int ds = 1; ds <= dw_segs; ++ds) {
                        double ds_norm = (double)ds / (double)dw_segs;
                        Vec3Fixed dw_sag(0, -double_to_fixed(fixed_to_double(cands.cfg.droop_strength) * 0.65 * std::pow(ds_norm, 1.4)), 0);
                        Vec3Fixed dw_steer = (cur_dw_d * double_to_fixed(0.78) + dw_sag * double_to_fixed(0.45)).normalized();
                        Vec3Fixed next_dw_p = cur_dw_p + dw_steer * double_to_fixed(dw_seg_len);

                        int64_t dw_gy = get_ground_y(next_dw_p, ground_pos, ground_normal);
                        if (next_dw_p.y < dw_gy + (16 * ONE) / 100) {
                            next_dw_p.y = dw_gy + (16 * ONE) / 100;
                            dw_steer = (next_dw_p - cur_dw_p).normalized();
                        }

                        BotanicalNode dw_node;
                        dw_node.pos = next_dw_p;
                        dw_node.dir = dw_steer;
                        dw_node.radius = std::max<int64_t>(196, double_to_fixed(fixed_to_double(dw_root.radius) * (1.0 - ds_norm * 0.70)));
                        dw_node.branch_order = 2;
                        dw_node.tier_idx = tier;
                        dw_node.is_terminal = (ds == dw_segs);
                        dw_chain.push_back(dw_node);

                        cur_dw_p = next_dw_p;
                        cur_dw_d = dw_steer;
                    }

                    // Foliage shoot at secondary twig tip
                    BasisFixed dw_basis = BasisFixed::from_forward_up(cur_dw_d);
                    int dw_cl_id = next_cluster_id++;
                    double dw_scale = branch_card_scale * 0.92;
                    double dw_cl_r = (fixed_to_double(cands.cfg.card_length) * 0.45) * dw_scale;

                    TreeClusterData cd_dw;
                    cd_dw.x = fixed_to_float(cur_dw_p.x); cd_dw.y = fixed_to_float(cur_dw_p.y); cd_dw.z = fixed_to_float(cur_dw_p.z);
                    cd_dw.radius = (float)dw_cl_r; cd_dw.leaf_count = 6.0f; cd_dw.tree_id = (float)this_b_id;
                    cd_dw.gamma = (float)std::pow(0.55, dw_cl_r / 0.65);
                    cd_dw.eff_r_sq = (float)((dw_cl_r * 1.35) * (dw_cl_r * 1.35));
                    cands.candidate_clusters.push_back(cd_dw);

                    BotanicalShoot dw_shoot;
                    dw_shoot.id = (int)cands.candidate_clusters.size();
                    dw_shoot.branch_id = this_b_id;
                    dw_shoot.segment_idx = (int)dw_i;
                    dw_shoot.origin = cur_dw_p;
                    dw_shoot.direction = cur_dw_d;
                    dw_shoot.cluster_id = dw_cl_id;

                    // Weeping card 1 (center)
                    BotanicalCard c_card;
                    c_card.basis = dw_basis.rotated(dw_basis.side, double_to_fixed(0.2094));
                    c_card.pos = cur_dw_p;
                    c_card.cluster_id = dw_cl_id;
                    c_card.cluster_center = cur_dw_p + cur_dw_d * double_to_fixed(dw_cl_r);
                    c_card.cluster_radius = double_to_fixed(fixed_to_double(cands.cfg.card_length) * dw_scale);
                    c_card.size_scale = double_to_fixed(dw_scale);
                    c_card.card_variant = 2;
                    c_card.species_type = species;
                    dw_shoot.cards.push_back(c_card);

                    // Weeping card 2 (wing)
                    BotanicalCard w_card;
                    w_card.basis = dw_basis.rotated(dw_basis.up, double_to_fixed(0.6283 * side_sign))
                                           .rotated(dw_basis.side, double_to_fixed(-0.2792));
                    w_card.pos = cur_dw_p + dw_basis.side * double_to_fixed(0.16 * dw_scale * side_sign);
                    w_card.cluster_id = dw_cl_id;
                    w_card.cluster_center = c_card.cluster_center;
                    w_card.cluster_radius = c_card.cluster_radius;
                    w_card.size_scale = double_to_fixed(dw_scale * 0.95);
                    w_card.card_variant = 1;
                    w_card.species_type = species;
                    dw_shoot.cards.push_back(w_card);

                    CandidateBranchData::TwigCandidate tw_cand;
                    tw_cand.chain = dw_chain;
                    tw_cand.shoots = { dw_shoot };
                    tw_cand.junction_seg_idx = (int)dw_i;
                    tw_cand.cluster_id = dw_cl_id;
                    twig_cands.push_back(tw_cand);
                }
            }

            // Distal micro-twig shingling along the primary bough
            const auto& b_tip_n = b_chain.back();
            BasisFixed b_basis = BasisFixed::from_forward_up(b_tip_n.dir);

            int term_cl_id = next_cluster_id++;
            double term_cl_r = (fixed_to_double(cands.cfg.card_length) * 0.45) * branch_card_scale;

            TreeClusterData cd_term;
            cd_term.x = fixed_to_float(b_tip_n.pos.x); cd_term.y = fixed_to_float(b_tip_n.pos.y); cd_term.z = fixed_to_float(b_tip_n.pos.z);
            cd_term.radius = (float)term_cl_r; cd_term.leaf_count = 9.0f; cd_term.tree_id = (float)this_b_id;
            cd_term.gamma = (float)std::pow(0.55, term_cl_r / 0.65);
            cd_term.eff_r_sq = (float)((term_cl_r * 1.35) * (term_cl_r * 1.35));
            cands.candidate_clusters.push_back(cd_term);

            BotanicalShoot term_shoot;
            term_shoot.id = (int)cands.candidate_clusters.size();
            term_shoot.branch_id = this_b_id;
            term_shoot.segment_idx = (int)b_chain.size() - 1;
            term_shoot.origin = b_tip_n.pos;
            term_shoot.direction = b_tip_n.dir;
            term_shoot.cluster_id = term_cl_id;

            // 3 weeping cards at primary tip (center, left, right)
            for (int card_k = 0; card_k < 3; ++card_k) {
                BotanicalCard p_card;
                BasisFixed card_b = b_basis;
                Vec3Fixed card_p = b_tip_n.pos;
                if (card_k == 0) {
                    card_b = b_basis.rotated(b_basis.side, double_to_fixed(0.1745));
                } else if (card_k == 1) {
                    card_b = b_basis.rotated(b_basis.up, double_to_fixed(-0.6632))
                                    .rotated(b_basis.side, double_to_fixed(-0.2792));
                    card_p = card_p - b_basis.side * double_to_fixed(0.16 * branch_card_scale);
                } else {
                    card_b = b_basis.rotated(b_basis.up, double_to_fixed(0.6632))
                                    .rotated(b_basis.side, double_to_fixed(-0.2792));
                    card_p = card_p + b_basis.side * double_to_fixed(0.16 * branch_card_scale);
                }

                p_card.basis = card_b;
                p_card.pos = card_p;
                p_card.cluster_id = term_cl_id;
                p_card.cluster_center = b_tip_n.pos + b_tip_n.dir * double_to_fixed(term_cl_r);
                p_card.cluster_radius = double_to_fixed(fixed_to_double(cands.cfg.card_length) * branch_card_scale);
                p_card.size_scale = double_to_fixed(branch_card_scale);
                p_card.card_variant = (card_k == 0) ? 0 : 2;
                p_card.species_type = species;
                term_shoot.cards.push_back(p_card);
            }
            b_shoots_for_this.push_back(term_shoot);

            // Intermediate micro-twig shoots along distal 60% of b_chain
            int start_distal_idx = std::max(1, (int)(b_chain.size() * 4 / 10));
            for (size_t mid_idx = start_distal_idx; mid_idx < b_chain.size() - 1; ++mid_idx) {
                const auto& mid_n = b_chain[mid_idx];
                BasisFixed mid_basis = BasisFixed::from_forward_up(mid_n.dir);

                int mid_cl_id = next_cluster_id++;
                double mid_scale = branch_card_scale * (0.85 + 0.15 * (double)mid_idx / (double)b_chain.size());
                double mid_cl_r = (fixed_to_double(cands.cfg.card_length) * 0.42) * mid_scale;

                TreeClusterData cd_mid;
                cd_mid.x = fixed_to_float(mid_n.pos.x); cd_mid.y = fixed_to_float(mid_n.pos.y); cd_mid.z = fixed_to_float(mid_n.pos.z);
                cd_mid.radius = (float)mid_cl_r; cd_mid.leaf_count = 6.0f; cd_mid.tree_id = (float)this_b_id;
                cd_mid.gamma = (float)std::pow(0.55, mid_cl_r / 0.65);
                cd_mid.eff_r_sq = (float)((mid_cl_r * 1.35) * (mid_cl_r * 1.35));
                cands.candidate_clusters.push_back(cd_mid);

                BotanicalShoot mid_shoot;
                mid_shoot.id = (int)cands.candidate_clusters.size();
                mid_shoot.branch_id = this_b_id;
                mid_shoot.segment_idx = (int)mid_idx;
                mid_shoot.origin = mid_n.pos;
                mid_shoot.direction = mid_n.dir;
                mid_shoot.cluster_id = mid_cl_id;

                double side_sign = ((int)mid_idx + tier) % 2 == 0 ? 1.0 : -1.0;

                // Card 1
                BotanicalCard mc1;
                mc1.basis = mid_basis.rotated(mid_basis.up, double_to_fixed(-0.5585 * side_sign))
                                     .rotated(mid_basis.side, double_to_fixed(-0.2443));
                mc1.pos = mid_n.pos - mid_basis.side * double_to_fixed(0.14 * mid_scale * side_sign);
                mc1.cluster_id = mid_cl_id;
                mc1.cluster_center = mid_n.pos + mid_n.dir * double_to_fixed(mid_cl_r);
                mc1.cluster_radius = double_to_fixed(fixed_to_double(cands.cfg.card_length) * mid_scale);
                mc1.size_scale = double_to_fixed(mid_scale);
                mc1.card_variant = 1;
                mc1.species_type = species;
                mid_shoot.cards.push_back(mc1);

                // Card 2
                BotanicalCard mc2;
                mc2.basis = mid_basis.rotated(mid_basis.up, double_to_fixed(0.5585 * side_sign))
                                     .rotated(mid_basis.side, double_to_fixed(-0.2792));
                mc2.pos = mid_n.pos + mid_basis.side * double_to_fixed(0.14 * mid_scale * side_sign);
                mc2.cluster_id = mid_cl_id;
                mc2.cluster_center = mc1.cluster_center;
                mc2.cluster_radius = mc1.cluster_radius;
                mc2.size_scale = double_to_fixed(mid_scale);
                mc2.card_variant = 2;
                mc2.species_type = species;
                mid_shoot.cards.push_back(mc2);

                b_shoots_for_this.push_back(mid_shoot);
            }
        } else {
            // Conifer / Broadleaf Oak / Snag
            const auto& tip_n = b_chain.back();
            BasisFixed tip_basis = BasisFixed::from_forward_up(tip_n.dir);

            int tip_cl_id = next_cluster_id++;
            double tip_cl_r = 0.65 * branch_card_scale * 0.95;

            TreeClusterData cd_term;
            cd_term.x = fixed_to_float(tip_n.pos.x); cd_term.y = fixed_to_float(tip_n.pos.y); cd_term.z = fixed_to_float(tip_n.pos.z);
            cd_term.radius = (float)tip_cl_r; cd_term.leaf_count = 9.0f; cd_term.tree_id = (float)this_b_id;
            cd_term.gamma = (float)std::pow(0.55, tip_cl_r / 0.65);
            cd_term.eff_r_sq = (float)((tip_cl_r * 1.35) * (tip_cl_r * 1.35));
            cands.candidate_clusters.push_back(cd_term);

            BotanicalShoot term_shoot;
            term_shoot.id = (int)cands.candidate_clusters.size();
            term_shoot.branch_id = this_b_id;
            term_shoot.segment_idx = (int)b_chain.size() - 1;
            term_shoot.origin = tip_n.pos;
            term_shoot.direction = tip_n.dir;
            term_shoot.cluster_id = tip_cl_id;

            // Apical
            BotanicalCard ac;
            ac.basis = tip_basis.rotated(tip_basis.side, double_to_fixed(0.1047));
            ac.pos = tip_n.pos;
            ac.cluster_id = tip_cl_id;
            ac.cluster_center = tip_n.pos + tip_n.dir * double_to_fixed(tip_cl_r);
            ac.cluster_radius = double_to_fixed(fixed_to_double(cands.cfg.card_length) * branch_card_scale);
            ac.size_scale = double_to_fixed(branch_card_scale * 1.05);
            ac.card_variant = 0;
            ac.species_type = species;
            term_shoot.cards.push_back(ac);

            // Left
            BotanicalCard lc;
            lc.basis = tip_basis.rotated(tip_basis.up, double_to_fixed(-0.6632))
                                .rotated(tip_basis.side, double_to_fixed(-0.2443));
            lc.pos = tip_n.pos - tip_basis.side * double_to_fixed(0.16 * branch_card_scale);
            lc.cluster_id = tip_cl_id;
            lc.cluster_center = ac.cluster_center;
            lc.cluster_radius = ac.cluster_radius;
            lc.size_scale = double_to_fixed(branch_card_scale);
            lc.card_variant = 1;
            lc.species_type = species;
            term_shoot.cards.push_back(lc);

            // Right
            BotanicalCard rc;
            rc.basis = tip_basis.rotated(tip_basis.up, double_to_fixed(0.6632))
                                .rotated(tip_basis.side, double_to_fixed(-0.2443));
            rc.pos = tip_n.pos + tip_basis.side * double_to_fixed(0.16 * branch_card_scale);
            rc.cluster_id = tip_cl_id;
            rc.cluster_center = ac.cluster_center;
            rc.cluster_radius = ac.cluster_radius;
            rc.size_scale = double_to_fixed(branch_card_scale);
            rc.card_variant = 2;
            rc.species_type = species;
            term_shoot.cards.push_back(rc);

            b_shoots_for_this.push_back(term_shoot);

            // Intermediate node shoots along branch
            for (size_t seg_i = 1; seg_i < b_chain.size() - 1; ++seg_i) {
                const auto& node_n = b_chain[seg_i];
                BasisFixed n_basis = BasisFixed::from_forward_up(node_n.dir);

                double seg_frac = (double)seg_i / (double)b_chain.size();
                double node_scale = branch_card_scale * std::lerp(0.85, 1.05, seg_frac);

                int node_cl_id = next_cluster_id++;
                double node_cl_r = std::clamp(fixed_to_double(cands.cfg.card_length) * 0.40, 0.55, 1.10) * node_scale * 0.75;

                TreeClusterData cd_node;
                cd_node.x = fixed_to_float(node_n.pos.x); cd_node.y = fixed_to_float(node_n.pos.y); cd_node.z = fixed_to_float(node_n.pos.z);
                cd_node.radius = (float)node_cl_r; cd_node.leaf_count = 6.0f; cd_node.tree_id = (float)this_b_id;
                cd_node.gamma = (float)std::pow(0.55, node_cl_r / 0.65);
                cd_node.eff_r_sq = (float)((node_cl_r * 1.35) * (node_cl_r * 1.35));
                cands.candidate_clusters.push_back(cd_node);

                BotanicalShoot node_shoot;
                node_shoot.id = (int)cands.candidate_clusters.size();
                node_shoot.branch_id = this_b_id;
                node_shoot.segment_idx = (int)seg_i;
                node_shoot.origin = node_n.pos;
                node_shoot.direction = node_n.dir;
                node_shoot.cluster_id = node_cl_id;

                BotanicalCard l_flank;
                l_flank.basis = n_basis.rotated(n_basis.up, double_to_fixed(-0.7330))
                                       .rotated(n_basis.side, double_to_fixed(-0.3141));
                l_flank.pos = node_n.pos - n_basis.side * double_to_fixed(0.14 * node_scale);
                l_flank.cluster_id = node_cl_id;
                l_flank.cluster_center = node_n.pos + node_n.dir * double_to_fixed(node_cl_r);
                l_flank.cluster_radius = double_to_fixed(fixed_to_double(cands.cfg.card_length) * node_scale);
                l_flank.size_scale = double_to_fixed(node_scale);
                l_flank.card_variant = ((seg_i + tier) % 2 == 0) ? 1 : 2;
                l_flank.species_type = species;
                node_shoot.cards.push_back(l_flank);

                BotanicalCard r_flank;
                r_flank.basis = n_basis.rotated(n_basis.up, double_to_fixed(0.7330))
                                       .rotated(n_basis.side, double_to_fixed(-0.3141));
                r_flank.pos = node_n.pos + n_basis.side * double_to_fixed(0.14 * node_scale);
                r_flank.cluster_id = node_cl_id;
                r_flank.cluster_center = l_flank.cluster_center;
                r_flank.cluster_radius = l_flank.cluster_radius;
                r_flank.size_scale = double_to_fixed(node_scale);
                r_flank.card_variant = ((seg_i + tier) % 2 == 0) ? 2 : 1;
                r_flank.species_type = species;
                node_shoot.cards.push_back(r_flank);

                b_shoots_for_this.push_back(node_shoot);
            }
        }

        CandidateBranchData cb_data;
        cb_data.branch_id = this_b_id;
        cb_data.tier = tier;
        cb_data.chain = b_chain;
        cb_data.shoots = b_shoots_for_this;
        cb_data.branch_card_scale = branch_card_scale;
        cb_data.sun_align = sun_align;
        cb_data.b_vigor = b_vigor;
        cb_data.tier_elongation = tier_elongation;
        cb_data.twigs = twig_cands;
        cands.candidate_branches.push_back(cb_data);
    }

    cands.max_spread_sq = max_spread_sq;
    cands.collisions_avoided = collisions_avoided;
    return cands;
}

// ==============================================================================
// Pass 2 & 3: Biological Cladoptosis, Cambial Thickening, and Final Mesh Assembly
// ==============================================================================
BotanicalTreeResult BotanicalTreeGenerator::assemble_tree(
    BotanicalCandidates& cands,
    const std::vector<float>& cluster_exposures,
    bool enable_self_pruning,
    int64_t cladoptosis_override
) {
    BotanicalTreeResult res;
    if (cands.candidate_branches.empty() && cands.trunk_chain.empty()) return res;

    if (cladoptosis_override >= 0) {
        cands.cfg.cladoptosis_threshold = cladoptosis_override;
        cands.cfg.shoot_prune_threshold = mul_fixed(cladoptosis_override, double_to_fixed(1.5));
    }

    uint64_t running_hash = (uint64_t)cands.seed_val ^ 0x55AA55AA55AA55AAULL;

    res.stats.height = fixed_to_float(cands.tree_height);
    res.stats.base_radius = fixed_to_float(cands.base_radius);

    // Stage 1: Fine Shoot Micro-Pruning under Shade
    auto update_shoot_light = [&](BotanicalShoot& sh) {
        float l = 1.0f;
        if (sh.cluster_id >= 1 && (size_t)(sh.cluster_id - 1) < cluster_exposures.size()) {
            l = cluster_exposures[sh.cluster_id - 1];
        }
        sh.light_exposure = double_to_fixed(l);

        if (enable_self_pruning && cands.g >= 0.25 && l < fixed_to_double(cands.cfg.shoot_prune_threshold)) {
            sh.is_pruned = true;
            for (auto& c : sh.cards) {
                c.is_pruned = true;
                c.light_exposure = double_to_fixed(l);
            }
        } else {
            sh.is_pruned = false;
            for (auto& c : sh.cards) {
                c.is_pruned = false;
                c.light_exposure = double_to_fixed(l);
            }
        }
    };

    for (auto& cb : cands.candidate_branches) {
        for (auto& sh : cb.shoots) {
            update_shoot_light(sh);
        }
        for (auto& tw : cb.twigs) {
            for (auto& sh : tw.shoots) {
                update_shoot_light(sh);
            }
        }
    }

    // Stage 2: Whole-Branch Cladoptosis & Cambial Thickening
    std::vector<std::vector<BotanicalNode>> all_branch_chains;
    std::vector<BotanicalCard> all_cards;
    int living_branches = 0;
    int pruned_branches = 0;
    int pruned_cards_count = 0;

    for (auto& cb : cands.candidate_branches) {
        auto& b_chain = cb.chain;
        auto& b_shoots = cb.shoots;
        auto& twigs = cb.twigs;

        // Step 1: Check living secondary twigs
        std::vector<CandidateBranchData::TwigCandidate> living_twigs;
        int max_living_child_junction = -1;

        for (auto& tw : twigs) {
            bool tw_alive = false;
            double tw_avg_l = 0.0;
            for (const auto& sh : tw.shoots) {
                tw_avg_l += fixed_to_double(sh.light_exposure);
                if (!sh.is_pruned) tw_alive = true;
            }
            tw_avg_l /= std::max(1, (int)tw.shoots.size());
            tw.is_alive = tw_alive;
            tw.avg_light = tw_avg_l;

            if (tw_alive) {
                living_twigs.push_back(tw);
                if (tw.junction_seg_idx > max_living_child_junction) {
                    max_living_child_junction = tw.junction_seg_idx;
                }
            }
        }

        // Step 2: Check direct shoots on parent branch
        int max_active_parent_seg = -1;
        for (const auto& sh : b_shoots) {
            if (!sh.is_pruned && sh.segment_idx > max_active_parent_seg) {
                max_active_parent_seg = sh.segment_idx;
            }
        }

        // Step 3: Furthest preserved node index
        int cut_node_idx = std::max(max_active_parent_seg, max_living_child_junction);

        if (cut_node_idx <= 0) {
            pruned_branches++;
            for (auto& n : b_chain) n.is_pruned = true;
            for (auto& sh : b_shoots) {
                sh.is_pruned = true;
                for (auto& c : sh.cards) { c.is_pruned = true; pruned_cards_count++; }
            }
            for (auto& tw : twigs) {
                for (auto& sh : tw.shoots) {
                    sh.is_pruned = true;
                    for (auto& c : sh.cards) { c.is_pruned = true; pruned_cards_count++; }
                }
            }
            continue; // Culled entirely, 0 bark triangles
        }

        // Step 4: Truncate distal bare wood beyond furthest surviving node
        if (cut_node_idx < (int)b_chain.size() - 1) {
            for (auto& sh : b_shoots) {
                if (sh.segment_idx > cut_node_idx) {
                    sh.is_pruned = true;
                    for (auto& c : sh.cards) { c.is_pruned = true; pruned_cards_count++; }
                }
            }
            b_chain.resize(cut_node_idx + 1);
        }

        // Step 5: Net Photosynthetic Energy Density Check (Stage 2 Cladoptosis)
        double preserved_len = 0.0;
        for (size_t si = 0; si + 1 < b_chain.size(); ++si) {
            preserved_len += fixed_to_double((b_chain[si + 1].pos - b_chain[si].pos).length());
        }
        preserved_len = std::max(0.10, preserved_len);

        double sum_zeta = 0.0;
        double sum_l = 0.0;
        int active_shoot_count = 0;
        double card_rad = fixed_to_double(cands.cfg.card_length) * cb.branch_card_scale * 0.85;

        for (const auto& sh : b_shoots) {
            if (!sh.is_pruned && sh.segment_idx <= cut_node_idx) {
                active_shoot_count++;
                sum_l += fixed_to_double(sh.light_exposure);
                sum_zeta += 6.2831853 * (card_rad * card_rad) * fixed_to_double(sh.light_exposure);
            }
        }
        for (const auto& tw : living_twigs) {
            for (const auto& sh : tw.shoots) {
                if (!sh.is_pruned) {
                    active_shoot_count++;
                    sum_l += fixed_to_double(sh.light_exposure);
                    sum_zeta += 6.2831853 * (card_rad * card_rad) * fixed_to_double(sh.light_exposure);
                }
            }
        }

        double avg_b_light = (active_shoot_count > 0) ? (sum_l / (double)active_shoot_count) : 0.5;
        double norm_factor = std::pow(fixed_to_double(cands.cfg.card_length) / 0.65, 2.0);
        double rho_b = (sum_zeta / norm_factor) / preserved_len;
        double psi_min = fixed_to_double(cands.cfg.cladoptosis_threshold) * 2.8;

        if (enable_self_pruning && cands.g >= 0.30 && active_shoot_count == 0) {
            pruned_branches++;
            for (auto& n : b_chain) n.is_pruned = true;
            for (auto& sh : b_shoots) {
                sh.is_pruned = true;
                for (auto& c : sh.cards) { c.is_pruned = true; pruned_cards_count++; }
            }
            for (auto& tw : twigs) {
                for (auto& sh : tw.shoots) {
                    sh.is_pruned = true;
                    for (auto& c : sh.cards) { c.is_pruned = true; pruned_cards_count++; }
                }
            }
            continue;
        }

        // Step 6: Branch survived!
        living_branches++;
        for (auto& n : b_chain) {
            n.is_pruned = false;
            n.light_exposure = double_to_fixed(avg_b_light);
        }

        // Basipetal Nutrition Flux & Pressler Secondary Thickening
        int seg_count = (int)b_chain.size();
        std::vector<double> flux_per_seg(seg_count, 0.0);
        double flux_accum = 0.0;

        for (int s_rev = seg_count - 1; s_rev >= 0; --s_rev) {
            for (const auto& sh : b_shoots) {
                if (!sh.is_pruned && sh.segment_idx == s_rev) {
                    for (const auto& c : sh.cards) {
                        if (!c.is_pruned) flux_accum += (card_rad * card_rad) * fixed_to_double(sh.light_exposure);
                    }
                }
            }
            for (const auto& tw : living_twigs) {
                if (tw.junction_seg_idx == s_rev) {
                    for (const auto& sh : tw.shoots) {
                        if (!sh.is_pruned) {
                            for (const auto& c : sh.cards) {
                                if (!c.is_pruned) flux_accum += (card_rad * card_rad) * fixed_to_double(sh.light_exposure);
                            }
                        }
                    }
                }
            }
            flux_per_seg[s_rev] = flux_accum;
        }

        double base_flux = std::max(0.10, flux_per_seg[0]);
        double ref_flux = 3.6;
        double thickening_mult = std::clamp(std::pow(base_flux / ref_flux, 0.38), 0.80, 2.05);

        for (int si = 0; si < seg_count; ++si) {
            double local_ratio = std::clamp(flux_per_seg[si] / base_flux, 0.15, 1.0);
            double local_taper = std::sqrt(local_ratio);
            double new_r = fixed_to_double(b_chain[si].radius) * thickening_mult * local_taper;
            b_chain[si].radius = double_to_fixed(std::max(0.005, new_r));
        }

        all_branch_chains.push_back(b_chain);

        // Commit active direct cards
        for (const auto& sh : b_shoots) {
            if (!sh.is_pruned && sh.segment_idx <= cut_node_idx) {
                for (const auto& c : sh.cards) {
                    if (!c.is_pruned) all_cards.push_back(c);
                    else pruned_cards_count++;
                }
            }
        }

        // Commit living child secondary twigs
        for (const auto& tw : living_twigs) {
            all_branch_chains.push_back(tw.chain);
            for (const auto& sh : tw.shoots) {
                if (!sh.is_pruned) {
                    for (const auto& c : sh.cards) {
                        if (!c.is_pruned) all_cards.push_back(c);
                        else pruned_cards_count++;
                    }
                }
            }
        }
    }

    // Apical Summit Cards
    if (cands.trunk_chain.size() > 1 && cands.species != BotanicalSpecies::DEAD_SNAG) {
        const auto& apex_n = cands.trunk_chain.back();
        BasisFixed apex_basis = BasisFixed::from_forward_up(apex_n.dir);

        if (cands.species == BotanicalSpecies::SLENDER_BIRCH) {
            // Weeping 5-plume fan
            for (int k = 0; k < 5; ++k) {
                double arch_ang = (double)k * 6.2831853 / 5.0;
                Vec3Fixed plume_dir = (Vec3Fixed(double_to_fixed(std::cos(arch_ang) * 0.60), double_to_fixed(-0.70), double_to_fixed(std::sin(arch_ang) * 0.60))).normalized();
                BasisFixed plume_b = BasisFixed::from_forward_up(plume_dir);

                BotanicalCard ac;
                ac.basis = plume_b;
                ac.pos = apex_n.pos + plume_dir * double_to_fixed(0.12);
                ac.cluster_id = 9999;
                ac.cluster_center = apex_n.pos;
                ac.cluster_radius = cands.cfg.card_length;
                ac.size_scale = double_to_fixed(std::clamp(cands.g * 1.1, 0.40, 1.1) * 0.95);
                ac.card_variant = (k == 0) ? 0 : 2;
                ac.species_type = cands.species;
                all_cards.push_back(ac);
            }
        } else {
            // 4 crowning rosette cards
            for (int k = 0; k < 4; ++k) {
                BasisFixed roset_b = apex_basis.rotated(apex_basis.forward, double_to_fixed((double)k * 6.2831853 / 4.0))
                                                .rotated(apex_basis.side, double_to_fixed(0.3490));
                BotanicalCard ac;
                ac.basis = roset_b;
                ac.pos = apex_n.pos;
                ac.cluster_id = 9999;
                ac.cluster_center = apex_n.pos;
                ac.cluster_radius = cands.cfg.card_length;
                ac.size_scale = double_to_fixed(std::clamp(cands.g * 1.1, 0.40, 1.1) * 1.05);
                ac.card_variant = k % 3;
                ac.species_type = cands.species;
                all_cards.push_back(ac);
            }
        }
    }

    res.clusters = cands.candidate_clusters;

    // Fill Statistics
    res.stats.crown_spread = fixed_to_float(sqrt_fixed(cands.max_spread_sq));
    res.stats.total_branches = (int)cands.candidate_branches.size();
    res.stats.living_branches = living_branches;
    res.stats.pruned_branches = pruned_branches;
    res.stats.active_cards_count = (int)all_cards.size();
    res.stats.pruned_cards_count = pruned_cards_count;
    res.stats.collisions_avoided = cands.collisions_avoided;
    res.stats.state_hash = running_hash ^ ((uint64_t)all_cards.size() << 16) ^ (uint64_t)living_branches;

    int total_nodes = (int)cands.trunk_chain.size();
    for (const auto& bc : all_branch_chains) total_nodes += (int)bc.size();
    res.stats.total_nodes = total_nodes;

    // Generate 3D Meshes
    res.bark_mesh = build_bark_mesh(cands.trunk_chain, all_branch_chains, cands.cfg.radial_segments_trunk, cands.cfg.radial_segments_branch);
    res.stats.triangle_count = (int)res.bark_mesh.indices.size() / 3;

    res.foliage_mesh = build_foliage_mesh(all_cards, cands.species, cands.tree_height, cands.ground_pos, cands.cfg.card_length, cands.cfg.card_width, cands.ground_normal);
    res.stats.foliage_triangle_count = (int)res.foliage_mesh.indices.size() / 3;

    return res;
}

// ==============================================================================
// Core Generator: generate() (Single Tree Pipeline)
// ==============================================================================
BotanicalTreeResult BotanicalTreeGenerator::generate(
    int64_t seed_val,
    BotanicalSpecies species,
    int64_t growth_fixed,
    const Vec3Fixed& ground_pos,
    const Vec3Fixed& ground_normal,
    const Vec3Fixed& sun_dir,
    bool enable_self_pruning,
    int64_t cladoptosis_override,
    int64_t lateral_bias_override,
    int64_t vertical_bias_override
) {
    if (growth_fixed <= 0) return BotanicalTreeResult();

    BotanicalCandidates cands = generate_candidates(
        seed_val, species, growth_fixed, ground_pos, ground_normal, sun_dir,
        {}, cladoptosis_override, lateral_bias_override, vertical_bias_override
    );

    std::vector<float> exposures = evaluate_canopy_shadows_cpu(cands.candidate_clusters, cands.sun_norm);

    return assemble_tree(cands, exposures, enable_self_pruning, cladoptosis_override);
}


// 3D Bark Mesh Extrusion (Rotation Minimizing Frames)
// ==============================================================================
TreeMeshData BotanicalTreeGenerator::build_bark_mesh(
    const std::vector<BotanicalNode>& trunk,
    const std::vector<std::vector<BotanicalNode>>& branches,
    int radial_trunk,
    int radial_branch
) {
    TreeMeshData mesh;
    if (trunk.size() >= 2) extrude_indexed_chain(mesh, trunk, radial_trunk);
    for (const auto& b : branches) {
        if (b.size() >= 2) extrude_indexed_chain(mesh, b, radial_branch);
    }
    return mesh;
}

void BotanicalTreeGenerator::extrude_indexed_chain(
    TreeMeshData& mesh,
    const std::vector<BotanicalNode>& chain,
    int sides
) {
    if (chain.size() < 2) return;

    float v_coord = 0.0f;
    const auto& first = chain[0];

    Vec3Fixed cur_fwd = first.dir.normalized();
    Vec3Fixed cur_side;
    if (abs_fixed(cur_fwd.y) < 58982) {
        cur_side = cur_fwd.cross(Vec3Fixed(0, ONE, 0)).normalized();
    } else {
        cur_side = cur_fwd.cross(Vec3Fixed(ONE, 0, 0)).normalized();
    }
    Vec3Fixed cur_up = cur_fwd.cross(cur_side).normalized();

    uint32_t base_center_idx = (uint32_t)mesh.vertices.size();
    TreeVertex base_center;
    base_center.x = fixed_to_float(first.pos.x); base_center.y = fixed_to_float(first.pos.y); base_center.z = fixed_to_float(first.pos.z);
    base_center.nx = -fixed_to_float(cur_fwd.x); base_center.ny = -fixed_to_float(cur_fwd.y); base_center.nz = -fixed_to_float(cur_fwd.z);
    base_center.u = 0.5f; base_center.v = 0.0f;
    base_center.r = 0.35f; base_center.g = 0.25f; base_center.b = 0.16f; base_center.a = 1.0f;
    mesh.vertices.push_back(base_center);

    int64_t r0 = std::max<int64_t>(196, first.radius);
    uint32_t prev_ring_start = (uint32_t)mesh.vertices.size();

    for (int s = 0; s < sides; ++s) {
        int64_t angle = div_fixed((int64_t)s * TWO_PI, (int64_t)sides * ONE);
        Vec3Fixed norm = (cur_side * cos_fixed(angle) + cur_up * sin_fixed(angle)).normalized();
        Vec3Fixed p = first.pos + norm * r0;

        TreeVertex v;
        v.x = fixed_to_float(p.x); v.y = fixed_to_float(p.y); v.z = fixed_to_float(p.z);
        v.nx = fixed_to_float(norm.x); v.ny = fixed_to_float(norm.y); v.nz = fixed_to_float(norm.z);
        v.u = (float)s / (float)sides; v.v = 0.0f;
        v.r = 0.35f; v.g = 0.25f; v.b = 0.16f; v.a = 1.0f;
        mesh.vertices.push_back(v);
    }

    for (int s = 0; s < sides; ++s) {
        int next_s = (s + 1) % sides;
        mesh.indices.push_back(base_center_idx);
        mesh.indices.push_back(prev_ring_start + s);
        mesh.indices.push_back(prev_ring_start + next_s);
    }

    for (size_t i = 1; i < chain.size(); ++i) {
        const auto& node = chain[i];
        Vec3Fixed next_fwd = node.dir.normalized();

        Vec3Fixed rot_axis = cur_fwd.cross(next_fwd);
        if (rot_axis.length_sq() > 16) {
            rot_axis = rot_axis.normalized();
            int64_t dot_val = std::clamp<int64_t>(cur_fwd.dot(next_fwd), -ONE, ONE);
            double rot_ang = std::acos(std::clamp(fixed_to_double(dot_val), -1.0, 1.0));
            QuatFixed q = QuatFixed::from_axis_angle(rot_axis, double_to_fixed(rot_ang));
            cur_side = q.rotate_vector(cur_side).normalized();
            cur_up = q.rotate_vector(cur_up).normalized();
            cur_fwd = next_fwd;
        } else {
            cur_fwd = next_fwd;
            cur_side = (cur_side - cur_fwd * cur_side.dot(cur_fwd)).normalized();
            cur_up = cur_fwd.cross(cur_side).normalized();
        }

        int64_t r = std::max<int64_t>(196, node.radius);
        int64_t seg_len = (node.pos - chain[i - 1].pos).length();
        v_coord += fixed_to_float(seg_len) * 1.5f;

        uint32_t cur_ring_start = (uint32_t)mesh.vertices.size();
        for (int s = 0; s < sides; ++s) {
            int64_t angle = div_fixed((int64_t)s * TWO_PI, (int64_t)sides * ONE);
            Vec3Fixed norm = (cur_side * cos_fixed(angle) + cur_up * sin_fixed(angle)).normalized();
            Vec3Fixed p = node.pos + norm * r;

            TreeVertex v;
            v.x = fixed_to_float(p.x); v.y = fixed_to_float(p.y); v.z = fixed_to_float(p.z);
            v.nx = fixed_to_float(norm.x); v.ny = fixed_to_float(norm.y); v.nz = fixed_to_float(norm.z);
            v.u = (float)s / (float)sides; v.v = v_coord;
            v.r = 0.35f; v.g = 0.25f; v.b = 0.16f; v.a = 1.0f;
            mesh.vertices.push_back(v);
        }

        for (int s = 0; s < sides; ++s) {
            int next_s = (s + 1) % sides;
            uint32_t p0 = prev_ring_start + s;
            uint32_t p1 = prev_ring_start + next_s;
            uint32_t p2 = cur_ring_start + s;
            uint32_t p3 = cur_ring_start + next_s;

            // Tri 1
            mesh.indices.push_back(p2);
            mesh.indices.push_back(p3);
            mesh.indices.push_back(p0);

            // Tri 2
            mesh.indices.push_back(p3);
            mesh.indices.push_back(p1);
            mesh.indices.push_back(p0);
        }

        prev_ring_start = cur_ring_start;
    }

    const auto& last_n = chain.back();
    Vec3Fixed tip_p = last_n.pos + last_n.dir * 1310;
    uint32_t tip_center_idx = (uint32_t)mesh.vertices.size();

    TreeVertex tip_v;
    tip_v.x = fixed_to_float(tip_p.x); tip_v.y = fixed_to_float(tip_p.y); tip_v.z = fixed_to_float(tip_p.z);
    tip_v.nx = fixed_to_float(last_n.dir.x); tip_v.ny = fixed_to_float(last_n.dir.y); tip_v.nz = fixed_to_float(last_n.dir.z);
    tip_v.u = 0.5f; tip_v.v = v_coord + 0.1f;
    tip_v.r = 0.35f; tip_v.g = 0.25f; tip_v.b = 0.16f; tip_v.a = 1.0f;
    mesh.vertices.push_back(tip_v);

    for (int s = 0; s < sides; ++s) {
        int next_s = (s + 1) % sides;
        mesh.indices.push_back(tip_center_idx);
        mesh.indices.push_back(prev_ring_start + next_s);
        mesh.indices.push_back(prev_ring_start + s);
    }
}

// ==============================================================================
// SpeedTree Foliage Card Mesh Builder
// ==============================================================================
TreeMeshData BotanicalTreeGenerator::build_foliage_mesh(
    const std::vector<BotanicalCard>& cards,
    BotanicalSpecies species,
    int64_t tree_height,
    const Vec3Fixed& ground_pos,
    int64_t base_card_len,
    int64_t base_card_wid,
    const Vec3Fixed& ground_normal
) {
    TreeMeshData mesh;
    if (cards.empty()) return mesh;

    BotanicalParams p = BotanicalParams::get_preset(species);
    Vec3Fixed crown_center = ground_pos + Vec3Fixed(0, mul_fixed(tree_height, (62 * ONE) / 100), 0);

    // Crossed-quad angles: 35 deg and -35 deg
    std::vector<double> angles = { 0.610865, -0.610865 };
    if (species == BotanicalSpecies::BROADLEAF_OAK) {
        angles = { 0.0, 1.047198, -1.047198 }; // 3-way fan for oak
    }

    for (const auto& card : cards) {
        if (card.is_pruned) continue;

        int variant = std::clamp(card.card_variant, 0, 2);
        float u_min = (float)variant / 3.0f;
        float u_max = (float)(variant + 1) / 3.0f;
        float u_inset = (u_max - u_min) * 0.012f;
        float u0 = u_min + u_inset;
        float u1 = u_max - u_inset;

        int64_t clen = mul_fixed(base_card_len, card.size_scale);
        int64_t cwid = mul_fixed(base_card_wid, card.size_scale);

        Vec3Fixed card_fwd(0, 0, ONE);

        for (double ang : angles) {
            QuatFixed roll_q = QuatFixed::from_axis_angle(card_fwd, double_to_fixed(ang));
            Vec3Fixed p_side = roll_q.rotate_vector(Vec3Fixed(ONE, 0, 0)) * (cwid / 2);
            Vec3Fixed p_billow = roll_q.rotate_vector(Vec3Fixed(0, ONE, 0)) * mul_fixed(cwid, (8 * ONE) / 100);

            int64_t droop_factor = (variant == 2 || species == BotanicalSpecies::SLENDER_BIRCH) ? 15728 : 7864; // 0.24 vs 0.12
            Vec3Fixed tip_droop(0, -mul_fixed(clen, droop_factor), 0);

            // Local quad vertices
            Vec3Fixed v0_loc = p_side * -ONE;
            Vec3Fixed v1_loc = p_side;
            Vec3Fixed v2_loc = p_side * -ONE + card_fwd * clen - p_billow;
            Vec3Fixed v3_loc = p_side + card_fwd * clen - p_billow;

            // Transform into world space via card basis and pos
            Vec3Fixed v0 = card.pos + card.basis.xform(v0_loc);
            Vec3Fixed v1 = card.pos + card.basis.xform(v1_loc);
            Vec3Fixed v2 = card.pos + card.basis.xform(v2_loc) + tip_droop;
            Vec3Fixed v3 = card.pos + card.basis.xform(v3_loc) + tip_droop;

            // Ground clearance check
            int64_t gy2 = get_ground_y(v2, ground_pos, ground_normal);
            if (v2.y < gy2 + 3932) v2.y = gy2 + 3932;
            int64_t gy3 = get_ground_y(v3, ground_pos, ground_normal);
            if (v3.y < gy3 + 3932) v3.y = gy3 + 3932;

            // Planar quad normal
            Vec3Fixed quad_norm = card.basis.xform(roll_q.rotate_vector(Vec3Fixed(0, ONE, 0))).normalized();
            Vec3Fixed cr_dir0 = (v0 - crown_center).normalized();
            if (quad_norm.dot(cr_dir0) < 0) quad_norm = quad_norm * -ONE;

            Vec3Fixed cl_dir0 = (v0 - card.cluster_center).normalized();
            Vec3Fixed cl_dir1 = (v1 - card.cluster_center).normalized();
            Vec3Fixed cl_dir2 = (v2 - card.cluster_center).normalized();
            Vec3Fixed cl_dir3 = (v3 - card.cluster_center).normalized();

            Vec3Fixed cr_dir1 = (v1 - crown_center).normalized();
            Vec3Fixed cr_dir2 = (v2 - crown_center).normalized();
            Vec3Fixed cr_dir3 = (v3 - crown_center).normalized();

            Vec3Fixed n0 = (quad_norm * 9830 + cl_dir0 * 13107 + cr_dir0 * 42598).normalized();
            Vec3Fixed n1 = (quad_norm * 9830 + cl_dir1 * 13107 + cr_dir1 * 42598).normalized();
            Vec3Fixed n2 = (quad_norm * 9830 + cl_dir2 * 13107 + cr_dir2 * 42598).normalized();
            Vec3Fixed n3 = (quad_norm * 9830 + cl_dir3 * 13107 + cr_dir3 * 42598).normalized();

            uint32_t idx_start = (uint32_t)mesh.vertices.size();

            // V0 (base left)
            TreeVertex tv0;
            tv0.x = fixed_to_float(v0.x); tv0.y = fixed_to_float(v0.y); tv0.z = fixed_to_float(v0.z);
            tv0.nx = fixed_to_float(n0.x); tv0.ny = fixed_to_float(n0.y); tv0.nz = fixed_to_float(n0.z);
            tv0.u = u0; tv0.v = 1.0f;
            tv0.r = p.stem_r; tv0.g = p.stem_g; tv0.b = p.stem_b; tv0.a = 1.0f;
            mesh.vertices.push_back(tv0);

            // V1 (base right)
            TreeVertex tv1;
            tv1.x = fixed_to_float(v1.x); tv1.y = fixed_to_float(v1.y); tv1.z = fixed_to_float(v1.z);
            tv1.nx = fixed_to_float(n1.x); tv1.ny = fixed_to_float(n1.y); tv1.nz = fixed_to_float(n1.z);
            tv1.u = u1; tv1.v = 1.0f;
            tv1.r = p.stem_r; tv1.g = p.stem_g; tv1.b = p.stem_b; tv1.a = 1.0f;
            mesh.vertices.push_back(tv1);

            // V2 (tip left)
            TreeVertex tv2;
            tv2.x = fixed_to_float(v2.x); tv2.y = fixed_to_float(v2.y); tv2.z = fixed_to_float(v2.z);
            tv2.nx = fixed_to_float(n2.x); tv2.ny = fixed_to_float(n2.y); tv2.nz = fixed_to_float(n2.z);
            tv2.u = u0; tv2.v = 0.0f;
            tv2.r = p.leaf_r; tv2.g = p.leaf_g; tv2.b = p.leaf_b; tv2.a = 1.0f;
            mesh.vertices.push_back(tv2);

            // V3 (tip right)
            TreeVertex tv3;
            tv3.x = fixed_to_float(v3.x); tv3.y = fixed_to_float(v3.y); tv3.z = fixed_to_float(v3.z);
            tv3.nx = fixed_to_float(n3.x); tv3.ny = fixed_to_float(n3.y); tv3.nz = fixed_to_float(n3.z);
            tv3.u = u1; tv3.v = 0.0f;
            tv3.r = p.leaf_r; tv3.g = p.leaf_g; tv3.b = p.leaf_b; tv3.a = 1.0f;
            mesh.vertices.push_back(tv3);

            // Front-facing (v2 -> v1 -> v0 & v2 -> v3 -> v1)
            mesh.indices.push_back(idx_start + 2);
            mesh.indices.push_back(idx_start + 1);
            mesh.indices.push_back(idx_start + 0);

            mesh.indices.push_back(idx_start + 2);
            mesh.indices.push_back(idx_start + 3);
            mesh.indices.push_back(idx_start + 1);

            // Back-facing (double-sided)
            mesh.indices.push_back(idx_start + 0);
            mesh.indices.push_back(idx_start + 1);
            mesh.indices.push_back(idx_start + 2);

            mesh.indices.push_back(idx_start + 1);
            mesh.indices.push_back(idx_start + 3);
            mesh.indices.push_back(idx_start + 2);
        }
    }
    return mesh;
}

// ==============================================================================
// Fast CPU Hemispherical Cluster Lighting (Parity with GLSL Compute)
// ==============================================================================
std::vector<float> BotanicalTreeGenerator::evaluate_canopy_shadows_cpu(
    const std::vector<TreeClusterData>& clusters,
    const Vec3Fixed& sun_dir
) {
    std::vector<float> exposures(clusters.size(), 1.0f);
    if (clusters.empty()) return exposures;

    float sx = fixed_to_float(sun_dir.x);
    float sy = fixed_to_float(sun_dir.y);
    float sz = fixed_to_float(sun_dir.z);
    float slen = std::sqrt(sx * sx + sy * sy + sz * sz);
    if (slen > 1e-4f) { sx /= slen; sy /= slen; sz /= slen; }
    else { sx = 0.5f; sy = 0.707f; sz = 0.5f; }

    float ray_dirs[2][3] = {
        { sx, sy, sz },
        { 0.0f, 1.0f, 0.0f }
    };
    float ray_weights[2] = { 0.70f, 0.30f };

    for (size_t i = 0; i < clusters.size(); ++i) {
        float cx = clusters[i].x;
        float cy = clusters[i].y;
        float cz = clusters[i].z;

        float total_light = 0.0f;

        for (int r = 0; r < 2; ++r) {
            float rx = ray_dirs[r][0];
            float ry = ray_dirs[r][1];
            float rz = ray_dirs[r][2];
            float ray_factor = 1.0f;

            for (size_t j = 0; j < clusters.size(); ++j) {
                if (i == j) continue;
                if (clusters[j].y < cy - 0.15f) continue;

                float vx = clusters[j].x - cx;
                float vy = clusters[j].y - cy;
                float vz = clusters[j].z - cz;
                float t = vx * rx + vy * ry + vz * rz;

                if (t > 0.08f) {
                    float v_sq = vx * vx + vy * vy + vz * vz;
                    float d_perp_sq = std::max(0.0f, v_sq - t * t);
                    float eff_r_sq = clusters[j].eff_r_sq;

                    if (d_perp_sq < eff_r_sq) {
                        float gamma = clusters[j].gamma;
                        float o_rad = clusters[j].radius;
                        float o_rad_sq = o_rad * o_rad;
                        if (d_perp_sq > o_rad_sq) {
                            float d_perp = std::sqrt(d_perp_sq);
                            float penumbra = std::clamp((d_perp - o_rad) / std::max(0.01f, o_rad * 0.35f), 0.0f, 1.0f);
                            gamma = gamma + (1.0f - gamma) * penumbra;
                        }
                        ray_factor *= gamma;
                        if (ray_factor < 0.04f) {
                            ray_factor = 0.04f;
                            break;
                        }
                    }
                }
            }
            total_light += ray_weights[r] * ray_factor;
        }
        exposures[i] = std::clamp(total_light, 0.05f, 1.0f);
    }
    return exposures;
}

} // namespace exofarm
