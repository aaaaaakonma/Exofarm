#[compute]
#version 450

// ----------------------------------------------------------------------
// ExoFarm: Yuksel (2007) Wave Particle Simulation on Boundary Wave Cages
// Evaluates discrete wave packet particles on GPU:
// - Hann window compact radial kernel envelope
// - Longitudinal wave carrier oscillation
// - Gerstner-style horizontal peaking (trochoidal sharpening)
// - Analytic surface normal derivation
// - Vertical acceleration calculation for breaking whitecap trigger
// - Jeschke 2020 Ellipsoidal Boundary Cage soft-clipping
// ----------------------------------------------------------------------

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

// Terrain heightmap — sampled to reconstruct seafloor surface normal (a3)
layout(set = 1, binding = 1) uniform sampler2D heightmap_tex;

// Wave parameters UBO (32 bytes, std140).
layout(set = 2, binding = 0) uniform WaveParams {
    float time;
    float cage_softness;         // tanh 's' parameter (Jeschke §4, must be >> 1, e.g. 6.0)
    int   num_particles;         // active particle count
    float world_size;            // physical world extent in metres
    float height_scale_factor;   // heightmap [0..1] → world-space metres
    float sea_level_normalized;  // sea level as a [0..1] heightmap value
    float _pad0;
    float _pad1;
} params;

// Per-particle data (std430, 64 bytes each).
struct WaveParticle {
    vec2  pos;         // world position (x, z) [metres]
    vec2  dir;         // normalised propagation direction (dx, dz)
    float speed;       // propagation velocity [m/s]
    float amplitude;   // wave packet height [metres]
    float radius;      // kernel influence radius [metres]
    float wavelength;  // spatial wavelength [metres]
    float age;         // current lifetime [seconds]
    float lifetime;    // total lifespan [seconds]
    float steepness;   // Gerstner Q horizontal choppiness [0..1]
    float is_active;   // 1.0 = active, 0.0 = inactive
    vec4  _pad;        // padding to 64 bytes
};

layout(set = 3, binding = 0, std430) buffer ParticleData {
    WaveParticle particles[];
};

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

    float tw = params.world_size / float(sz.x);
    float th = params.world_size / float(sz.y);

    float gx = (hR - hL) / (2.0 * tw);
    float gz = (hD - hU) / (2.0 * th);
    return normalize(vec3(-gx, 1.0, -gz));
}

// ---------------------------------------------------------------
// Yuksel et al. (2007) Wave Particle Superposition
// Evaluates the sum of all overlapping active wave packet kernels.
// ---------------------------------------------------------------
vec3 evaluate_wave_particles(vec2 world_pos, out vec3 out_n, out float out_accel) {
    vec3  eta    = vec3(0.0);
    float sum_nx = 0.0;
    float sum_nz = 0.0;
    float sum_ny = 0.0;
    float accel  = 0.0;

    int count = params.num_particles;
    for (int i = 0; i < count; i++) {
        if (particles[i].is_active < 0.5) continue;

        vec2 diff = world_pos - particles[i].pos;
        float dist = length(diff);
        float r = particles[i].radius;
        if (dist > r || r < 0.001) continue;

        float u = dist / r; // [0, 1]
        // Smooth Hann window envelope (0 at edge, 1 at center, C1 smooth)
        float pi_u = 3.14159265359 * u;
        float envelope = 0.5 * (cos(pi_u) + 1.0);
        float d_env = -0.5 * (3.14159265359 / r) * sin(pi_u);

        // Carrier wave oscillation along propagation direction
        float k = 6.28318530718 / max(0.1, particles[i].wavelength);
        float phase = k * dot(diff, particles[i].dir);
        float cp = cos(phase);
        float sp = sin(phase);

        float amp = particles[i].amplitude;
        float qi  = clamp(particles[i].steepness, 0.0, 1.0);

        // Vertical displacement:
        float dy = amp * envelope * cp;
        eta.y += dy;

        // Horizontal trochoidal displacement (Gerstner-style sharpening toward crests):
        vec2 h_disp = -qi * amp * particles[i].dir * envelope * sp;
        eta.x += h_disp.x;
        eta.z += h_disp.y;

        // Analytic surface normal gradient:
        vec2 dist_grad = (dist > 0.0001) ? (diff / dist) : vec2(0.0);
        vec2 grad_y = d_env * cp * dist_grad - envelope * k * sp * particles[i].dir;
        sum_nx -= amp * grad_y.x;
        sum_nz -= amp * grad_y.y;
        sum_ny += qi * amp * k * envelope * cp;

        // Second time-derivative of vertical displacement:
        float omega = k * particles[i].speed;
        accel += -amp * omega * omega * envelope * cp;
    }

    out_n     = normalize(vec3(sum_nx, 1.0 - sum_ny, sum_nz));
    out_accel = accel;
    return eta;
}

