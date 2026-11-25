shader_type spatial;
render_mode blend_mix, depth_draw_opaque, cull_back, diffuse_burley, specular_schlick_ggx;

// ----------------------------------------------------------------------
// ExoFarm: Sophisticated 3D Continuous Tectonic Terrain Shader
// Features:
// - Physical Geomorphological Rock Exposure Model:
//     * Heightfield Laplacian curvature (convex ridges strip soil; concave ravines pool sediment)
//     * Angle-of-repose slope drive (replaces crude naive slope cutoff)
//     * Altitude-based soil depth thinning (thick valley regolith -> bare alpine bedrock)
//     * Fluvial incision scouring along river channels (vertex color blue channel)
//     * Organic geological fracture & strata noise modulation
// - Physical Height-Blended PBR Transitions with displacement maps (_orm.b)
// - Texture2DArray multi-layer PBR sampling with Dual-Scale Anti-Tiling
// - Biome-aware Cliff Triplanar splitting:
//     * Rock028 (Metamorphic Slate) for default Alpine / Collision belts
//     * Rock030 (Layered Sedimentary Sandstone) for River Canyons & Arid Scarps
//     * Rock031 (Dark Charcoal Shale) for Deep Shear Ravines & Clefts
//     * Rock063 (Mossy Boulders) for Forest Slopes
// - Simplified Whittaker Biomes (Ocean 0, Desert 2, Grassland 3, Forest 4):
//     * Grass005 (Clean short under-turf)
//     * Grass006 (Lush meadow & clover forest floor)
//     * Ground033 (Aerodynamic desert dunes)
//     * Ground100 (Wave-sculpted seabed sand ripples)
//     * Gravel020 (Smooth rounded river cobbles)
//     * Gravel036L (High-altitude granite talus grit)
// - Crystalline Glacial Snow Peaks with physical slope slippage
// - Dynamic shoreline moisture / wet-sand darkening near water level
// ----------------------------------------------------------------------

uniform sampler2D heightmap_tex : filter_linear, repeat_disable;
uniform sampler2D biome_tex     : filter_nearest, repeat_disable;

// Texture2DArray Samplers (14 Slices)
uniform sampler2DArray terrain_albedo_array : source_color, filter_linear_mipmap_anisotropic;
uniform sampler2DArray terrain_normal_array : hint_normal, filter_linear_mipmap_anisotropic;
uniform sampler2DArray terrain_orm_array    : filter_linear_mipmap_anisotropic;
uniform bool use_texture_arrays = false;
uniform float uv_scale_ground   : hint_range(0.01, 1.0) = 0.20;
uniform float uv_scale_cliff    : hint_range(0.01, 1.0) = 0.15;

uniform vec2  world_size        = vec2(1024.0, 1024.0);
uniform float height_scale      = 0.4;
uniform float sea_level         = 0.0;

// Geomorphology & Shading Knobs
uniform float cliff_slope_threshold   : hint_range(0.2, 0.9)  = 0.55;
uniform float curvature_strength      : hint_range(0.0, 2.0)  = 0.45;
uniform float soil_depth_scale        : hint_range(5.0, 60.0) = 28.0;
uniform float height_blend_contrast   : hint_range(0.02, 0.5) = 0.16;
uniform float rock_elevation_threshold: hint_range(15.0, 60.0) = 35.0;
uniform float snow_elevation_threshold: hint_range(40.0, 90.0) = 60.0;
uniform float beach_threshold         : hint_range(0.5, 8.0)  = 3.2;
uniform float detail_normal_strength  : hint_range(0.0, 1.5)  = 0.60;
uniform float strata_frequency        : hint_range(0.2, 4.0)  = 1.1;
uniform bool      show_rivers             = true;
uniform sampler2D water_depth_tex         : hint_default_black, filter_linear;

