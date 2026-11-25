#[compute]
#version 450

// ----------------------------------------------------------------------
// ExoFarm: Parallel Canopy Cluster Illumination & Mutual Shadowing
// Pirk et al. (2012) Plastic Trees & Multi-Scale Volumetric Extinction
//
// Evaluates direct solar beam attenuation and sky zenith ambient occlusion
// across foliage cluster bounding spheres on the GPU via RenderingDevice.
//
// Optimized:
// - Single-pass joint ray evaluation (halves SSBO buffer memory bandwidth)
// - Register-cached cluster data
// - Fast distance bounding rejection
// - Early-out when both solar and skylight rays are fully occluded
// ----------------------------------------------------------------------

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

// std430 cluster struct: 32 bytes (2 x vec4)
struct CanopyCluster {
    vec4 pos_radius; // xyz = world position, w = sphere radius
    vec4 aux_params; // x = leaf_count, y = tree_id, z = gamma (translucency), w = eff_r_sq
};

// SSBO 0: Input Foliage Clusters
layout(std430, set = 0, binding = 0) readonly buffer ClusterBuffer {
    CanopyCluster clusters[];
};

// SSBO 1: Output Cluster Light Exposures [0.05 .. 1.0]
layout(std430, set = 0, binding = 1) writeonly buffer ExposureBuffer {
    float exposures[];
};

// UBO 2: Sun and Ambient Environment Parameters (32 bytes std140)
layout(std140, set = 0, binding = 2) uniform CanopyParams {
    vec4 sun_dir;        // xyz = normalized sun vector, w = sun ray weight (e.g. 0.70)
    vec4 zenith_dir;     // xyz = normalized zenith vector (0, 1, 0), w = ambient weight (e.g. 0.30)
    uint num_clusters;   // total active clusters in stand/chunk
    float min_light;     // minimum shadow clamp (0.04)
    float pad0;
    float pad1;
} params;

void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= params.num_clusters) {
        return;
    }

    vec3 c_pos = clusters[idx].pos_radius.xyz;

    vec3 ray_dirs[2] = vec3[2](params.sun_dir.xyz, params.zenith_dir.xyz);
    float ray_factors[2] = float[2](1.0, 1.0);
    bool ray_active[2] = bool[2](true, true);

    uint n = params.num_clusters;
    float min_l = params.min_light;

    for (uint j = 0; j < n; ++j) {
        if (j == idx) continue;

        // Fetch occluder cluster data once into registers
        vec4 o_pos_rad = clusters[j].pos_radius;
        vec3 o_pos = o_pos_rad.xyz;

        // Fast vertical reject: clusters below cannot cast shadows from overhead light
        if (o_pos.y < c_pos.y - 0.15) continue;

        vec3 v = o_pos - c_pos;
        float v_sq = dot(v, v);

        // Fast distance reject: clusters beyond 25m cannot meaningfully cast shadows
        if (v_sq > 625.0) continue;

        vec4 o_aux = clusters[j].aux_params;
        float eff_r_sq = o_aux.w;
        float gamma_base = o_aux.z;
        float o_rad = o_pos_rad.w;
        float o_rad_sq = o_rad * o_rad;
        float penumbra_denom = max(0.01, o_rad * 0.35);

        // Evaluate Ray 0 (Direct Sun)
        if (ray_active[0]) {
            float t0 = dot(v, ray_dirs[0]);
            if (t0 > 0.08) {
                float d_perp_sq = max(0.0, v_sq - t0 * t0);
                if (d_perp_sq < eff_r_sq) {
                    float gamma_c = gamma_base;
                    if (d_perp_sq > o_rad_sq) {
                        float penumbra = clamp((sqrt(d_perp_sq) - o_rad) / penumbra_denom, 0.0, 1.0);
                        gamma_c = mix(gamma_c, 1.0, penumbra);
                    }
                    ray_factors[0] *= gamma_c;
                    if (ray_factors[0] <= min_l) {
                        ray_factors[0] = min_l;
                        ray_active[0] = false;
                    }
                }
            }
        }

        // Evaluate Ray 1 (Zenith Skylight)
        if (ray_active[1]) {
            float t1 = dot(v, ray_dirs[1]);
            if (t1 > 0.08) {
                float d_perp_sq = max(0.0, v_sq - t1 * t1);
                if (d_perp_sq < eff_r_sq) {
                    float gamma_c = gamma_base;
                    if (d_perp_sq > o_rad_sq) {
                        float penumbra = clamp((sqrt(d_perp_sq) - o_rad) / penumbra_denom, 0.0, 1.0);
                        gamma_c = mix(gamma_c, 1.0, penumbra);
                    }
                    ray_factors[1] *= gamma_c;
                    if (ray_factors[1] <= min_l) {
                        ray_factors[1] = min_l;
                        ray_active[1] = false;
                    }
                }
            }
        }

        // Early out if both solar and sky zenith rays are already fully occluded
        if (!ray_active[0] && !ray_active[1]) {
            break;
        }
    }

    float total_light = params.sun_dir.w * ray_factors[0] + params.zenith_dir.w * ray_factors[1];
    exposures[idx] = clamp(total_light, 0.05, 1.0);
}
