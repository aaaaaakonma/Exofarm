shader_type spatial;
render_mode blend_mix, depth_draw_always, cull_back, diffuse_burley, specular_schlick_ggx;

// ---- Inputs from the compute pipeline ----
// XYZ = cage-warped wave displacement (world space), A = breaking trigger [0..1].
uniform sampler2D displacement_tex : filter_linear, repeat_disable;
// XYZ = analytic Gerstner surface normal (world space), A unused.
uniform sampler2D normal_tex        : filter_linear, repeat_disable;

// ---- Terrain (for vertex clipping below seafloor) ----
uniform sampler2D heightmap_tex : filter_linear, repeat_disable;
uniform float height_scale   = 0.5;
uniform float sea_level_offset = 15.0;    // matches terrain shader sea threshold
uniform vec2  heightmap_world_size = vec2(1024.0, 1024.0);

// ---- Water appearance ----
uniform vec3  albedo : source_color = vec3(0.0, 0.3, 0.5);
uniform float roughness      : hint_range(0.0, 1.0)  = 0.1;
uniform float specular                                = 0.5;
// Scale factor for wave normals (1.0 = full strength, 0.0 = flat).
uniform float normal_strength : hint_range(0.0, 2.0) = 1.0;
// Foam colour blended in when the breaking trigger exceeds 0.5.
uniform vec3  foam_color : source_color = vec3(1.0, 1.0, 1.0);


void vertex() {
	// Apply cage-warped displacement to each vertex.
	vec4 disp_data = texture(displacement_tex, UV);
	VERTEX += disp_data.xyz;

	// The Jeschke wave cages inherently prevent the wave from intersecting the seafloor,
	// so we do NOT need to manually snap vertices up to the terrain. Doing so causes
	// jagged sawtooth artifacts at the shoreline when horizontal displacement pushes
	// vertices near the sloped beach.
}


void fragment() {
	ALBEDO    = albedo;
	ROUGHNESS = roughness;
	SPECULAR  = specular;

	// ---- Surface Normal ----
	// The compute shader writes analytic Gerstner normals in world space.
	// Transform them to view space for Godot's NORMAL built-in.
	vec3 wave_n_world = texture(normal_tex, UV).rgb;
	if (length(wave_n_world) > 0.01) {
		wave_n_world = normalize(wave_n_world);
		// Blend between flat up-normal and the wave normal based on strength.
		vec3 flat_n = vec3(0.0, 1.0, 0.0);
		wave_n_world = normalize(mix(flat_n, wave_n_world, normal_strength));
		// VIEW_MATRIX transforms world-space directions to camera space.
		// w=0 ignores translation, making this correct for normals.
		NORMAL = normalize((VIEW_MATRIX * vec4(wave_n_world, 0.0)).xyz);
	}

	// ---- Breaking Waves / Foam ----
	// The displacement texture's alpha channel holds the normalised vertical
	// acceleration (0 = calm, 1 = fully breaking). Blend in foam smoothly on wave crests.
	float breaking = texture(displacement_tex, UV).a;
	float foam_factor = smoothstep(0.60, 0.92, breaking);
	if (foam_factor > 0.0) {
		ALBEDO = mix(ALBEDO, foam_color, foam_factor * 0.75);
	}
}