// Texture2DArray Slice Constants
const float SLICE_GRASS_LAWN       = 0.0; // Grass005
const float SLICE_GRASS_MEADOW     = 1.0; // Grass006
const float SLICE_DESERT_SAND      = 2.0; // Ground033
const float SLICE_SEABED_RIPPLES   = 3.0; // Ground100
const float SLICE_SWAMP_MUD        = 4.0; // Ground083
const float SLICE_FARM_SOIL        = 5.0; // Ground104
const float SLICE_SCREE_SAND       = 6.0; // Gravel014
const float SLICE_RIVER_COBBLES    = 7.0; // Gravel020
const float SLICE_STREAM_PEBBLES   = 8.0; // Gravel022
const float SLICE_GRANITE_GRIT     = 9.0; // Gravel036L
const float SLICE_ROCK_METAMORPHIC = 10.0;// Rock028 (Default Slate/Alpine)
const float SLICE_ROCK_SEDIMENTARY = 11.0;// Rock030 (Canyons / Desert Scarps)
const float SLICE_ROCK_SHALE       = 12.0;// Rock031 (Dark Ravines)
const float SLICE_ROCK_MOSSY       = 13.0;// Rock063 (Forest Cliffs)

varying vec3 v_world_pos;
varying vec3 v_world_normal;
varying vec2 v_uv;
varying vec4 v_vertex_color;

// ---- Procedural Hash & Noise Math ----
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

float fbm2d(vec2 p) {
	return noise2d(p) * 0.62 + noise2d(p * 2.05) * 0.28 + noise2d(p * 4.1) * 0.10;
}

void vertex() {
	v_uv = UV;
	v_vertex_color = COLOR;
	v_world_pos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	mat3 norm_matrix = mat3(MODEL_MATRIX[0].xyz, MODEL_MATRIX[1].xyz, MODEL_MATRIX[2].xyz);
	v_world_normal = normalize(norm_matrix * NORMAL);
}

struct PBRSample {
	vec3 albedo;
	vec3 normal;
	float roughness;
	float ao;
	float height;
};

// Dual-scale sampling to completely eliminate repetition grids
PBRSample sample_terrain_layer(vec2 uv, float slice, float noise_val) {
	PBRSample s;
	vec2 uv_micro = uv;
	vec2 uv_macro = uv * 0.183;
	
	vec4 col_micro = texture(terrain_albedo_array, vec3(uv_micro, slice));
	vec4 col_macro = texture(terrain_albedo_array, vec3(uv_macro, slice));
	
	vec3 n_micro = texture(terrain_normal_array, vec3(uv_micro, slice)).rgb * 2.0 - 1.0;
	vec3 n_macro = texture(terrain_normal_array, vec3(uv_macro, slice)).rgb * 2.0 - 1.0;
	
	vec3 orm_micro = texture(terrain_orm_array, vec3(uv_micro, slice)).rgb;
	vec3 orm_macro = texture(terrain_orm_array, vec3(uv_macro, slice)).rgb;
	
	float blend = smoothstep(0.32, 0.68, noise_val);
	
	s.albedo = mix(col_micro.rgb, col_macro.rgb, blend * 0.45);
	s.normal = normalize(mix(n_micro, n_macro, blend * 0.40));
	s.ao = mix(orm_micro.r, orm_macro.r, blend * 0.45);
	s.roughness = mix(orm_micro.g, orm_macro.g, blend * 0.45);
	s.height = mix(orm_micro.b, orm_macro.b, blend * 0.45);
	return s;
}

PBRSample sample_cliff_triplanar(vec3 pos, vec3 norm_w, float slice, float noise_val) {
	vec3 weights = pow(abs(norm_w), vec3(4.0));
	weights /= max(0.0001, weights.x + weights.y + weights.z);
	
	vec2 uv_x = pos.zy * uv_scale_cliff;
	vec2 uv_y = pos.xz * uv_scale_cliff;
	vec2 uv_z = pos.xy * uv_scale_cliff;
	
	PBRSample sx = sample_terrain_layer(uv_x, slice, noise_val);
	PBRSample sy = sample_terrain_layer(uv_y, slice, noise_val);
	PBRSample sz = sample_terrain_layer(uv_z, slice, noise_val);
	
	PBRSample result;
	result.albedo = sx.albedo * weights.x + sy.albedo * weights.y + sz.albedo * weights.z;
	result.roughness = sx.roughness * weights.x + sy.roughness * weights.y + sz.roughness * weights.z;
	result.ao = sx.ao * weights.x + sy.ao * weights.y + sz.ao * weights.z;
	result.height = sx.height * weights.x + sy.height * weights.y + sz.height * weights.z;
	
	vec3 nx = vec3(0.0, sx.normal.y, sx.normal.x * sign(norm_w.x));
	vec3 ny = vec3(sy.normal.x, 0.0, sy.normal.y * sign(norm_w.y));
	vec3 nz = vec3(sz.normal.x * sign(norm_w.z), sz.normal.y, 0.0);
	result.normal = normalize(nx * weights.x + ny * weights.y + nz * weights.z);
	return result;
}

