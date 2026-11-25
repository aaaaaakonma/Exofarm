#include "botanical_tree_bridge.h"

#include <algorithm>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>

using namespace godot;

BotanicalTreeBridge::BotanicalTreeBridge() {}
BotanicalTreeBridge::~BotanicalTreeBridge() {}

void BotanicalTreeBridge::_bind_methods() {
    ClassDB::bind_method(D_METHOD("generate_tree", "seed_val", "species_id", "growth", "params"), &BotanicalTreeBridge::generate_tree, DEFVAL(0), DEFVAL(1.0), DEFVAL(Dictionary()));
    ClassDB::bind_method(D_METHOD("generate_stand", "master_seed", "species_id", "positions", "growths", "params"), &BotanicalTreeBridge::generate_stand, DEFVAL(Dictionary()));
    ClassDB::bind_method(D_METHOD("evaluate_canopy_shadows_cpu", "clusters_packed", "sun_dir"), &BotanicalTreeBridge::evaluate_canopy_shadows_cpu);
}

Ref<ArrayMesh> BotanicalTreeBridge::build_array_mesh(const exofarm::TreeMeshData &data, bool has_colors) {
    if (data.vertices.empty() || data.indices.empty()) {
        return Ref<ArrayMesh>();
    }

    PackedVector3Array verts;
    PackedVector3Array norms;
    PackedVector2Array uvs;
    PackedColorArray colors;
    PackedInt32Array indices;

    int64_t v_count = (int64_t)data.vertices.size();
    verts.resize(v_count);
    norms.resize(v_count);
    uvs.resize(v_count);
    if (has_colors) {
        colors.resize(v_count);
    }

    for (int64_t i = 0; i < v_count; ++i) {
        const auto &v = data.vertices[i];
        verts[i] = Vector3(v.x, v.y, v.z);
        norms[i] = Vector3(v.nx, v.ny, v.nz);
        uvs[i] = Vector2(v.u, v.v);
        if (has_colors) {
            colors[i] = Color(v.r, v.g, v.b, v.a);
        }
    }

    int64_t idx_count = (int64_t)data.indices.size();
    indices.resize(idx_count);
    for (int64_t i = 0; i < idx_count; ++i) {
        indices[i] = (int32_t)data.indices[i];
    }

    Array arrays;
    arrays.resize(Mesh::ARRAY_MAX);
    arrays[Mesh::ARRAY_VERTEX] = verts;
    arrays[Mesh::ARRAY_NORMAL] = norms;
    arrays[Mesh::ARRAY_TEX_UV] = uvs;
    if (has_colors) {
        arrays[Mesh::ARRAY_COLOR] = colors;
    }
    arrays[Mesh::ARRAY_INDEX] = indices;

    Ref<ArrayMesh> mesh;
    mesh.instantiate();
    mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
    return mesh;
}

