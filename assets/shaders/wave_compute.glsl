#[compute]
#version 450

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

// ---------- Outputs ----------
layout(rgba32f, set = 0, binding = 0) uniform image2D out_displacement;
layout(rgba32f, set = 0, binding = 1) uniform image2D out_normal;

// ---------- Inputs ----------
// Static cage data baked by wave_cage_baker.gd:
//   R = r1 : shore-facing cage radius (world units)
//   G = r2 : along-shore cage radius  (world units)
//   B = r3 : depth cage radius        (world units ≈ water depth)
//   A = shore_angle : atan2 direction toward nearest shore in XZ
layout(set = 1, binding = 0) uniform sampler2D static_cage_tex;

// Terrain heightmap — sampled to reconstruct the seafloor surface normal (a3).
layout(set = 1, binding = 1) uniform sampler2D heightmap_tex;

// Wave parameters UBO (32 bytes, std140).
layout(set = 2, binding = 0) uniform WaveParams {
    float time;
    float cage_softness;         // tanh 's' parameter (paper §4, typically 0.5)
    int   num_waves;
    float world_size;            // physical world extent in metres
    float height_scale_factor;   // heightmap [0..1] → world-space metres
    float sea_level_normalized;  // sea level as a [0..1] heightmap value
    float _pad0;
    float _pad1;
} params;

// Per-wave data (std430, 32 bytes each).
struct Wave {
    vec2  dir;       // normalised XZ propagation direction
    float amplitude; // wave height (metres)
    float frequency; // spatial frequency k = 2π/λ
    float speed;     // angular frequency ω
    float phase;     // initial phase offset (radians)
    float steepness; // Gerstner Q ∈ [0, 1] — horizontal choppiness
    float _pad;
};

layout(set = 3, binding = 0, std430) buffer WaveData {
    Wave waves[];
};

// ---------------------------------------------------------------
// Analytic Gerstner wave summation (GPU Gems 1, Ch. 1).
//
// Returns the XYZ displacement in world space.
// Also writes:
//   out_n     — analytic surface normal (world space, before cage warping)
//   out_accel — summed vertical acceleration d²η_y/dt² (m/s²)
// ---------------------------------------------------------------
vec3 gerstner_eval(vec2 world_pos, out vec3 out_n, out float out_accel) {
    vec3  eta    = vec3(0.0);
    float sum_nx = 0.0;
    float sum_nz = 0.0;
    float sum_ny = 0.0;  // accumulated y-normal term
    float accel  = 0.0;

    for (int i = 0; i < params.num_waves; i++) {
        Wave  w  = waves[i];
        float qi = clamp(w.steepness, 0.0, 1.0);
        float phi = w.frequency * dot(w.dir, world_pos)
                  - w.speed    * params.time
                  + w.phase;
        float sp = sin(phi);
        float cp = cos(phi);

        // Standard Gerstner displacement:
        //   η_xz = Q·A·dir·cos(φ)   (horizontal)
        //   η_y  = A·sin(φ)          (vertical)
        eta.x += qi * w.amplitude * w.dir.x * cp;
        eta.z += qi * w.amplitude * w.dir.y * cp;
        eta.y +=      w.amplitude            * sp;

        // Analytic surface normal terms (GPU Gems 1, §1.3):
        float kA = w.frequency * w.amplitude;
        sum_nx  -= w.dir.x * kA * cp;
        sum_nz  -= w.dir.y * kA * cp;
        sum_ny  += qi * kA * sp;

        // Second time-derivative of A·sin(φ):  d²/dt² = -A·ω²·sin(φ)
        accel   += -w.amplitude * w.speed * w.speed * sp;
    }

    out_n     = normalize(vec3(sum_nx, 1.0 - sum_ny, sum_nz));
    out_accel = accel;
    return eta;
}

// ---------------------------------------------------------------
// Terrain surface normal at the given UV, derived from finite
// differences on the heightmap.
// ---------------------------------------------------------------
vec3 terrain_normal_at(vec2 uv) {
    ivec2 sz    = textureSize(heightmap_tex, 0);
    vec2  inv_s = 1.0 / vec2(sz);

    float hL = texture(heightmap_tex, uv + vec2(-inv_s.x,     0.0)).r;
    float hR = texture(heightmap_tex, uv + vec2( inv_s.x,     0.0)).r;
    float hU = texture(heightmap_tex, uv + vec2(      0.0, -inv_s.y)).r;
    float hD = texture(heightmap_tex, uv + vec2(      0.0,  inv_s.y)).r;

    // World units per heightmap texel.
    float tw = params.world_size / float(sz.x);
    float th = params.world_size / float(sz.y);

    float gx = (hR - hL) / (2.0 * tw);
    float gz = (hD - hU) / (2.0 * th);
    return normalize(vec3(-gx, 1.0, -gz));
}

