#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include "../core/botanical_tree.h"

class BotanicalTreeBridge : public godot::RefCounted {
    GDCLASS(BotanicalTreeBridge, godot::RefCounted)

public:
    BotanicalTreeBridge();
    ~BotanicalTreeBridge();

    static void _bind_methods();

    // Generate single tree mesh and cluster data
    godot::Dictionary generate_tree(
        int64_t seed_val,
        int species_id = 0,
        double growth = 1.0,
        const godot::Dictionary &params = godot::Dictionary()
    );

    // Generate stand / grove of trees, returning combined meshes and all cluster spheres
    godot::Dictionary generate_stand(
        int64_t master_seed,
        int species_id,
        const godot::PackedVector3Array &positions,
        const godot::PackedFloat32Array &growths,
        const godot::Dictionary &params = godot::Dictionary()
    );

    // Evaluate canopy shadow field on CPU (parity with GLSL compute shader)
    godot::PackedFloat32Array evaluate_canopy_shadows_cpu(
        const godot::PackedFloat32Array &clusters_packed,
        const godot::Vector3 &sun_dir
    );

private:
    static godot::Ref<godot::ArrayMesh> build_array_mesh(const exofarm::TreeMeshData &data, bool has_colors = false);
};