void main() {
    ivec2 pos  = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(out_displacement);
    if (pos.x >= size.x || pos.y >= size.y) return;

    vec2 uv        = vec2(pos) / vec2(size - 1);
    vec2 world_pos = uv * params.world_size;

    // ---------------------------------------------------------------
    // 1. Read cage parameters
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

    // ---------------------------------------------------------------
    // 2. Wave Particle summation (raw, uncaged)
    // ---------------------------------------------------------------
    vec3  wp_normal;
    float accel_y;
    vec3  eta = evaluate_wave_particles(world_pos, wp_normal, accel_y);

    // Guard against division-by-zero in ellipsoidal-space transform.
    float r1 = max(r1_raw, 0.001);
    float r2 = max(r2_raw, 0.001);
    float r3 = max(r3_raw, 0.001);

    // ---------------------------------------------------------------
    // 3. Build cage frame A = [a1, a2, a3]  (Jeschke §4)
    // ---------------------------------------------------------------
    vec3 a3 = terrain_normal_at(uv);

    // Project packed shore direction onto tangent plane of a3.
    vec3 a1 = vec3(cos(s_angle), 0.0, sin(s_angle));
    a1 = a1 - dot(a1, a3) * a3;
    if (length(a1) < 0.001) {
        a1 = vec3(1.0, 0.0, 0.0) - dot(vec3(1.0, 0.0, 0.0), a3) * a3;
        if (length(a1) < 0.001)
            a1 = vec3(0.0, 0.0, 1.0) - dot(vec3(0.0, 0.0, 1.0), a3) * a3;
    }
    a1 = normalize(a1);
    vec3 a2 = normalize(cross(a3, a1));

    // ---------------------------------------------------------------
    // 4. Cage warping — soft-clip η in ellipsoidal space.
    // ---------------------------------------------------------------
    float e1   = dot(a1, eta) / r1;
    float e2   = dot(a2, eta) / r2;
    float e3   = dot(a3, eta) / r3;
    float elen = length(vec3(e1, e2, e3));

    vec3 en = vec3(0.0);
    float cage_ratio = 1.0;
    if (elen > 0.0001) {
        // Jeschke 2020 ellipsoidal cage soft-clipping:
        // Bound displacement magnitude strictly within the unit ellipsoid (||en|| < 1.0),
        // while preserving unconstrained Gerstner waves when elen << 1 in open water (clip -> 1.0).
        float p = max(2.0, params.cage_softness);
        float clip = 1.0 / pow(1.0 + pow(elen, p), 1.0 / p);
        en = vec3(e1, e2, e3) * clip;
        cage_ratio = clamp(clip, 0.0, 1.0);  // Attenuation ratio for normal warping
    }

    // A R ê — reconstruct world-space cage-warped displacement.
    vec3 eta_c = a1 * (en.x * r1)
               + a2 * (en.y * r2)
               + a3 * (en.z * r3);

    // Cage-warp the normal: lerp toward flat (0,1,0) by the same attenuation
    // ratio so specular highlights match the attenuated displacement near shore.
    vec3 caged_normal = normalize(mix(vec3(0.0, 1.0, 0.0), wp_normal, cage_ratio));

    // ---------------------------------------------------------------
    // 5. Breaking-wave trigger.
    //    A crest breaks when vertical acceleration exceeds gravity (|a_y| > g).
    // ---------------------------------------------------------------
    float breaking = clamp(abs(accel_y) / 9.8, 0.0, 1.0);

    // ---------------------------------------------------------------
    // 6. Write outputs
    //    displacement: XYZ = cage-warped displacement, A = breaking trigger
    //    normal:       XYZ = analytic cage-warped surface normal (world space)
    // ---------------------------------------------------------------
    imageStore(out_displacement, pos, vec4(eta_c, breaking));
    imageStore(out_normal,       pos, vec4(caged_normal, 1.0));
}