Dictionary BotanicalTreeBridge::generate_tree(
    int64_t seed_val,
    int species_id,
    double growth,
    const Dictionary &params
) {
    Dictionary res;

    exofarm::BotanicalSpecies sp = static_cast<exofarm::BotanicalSpecies>(std::clamp(species_id, 0, 3));
    int64_t growth_fixed = double_to_fixed(std::clamp(growth, 0.0, 1.0));

    Vector3 g_pos_v = params.get("ground_pos", Vector3(0, 0, 0));
    Vector3 g_norm_v = params.get("ground_normal", Vector3(0, 1, 0));
    Vector3 sun_dir_v = params.get("sun_dir", Vector3(0.5, 0.707, 0.5));
    bool enable_self_pruning = params.get("enable_self_pruning", true);

    Vec3Fixed ground_pos(
        float_to_fixed(g_pos_v.x),
        float_to_fixed(g_pos_v.y),
        float_to_fixed(g_pos_v.z)
    );
    Vec3Fixed ground_normal(
        float_to_fixed(g_norm_v.x),
        float_to_fixed(g_norm_v.y),
        float_to_fixed(g_norm_v.z)
    );
    Vec3Fixed sun_dir(
        float_to_fixed(sun_dir_v.x),
        float_to_fixed(sun_dir_v.y),
        float_to_fixed(sun_dir_v.z)
    );

    int64_t clad_override = params.has("cladoptosis_threshold") ? double_to_fixed((double)params["cladoptosis_threshold"]) : -1;
    int64_t lat_override = params.has("lateral_bias") ? double_to_fixed((double)params["lateral_bias"]) : -1;
    int64_t vert_override = params.has("vertical_bias") ? double_to_fixed((double)params["vertical_bias"]) : -1;

    exofarm::BotanicalTreeResult tree_res = exofarm::BotanicalTreeGenerator::generate(
        seed_val,
        sp,
        growth_fixed,
        ground_pos,
        ground_normal,
        sun_dir,
        enable_self_pruning,
        clad_override,
        lat_override,
        vert_override
    );

    Ref<ArrayMesh> bark_mesh = build_array_mesh(tree_res.bark_mesh, false);
    Ref<ArrayMesh> foliage_mesh = build_array_mesh(tree_res.foliage_mesh, true);

    // Pack clusters into PackedFloat32Array for compute shader SSBO (8 floats = 32 bytes per cluster)
    PackedFloat32Array packed_clusters;
    packed_clusters.resize((int64_t)tree_res.clusters.size() * 8);
    for (size_t i = 0; i < tree_res.clusters.size(); ++i) {
        int64_t off = (int64_t)i * 8;
        const auto &c = tree_res.clusters[i];
        packed_clusters[off + 0] = c.x;
        packed_clusters[off + 1] = c.y;
        packed_clusters[off + 2] = c.z;
        packed_clusters[off + 3] = c.radius;
        packed_clusters[off + 4] = c.leaf_count;
        packed_clusters[off + 5] = c.tree_id;
        packed_clusters[off + 6] = c.gamma;
        packed_clusters[off + 7] = c.eff_r_sq;
    }

    Dictionary stats;
    stats["height"] = tree_res.stats.height;
    stats["base_radius"] = tree_res.stats.base_radius;
    stats["crown_spread"] = tree_res.stats.crown_spread;
    stats["total_branches"] = tree_res.stats.total_branches;
    stats["living_branches"] = tree_res.stats.living_branches;
    stats["pruned_branches"] = tree_res.stats.pruned_branches;
    stats["active_cards_count"] = tree_res.stats.active_cards_count;
    stats["pruned_cards_count"] = tree_res.stats.pruned_cards_count;
    stats["collisions_avoided"] = tree_res.stats.collisions_avoided;
    stats["total_nodes"] = tree_res.stats.total_nodes;
    stats["triangle_count"] = tree_res.stats.triangle_count;
    stats["foliage_triangle_count"] = tree_res.stats.foliage_triangle_count;
    stats["avg_light_exposure"] = tree_res.stats.avg_light_exposure;
    stats["state_hash"] = (int64_t)tree_res.stats.state_hash;

    res["bark_mesh"] = bark_mesh;
    res["foliage_mesh"] = foliage_mesh;
    res["clusters"] = packed_clusters;
    res["stats"] = stats;

    return res;
}