void fragment() {
	vec3 norm_w = normalize(v_world_normal);
	float elevation = v_world_pos.y / max(0.001, height_scale);
	
	// Sample Biome ID (0..4) with fallback to vertex color
	int biome_id = int(v_vertex_color.r * 255.0 + 0.5);
	if (textureSize(biome_tex, 0).x > 1) {
		biome_id = int(texture(biome_tex, v_uv).r * 255.0 + 0.5);
	}
	
	// -------------------------------------------------------------
	// 1. Geomorphological Terrain Analysis (Curvature, Slope, Soil Depth)
	// -------------------------------------------------------------
	// Slope drive: 0 on flat ground, rising across angle of repose
	float slope = 1.0 - clamp(norm_w.y, 0.0, 1.0);
	float slope_drive = smoothstep(0.12, 0.52, slope);
	
	// Heightfield Laplacian Curvature: sample 4 cross neighbors (2.5-texel footprint)
	ivec2 hm_size = textureSize(heightmap_tex, 0);
	float norm_curvature = 0.0;
	if (hm_size.x > 1) {
		vec2 texel = 1.0 / vec2(hm_size);
		float hc = texture(heightmap_tex, v_uv).r;
		float hl = texture(heightmap_tex, v_uv - vec2(texel.x * 2.5, 0.0)).r;
		float hr = texture(heightmap_tex, v_uv + vec2(texel.x * 2.5, 0.0)).r;
		float hd = texture(heightmap_tex, v_uv - vec2(0.0, texel.y * 2.5)).r;
		float hu = texture(heightmap_tex, v_uv + vec2(0.0, texel.y * 2.5)).r;
		// Discrete Laplacian: inverted so + = convex crest/ridge, - = concave hollow/valley
		float laplacian = -((hl + hr + hd + hu) - 4.0 * hc);
		norm_curvature = clamp(laplacian * 0.18, -1.0, 1.0);
	}
	
	// Altitude-dependent soil thinning: valleys retain thick regolith, alpine summits thin to bedrock
	float altitude_thinning = clamp((elevation - 18.0) / max(0.1, soil_depth_scale), 0.0, 1.0);
	
	// Fluvial incision: river channels scour loose soil down to hard bedrock
	float river_incision = smoothstep(0.02, 0.25, v_vertex_color.b);
	
	// Geological structural modulation: organic strata bedding and joint fracture noise
	float strata = sin((v_world_pos.y * 0.50 + fbm2d(v_world_pos.xz * 0.035) * 3.5) * strata_frequency);
	float fracture = fbm2d(v_world_pos.xz * 0.12 + vec2(v_world_pos.y * 0.05));
	float geological_structure = mix(fracture, strata * 0.5 + 0.5, 0.35);
	
	// Integrated Geomorphological Rock Exposure Weight
	float exposure = slope_drive * 0.70;
	exposure += norm_curvature * curvature_strength; // Convex ridge +soil loss, concave hollow -sediment trap
	exposure += altitude_thinning * 0.35;           // Alpine soil depletion
	exposure += river_incision * 0.45;              // Canyon water scouring
	exposure += (geological_structure - 0.5) * 0.30;// Natural fracture edge break-up
	
	float raw_rock_weight = smoothstep(cliff_slope_threshold - 0.18, cliff_slope_threshold + 0.18, exposure);
	
	// -------------------------------------------------------------
	// 2. Biome-Aware Cliff Rock Layer Selection
	// -------------------------------------------------------------
	float cliff_slice = SLICE_ROCK_METAMORPHIC; // Default: Rock028 (Metamorphic Slate/Schist)
	
	if (biome_id == 4) {
		// Temperate Forest: Mossy Rock063 on lower damp slopes, Rock028 above canopy
		float canopy_line = smoothstep(22.0, 32.0, elevation);
		cliff_slice = (canopy_line > 0.5) ? SLICE_ROCK_METAMORPHIC : SLICE_ROCK_MOSSY;
	} else if (biome_id == 2) {
		// Desert: Warm layered sedimentary sandstone Rock030
		cliff_slice = SLICE_ROCK_SEDIMENTARY;
	} else if (v_vertex_color.b > 0.04) {
		// Fluvial carved river canyons & gorges: Sedimentary rock strata Rock030
		cliff_slice = SLICE_ROCK_SEDIMENTARY;
	} else {
		// Grassland (Biome 3) & Alpine Summit: Rock028 (Metamorphic Slate)
		// Deep shaded clefts and shear ravines blend into dark charcoal shale Rock031
		float shear_ravine = smoothstep(0.40, 0.10, norm_w.y) * smoothstep(0.58, 0.38, fbm2d(v_world_pos.xz * 0.05));
		cliff_slice = (shear_ravine > 0.50) ? SLICE_ROCK_SHALE : SLICE_ROCK_METAMORPHIC;
	}
	
	// -------------------------------------------------------------
	// 3. Lowland Ground Layer Selection (3 Land Biomes)
	// -------------------------------------------------------------
	float ground_slice = SLICE_GRASS_LAWN; // Default: Grass005 (Clean short under-turf)
	
	if (elevation <= sea_level) {
		ground_slice = SLICE_SEABED_RIPPLES; // Ground100
	} else if (elevation < beach_threshold) {
		float beach_t = clamp(elevation / max(0.01, beach_threshold), 0.0, 1.0);
		ground_slice = (beach_t < 0.45) ? SLICE_SEABED_RIPPLES : SLICE_DESERT_SAND;
	} else if (v_vertex_color.b > 0.15) {
		// Active riverbed reach / pebble bank
		ground_slice = SLICE_RIVER_COBBLES; // Gravel020
	} else {
		switch (biome_id) {
			case 2: // Desert
				ground_slice = SLICE_DESERT_SAND; // Ground033 (Wind dunes)
				break;
			case 4: // Forest
				ground_slice = SLICE_GRASS_MEADOW; // Grass006 (Lush meadow & clover forest floor)
				break;
			default: // Grassland (Biome 3)
				ground_slice = SLICE_GRASS_LAWN; // Grass005 (Under-turf)
				break;
		}
		
		// Alpine scree / talus transition before snow line
		if (elevation > rock_elevation_threshold && elevation <= snow_elevation_threshold) {
			float talus_t = clamp((elevation - rock_elevation_threshold) / 15.0, 0.0, 1.0);
			if (talus_t > 0.55) {
				ground_slice = SLICE_GRANITE_GRIT; // Gravel036L (Granite talus grit)
			}
		}
	}
	
	// -------------------------------------------------------------
	// 4. PBR Sampling & Physical Height-Blended Transition
	// -------------------------------------------------------------
	float noise_anti_tile = fbm2d(v_world_pos.xz * 0.035);
	
	vec3 final_albedo;
	float final_roughness;
	float final_ao;
	vec3 final_normal_detail;
	
	if (use_texture_arrays) {
		vec2 ground_uv = v_world_pos.xz * uv_scale_ground;
		PBRSample ground_sample = sample_terrain_layer(ground_uv, ground_slice, noise_anti_tile);
		PBRSample cliff_sample = sample_cliff_triplanar(v_world_pos, norm_w, cliff_slice, noise_anti_tile);
		
		// Physical height blending using displacement maps (_orm.b)
		float h_g = ground_sample.height + (1.0 - raw_rock_weight);
		float h_c = cliff_sample.height + raw_rock_weight;
		float max_h = max(h_g, h_c) - height_blend_contrast;
		float w_g = max(h_g - max_h, 0.0);
		float w_c = max(h_c - max_h, 0.0);
		float rock_factor = clamp(w_c / max(0.0001, w_g + w_c), 0.0, 1.0);
		
		final_albedo = mix(ground_sample.albedo, cliff_sample.albedo, rock_factor);
		final_roughness = mix(ground_sample.roughness, cliff_sample.roughness, rock_factor);
		final_ao = mix(ground_sample.ao, cliff_sample.ao, rock_factor);
		final_normal_detail = mix(ground_sample.normal, cliff_sample.normal, rock_factor);
	} else {
		// Procedural fallback when texture arrays are not bound
		vec3 base_ground_col = vec3(0.24, 0.44, 0.16);
		if (elevation <= sea_level) {
			base_ground_col = vec3(0.18, 0.32, 0.38);
		} else if (biome_id == 2) {
			base_ground_col = vec3(0.76, 0.62, 0.38);
		} else if (biome_id == 4) {
			base_ground_col = vec3(0.14, 0.32, 0.12);
		}
		vec3 base_cliff_col = vec3(0.35, 0.34, 0.33);
		if (biome_id == 2 || v_vertex_color.b > 0.04) {
			base_cliff_col = vec3(0.60, 0.45, 0.32);
		} else if (biome_id == 4) {
			base_cliff_col = vec3(0.28, 0.33, 0.22);
		}
		final_albedo = mix(base_ground_col, base_cliff_col, raw_rock_weight);
		final_roughness = mix(0.85, 0.65, raw_rock_weight);
		final_ao = 1.0;
		final_normal_detail = vec3(0.0, 1.0, 0.0);
	}
	
	// -------------------------------------------------------------
	// 5. Macro Landscape Modulation (100m Color/Moisture Variation)
	// -------------------------------------------------------------
	float macro_mod = fbm2d(v_world_pos.xz * 0.012);
	final_albedo *= mix(vec3(0.92, 0.96, 0.92), vec3(1.05, 1.02, 0.98), macro_mod);
	final_roughness = clamp(final_roughness * mix(0.92, 1.08, macro_mod), 0.05, 1.0);
	
	// -------------------------------------------------------------
	// 6. Dynamic Shoreline Water Wash & Wet-Sand Darkening
	// -------------------------------------------------------------
	if (elevation >= sea_level && elevation < sea_level + 2.0) {
		float wetness = clamp(1.0 - (elevation - sea_level) / 2.0, 0.0, 1.0);
		final_albedo = mix(final_albedo, final_albedo * 0.60, wetness * 0.85);
		final_roughness = mix(final_roughness, 0.14, wetness * 0.80);
	}
	
	// -------------------------------------------------------------
	// 7. Glacial Snow Caps with Slope Slippage (Snow slides off cliffs)
	// -------------------------------------------------------------
	if (elevation > snow_elevation_threshold) {
		float snow_noise = fbm2d(v_world_pos.xz * 0.05) * 8.0;
		float snow_t = clamp((elevation + snow_noise - snow_elevation_threshold) / 12.0, 0.0, 1.0);
		// Snow only sticks to flat ledges
		float snow_stick = smoothstep(0.55, 0.85, norm_w.y);
		float snow_factor = snow_t * snow_stick;
		
		vec3 snow_color = vec3(0.93, 0.96, 0.99);
		snow_color = mix(vec3(0.82, 0.90, 0.96), snow_color, fbm2d(v_world_pos.xz * 0.15) * 0.3 + 0.7);
		
		final_albedo = mix(final_albedo, snow_color, snow_factor);
		final_roughness = mix(final_roughness, 0.45, snow_factor);
	}
	
	// -------------------------------------------------------------
	// 7.5. Fluvial Riverbed Dampness
	// -------------------------------------------------------------
	// Subtle moistening / darkening of carved fluvial cobbles and gravels
	if (v_vertex_color.b > 0.05) {
		float damp = clamp(v_vertex_color.b * 1.5, 0.0, 1.0);
		final_albedo = mix(final_albedo, final_albedo * 0.78, damp * 0.50);
		final_roughness = mix(final_roughness, clamp(final_roughness * 0.75, 0.15, 1.0), damp * 0.50);
	}
	
	// -------------------------------------------------------------
	// 8. Normal View Transform & PBR Output
	// -------------------------------------------------------------
	vec3 n_view = normalize((VIEW_MATRIX * vec4(norm_w, 0.0)).xyz);
	vec3 dn_world = normalize(norm_w + final_normal_detail * detail_normal_strength);
	vec3 dn_view = normalize((VIEW_MATRIX * vec4(dn_world, 0.0)).xyz);
	NORMAL = normalize(mix(n_view, dn_view, detail_normal_strength));
	
	ALBEDO = final_albedo;
	ROUGHNESS = final_roughness;
	AO = final_ao;
	SPECULAR = 0.25;
}
