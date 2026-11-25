shader_type spatial;
render_mode blend_mix, depth_draw_always, cull_disabled, diffuse_burley, specular_schlick_ggx;

// ----------------------------------------------------------------------
// ExoFarm: Continuous 3D Flowing River Water Ribbon Shader
// Features:
// - Physical longitudinal UV flow mapping along river reaches
// - Dual-frequency counter-scrolling ripple normals & specular sun glints
// - Procedural edge foam / white water on channel banks
// - Soft edge alpha fading to seamlessly merge ribbon geometry into carved riverbeds
// - Schlick Fresnel reflection for dynamic water surface sheen
// ----------------------------------------------------------------------

uniform vec3  shallow_color       : source_color = vec3(0.16, 0.62, 0.82);
uniform vec3  deep_color          : source_color = vec3(0.04, 0.28, 0.52);
uniform vec3  foam_color          : source_color = vec3(0.92, 0.96, 1.0);

uniform float flow_speed          : hint_range(0.0, 5.0) = 1.2;
uniform float water_transparency  : hint_range(0.1, 1.0) = 0.86;
uniform float roughness_val       : hint_range(0.0, 1.0) = 0.12;
uniform float metallic_val        : hint_range(0.0, 1.0) = 0.08;
uniform float specular_val        : hint_range(0.0, 1.0) = 0.70;

uniform float ripple_scale_u      : hint_range(0.5, 10.0) = 2.0;
uniform float ripple_scale_v      : hint_range(0.1, 5.0) = 0.6;
uniform float ripple_strength     : hint_range(0.0, 2.0) = 0.45;

uniform float edge_foam_width     : hint_range(0.0, 0.5) = 0.22;
uniform float edge_foam_strength  : hint_range(0.0, 2.0) = 0.85;
uniform float edge_fade_width     : hint_range(0.0, 0.25) = 0.08;

uniform sampler2D depth_texture   : hint_depth_texture, filter_linear_mipmap;

varying vec3 v_world_pos;
varying vec3 v_normal;
varying vec2 v_uv;

// Fast deterministic 2D hash and gradient noise
vec2 hash2(vec2 p) {
	p = vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)));
	return -1.0 + 2.0 * fract(sin(p) * 43758.5453123);
}

float noise2d(vec2 p) {
	vec2 i = floor(p);
	vec2 f = fract(p);
	vec2 u = f * f * (3.0 - 2.0 * f);
	return mix(
		mix(dot(hash2(i + vec2(0.0, 0.0)), f - vec2(0.0, 0.0)),
		    dot(hash2(i + vec2(1.0, 0.0)), f - vec2(1.0, 0.0)), u.x),
		mix(dot(hash2(i + vec2(0.0, 1.0)), f - vec2(0.0, 1.0)),
		    dot(hash2(i + vec2(1.0, 1.0)), f - vec2(1.0, 1.0)), u.x),
		u.y
	);
}

// Multi-octave wave height evaluation
float sample_waves(vec2 uv, float time) {
	vec2 uv1 = vec2(uv.x * ripple_scale_u, (uv.y * ripple_scale_v) - (time * flow_speed * 0.35));
	vec2 uv2 = vec2((uv.x * ripple_scale_u * 1.6) + 0.4, (uv.y * ripple_scale_v * 1.4) - (time * flow_speed * 0.52));
	
	float h1 = noise2d(uv1);
	float h2 = noise2d(uv2) * 0.5;
	return h1 + h2;
}

void vertex() {
	v_uv = UV;
	v_normal = NORMAL;
	v_world_pos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
}

void fragment() {
	// 1. Calculate Analytical Wave Perturbation
	float eps = 0.05;
	float h_center = sample_waves(v_uv, TIME);
	float h_dx = sample_waves(v_uv + vec2(eps, 0.0), TIME) - h_center;
	float h_dy = sample_waves(v_uv + vec2(0.0, eps), TIME) - h_center;
	
	vec3 tangent_norm = normalize(vec3(-h_dx * ripple_strength, 1.0, -h_dy * ripple_strength));
	NORMAL = normalize(NORMAL + tangent_norm * 0.6);
	
	// 2. Transverse Bank Distance & Foam
	// UV.x runs from 0.0 (left bank) to 1.0 (right bank), 0.5 is centerline
	float bank_dist = abs(v_uv.x - 0.5) * 2.0; // 0 at center, 1 at banks
	
	// Parabolic water depth approximation across channel (deepest in center)
	float depth_factor = 1.0 - (bank_dist * bank_dist);
	vec3 base_water = mix(shallow_color, deep_color, clamp(depth_factor, 0.0, 1.0));
	
	// Edge foam noise
	float foam_mask = smoothstep(1.0 - edge_foam_width, 1.0, bank_dist);
	vec2 foam_uv = vec2(v_uv.x * 8.0, (v_uv.y * 3.0) - (TIME * flow_speed * 0.7));
	float foam_noise = clamp(noise2d(foam_uv) * 0.5 + 0.6, 0.0, 1.0);
	float final_foam = foam_mask * foam_noise * edge_foam_strength;
	
	// 3. Schlick Fresnel Sky Reflection
	float fresnel = pow(1.0 - clamp(dot(NORMAL, VIEW), 0.0, 1.0), 4.0);
	vec3 color = mix(base_water, foam_color, clamp(final_foam, 0.0, 1.0));
	color = mix(color, vec3(0.9, 0.95, 1.0), fresnel * 0.35);
	
	// 4. Edge Fade & Depth Fade for Seamless Bank Merging
	float edge_fade = 1.0;
	if (edge_fade_width > 0.001) {
		edge_fade = smoothstep(1.0, 1.0 - edge_fade_width, bank_dist);
	}
	
	// Screen-space depth soft shore fading
	float depth_raw = textureLod(depth_texture, SCREEN_UV, 0.0).r;
	vec4 upos = INV_PROJECTION_MATRIX * vec4(SCREEN_UV * 2.0 - 1.0, depth_raw, 1.0);
	vec3 scene_view_pos = upos.xyz / upos.w;
	// In view space, looking down -Z. VERTEX.z is negative.
	float water_depth = max(0.0, VERTEX.z - scene_view_pos.z);
	float depth_fade = clamp(water_depth * 3.0, 0.0, 1.0);
	
	// Vertex color modulates water tint and flow energy
	color *= COLOR.rgb;
	
	ALBEDO = color;
	ROUGHNESS = mix(roughness_val, 0.7, final_foam);
	METALLIC = metallic_val;
	SPECULAR = specular_val;
	float base_alpha = water_transparency * edge_fade;
	ALPHA = clamp((base_alpha * max(0.45, depth_fade)) + final_foam * 0.5, 0.0, 1.0);
}