Dictionary BotanicalTreeBridge::generate_stand(
    int64_t master_seed,
    int species_id,
    const PackedVector3Array &positions,
    const PackedFloat32Array &growths,
    const Dictionary &params
) {
    Dictionary res;
    Array tree_list;
    PackedFloat32Array all_clusters;

    int count = (int)positions.size();
    if (count <= 0) {
        res["trees"] = tree_list;
        res["clusters"] = all_clusters;
        return res;
    }

    exofarm::BotanicalSpecies sp = static_cast<exofarm::BotanicalSpecies>(std::clamp(species_id, 0, 3));
    Vector3 g_norm_v = params.get("ground_normal", Vector3(0, 1, 0));
    Vector3 sun_dir_v = params.get("sun_dir", Vector3(0.5, 0.707, 0.5));
    bool enable_self_pruning = params.get("enable_self_pruning", true);

    Vec3Fixed ground_normal(
        float_to_fixed(g_norm_v.x),
        float_to_fixed(g_norm_v.y),
        float_to_fixed(g_norm_v.z)
    );
    Vec3Fixed sun_dir(
        float_to_fixed(sun_dir_v.x),
        float_to_fixed(sun_dir_v.y),
        float_to_fixed(sun_dir_v.z)
    );

    int64_t clad_override = params.has("cladoptosis_threshold") ? double_to_fixed((double)params["cladoptosis_threshold"]) : -1;
    int64_t lat_override = params.has("lateral_bias") ? double_to_fixed((double)params["lateral_bias"]) : -1;
    int64_t vert_override = params.has("vertical_bias") ? double_to_fixed((double)params["vertical_bias"]) : -1;

    Array seeds_array;
    if (params.has("seeds")) {
        seeds_array = params["seeds"];
    }

    // Pass 1: Architecture generation and candidate cluster placement for all trees
    std::vector<exofarm::BotanicalCandidates> stand_cands(count);
    std::vector<exofarm::TreeClusterData> stand_clusters;
    std::vector<int> cluster_offsets(count);
    std::vector<int> cluster_counts(count);

    for (int i = 0; i < count; ++i) {
        int64_t tree_seed = (i < seeds_array.size()) ? (int64_t)seeds_array[i] : (master_seed ^ ((int64_t)i * 1013904223ULL));
        double g = (i < growths.size()) ? (double)growths[i] : 1.0;
        int64_t growth_fixed = double_to_fixed(std::clamp(g, 0.0, 1.0));

        Vector3 pos_i = positions[i];
        Vec3Fixed ground_pos(
            float_to_fixed(pos_i.x),
            float_to_fixed(pos_i.y),
            float_to_fixed(pos_i.z)
        );

        // Build obstacle cylinders for other trees in the grove
        std::vector<exofarm::TreeObstacle> obstacles;
        for (int j = 0; j < count; ++j) {
            if (i == j) continue;
            Vector3 pos_j = positions[j];
            exofarm::TreeObstacle obs;
            obs.center = Vec3Fixed(float_to_fixed(pos_j.x), float_to_fixed(pos_j.y), float_to_fixed(pos_j.z));
            obs.radius = double_to_fixed(0.40);
            obs.height = double_to_fixed(14.0);
            obstacles.push_back(obs);
        }

        stand_cands[i] = exofarm::BotanicalTreeGenerator::generate_candidates(
            tree_seed,
            sp,
            growth_fixed,
            ground_pos,
            ground_normal,
            sun_dir,
            obstacles,
            clad_override,
            lat_override,
            vert_override
        );

        cluster_offsets[i] = (int)stand_clusters.size();
        cluster_counts[i] = (int)stand_cands[i].candidate_clusters.size();
        for (auto &cl : stand_cands[i].candidate_clusters) {
            cl.tree_id = (float)i;
            stand_clusters.push_back(cl);
        }
    }

    // Pass 2: Stand-wide canopy shadow field evaluation
    std::vector<float> stand_exposures = exofarm::BotanicalTreeGenerator::evaluate_canopy_shadows_cpu(stand_clusters, sun_dir);

    // Pass 3: Assemble each tree with mutual grove shading
    for (int i = 0; i < count; ++i) {
        std::vector<float> tree_exposures;
        if (cluster_counts[i] > 0) {
            tree_exposures.assign(
                stand_exposures.begin() + cluster_offsets[i],
                stand_exposures.begin() + cluster_offsets[i] + cluster_counts[i]
            );
        }

        exofarm::BotanicalTreeResult tree_res = exofarm::BotanicalTreeGenerator::assemble_tree(
            stand_cands[i],
            tree_exposures,
            enable_self_pruning,
            clad_override
        );

        Ref<ArrayMesh> bark_mesh = build_array_mesh(tree_res.bark_mesh, false);
        Ref<ArrayMesh> foliage_mesh = build_array_mesh(tree_res.foliage_mesh, true);

        // Pack tree clusters
        PackedFloat32Array packed_clusters;
        packed_clusters.resize((int64_t)tree_res.clusters.size() * 8);
        int cur_all_sz = (int)all_clusters.size();
        all_clusters.resize(cur_all_sz + (int64_t)tree_res.clusters.size() * 8);

        for (size_t c = 0; c < tree_res.clusters.size(); ++c) {
            int64_t off = (int64_t)c * 8;
            const auto &cl = tree_res.clusters[c];
            packed_clusters[off + 0] = cl.x;
            packed_clusters[off + 1] = cl.y;
            packed_clusters[off + 2] = cl.z;
            packed_clusters[off + 3] = cl.radius;
            packed_clusters[off + 4] = cl.leaf_count;
            packed_clusters[off + 5] = (float)i;
            packed_clusters[off + 6] = cl.gamma;
            packed_clusters[off + 7] = cl.eff_r_sq;

            int dst_off = cur_all_sz + (int)off;
            all_clusters[dst_off + 0] = cl.x;
            all_clusters[dst_off + 1] = cl.y;
            all_clusters[dst_off + 2] = cl.z;
            all_clusters[dst_off + 3] = cl.radius;
            all_clusters[dst_off + 4] = cl.leaf_count;
            all_clusters[dst_off + 5] = (float)i;
            all_clusters[dst_off + 6] = cl.gamma;
            all_clusters[dst_off + 7] = cl.eff_r_sq;
        }

        Dictionary stats;
        stats["height"] = tree_res.stats.height;
        stats["base_radius"] = tree_res.stats.base_radius;
        stats["crown_spread"] = tree_res.stats.crown_spread;
        stats["total_branches"] = tree_res.stats.total_branches;
        stats["living_branches"] = tree_res.stats.living_branches;
        stats["pruned_branches"] = tree_res.stats.pruned_branches;
        stats["active_cards_count"] = tree_res.stats.active_cards_count;
        stats["pruned_cards_count"] = tree_res.stats.pruned_cards_count;
        stats["collisions_avoided"] = tree_res.stats.collisions_avoided;
        stats["total_nodes"] = tree_res.stats.total_nodes;
        stats["triangle_count"] = tree_res.stats.triangle_count;
        stats["foliage_triangle_count"] = tree_res.stats.foliage_triangle_count;
        stats["avg_light_exposure"] = tree_res.stats.avg_light_exposure;
        stats["state_hash"] = (int64_t)tree_res.stats.state_hash;

        Dictionary tree_dict;
        tree_dict["bark_mesh"] = bark_mesh;
        tree_dict["foliage_mesh"] = foliage_mesh;
        tree_dict["clusters"] = packed_clusters;
        tree_dict["stats"] = stats;

        tree_list.append(tree_dict);
    }

    res["trees"] = tree_list;
    res["clusters"] = all_clusters;
    return res;
}

