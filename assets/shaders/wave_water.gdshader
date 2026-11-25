shader_type spatial;
render_mode blend_mix, depth_draw_always, cull_back, diffuse_burley, specular_schlick_ggx;

// ----------------------------------------------------------------------
// ExoFarm: Sophisticated Boundary-Aware Ocean & Water Surface Shader
// Features:
// - Physical Beer-Lambert optical depth light absorption
// - Artifact-free refraction of submerged terrain without coastline halos
// - Dual-octave counter-drifting micro-ripple normals & sun glitter
// - Procedural shallow water seabed caustics
// - Wave crest subsurface scattering (SSS) & Schlick Fresnel sky reflection
// - Seamless Jeschke 2020 boundary cage shoreline flattening
// ----------------------------------------------------------------------

// ---- Inputs from Compute Pipeline & Heightmap ----
// XYZ = cage-warped wave displacement (world space), A = breaking trigger [0..1]
uniform sampler2D displacement_tex : filter_linear, repeat_disable;
// XYZ = analytic surface normal (world space)
uniform sampler2D normal_tex        : filter_linear, repeat_disable;
// Terrain heightmap (for boundary context)
uniform sampler2D heightmap_tex     : filter_linear, repeat_disable;

// Depth & Screen textures (Forward+ / Mobile)
uniform sampler2D depth_texture     : hint_depth_texture, filter_linear, repeat_disable;
uniform sampler2D screen_texture    : hint_screen_texture, filter_linear, repeat_disable;

// World & Transform Parameters
uniform vec2  heightmap_world_size  = vec2(1024.0, 1024.0);
uniform float height_scale          = 0.4;
uniform float sea_level_offset      = 0.0;

// Water Color & Optical Absorption (Beer-Lambert)
uniform vec3  shallow_color : source_color = vec3(0.08, 0.68, 0.75);
uniform vec3  mid_color     : source_color = vec3(0.03, 0.35, 0.62);
uniform vec3  deep_color    : source_color = vec3(0.01, 0.08, 0.24);
uniform float depth_density : hint_range(0.01, 0.5) = 0.08;

// Waves & Micro-Ripples
uniform float normal_strength       : hint_range(0.0, 2.0) = 1.0;
uniform float micro_ripple_strength : hint_range(0.0, 1.0) = 0.40;
uniform float micro_ripple_scale    : hint_range(0.1, 5.0) = 0.85;

// Refraction & Caustics
uniform float refraction_strength   : hint_range(0.0, 1.0) = 0.12;
uniform float caustics_strength     : hint_range(0.0, 1.0) = 0.28;

// Subsurface Scattering (Wave Translucency)
uniform vec3  sss_color             : source_color = vec3(0.12, 0.85, 0.65);
uniform float sss_strength          : hint_range(0.0, 2.0) = 0.70;

// Foam & Shoreline Dynamics
uniform vec3  foam_color            : source_color = vec3(0.94, 0.97, 1.0);
uniform float shore_foam_distance   : hint_range(0.1, 5.0) = 1.4;
uniform float shore_foam_intensity  : hint_range(0.0, 2.0) = 0.95;
uniform float crest_foam_intensity  : hint_range(0.0, 2.0) = 1.15;
uniform float crest_foam_threshold  : hint_range(0.1, 1.0) = 0.45;

varying vec3 v_world_pos;
varying vec2 v_uv;

// ---- Procedural Noise & Cellular Helpers ----
vec2 hash2(vec2 p) {
	p = vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)));
	return -1.0 + 2.0 * fract(sin(p) * 43758.5453123);
}

float noise2d(vec2 p) {
	vec2 i = floor(p);
	vec2 f = fract(p);
	vec2 u = f * f * (3.0 - 2.0 * f);
	return mix(mix(dot(hash2(i + vec2(0.0, 0.0)), f - vec2(0.0, 0.0)),
	               dot(hash2(i + vec2(1.0, 0.0)), f - vec2(1.0, 0.0)), u.x),
	           mix(dot(hash2(i + vec2(0.0, 1.0)), f - vec2(0.0, 1.0)),
	               dot(hash2(i + vec2(1.0, 1.0)), f - vec2(1.0, 1.0)), u.x), u.y);
}

float voronoi2d(vec2 p) {
	vec2 n = floor(p);
	vec2 f = fract(p);
	float md = 8.0;
	for (int j = -1; j <= 1; j++) {
		for (int i = -1; i <= 1; i++) {
			vec2 g = vec2(float(i), float(j));
			vec2 o = hash2(n + g) * 0.5 + 0.5;
			vec2 r = g + o - f;
			float d = dot(r, r);
			if (d < md) md = d;
		}
	}
	return sqrt(md);
}