void main() {
    ivec2 pos  = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(out_displacement);
    if (pos.x >= size.x || pos.y >= size.y) return;

    vec2 uv        = vec2(pos) / vec2(size - 1);
    vec2 world_pos = uv * params.world_size;

    // ---------------------------------------------------------------
    // 1. Gerstner summation (raw, uncaged)
    // ---------------------------------------------------------------
    vec3  gerstner_n;
    float accel_y;
    vec3  eta = gerstner_eval(world_pos, gerstner_n, accel_y);

    // ---------------------------------------------------------------
    // 2. Read cage parameters
    // ---------------------------------------------------------------
    vec4  cage    = texture(static_cage_tex, uv);
    float r1_raw  = cage.r;   // shore-facing radius
    float r2_raw  = cage.g;   // along-shore radius
    float r3_raw  = cage.b;   // depth radius
    float s_angle = cage.a;   // shore angle (radians)

    // Land / waterline pixels: output zero displacement (waterline Dirichlet condition).
    if (r1_raw < 0.01 && r3_raw < 0.01) {
        imageStore(out_displacement, pos, vec4(0.0));
        imageStore(out_normal,       pos, vec4(0.0, 1.0, 0.0, 1.0));
        return;
    }

    // Guard against division-by-zero in the ellipsoidal-space transform.
    float r1 = max(r1_raw, 0.001);
    float r2 = max(r2_raw, 0.001);
    float r3 = max(r3_raw, 0.001);

    // ---------------------------------------------------------------
    // 3. Build cage frame A = [a1, a2, a3]  (Jeschke §4)
    //
    //   a3 = seafloor surface normal (from heightmap gradients)
    //   a1 = shore direction projected onto a3's tangent plane
    //   a2 = cross(a3, a1)   (along-shore axis)
    // ---------------------------------------------------------------
    vec3 a3 = terrain_normal_at(uv);

    // Project the packed shore direction onto the tangent plane of a3.
    vec3 a1 = vec3(cos(s_angle), 0.0, sin(s_angle));
    a1 = a1 - dot(a1, a3) * a3;
    if (length(a1) < 0.001) {
        // Degenerate: terrain is nearly vertical along shore direction — try X, then Z.
        a1 = vec3(1.0, 0.0, 0.0) - dot(vec3(1.0, 0.0, 0.0), a3) * a3;
        if (length(a1) < 0.001)
            a1 = vec3(0.0, 0.0, 1.0) - dot(vec3(0.0, 0.0, 1.0), a3) * a3;
    }
    a1 = normalize(a1);
    vec3 a2 = normalize(cross(a3, a1));

    // ---------------------------------------------------------------
    // 4. Cage warping — soft-clip η in ellipsoidal space.
    //
    //   Correct formula (paper §4, fixing the original code's error):
    //
    //     e  = R⁻¹ Aᵀ η          — project η into normalised ellipsoidal coords
    //     ê  = tanh(s|e|)/|e| · e — tanh soft-clip, preserving direction
    //     η_c = A R ê             — map back to world space
    //
    //   This applies the clipping asymmetrically per axis (each radius ri
    //   independently limits displacement in that cage direction), which is
    //   the key correction over the previous code that clipped in world-space.
    // ---------------------------------------------------------------
    float e1   = dot(a1, eta) / r1;
    float e2   = dot(a2, eta) / r2;
    float e3   = dot(a3, eta) / r3;
    float elen = length(vec3(e1, e2, e3));

    vec3 en = vec3(0.0);
    if (elen > 0.0001) {
        float clip = tanh(params.cage_softness * elen) / elen;
        en = vec3(e1, e2, e3) * clip;
    }

    // A R ê — reconstruct world-space cage-warped displacement.
    vec3 eta_c = a1 * (en.x * r1)
               + a2 * (en.y * r2)
               + a3 * (en.z * r3);

    // ---------------------------------------------------------------
    // 5. Breaking-wave trigger.
    //    A crest breaks when vertical acceleration exceeds gravity (|a_y| > g).
    //    Normalise to [0, 1] so the spatial shader can threshold at 0.5.
    // ---------------------------------------------------------------
    float breaking = clamp(abs(accel_y) / 9.8, 0.0, 1.0);

    // ---------------------------------------------------------------
    // 6. Write outputs
    //    displacement: XYZ = cage-warped displacement, A = breaking trigger
    //    normal:       XYZ = analytic Gerstner surface normal (world space)
    // ---------------------------------------------------------------
    imageStore(out_displacement, pos, vec4(eta_c, breaking));
    imageStore(out_normal,       pos, vec4(gerstner_n, 1.0));
}