PackedFloat32Array BotanicalTreeBridge::evaluate_canopy_shadows_cpu(
    const PackedFloat32Array &clusters_packed,
    const Vector3 &sun_dir
) {
    int cl_count = (int)clusters_packed.size() / 8;
    std::vector<exofarm::TreeClusterData> clusters(cl_count);

    for (int i = 0; i < cl_count; ++i) {
        int off = i * 8;
        clusters[i].x = clusters_packed[off + 0];
        clusters[i].y = clusters_packed[off + 1];
        clusters[i].z = clusters_packed[off + 2];
        clusters[i].radius = clusters_packed[off + 3];
        clusters[i].leaf_count = clusters_packed[off + 4];
        clusters[i].tree_id = clusters_packed[off + 5];
        clusters[i].gamma = clusters_packed[off + 6];
        clusters[i].eff_r_sq = clusters_packed[off + 7];
    }

    Vec3Fixed sdir(
        float_to_fixed(sun_dir.x),
        float_to_fixed(sun_dir.y),
        float_to_fixed(sun_dir.z)
    );

    std::vector<float> exposures = exofarm::BotanicalTreeGenerator::evaluate_canopy_shadows_cpu(clusters, sdir);

    PackedFloat32Array res;
    res.resize((int64_t)exposures.size());
    for (size_t i = 0; i < exposures.size(); ++i) {
        res[(int64_t)i] = exposures[i];
    }
    return res;
}