void vertex() {
	v_uv = UV;
	
	// Sample cage-warped displacement from the compute pipeline
	vec4 disp_data = texture(displacement_tex, UV);
	VERTEX += disp_data.xyz;
	
	v_world_pos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
}

void fragment() {
	// -------------------------------------------------------------
	// 1. Wave Normal + Micro-Ripple Synthesis
	// -------------------------------------------------------------
	vec3 wave_n_world = texture(normal_tex, v_uv).rgb;
	if (length(wave_n_world) > 0.01) {
		wave_n_world = normalize(wave_n_world);
	} else {
		wave_n_world = vec3(0.0, 1.0, 0.0);
	}
	
	// Dual-octave counter-drifting micro ripples
	vec2 rip_uv1 = v_world_pos.xz * (micro_ripple_scale * 0.16) + vec2(TIME * 0.038, TIME * 0.024);
	vec2 rip_uv2 = v_world_pos.xz * (micro_ripple_scale * 0.24) - vec2(TIME * 0.028, -TIME * 0.042);
	float r1 = noise2d(rip_uv1);
	float r2 = noise2d(rip_uv2);
	vec3 micro_n_world = normalize(vec3((r1 - 0.5) * 1.5 + (r2 - 0.5) * 1.2, 2.8, (r2 - 0.5) * 1.5 - (r1 - 0.5) * 1.2));
	
	// Blend swell normal with micro ripples
	vec3 blended_n_world = normalize(mix(wave_n_world, micro_n_world, micro_ripple_strength));
	blended_n_world = normalize(mix(vec3(0.0, 1.0, 0.0), blended_n_world, normal_strength));
	
	// Transform world normal to view space for Godot PBR lighting
	vec3 normal_view = normalize((VIEW_MATRIX * vec4(blended_n_world, 0.0)).xyz);
	NORMAL = normal_view;
	
	// -------------------------------------------------------------
	// 2. Physical Water Depth (Linear View-Space Reconstruction)
	// -------------------------------------------------------------
	float depth_raw = textureLod(depth_texture, SCREEN_UV, 0.0).r;
	vec4 upos = INV_PROJECTION_MATRIX * vec4(SCREEN_UV * 2.0 - 1.0, depth_raw, 1.0);
	vec3 scene_view_pos = upos.xyz / upos.w;
	// In view space, looking down -Z. VERTEX.z is negative.
	float water_depth = max(0.0, VERTEX.z - scene_view_pos.z);
	
	// -------------------------------------------------------------
	// 3. Artifact-Free Refraction of Submerged Terrain
	// -------------------------------------------------------------
	// Distort screen UV along normal, scaled by water depth
	vec2 refr_offset = normal_view.xy * (refraction_strength * 0.04) * clamp(water_depth * 0.4, 0.0, 1.0);
	vec2 refr_uv = SCREEN_UV - refr_offset;
	
	// Safety check: ensure refracted sample does not cross onto dry land above water
	float refr_depth_raw = textureLod(depth_texture, refr_uv, 0.0).r;
	vec4 refr_upos = INV_PROJECTION_MATRIX * vec4(refr_uv * 2.0 - 1.0, refr_depth_raw, 1.0);
	vec3 refr_view_pos = refr_upos.xyz / refr_upos.w;
	float refr_water_depth = VERTEX.z - refr_view_pos.z;
	if (refr_water_depth < 0.001) {
		refr_uv = SCREEN_UV; // Fallback to unrefracted sample at coastline
	}
	vec3 scene_color = textureLod(screen_texture, refr_uv, 0.0).rgb;
	
	// -------------------------------------------------------------
	// 4. Procedural Seabed Caustics in Shallow Waters
	// -------------------------------------------------------------
	if (water_depth > 0.02 && water_depth < 10.0) {
		vec2 c_uv = v_world_pos.xz * 0.35;
		float c1 = voronoi2d(c_uv + vec2(TIME * 0.055, TIME * 0.038));
		float c2 = voronoi2d(c_uv * 1.3 - vec2(TIME * 0.045, -TIME * 0.065));
		float caustic_mesh = clamp(pow(1.0 - min(c1, c2), 3.0), 0.0, 1.0);
		float caustic_falloff = exp(-water_depth * 0.32);
		scene_color += caustic_mesh * vec3(0.18, 0.38, 0.45) * caustics_strength * caustic_falloff;
	}
	
	// -------------------------------------------------------------
	// 5. Multi-Stop Beer-Lambert Optical Light Absorption
	// -------------------------------------------------------------
	float depth_factor = clamp(1.0 - exp(-water_depth * depth_density), 0.0, 1.0);
	vec3 water_body_color;
	if (depth_factor < 0.38) {
		water_body_color = mix(shallow_color, mid_color, depth_factor / 0.38);
	} else {
		water_body_color = mix(mid_color, deep_color, (depth_factor - 0.38) / 0.62);
	}

	// Estuary Outflow Plume: Alluvial sediment tint in river discharge zones
	vec4 hm_sample = texture(heightmap_tex, v_uv);
	vec2 plume_flow = hm_sample.ba;
	float plume_mag = length(plume_flow);
	if (plume_mag > 0.05) {
		vec3 silt_tint = vec3(0.16, 0.46, 0.38); // Warm sandy jade / alluvial silt
		float silt_factor = clamp(plume_mag * 0.40, 0.0, 0.70);
		water_body_color = mix(water_body_color, silt_tint, silt_factor);
	}
	
	// Selective wavelength absorption: seabed light loses warm red tones faster underwater
	vec3 absorption_tint = mix(vec3(1.0), shallow_color * 0.85, clamp(water_depth * 0.18, 0.0, 1.0));
	vec3 absorbed_scene = scene_color * absorption_tint;
	vec3 final_albedo = mix(absorbed_scene, water_body_color, depth_factor);
	
	// -------------------------------------------------------------
	// 6. Subsurface Scattering (Backlit Emerald Crest Translucency)
	// -------------------------------------------------------------
	vec4 disp_sample = texture(displacement_tex, v_uv);
	vec3 view_dir_world = normalize(CAMERA_POSITION_WORLD - v_world_pos);
	vec3 sun_dir_approx = normalize(vec3(0.55, 0.75, -0.35));
	float sss_dot = clamp(dot(-view_dir_world, sun_dir_approx), 0.0, 1.0);
	float crest_elev = clamp(disp_sample.y * 0.5 + 0.5, 0.0, 1.0);
	float sss_light = pow(sss_dot, 3.2) * crest_elev * sss_strength;
	final_albedo += sss_color * sss_light;
	
	// -------------------------------------------------------------
	// 7. Dynamic Wave Crest Whitecaps & Shoreline Surf Foam
	// -------------------------------------------------------------
	float total_foam = 0.0;
	
	// A. Wave Crest Whitecaps (Breaking waves from compute pipeline disp_sample.a)
	float break_trigger = disp_sample.a;
	if (break_trigger > crest_foam_threshold) {
		float crest_mask = smoothstep(crest_foam_threshold, 1.0, break_trigger);
		vec2 crest_noise_uv = v_world_pos.xz * 1.8 + vec2(TIME * 0.15, TIME * 0.08);
		float crest_noise = clamp(noise2d(crest_noise_uv) * 0.6 + noise2d(crest_noise_uv * 2.2) * 0.4, 0.0, 1.0);
		total_foam += crest_mask * crest_noise * crest_foam_intensity;
	}
	
	// B. Shoreline Surf & Lapping Foam Ring
	if (water_depth < shore_foam_distance) {
		float shore_ratio = 1.0 - clamp(water_depth / shore_foam_distance, 0.0, 1.0);
		float shore_wave = sin((v_world_pos.x + v_world_pos.z) * 0.8 - TIME * 2.2) * 0.5 + 0.5;
		vec2 shore_noise_uv = v_world_pos.xz * 2.5 + vec2(TIME * 0.08, -TIME * 0.06);
		float shore_noise = clamp(noise2d(shore_noise_uv) * 0.7 + noise2d(shore_noise_uv * 3.1) * 0.3, 0.0, 1.0);
		float shore_foam = pow(shore_ratio, 1.4) * (shore_noise * 0.7 + shore_wave * 0.3) * shore_foam_intensity;
		total_foam += shore_foam;
	}
	
	total_foam = clamp(total_foam, 0.0, 1.0);
	final_albedo = mix(final_albedo, foam_color, total_foam);

	// -------------------------------------------------------------
	// 8. Surface PBR Shading & Fresnel Rim Reflectivity
	// -------------------------------------------------------------
	ALBEDO = final_albedo;
	
	// Ultra-glossy water surface, rougher where foamy
	ROUGHNESS = mix(0.04, 0.60, total_foam);
	SPECULAR = mix(0.85, 0.35, total_foam);
	
	// Schlick Fresnel: grazing angles have high specular sky reflectivity
	float fresnel = pow(1.0 - clamp(dot(normalize(-VERTEX), NORMAL), 0.0, 1.0), 4.5);
	SPECULAR = mix(SPECULAR, 1.0, fresnel * 0.6);
}
