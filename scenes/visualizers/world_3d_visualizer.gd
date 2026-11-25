extends Node3D

# ----------------------------------------------------------------------
# ExoFarm: 3D Continuous Tectonic World & Boundary-Aware Wave Visualizer
# ----------------------------------------------------------------------

@onready var bridge: SimBridge = SimBridge.new()
var wave_baker: WaveCageBaker = WaveCageBaker.new()
var wave_driver: WaveComputeDriver = WaveComputeDriver.new()

# World Parameters (1:1 Unified Simulation Grid)
const CONFIG_PATH: String = "res://world_3d_config.json"
const FALLBACK_CONFIG_PATH: String = "user://world_3d_config.json"
const BotanicalTreeMaterials = preload("res://scripts/botanical_tree_materials.gd")

var current_seed: int = 1337
var map_width: int = 512
var map_height: int = 512
var cell_size: float = 2.0 # 3D units per tile
var elevation_scale: float = 0.4 # Height multiplier for 3D view
var sea_level: float = 0.0

# 3D Node References
var terrain_mesh_instance: MeshInstance3D
var water_mesh_instance: MeshInstance3D
var tectonic_gizmos_node: Node3D
var camera_rig: Node3D
var camera_3d: Camera3D
var sun_light: DirectionalLight3D
var env_node: WorldEnvironment

# Camera Orbit State
var cam_target: Vector3 = Vector3.ZERO
var cam_distance: float = 550.0
var cam_rot_x: float = -35.0
var cam_rot_y: float = 45.0
var is_dragging_orbit: bool = false
var is_dragging_pan: bool = false
var last_mouse_pos: Vector2 = Vector2.ZERO

# UI Control References
var ui_root: Control
var seed_spin: SpinBox
var num_plates_spin: SpinBox
var center_bias_spin: SpinBox
var continent_chance_spin: SpinBox
var island_arc_chance_spin: SpinBox
var warp_scale_spin: SpinBox
var fold_belt_width_spin: SpinBox

var collision_uplift_spin: SpinBox
var subduction_arc_spin: SpinBox
var subduction_trench_spin: SpinBox
var rift_graben_spin: SpinBox
var rift_shoulder_spin: SpinBox
var cont_base_elev_spin: SpinBox
var ocean_base_elev_spin: SpinBox
var coastal_boundary_elev_spin: SpinBox
var shelf_width_spin: SpinBox
var shelf_depth_spin: SpinBox

var detail_noise_spin: SpinBox
var noise_octaves_spin: SpinBox

# Display / Visual Controls
var wave_amp_slider: HSlider
var toggle_water_chk: CheckBox
var toggle_vectors_chk: CheckBox
var toggle_wireframe_chk: CheckBox
var toggle_trees_chk: CheckBox
var tree_load_all_chk: CheckBox
var tree_radius_slider: HSlider
var status_lbl: Label
var probe_lbl: Label

# Chunked Tree & Procedural Vegetation State
var tree_chunks_node: Node3D
var tree_bridge = null
var loaded_tree_chunks: Dictionary = {}
var pending_tree_chunks: Array[Vector2i] = []
var tree_bark_material: StandardMaterial3D
var current_chunk_coords: Vector2i = Vector2i(-9999, -9999)
var tree_chunk_radius: int = 16
var tree_load_all: bool = true # Default to Full World across all chunks
const CHUNKS_PER_FRAME: int = 8 # Progressive time-sliced load to ensure smooth 60fps

# OpenTopography & River Controls
var world_gen_mode_opt: OptionButton
var opentopo_preset_opt: OptionButton
var sea_level_offset_spin: SpinBox
var sea_buffer_spin: SpinBox
var apply_erosion_chk: CheckBox
var apply_coastal_erosion_chk: CheckBox
var coastal_erosion_passes_spin: SpinBox
var coastal_erosion_strength_spin: SpinBox
var river_mesh_instance: MeshInstance3D
var river_spline_instance: MeshInstance3D

# Droplet Erosion & River Network Controls
var apply_droplet_erosion_chk: CheckBox
var droplet_count_spin: SpinBox
var droplet_erosion_rate_spin: SpinBox
var droplet_deposition_rate_spin: SpinBox
var river_min_area_spin: SpinBox
var river_width_mult_spin: SpinBox
var river_carve_depth_spin: SpinBox
var show_rivers_chk: CheckBox
var show_river_splines_chk: CheckBox
var shore_foam_dist_spin: SpinBox
var crest_foam_spin: SpinBox

# Cached Data & Shader Materials
var elevation_data: PackedInt32Array
var biome_data: PackedByteArray
var hm_tex: ImageTexture
var biome_tex: ImageTexture
var water_depth_tex: ImageTexture
var terrain_mat: ShaderMaterial
var water_mat: ShaderMaterial

# Terrain PBR Texture2DArray Packs
var terrain_albedo_array: Texture2DArray
var terrain_normal_array: Texture2DArray
var terrain_orm_array: Texture2DArray

# Shader Fidelity UI Knobs
var water_clarity_spin: SpinBox
var micro_ripple_spin: SpinBox
var cliff_threshold_spin: SpinBox
var snow_threshold_spin: SpinBox
var fog_density_spin: SpinBox

func _ready() -> void:
	add_child(bridge)
	add_child(wave_baker)
	add_child(wave_driver)
	
	_load_terrain_material_arrays()
	_setup_environment()
	_setup_camera()
	_setup_nodes()
	_setup_ui()
	
	if OS.is_debug_build() or FileAccess.file_exists(CONFIG_PATH) or FileAccess.file_exists(FALLBACK_CONFIG_PATH):
		load_debug_config()
	
	generate_world(current_seed)

func _load_terrain_material_arrays() -> void:
	var dir_path = "res://assets/material_packed"
	if not DirAccess.dir_exists_absolute(dir_path):
		print("Terrain material pack directory not found: ", dir_path)
		return
	
	var dir = DirAccess.open(dir_path)
	if not dir:
		return
	
	var albedo_imgs: Array[Image] = []
	var normal_imgs: Array[Image] = []
	var orm_imgs: Array[Image] = []
	
	var num_slices = 14
	for i in range(num_slices):
		dir.list_dir_begin()
		var fn = dir.get_next()
		var a_path = ""
		var n_path = ""
		var o_path = ""
		while fn != "":
			if fn.begins_with("slice_%02d_" % i):
				if fn.ends_with("_albedo.jpg") or fn.ends_with("_albedo.png"):
					a_path = dir_path + "/" + fn
				elif fn.ends_with("_normal.jpg") or fn.ends_with("_normal.png"):
					n_path = dir_path + "/" + fn
				elif fn.ends_with("_orm.jpg") or fn.ends_with("_orm.png"):
					o_path = dir_path + "/" + fn
			fn = dir.get_next()
		dir.list_dir_end()
		
		if a_path != "":
			var img_a = Image.load_from_file(a_path)
			if img_a:
				img_a.generate_mipmaps()
				albedo_imgs.append(img_a)
		if n_path != "":
			var img_n = Image.load_from_file(n_path)
			if img_n:
				img_n.generate_mipmaps()
				normal_imgs.append(img_n)
		if o_path != "":
			var img_o = Image.load_from_file(o_path)
			if img_o:
				img_o.generate_mipmaps()
				orm_imgs.append(img_o)
	
	if albedo_imgs.size() == num_slices:
		terrain_albedo_array = Texture2DArray.new()
		terrain_albedo_array.create_from_images(albedo_imgs)
	if normal_imgs.size() == num_slices:
		terrain_normal_array = Texture2DArray.new()
		terrain_normal_array.create_from_images(normal_imgs)
	if orm_imgs.size() == num_slices:
		terrain_orm_array = Texture2DArray.new()
		terrain_orm_array.create_from_images(orm_imgs)
	
	print("Loaded terrain Texture2DArrays with ", num_slices, " slices successfully.")

func _setup_environment() -> void:
	# Directional Sun Light
	sun_light = DirectionalLight3D.new()
	sun_light.rotation_degrees = Vector3(-50, 45, 0)
	sun_light.light_color = Color(1.0, 0.98, 0.92)
	sun_light.light_energy = 1.35
	sun_light.shadow_enabled = true
	sun_light.shadow_blur = 1.2
	sun_light.directional_shadow_mode = DirectionalLight3D.SHADOW_PARALLEL_4_SPLITS
	sun_light.directional_shadow_max_distance = 1800.0
	add_child(sun_light)
	
	# Procedural Atmospheric Sky
	var sky_mat = ProceduralSkyMaterial.new()
	sky_mat.sky_top_color = Color(0.20, 0.44, 0.78)
	sky_mat.sky_horizon_color = Color(0.68, 0.78, 0.88)
	sky_mat.sky_curve = 0.12
	sky_mat.ground_bottom_color = Color(0.12, 0.15, 0.20)
	sky_mat.ground_horizon_color = Color(0.55, 0.65, 0.75)
	sky_mat.ground_curve = 0.08
	sky_mat.sun_angle_max = 30.0
	sky_mat.sun_curve = 0.15
	
	var sky = Sky.new()
	sky.sky_material = sky_mat
	
	# Environment with Sky, Ambient Illumination, and Atmospheric Fog
	var env = Environment.new()
	env.background_mode = Environment.BG_SKY
	env.sky = sky
	env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	env.ambient_light_energy = 0.65
	env.reflected_light_source = Environment.REFLECTION_SOURCE_SKY
	env.tonemap_mode = Environment.TONE_MAPPER_ACES
	env.tonemap_exposure = 1.05
	env.glow_enabled = true
	env.glow_intensity = 0.4
	env.glow_bloom = 0.12
	
	# Atmospheric Distance Haze (Depth Fog)
	env.fog_enabled = true
	env.fog_light_color = Color(0.68, 0.78, 0.88)
	env.fog_light_energy = 0.85
	env.fog_density = 0.0008
	env.fog_aerial_perspective = 0.5
	env.fog_sky_affect = 0.35
	
	env_node = WorldEnvironment.new()
	env_node.environment = env
	add_child(env_node)

func _setup_camera() -> void:
	camera_rig = Node3D.new()
	add_child(camera_rig)
	
	camera_3d = Camera3D.new()
	camera_3d.fov = 65.0
	camera_3d.near = 0.5
	camera_3d.far = 2000.0
	camera_rig.add_child(camera_3d)
	_update_camera_transform()

func _setup_nodes() -> void:
	terrain_mesh_instance = MeshInstance3D.new()
	add_child(terrain_mesh_instance)
	
	water_mesh_instance = MeshInstance3D.new()
	add_child(water_mesh_instance)

	river_mesh_instance = MeshInstance3D.new()
	add_child(river_mesh_instance)

	river_spline_instance = MeshInstance3D.new()
	add_child(river_spline_instance)
	
	tectonic_gizmos_node = Node3D.new()
	add_child(tectonic_gizmos_node)

	tree_chunks_node = Node3D.new()
	tree_chunks_node.name = "TreeChunks"
	add_child(tree_chunks_node)

	if ClassDB.class_exists("BotanicalTreeBridge"):
		tree_bridge = ClassDB.instantiate("BotanicalTreeBridge")

	tree_bark_material = StandardMaterial3D.new()
	tree_bark_material.albedo_color = Color(0.36, 0.25, 0.16)
	tree_bark_material.roughness = 0.88
	tree_bark_material.metallic_specular = 0.15
	tree_bark_material.cull_mode = BaseMaterial3D.CULL_BACK
	tree_bark_material.depth_draw_mode = BaseMaterial3D.DEPTH_DRAW_ALWAYS

func _update_camera_transform() -> void:
	var yaw_rad = deg_to_rad(cam_rot_y)
	var pitch_rad = deg_to_rad(cam_rot_x)
	
	var offset = Vector3(
		cos(pitch_rad) * sin(yaw_rad),
		-sin(pitch_rad),
		cos(pitch_rad) * cos(yaw_rad)
	) * cam_distance
	
	camera_3d.global_position = cam_target + offset
	camera_3d.look_at(cam_target, Vector3.UP)

func generate_world(seed_val: int) -> void:
	current_seed = seed_val
	if seed_spin:
		seed_spin.value = current_seed
	
	var cfg = get_ui_config()
	cfg["seed"] = current_seed
	bridge.set_config(cfg)
	
	var cell_size_fixed = int(cell_size * 65536.0)
	bridge.generate_player_map(seed_val, map_width, map_height, cell_size_fixed)
	
	elevation_data = bridge.get_elevation()
	biome_data = bridge.get_biome()
	
	# Center camera over terrain if not set
	if cam_target == Vector3.ZERO:
		cam_target = Vector3((map_width * cell_size) / 2.0, 0.0, (map_height * cell_size) / 2.0)
	_update_camera_transform()
	
	# 1. Build 3D Terrain Mesh
	_build_terrain_mesh()
	
	# 2. Build 3D Boundary-Aware Water Surface
	_build_water_surface()
	
	# 3. Build 3D Continuous Evaluated River Ribbons & Spline Overlay
	_build_river_ribbons()
	_build_river_splines()
	
	# 4. Build Tectonic Plate Vector Gizmos
	_build_tectonic_gizmos()
	
	# 5. Build Chunked Procedural Vegetation
	_clear_tree_chunks()
	if toggle_trees_chk == null or toggle_trees_chk.button_pressed:
		_update_active_tree_chunks(true)
	
	_update_status_bar()

func _ensure_world_textures() -> void:
	if elevation_data.is_empty():
		return
	var num_pixels = map_width * map_height

	# Query 4-channel wave cage & estuary data from bridge:
	# R: Elevation (m), G: Boundary/Estuary Mask [0,1], B: Plume Vx (m/s), A: Plume Vy (m/s)
	var cage_floats = bridge.get_wave_cage_data(sea_level)
	var hm_img: Image
	if cage_floats.size() == num_pixels * 4:
		hm_img = Image.create_from_data(map_width, map_height, false, Image.FORMAT_RGBAF, cage_floats.to_byte_array())
	else:
		var heights = PackedFloat32Array()
		heights.resize(num_pixels)
		for i in range(num_pixels):
			heights[i] = float(elevation_data[i]) / 65536.0
		hm_img = Image.create_from_data(map_width, map_height, false, Image.FORMAT_RF, heights.to_byte_array())
	
	hm_tex = ImageTexture.create_from_image(hm_img)
	
	var biome_bytes = biome_data
	if biome_bytes.is_empty() or biome_bytes.size() != num_pixels:
		biome_bytes = PackedByteArray()
		biome_bytes.resize(num_pixels)
	var b_img = Image.create_from_data(map_width, map_height, false, Image.FORMAT_R8, biome_bytes)
	biome_tex = ImageTexture.create_from_image(b_img)
	
	# Continuous Water Depth & Flux Field (Lagrangian Droplet Flow)
	if bridge.has_method("get_water_depth_texture"):
		water_depth_tex = bridge.get_water_depth_texture()
	elif bridge.has_method("get_water_depth_data"):
		var w_tex_data = bridge.get_water_depth_data()
		if w_tex_data.size() == num_pixels * 4:
			var w_img = Image.create_from_data(map_width, map_height, false, Image.FORMAT_RGBAF, w_tex_data.to_byte_array())
			water_depth_tex = ImageTexture.create_from_image(w_img)
		elif w_tex_data.size() == num_pixels:
			var w_img = Image.create_from_data(map_width, map_height, false, Image.FORMAT_RF, w_tex_data.to_byte_array())
			water_depth_tex = ImageTexture.create_from_image(w_img)
	if water_depth_tex == null:
		var blank_img = Image.create(map_width, map_height, false, Image.FORMAT_RGBA8)
		blank_img.fill(Color(0, 0, 0, 0))
		water_depth_tex = ImageTexture.create_from_image(blank_img)

func _build_terrain_mesh() -> void:
	_ensure_world_textures()
	
	var array_mesh = ArrayMesh.new()
	var chunk_size = 128 # 4x4 = 16 chunk surfaces for 512x512
	var chunks_x = (map_width + chunk_size - 1) / chunk_size
	var chunks_y = (map_height + chunk_size - 1) / chunk_size
	
	# Instantiate Sophisticated 3D Continuous Terrain Shader
	var shader = load("res://assets/shaders/world_terrain_3d.gdshader")
	if shader:
		terrain_mat = ShaderMaterial.new()
		terrain_mat.shader = shader
		terrain_mat.set_shader_parameter("heightmap_tex", hm_tex)
		terrain_mat.set_shader_parameter("biome_tex", biome_tex)
		if terrain_albedo_array:
			terrain_mat.set_shader_parameter("terrain_albedo_array", terrain_albedo_array)
		if terrain_normal_array:
			terrain_mat.set_shader_parameter("terrain_normal_array", terrain_normal_array)
		if terrain_orm_array:
			terrain_mat.set_shader_parameter("terrain_orm_array", terrain_orm_array)
		terrain_mat.set_shader_parameter("use_texture_arrays", terrain_albedo_array != null)
		terrain_mat.set_shader_parameter("world_size", Vector2(map_width * cell_size, map_height * cell_size))
		terrain_mat.set_shader_parameter("height_scale", elevation_scale)
		terrain_mat.set_shader_parameter("sea_level", sea_level)
		terrain_mat.set_shader_parameter("cliff_slope_threshold", cliff_threshold_spin.value if cliff_threshold_spin else 0.55)
		terrain_mat.set_shader_parameter("snow_elevation_threshold", snow_threshold_spin.value if snow_threshold_spin else 60.0)
		terrain_mat.set_shader_parameter("rock_elevation_threshold", 35.0)
		terrain_mat.set_shader_parameter("detail_normal_strength", 0.60)
		terrain_mat.set_shader_parameter("strata_frequency", 1.1)
		terrain_mat.set_shader_parameter("curvature_strength", 0.45)
		terrain_mat.set_shader_parameter("soil_depth_scale", 28.0)
		terrain_mat.set_shader_parameter("height_blend_contrast", 0.16)
		terrain_mat.set_shader_parameter("show_rivers", show_rivers_chk.button_pressed if show_rivers_chk else true)
		terrain_mat.set_shader_parameter("water_depth_tex", water_depth_tex)
	
	var w = map_width
	var h = map_height
	var rivers = bridge.get_rivers()
	
	for cy in range(chunks_y):
		for cx in range(chunks_x):
			var x_start = cx * chunk_size
			var y_start = cy * chunk_size
			var x_end = min(w - 1, x_start + chunk_size)
			var y_end = min(h - 1, y_start + chunk_size)
			
			var cw = (x_end - x_start) + 1
			var ch = (y_end - y_start) + 1
			var num_verts = cw * ch
			
			var verts = PackedVector3Array()
			verts.resize(num_verts)
			var norms = PackedVector3Array()
			norms.resize(num_verts)
			var uvs = PackedVector2Array()
			uvs.resize(num_verts)
			var colors = PackedColorArray()
			colors.resize(num_verts)
			
			var num_quads = (cw - 1) * (ch - 1)
			var indices = PackedInt32Array()
			indices.resize(num_quads * 6)
			
			var v_idx = 0
			for ly in range(ch):
				var gy = y_start + ly
				for lx in range(cw):
					var gx = x_start + lx
					var idx = gy * w + gx
					var el = float(elevation_data[idx]) / 65536.0
					var b = biome_data[idx] if not biome_data.is_empty() else 0
					
					verts[v_idx] = Vector3(gx * cell_size, el * elevation_scale, gy * cell_size)
					uvs[v_idx] = Vector2(float(gx) / float(w - 1), float(gy) / float(h - 1))
					var r_val = float(rivers[idx]) / 3.0 if not rivers.is_empty() and idx < rivers.size() else 0.0
					colors[v_idx] = Color(float(b) / 255.0, el / 100.0, r_val, 1.0)
					
					# Analytical smooth heightfield normal (fast single-pass math)
					var gx_l = max(0, gx - 1)
					var gx_r = min(w - 1, gx + 1)
					var gy_d = max(0, gy - 1)
					var gy_u = min(h - 1, gy + 1)
					var el_l = float(elevation_data[gy * w + gx_l]) / 65536.0
					var el_r = float(elevation_data[gy * w + gx_r]) / 65536.0
					var el_d = float(elevation_data[gy_d * w + gx]) / 65536.0
					var el_u = float(elevation_data[gy_u * w + gx]) / 65536.0
					
					var norm = Vector3(
						-(el_r - el_l) * elevation_scale,
						2.0 * cell_size,
						-(el_u - el_d) * elevation_scale
					).normalized()
					norms[v_idx] = norm
					v_idx += 1
					
			var i_idx = 0
			for ly in range(ch - 1):
				for lx in range(cw - 1):
					var i0 = ly * cw + lx
					var i1 = ly * cw + (lx + 1)
					var i2 = (ly + 1) * cw + lx
					var i3 = (ly + 1) * cw + (lx + 1)
					
					# Front-facing upward
					indices[i_idx + 0] = i0
					indices[i_idx + 1] = i1
					indices[i_idx + 2] = i2
					
					indices[i_idx + 3] = i1
					indices[i_idx + 4] = i3
					indices[i_idx + 5] = i2
					i_idx += 6
					
			var arrays = []
			arrays.resize(Mesh.ARRAY_MAX)
			arrays[Mesh.ARRAY_VERTEX] = verts
			arrays[Mesh.ARRAY_NORMAL] = norms
			arrays[Mesh.ARRAY_TEX_UV] = uvs
			arrays[Mesh.ARRAY_COLOR] = colors
			arrays[Mesh.ARRAY_INDEX] = indices
			
			array_mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
			
	terrain_mesh_instance.mesh = array_mesh
	if terrain_mat:
		terrain_mesh_instance.material_override = terrain_mat

func _get_terrain_color(elevation: float, biome: int) -> Color:
	if elevation <= 0.0:
		# Ocean floor bathymetry
		var t = clamp((elevation + 40.0) / 40.0, 0.0, 1.0)
		return Color(0.04, 0.12, 0.28).lerp(Color(0.18, 0.35, 0.45), t)
	elif elevation < 3.0:
		# Coastal Beach Sand: subtle gradient from moist shore to dry dune
		var t = clamp(elevation / 3.0, 0.0, 1.0)
		return Color(0.72, 0.65, 0.48).lerp(Color(0.88, 0.80, 0.58), t)
	elif elevation > 60.0:
		# High Mountain Snow Peak
		var t = clamp((elevation - 60.0) / 15.0, 0.0, 1.0)
		return Color(0.75, 0.75, 0.78).lerp(Color(0.96, 0.98, 1.0), t)
	elif elevation > 35.0:
		# Alpine Mountain Rock
		var t = clamp((elevation - 35.0) / 25.0, 0.0, 1.0)
		return Color(0.42, 0.38, 0.32).lerp(Color(0.75, 0.75, 0.78), t)
	else:
		# Lowlands / Forest / Grassland by biome
		match biome:
			1: return Color(0.55, 0.65, 0.60) # Tundra
			2: return Color(0.88, 0.75, 0.45) # Desert
			4: return Color(0.18, 0.42, 0.18) # Forest
			5: return Color(0.10, 0.52, 0.20) # Rainforest
			6: return Color(0.25, 0.38, 0.22) # Swamp
			_: return Color(0.32, 0.60, 0.24) # Grassland

func _build_water_surface() -> void:
	_ensure_world_textures()

	# 1. Setup Plane Mesh for Water Surface (Higher subdivision for crisp Gerstner wave crests)
	var plane_mesh = PlaneMesh.new()
	plane_mesh.size = Vector2(map_width * cell_size, map_height * cell_size)
	plane_mesh.subdivide_width = 384
	plane_mesh.subdivide_depth = 384
	
	water_mesh_instance.mesh = plane_mesh
	water_mesh_instance.position = Vector3(
		(map_width * cell_size) / 2.0 - (cell_size / 2.0),
		sea_level * elevation_scale,
		(map_height * cell_size) / 2.0 - (cell_size / 2.0)
	)
	
	# 2. Create Sophisticated Boundary-Aware Water Material
	var shader = load("res://assets/shaders/wave_water.gdshader")
	if shader:
		water_mat = ShaderMaterial.new()
		water_mat.shader = shader
		water_mat.set_shader_parameter("heightmap_world_size", Vector2(map_width * cell_size, map_height * cell_size))
		water_mat.set_shader_parameter("height_scale", elevation_scale)
		water_mat.set_shader_parameter("sea_level_offset", sea_level)
		water_mat.set_shader_parameter("shallow_color", Color(0.08, 0.68, 0.75))
		water_mat.set_shader_parameter("mid_color", Color(0.03, 0.35, 0.62))
		water_mat.set_shader_parameter("deep_color", Color(0.01, 0.08, 0.24))
		water_mat.set_shader_parameter("depth_density", water_clarity_spin.value if water_clarity_spin else 0.08)
		water_mat.set_shader_parameter("normal_strength", 1.0)
		water_mat.set_shader_parameter("micro_ripple_strength", micro_ripple_spin.value if micro_ripple_spin else 0.40)
		water_mat.set_shader_parameter("micro_ripple_scale", 0.85)
		water_mat.set_shader_parameter("refraction_strength", 0.12)
		water_mat.set_shader_parameter("caustics_strength", 0.28)
		water_mat.set_shader_parameter("sss_color", Color(0.12, 0.85, 0.65))
		water_mat.set_shader_parameter("sss_strength", 0.70)
		water_mat.set_shader_parameter("shore_foam_distance", shore_foam_dist_spin.value if shore_foam_dist_spin else 1.4)
		water_mat.set_shader_parameter("shore_foam_intensity", 0.95)
		water_mat.set_shader_parameter("crest_foam_intensity", crest_foam_spin.value if crest_foam_spin else 1.15)
		water_mat.set_shader_parameter("crest_foam_threshold", 0.45)
		water_mat.set_shader_parameter("heightmap_tex", hm_tex)
		water_mesh_instance.material_override = water_mat
	
	# 3. Bake Wave Cages via GPU Compute Baker (Jeschke 2020 JFA + Raymarching)
	wave_baker.physical_size = map_width * cell_size
	wave_baker.grid_size = map_width
	wave_baker.sea_level_normalized = 0.0
	wave_baker.height_scale_factor = 1.0
	var cage_texture = wave_baker.bake_from_heightmap(hm_tex)
	
	# 4. Initialize Wave Compute Driver (Gerstner waves + Ellipsoidal Caging)
	wave_driver.grid_size = map_width
	wave_driver.world_size = map_width * cell_size
	wave_driver.height_scale_factor = 1.0
	wave_driver.target_material = water_mat
	wave_driver.update_textures(cage_texture, hm_tex)

func _build_river_ribbons() -> void:
	if not river_mesh_instance:
		return
		
	if show_rivers_chk != null and not show_rivers_chk.button_pressed:
		river_mesh_instance.mesh = null
		river_mesh_instance.visible = false
		return
	river_mesh_instance.visible = true

	if not bridge.has_method("get_parametric_river_reaches"):
		river_mesh_instance.mesh = null
		return

	var reaches = bridge.get_parametric_river_reaches()
	if reaches.is_empty():
		river_mesh_instance.mesh = null
		return
		
	var st = SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	
	var total_verts: int = 0
	for r in reaches:
		if r.get("is_dry", false):
			continue
		var wps: PackedVector2Array = r.get("waypoints", PackedVector2Array())
		var widths: PackedFloat32Array = r.get("widths", PackedFloat32Array())
		var water_elevs: PackedFloat32Array = r.get("water_elevations", PackedFloat32Array())
		var bed_elevs: PackedFloat32Array = r.get("bed_elevations", PackedFloat32Array())
		var n_wps = wps.size()
		if n_wps < 2:
			continue
			
		var flow_rate = float(r.get("flow_rate", 10.0))
		var tier = int(r.get("tier", 1))
		var base_vert_idx = total_verts
		var cum_length: float = 0.0
		var arc_lens: PackedFloat32Array = r.get("arc_lengths", PackedFloat32Array())
		
		# River waypoints from bridge are already in world meters (cx * cell_size)
		var coord_scale = 1.0
		
		# Build smooth fold-free cross-sections along reach waypoints
		var prev_p: Vector2 = Vector2.ZERO
		var prev_left: Vector2 = Vector2.ZERO
		var prev_right: Vector2 = Vector2.ZERO
		for i in range(n_wps):
			var p = wps[i] * coord_scale
			if not arc_lens.is_empty() and i < arc_lens.size():
				cum_length = float(arc_lens[i])
			elif i > 0:
				cum_length += (p - (wps[i - 1] * coord_scale)).length()
				
			# Compute continuous tangent vector
			var tangent: Vector2
			if i == 0:
				tangent = (wps[1] * coord_scale - p).normalized()
			elif i == n_wps - 1:
				tangent = (p - wps[n_wps - 2] * coord_scale).normalized()
			else:
				tangent = (wps[i + 1] * coord_scale - wps[i - 1] * coord_scale).normalized()
				
			if tangent.length_squared() < 0.0001:
				tangent = Vector2(1, 0)
				
			# Normal perpendicular in 2D (left vector)
			var perp = Vector2(-tangent.y, tangent.x)
			
			# Width at waypoint with brook source tapering (Tier 1 headwaters)
			var raw_w = widths[i] if i < widths.size() else 2.0
			var half_w = raw_w * 0.5
			
			# Taper headwater brook origin smoothly from spring source
			if i == 0:
				half_w = min(half_w * 0.20, 0.35)
			elif i == 1:
				half_w = min(half_w * 0.65, raw_w * 0.40)
				
			var left_pt = p + perp * half_w
			var right_pt = p - perp * half_w
			# Prevent tight inside bends from inverting triangles into crossing bowties
			if i > 0:
				var seg = p - prev_p
				var seg_len = seg.length()
				if seg_len > 0.001:
					var seg_dir = seg / seg_len
					if (left_pt - prev_left).dot(seg_dir) < 0.0:
						left_pt = p + perp * min(half_w, seg_len * 0.45)
						if (left_pt - prev_left).dot(seg_dir) < 0.0:
							left_pt = prev_left + seg_dir * 0.02
					if (right_pt - prev_right).dot(seg_dir) < 0.0:
						right_pt = p - perp * min(half_w, seg_len * 0.45)
						if (right_pt - prev_right).dot(seg_dir) < 0.0:
							right_pt = prev_right + seg_dir * 0.02
			prev_p = p
			prev_left = left_pt
			prev_right = right_pt
			
			var left_wx = left_pt.x
			var left_wz = left_pt.y
			var right_wx = right_pt.x
			var right_wz = right_pt.y
			
			# 3D Evaluated Water Surface Elevation (slight lift to stay nestled inside valley floor)
			var water_y: float = _eval_river_waypoint_y(p, i, water_elevs, bed_elevs, 0.04)
				
			var v_left = Vector3(left_wx, water_y, left_wz)
			var v_right = Vector3(right_wx, water_y, right_wz)
			
			# Longitudinal UV: U in [0, 1] across channel, V in meters along reach
			var v_tex = cum_length / 4.0
			var uv_l = Vector2(0.0, v_tex)
			var uv_r = Vector2(1.0, v_tex)
			
			var vert_col = Color(1.0, 1.0, 1.0, clamp(flow_rate / 40.0, 0.5, 1.2))
			var norm = Vector3.UP
			
			st.set_color(vert_col)
			st.set_normal(norm)
			st.set_uv(uv_l)
			st.add_vertex(v_left)
			
			st.set_color(vert_col)
			st.set_normal(norm)
			st.set_uv(uv_r)
			st.add_vertex(v_right)
			
			if i > 0:
				var i0 = base_vert_idx + (i - 1) * 2
				var i1 = base_vert_idx + (i - 1) * 2 + 1
				var i2 = base_vert_idx + i * 2
				var i3 = base_vert_idx + i * 2 + 1
				
				# Counter-clockwise upward facing triangles
				st.add_index(i0)
				st.add_index(i1)
				st.add_index(i2)
				
				st.add_index(i1)
				st.add_index(i3)
				st.add_index(i2)
				
		total_verts += n_wps * 2
		
	if total_verts == 0:
		river_mesh_instance.mesh = null
		return
		
	st.generate_tangents()
	river_mesh_instance.mesh = st.commit()
	
	# Flowing water shader material
	var river_shader = load("res://assets/shaders/river_water.gdshader")
	if river_shader:
		var r_mat = ShaderMaterial.new()
		r_mat.shader = river_shader
		r_mat.render_priority = 1
		river_mesh_instance.material_override = r_mat
	else:
		var std_mat = StandardMaterial3D.new()
		std_mat.albedo_color = Color(0.18, 0.62, 0.88, 0.85)
		std_mat.roughness = 0.12
		std_mat.metallic = 0.05
		std_mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		river_mesh_instance.material_override = std_mat

func _eval_river_waypoint_y(p: Vector2, idx: int, water_elevs: PackedFloat32Array, bed_elevs: PackedFloat32Array, lift: float = 0.12) -> float:
	if not water_elevs.is_empty() and idx < water_elevs.size():
		return float(water_elevs[idx]) * elevation_scale + (lift * elevation_scale)
	var center_ground = sample_ground_height(p.x, p.y)
	if not bed_elevs.is_empty() and idx < bed_elevs.size():
		center_ground = min(center_ground, float(bed_elevs[idx]) * elevation_scale)
	var water_depth = 0.35 * (elevation_scale / 0.4)
	if idx == 0:
		water_depth = 0.12
	return center_ground + water_depth

func _build_river_splines() -> void:
	if not river_spline_instance:
		return

	if show_river_splines_chk != null and not show_river_splines_chk.button_pressed:
		river_spline_instance.mesh = null
		river_spline_instance.visible = false
		return
	river_spline_instance.visible = true

	if not bridge.has_method("get_parametric_river_reaches"):
		river_spline_instance.mesh = null
		return

	var reaches = bridge.get_parametric_river_reaches()
	if reaches.is_empty():
		river_spline_instance.mesh = null
		return

	var im = ImmediateMesh.new()
	var mat = StandardMaterial3D.new()
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.vertex_color_use_as_albedo = true
	mat.render_priority = 2
	river_spline_instance.material_override = mat

	var has_segments = false
	for r in reaches:
		if r.get("is_dry", false):
			continue
		var wps: PackedVector2Array = r.get("waypoints", PackedVector2Array())
		var n_wps = wps.size()
		if n_wps < 2:
			continue
		var water_elevs: PackedFloat32Array = r.get("water_elevations", PackedFloat32Array())
		var bed_elevs: PackedFloat32Array = r.get("bed_elevations", PackedFloat32Array())
		var tier = int(r.get("tier", 1))

		var col: Color
		if tier >= 3:
			col = Color(1.0, 0.65, 0.12)
		elif tier == 2:
			col = Color(0.55, 1.0, 0.25)
		else:
			col = Color(0.20, 0.95, 1.0)

		if not has_segments:
			im.surface_begin(Mesh.PRIMITIVE_LINES)
			has_segments = true

		var pts3d: Array[Vector3] = []
		pts3d.resize(n_wps)
		for i in range(n_wps):
			var p = wps[i]
			var wy = _eval_river_waypoint_y(p, i, water_elevs, bed_elevs, 0.35)
			var gy = sample_ground_height(p.x, p.y) + 0.18
			pts3d[i] = Vector3(p.x, max(wy, gy), p.y)

		for i in range(n_wps - 1):
			im.surface_set_color(col)
			im.surface_add_vertex(pts3d[i])
			im.surface_set_color(col)
			im.surface_add_vertex(pts3d[i + 1])

		# Spring origin tick & terminal confluence/mouth tick
		var p_start = pts3d[0]
		im.surface_set_color(Color(0.90, 1.0, 1.0))
		im.surface_add_vertex(p_start)
		im.surface_set_color(Color(0.90, 1.0, 1.0))
		im.surface_add_vertex(p_start + Vector3(0.0, 0.70, 0.0))

		var p_end = pts3d[n_wps - 1]
		im.surface_set_color(Color(1.0, 0.25, 0.45))
		im.surface_add_vertex(p_end)
		im.surface_set_color(Color(1.0, 0.25, 0.45))
		im.surface_add_vertex(p_end + Vector3(0.0, 0.70, 0.0))

	if has_segments:
		im.surface_end()
		river_spline_instance.mesh = im
	else:
		river_spline_instance.mesh = null

func _build_tectonic_gizmos() -> void:
	for child in tectonic_gizmos_node.get_children():
		child.queue_free()
		
	var nodes = bridge.get_tectonic_nodes()
	for n in nodes:
		var pos_x = n["pos_x"] * cell_size
		var pos_y = n["pos_y"] * cell_size
		var vel_x = n["vel_x"] * 40.0
		var vel_y = n["vel_y"] * 40.0
		var is_cont = (n["plate_type"] == 1)
		
		# Marker Cylinder
		var marker = MeshInstance3D.new()
		var cyl = CylinderMesh.new()
		cyl.top_radius = 1.2
		cyl.bottom_radius = 1.2
		cyl.height = 8.0
		marker.mesh = cyl
		
		var mat = StandardMaterial3D.new()
		mat.albedo_color = Color(0.9, 0.3, 0.2) if is_cont else Color(0.1, 0.6, 0.9)
		mat.emission_enabled = true
		mat.emission = mat.albedo_color
		mat.emission_energy_multiplier = 0.5
		marker.material_override = mat
		
		marker.position = Vector3(pos_x, 15.0, pos_y)
		tectonic_gizmos_node.add_child(marker)
		
		# Velocity Arrow
		var arrow = ImmediateMesh.new()
		var arrow_inst = MeshInstance3D.new()
		arrow_inst.mesh = arrow
		
		var arr_mat = StandardMaterial3D.new()
		arr_mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		arr_mat.albedo_color = Color(1.0, 1.0, 0.2)
		arrow_inst.material_override = arr_mat
		
		var start_p = Vector3(pos_x, 16.0, pos_y)
		var end_p = start_p + Vector3(vel_x, 0.0, vel_y)
		
		arrow.surface_begin(Mesh.PRIMITIVE_LINES)
		arrow.surface_add_vertex(start_p)
		arrow.surface_add_vertex(end_p)
		# Arrow head
		var dir = (end_p - start_p).normalized()
		var side = dir.cross(Vector3.UP) * 2.0
		arrow.surface_add_vertex(end_p)
		arrow.surface_add_vertex(end_p - dir * 4.0 + side)
		arrow.surface_add_vertex(end_p)
		arrow.surface_add_vertex(end_p - dir * 4.0 - side)
		arrow.surface_end()
		
		tectonic_gizmos_node.add_child(arrow_inst)

func _process(delta: float) -> void:
	_handle_camera_input(delta)
	if toggle_trees_chk == null or toggle_trees_chk.button_pressed:
		_update_active_tree_chunks()
		_process_pending_tree_chunks()

func _process_pending_tree_chunks() -> void:
	if pending_tree_chunks.is_empty():
		return
	var batch = min(CHUNKS_PER_FRAME, pending_tree_chunks.size())
	for i in range(batch):
		var ckey = pending_tree_chunks.pop_front()
		if not loaded_tree_chunks.has(ckey):
			_load_tree_chunk(ckey.x, ckey.y)
	if status_lbl and pending_tree_chunks.is_empty():
		_update_status_bar()

func _handle_camera_input(delta: float) -> void:
	# Ignore WASD panning if Ctrl is held (e.g. Ctrl+S / Ctrl+L shortcuts)
	if Input.is_key_pressed(KEY_CTRL):
		return
	
	# WASD Panning
	var move_vec = Vector3.ZERO
	if Input.is_key_pressed(KEY_W): move_vec -= camera_3d.global_transform.basis.z
	if Input.is_key_pressed(KEY_S): move_vec += camera_3d.global_transform.basis.z
	if Input.is_key_pressed(KEY_A): move_vec -= camera_3d.global_transform.basis.x
	if Input.is_key_pressed(KEY_D): move_vec += camera_3d.global_transform.basis.x
	if Input.is_key_pressed(KEY_Q): move_vec -= Vector3.UP
	if Input.is_key_pressed(KEY_E): move_vec += Vector3.UP
	
	if move_vec != Vector3.ZERO:
		cam_target += move_vec.normalized() * (cam_distance * 0.8 * delta)
		_update_camera_transform()

func sample_ground_height(world_x: float, world_z: float) -> float:
	if elevation_data.is_empty() or map_width <= 0 or map_height <= 0:
		return 0.0
	var gx = clamp(world_x / cell_size, 0.0, float(map_width - 1))
	var gy = clamp(world_z / cell_size, 0.0, float(map_height - 1))
	var x0 = int(floor(gx))
	var y0 = int(floor(gy))
	var x1 = min(map_width - 1, x0 + 1)
	var y1 = min(map_height - 1, y0 + 1)
	var tx = gx - float(x0)
	var ty = gy - float(y0)
	
	var e00 = float(elevation_data[y0 * map_width + x0]) / 65536.0
	var e10 = float(elevation_data[y0 * map_width + x1]) / 65536.0
	var e01 = float(elevation_data[y1 * map_width + x0]) / 65536.0
	var e11 = float(elevation_data[y1 * map_width + x1]) / 65536.0
	
	var e_top = lerp(e00, e10, tx)
	var e_bot = lerp(e01, e11, tx)
	var raw_elev = lerp(e_top, e_bot, ty)
	return raw_elev * elevation_scale

func _clear_tree_chunks() -> void:
	current_chunk_coords = Vector2i(-9999, -9999)
	pending_tree_chunks.clear()
	for cnode in loaded_tree_chunks.values():
		if is_instance_valid(cnode):
			cnode.queue_free()
	loaded_tree_chunks.clear()

func _update_active_tree_chunks(force: bool = false) -> void:
	if not bridge or not tree_chunks_node:
		return
	var chunk_size_world = 16.0 * cell_size
	var pcx: int = int(floor(cam_target.x / chunk_size_world))
	var pcy: int = int(floor(cam_target.z / chunk_size_world))
	var new_chunk_coords = Vector2i(pcx, pcy)

	if not force and new_chunk_coords == current_chunk_coords and pending_tree_chunks.is_empty():
		return
	current_chunk_coords = new_chunk_coords

	var grid_size = bridge.get_chunk_grid_size()
	var required_chunks: Dictionary = {}

	if tree_load_all:
		for cy in range(grid_size.y):
			for cx in range(grid_size.x):
				required_chunks[Vector2i(cx, cy)] = true
	else:
		var rad = tree_chunk_radius
		for dy in range(-rad, rad + 1):
			for dx in range(-rad, rad + 1):
				var cx = pcx + dx
				var cy = pcy + dy
				if cx >= 0 and cx < grid_size.x and cy >= 0 and cy < grid_size.y:
					required_chunks[Vector2i(cx, cy)] = true

	# Unload distant chunks to preserve frame-rate
	var to_unload: Array = []
	for ckey in loaded_tree_chunks.keys():
		if not required_chunks.has(ckey):
			to_unload.append(ckey)

	for ckey in to_unload:
		var cnode: Node3D = loaded_tree_chunks[ckey]
		if is_instance_valid(cnode):
			cnode.queue_free()
		loaded_tree_chunks.erase(ckey)

	# Build progressive load queue sorted by distance from camera center
	var to_load: Array[Vector2i] = []
	for ckey in required_chunks.keys():
		if not loaded_tree_chunks.has(ckey) and not (ckey in pending_tree_chunks):
			to_load.append(ckey)

	if not to_load.is_empty():
		to_load.sort_custom(func(a: Vector2i, b: Vector2i):
			var d_a = (a.x - pcx) * (a.x - pcx) + (a.y - pcy) * (a.y - pcy)
			var d_b = (b.x - pcx) * (b.x - pcx) + (b.y - pcy) * (b.y - pcy)
			return d_a < d_b
		)
		pending_tree_chunks.append_array(to_load)

func _load_tree_chunk(cx: int, cy: int) -> void:
	if not tree_chunks_node or not bridge:
		return

	# Fast forward chunk trees to current simulation tick
	bridge.fast_forward_chunk_trees(cx, cy, bridge.current_tick())

	var chunk_trees = bridge.get_trees_in_chunk(cx, cy)
	var chunk_node = Node3D.new()
	chunk_node.name = "TreeChunk_%d_%d" % [cx, cy]
	tree_chunks_node.add_child(chunk_node)
	loaded_tree_chunks[Vector2i(cx, cy)] = chunk_node

	if chunk_trees.is_empty():
		return

	for t_data in chunk_trees:
		var raw_pos: Vector3 = t_data["position"]
		var ground_y = sample_ground_height(raw_pos.x, raw_pos.z)
		var tree_pos = Vector3(raw_pos.x, ground_y, raw_pos.z)
		var g_norm: Vector3 = t_data["normal"]
		var species: int = int(t_data["species"])
		var growth: float = clamp(float(t_data["growth"]), 0.15, 1.0)
		var t_scale: float = clamp(float(t_data["scale"]), 0.70, 1.30)
		var t_seed: int = int(t_data["seed"])

		var tree_node = Node3D.new()
		tree_node.position = tree_pos
		tree_node.scale = Vector3.ONE * t_scale
		chunk_node.add_child(tree_node)

		if tree_bridge:
			var res: Dictionary = tree_bridge.generate_tree(t_seed, species, growth, {
				"ground_pos": Vector3.ZERO,
				"ground_normal": g_norm,
				"sun_dir": Vector3(0.5, 0.707, 0.5)
			})
			if res.has("bark_mesh") and res["bark_mesh"] != null:
				var b_inst = MeshInstance3D.new()
				b_inst.mesh = res["bark_mesh"]
				b_inst.material_override = tree_bark_material
				tree_node.add_child(b_inst)
			if res.has("foliage_mesh") and res["foliage_mesh"] != null:
				var f_inst = MeshInstance3D.new()
				f_inst.mesh = res["foliage_mesh"]
				f_inst.material_override = BotanicalTreeMaterials.get_foliage_shader_material(species)
				tree_node.add_child(f_inst)

func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseButton:
		if event.button_index == MOUSE_BUTTON_RIGHT:
			is_dragging_orbit = event.pressed
			last_mouse_pos = event.position
		elif event.button_index == MOUSE_BUTTON_MIDDLE:
			is_dragging_pan = event.pressed
			last_mouse_pos = event.position
		elif event.button_index == MOUSE_BUTTON_WHEEL_UP:
			cam_distance = max(10.0, cam_distance * 0.9)
			_update_camera_transform()
		elif event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			cam_distance = min(800.0, cam_distance * 1.1)
			_update_camera_transform()
		elif event.button_index == MOUSE_BUTTON_LEFT and event.pressed:
			_probe_terrain_at_screen_pos(event.position)
	elif event is InputEventMouseMotion:
		var delta_pos = event.position - last_mouse_pos
		last_mouse_pos = event.position
		
		if is_dragging_orbit:
			cam_rot_y -= delta_pos.x * 0.3
			cam_rot_x = clamp(cam_rot_x - delta_pos.y * 0.3, -89.0, 10.0)
			_update_camera_transform()
		elif is_dragging_pan:
			var right = camera_3d.global_transform.basis.x
			var up = camera_3d.global_transform.basis.y
			cam_target -= (right * delta_pos.x - up * delta_pos.y) * (cam_distance * 0.0015)
			_update_camera_transform()
	elif event is InputEventKey and event.pressed:
		if event.keycode == KEY_S and event.ctrl_pressed:
			save_debug_config()
			get_viewport().set_input_as_handled()
		elif event.keycode == KEY_L and event.ctrl_pressed:
			if load_debug_config():
				generate_world(current_seed)
			get_viewport().set_input_as_handled()
		elif event.keycode == KEY_R:
			_on_random_seed_pressed()
		elif event.keycode == KEY_SPACE:
			_on_generate_pressed()
		elif event.keycode == KEY_E and not event.ctrl_pressed:
			if wave_driver:
				wave_driver.spawn_ripple(Vector2(cam_target.x, cam_target.z), 20, 2.5, 32.0, 26.0)

func _probe_terrain_at_screen_pos(mouse_pos: Vector2) -> void:
	var from = camera_3d.project_ray_origin(mouse_pos)
	var dir = camera_3d.project_ray_normal(mouse_pos)
	
	# Ray-plane intersection with ground plane / terrain
	if abs(dir.y) > 0.001:
		var t = -from.y / dir.y
		var hit_p = from + dir * t
		var tx = int(hit_p.x / cell_size)
		var ty = int(hit_p.z / cell_size)
		
		if tx >= 0 and tx < map_width and ty >= 0 and ty < map_height:
			var sample = bridge.get_tectonic_sample(tx * cell_size, ty * cell_size)
			var el = sample.get("elevation", 0.0)
			
			# If user clicked on water, spawn an interactive wave particle ripple!
			if el <= sea_level:
				if wave_driver:
					wave_driver.spawn_ripple(Vector2(hit_p.x, hit_p.z), 18, 2.2, 28.0, 24.0)
			
			var b_names = ["Ocean", "Tundra", "Desert", "Grassland", "Forest", "Rainforest", "Swamp", "Mountain"]
			var b_idx = biome_data[ty * map_width + tx] if not biome_data.is_empty() else 0
			var b_name = b_names[b_idx] if b_idx < b_names.size() else "Unknown"
			
			var btype_names = ["Interior", "Continental Collision", "Subduction", "Island Arc", "Continental Rift", "Ocean Spreading", "Transform Shear"]
			var b_type_idx = sample.get("boundary_type", 0)
			var b_type_name = btype_names[b_type_idx] if b_type_idx < btype_names.size() else "Interior"
			
			var cx = tx / 16
			var cy = ty / 16
			var c_count = bridge.get_trees_in_chunk(cx, cy).size() if bridge else 0
			
			probe_lbl.text = "Tile: (%d, %d) [Chunk: %d, %d (%d Trees)]\nElevation: %.1f m (Base: %.1f, Tectonic: %.1f)\nBiome: %s\nPlate ID: %d | Neighbor: %d\nBoundary Type: %s\nConvergence: %.2f | Shear: %.2f\nDist to Boundary: %.1f m" % [
				tx, ty, cx, cy, c_count,
				el,
				sample.get("elevation_base", 0.0),
				sample.get("elevation_tectonic", 0.0),
				b_name,
				sample.get("primary_plate_id", 0),
				sample.get("secondary_plate_id", 0),
				b_type_name,
				sample.get("convergence_rate", 0.0),
				sample.get("shear_rate", 0.0),
				sample.get("dist_to_boundary", 0.0)
			]

func _create_section_header(title_text: String) -> Control:
	var c = VBoxContainer.new()
	c.add_theme_constant_override("separation", 2)
	c.add_child(HSeparator.new())
	var lbl = Label.new()
	lbl.text = title_text
	lbl.add_theme_color_override("font_color", Color(0.95, 0.75, 0.35))
	var font = lbl.get_theme_default_font()
	if font:
		lbl.add_theme_font_size_override("font_size", 12)
	c.add_child(lbl)
	return c

func _create_spin_row(parent: VBoxContainer, label_text: String, min_v: float, max_v: float, step_v: float, def_v: float) -> SpinBox:
	var row = HBoxContainer.new()
	row.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	
	var lbl = Label.new()
	lbl.text = label_text
	lbl.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	lbl.add_theme_color_override("font_color", Color(0.85, 0.88, 0.92))
	var font = lbl.get_theme_default_font()
	if font:
		lbl.add_theme_font_size_override("font_size", 11)
	row.add_child(lbl)
	
	var spin = SpinBox.new()
	spin.min_value = min_v
	spin.max_value = max_v
	spin.step = step_v
	spin.value = def_v
	spin.custom_minimum_size = Vector2(90, 0)
	row.add_child(spin)
	
	parent.add_child(row)
	return spin

func _create_check_row(parent: VBoxContainer, label_text: String, def_v: bool) -> CheckBox:
	var chk = CheckBox.new()
	chk.text = label_text
	chk.button_pressed = def_v
	chk.add_theme_color_override("font_color", Color(0.85, 0.88, 0.92))
	var font = chk.get_theme_default_font()
	if font:
		chk.add_theme_font_size_override("font_size", 11)
	parent.add_child(chk)
	return chk

func get_ui_config() -> Dictionary:
	var cfg = {}
	cfg["seed"] = current_seed
	cfg["map_width"] = map_width
	cfg["map_height"] = map_height
	cfg["cell_size"] = cell_size
	cfg["elevation_scale"] = elevation_scale
	if num_plates_spin: cfg["num_plates"] = int(num_plates_spin.value)
	if center_bias_spin: cfg["center_bias"] = float(center_bias_spin.value)
	if continent_chance_spin: cfg["continent_chance"] = int(continent_chance_spin.value)
	if island_arc_chance_spin: cfg["island_arc_chance"] = int(island_arc_chance_spin.value)
	if warp_scale_spin: cfg["warp_scale"] = float(warp_scale_spin.value)
	if fold_belt_width_spin: cfg["fold_belt_width"] = float(fold_belt_width_spin.value)
	if collision_uplift_spin: cfg["collision_uplift"] = float(collision_uplift_spin.value)
	if subduction_arc_spin: cfg["subduction_arc"] = float(subduction_arc_spin.value)
	if subduction_trench_spin: cfg["subduction_trench"] = float(subduction_trench_spin.value)
	if rift_graben_spin: cfg["rift_graben"] = float(rift_graben_spin.value)
	if rift_shoulder_spin: cfg["rift_shoulder"] = float(rift_shoulder_spin.value)
	if cont_base_elev_spin: cfg["continental_base_elevation"] = float(cont_base_elev_spin.value)
	if ocean_base_elev_spin: cfg["oceanic_base_elevation"] = float(ocean_base_elev_spin.value)
	if coastal_boundary_elev_spin: cfg["coastal_boundary_elevation"] = float(coastal_boundary_elev_spin.value)
	if shelf_width_spin: cfg["shelf_width"] = float(shelf_width_spin.value)
	if shelf_depth_spin: cfg["shelf_depth"] = float(shelf_depth_spin.value)
	if detail_noise_spin: cfg["detail_noise_scale"] = float(detail_noise_spin.value)
	if noise_octaves_spin: cfg["noise_octaves"] = int(noise_octaves_spin.value)

	if world_gen_mode_opt: cfg["world_gen_mode"] = world_gen_mode_opt.selected
	if opentopo_preset_opt:
		var presets = [
			"assets/data/topo/alpine_basin.bin",
			"assets/data/topo/river_valley.bin",
			"assets/data/topo/rolling_plains.bin",
			"assets/data/topo/coastal_fjord.bin"
		]
		var sel = opentopo_preset_opt.selected
		if sel >= 0 and sel < presets.size():
			cfg["opentopo_dataset"] = presets[sel]
	if sea_level_offset_spin: cfg["sea_level_offset"] = float(sea_level_offset_spin.value)
	if sea_buffer_spin: cfg["sea_buffer_tiles"] = int(sea_buffer_spin.value)
	if apply_erosion_chk: cfg["apply_erosion"] = apply_erosion_chk.button_pressed
	if apply_coastal_erosion_chk: cfg["apply_coastal_erosion"] = apply_coastal_erosion_chk.button_pressed
	if coastal_erosion_passes_spin: cfg["coastal_erosion_iterations"] = int(coastal_erosion_passes_spin.value)
	if coastal_erosion_strength_spin: cfg["coastal_erosion_strength"] = float(coastal_erosion_strength_spin.value)

	# Droplet Hydraulic Erosion & River Network
	if apply_droplet_erosion_chk: cfg["apply_droplet_erosion"] = apply_droplet_erosion_chk.button_pressed
	if droplet_count_spin: cfg["droplet_count"] = int(droplet_count_spin.value)
	if droplet_erosion_rate_spin: cfg["droplet_erosion_rate"] = float(droplet_erosion_rate_spin.value)
	if droplet_deposition_rate_spin: cfg["droplet_deposition_rate"] = float(droplet_deposition_rate_spin.value)
	if river_min_area_spin: cfg["river_min_area"] = int(river_min_area_spin.value)
	if river_width_mult_spin: cfg["river_width_multiplier"] = float(river_width_mult_spin.value)
	if river_carve_depth_spin: cfg["river_carve_depth"] = float(river_carve_depth_spin.value)
	if show_rivers_chk: cfg["show_rivers"] = show_rivers_chk.button_pressed
	if show_river_splines_chk: cfg["show_river_splines"] = show_river_splines_chk.button_pressed
	if shore_foam_dist_spin: cfg["shore_foam_distance"] = float(shore_foam_dist_spin.value)
	if crest_foam_spin: cfg["crest_foam_intensity"] = float(crest_foam_spin.value)
	
	if water_clarity_spin: cfg["water_depth_density"] = float(water_clarity_spin.value)
	if micro_ripple_spin: cfg["micro_ripple_strength"] = float(micro_ripple_spin.value)
	if cliff_threshold_spin: cfg["cliff_slope_threshold"] = float(cliff_threshold_spin.value)
	if snow_threshold_spin: cfg["snow_elevation_threshold"] = float(snow_threshold_spin.value)
	if fog_density_spin: cfg["fog_density"] = float(fog_density_spin.value)
	
	if wave_amp_slider: cfg["wave_amplitude"] = wave_amp_slider.value
	if toggle_water_chk: cfg["show_water"] = toggle_water_chk.button_pressed
	if toggle_vectors_chk: cfg["show_vectors"] = toggle_vectors_chk.button_pressed
	if toggle_wireframe_chk: cfg["double_sided"] = toggle_wireframe_chk.button_pressed
	cfg["cam_distance"] = cam_distance
	cfg["cam_rot_x"] = cam_rot_x
	cfg["cam_rot_y"] = cam_rot_y
	cfg["cam_target_x"] = cam_target.x
	cfg["cam_target_y"] = cam_target.y
	cfg["cam_target_z"] = cam_target.z
	return cfg

func _load_config_into_ui(dict: Dictionary) -> void:
	if dict.is_empty():
		return
	if dict.has("seed"):
		current_seed = int(dict["seed"])
		if seed_spin: seed_spin.value = current_seed
	if dict.has("map_width"): map_width = int(dict["map_width"])
	if dict.has("map_height"): map_height = int(dict["map_height"])
	if dict.has("cell_size"): cell_size = float(dict["cell_size"])
	if dict.has("elevation_scale"): elevation_scale = float(dict["elevation_scale"])
	if dict.has("num_plates") and num_plates_spin: num_plates_spin.value = dict["num_plates"]
	if dict.has("center_bias") and center_bias_spin: center_bias_spin.value = float(dict["center_bias"])
	if dict.has("continent_chance") and continent_chance_spin: continent_chance_spin.value = dict["continent_chance"]
	if dict.has("island_arc_chance") and island_arc_chance_spin: island_arc_chance_spin.value = dict["island_arc_chance"]
	if dict.has("warp_scale") and warp_scale_spin: warp_scale_spin.value = dict["warp_scale"]
	if dict.has("fold_belt_width") and fold_belt_width_spin: fold_belt_width_spin.value = dict["fold_belt_width"]
	if dict.has("collision_uplift") and collision_uplift_spin: collision_uplift_spin.value = dict["collision_uplift"]
	if dict.has("subduction_arc") and subduction_arc_spin: subduction_arc_spin.value = dict["subduction_arc"]
	if dict.has("subduction_trench") and subduction_trench_spin: subduction_trench_spin.value = dict["subduction_trench"]
	if dict.has("rift_graben") and rift_graben_spin: rift_graben_spin.value = dict["rift_graben"]
	if dict.has("rift_shoulder") and rift_shoulder_spin: rift_shoulder_spin.value = dict["rift_shoulder"]
	if dict.has("continental_base_elevation") and cont_base_elev_spin: cont_base_elev_spin.value = dict["continental_base_elevation"]
	if dict.has("oceanic_base_elevation") and ocean_base_elev_spin: ocean_base_elev_spin.value = dict["oceanic_base_elevation"]
	if dict.has("coastal_boundary_elevation") and coastal_boundary_elev_spin: coastal_boundary_elev_spin.value = dict["coastal_boundary_elevation"]
	if dict.has("shelf_width") and shelf_width_spin: shelf_width_spin.value = dict["shelf_width"]
	if dict.has("shelf_depth") and shelf_depth_spin: shelf_depth_spin.value = dict["shelf_depth"]
	if dict.has("detail_noise_scale") and detail_noise_spin: detail_noise_spin.value = dict["detail_noise_scale"]
	if dict.has("noise_octaves") and noise_octaves_spin: noise_octaves_spin.value = dict["noise_octaves"]

	if dict.has("world_gen_mode") and world_gen_mode_opt:
		world_gen_mode_opt.selected = int(dict["world_gen_mode"])
	if dict.has("opentopo_dataset") and opentopo_preset_opt:
		var ds = str(dict["opentopo_dataset"])
		if "alpine" in ds: opentopo_preset_opt.selected = 0
		elif "river" in ds: opentopo_preset_opt.selected = 1
		elif "rolling" in ds or "plains" in ds: opentopo_preset_opt.selected = 2
		elif "fjord" in ds or "coastal" in ds: opentopo_preset_opt.selected = 3
	if dict.has("sea_level_offset") and sea_level_offset_spin:
		sea_level_offset_spin.value = float(dict["sea_level_offset"])
	if dict.has("sea_buffer_tiles") and sea_buffer_spin:
		sea_buffer_spin.value = int(dict["sea_buffer_tiles"])
	if dict.has("apply_erosion") and apply_erosion_chk:
		apply_erosion_chk.button_pressed = bool(dict["apply_erosion"])
	if dict.has("apply_coastal_erosion") and apply_coastal_erosion_chk:
		apply_coastal_erosion_chk.button_pressed = bool(dict["apply_coastal_erosion"])
	if dict.has("coastal_erosion_iterations") and coastal_erosion_passes_spin:
		coastal_erosion_passes_spin.value = int(dict["coastal_erosion_iterations"])
	if dict.has("coastal_erosion_strength") and coastal_erosion_strength_spin:
		coastal_erosion_strength_spin.value = float(dict["coastal_erosion_strength"])
	
	if dict.has("apply_droplet_erosion") and apply_droplet_erosion_chk:
		apply_droplet_erosion_chk.button_pressed = bool(dict["apply_droplet_erosion"])
	if dict.has("droplet_count") and droplet_count_spin:
		droplet_count_spin.value = int(dict["droplet_count"])
	if dict.has("droplet_erosion_rate") and droplet_erosion_rate_spin:
		droplet_erosion_rate_spin.value = float(dict["droplet_erosion_rate"])
	if dict.has("droplet_deposition_rate") and droplet_deposition_rate_spin:
		droplet_deposition_rate_spin.value = float(dict["droplet_deposition_rate"])
	if dict.has("river_min_area") and river_min_area_spin:
		river_min_area_spin.value = int(dict["river_min_area"])
	if dict.has("river_width_multiplier") and river_width_mult_spin:
		river_width_mult_spin.value = float(dict["river_width_multiplier"])
	if dict.has("river_carve_depth") and river_carve_depth_spin:
		river_carve_depth_spin.value = float(dict["river_carve_depth"])
	if dict.has("show_rivers") and show_rivers_chk:
		show_rivers_chk.button_pressed = bool(dict["show_rivers"])
		if river_mesh_instance:
			river_mesh_instance.visible = show_rivers_chk.button_pressed
		if terrain_mat:
			terrain_mat.set_shader_parameter("show_rivers", show_rivers_chk.button_pressed)
	if dict.has("show_river_splines") and show_river_splines_chk:
		show_river_splines_chk.button_pressed = bool(dict["show_river_splines"])
		if river_spline_instance:
			river_spline_instance.visible = show_river_splines_chk.button_pressed
			if show_river_splines_chk.button_pressed and river_spline_instance.mesh == null and not elevation_data.is_empty():
				_build_river_splines()
	if dict.has("shore_foam_distance") and shore_foam_dist_spin:
		shore_foam_dist_spin.value = float(dict["shore_foam_distance"])
		if water_mat: water_mat.set_shader_parameter("shore_foam_distance", shore_foam_dist_spin.value)
	if dict.has("crest_foam_intensity") and crest_foam_spin:
		crest_foam_spin.value = float(dict["crest_foam_intensity"])
		if water_mat: water_mat.set_shader_parameter("crest_foam_intensity", crest_foam_spin.value)
	
	if dict.has("water_depth_density") and water_clarity_spin:
		water_clarity_spin.value = float(dict["water_depth_density"])
		if water_mat: water_mat.set_shader_parameter("depth_density", water_clarity_spin.value)
	if dict.has("micro_ripple_strength") and micro_ripple_spin:
		micro_ripple_spin.value = float(dict["micro_ripple_strength"])
		if water_mat: water_mat.set_shader_parameter("micro_ripple_strength", micro_ripple_spin.value)
	if dict.has("cliff_slope_threshold") and cliff_threshold_spin:
		cliff_threshold_spin.value = float(dict["cliff_slope_threshold"])
		if terrain_mat: terrain_mat.set_shader_parameter("cliff_slope_threshold", cliff_threshold_spin.value)
	if dict.has("snow_elevation_threshold") and snow_threshold_spin:
		snow_threshold_spin.value = float(dict["snow_elevation_threshold"])
		if terrain_mat: terrain_mat.set_shader_parameter("snow_elevation_threshold", snow_threshold_spin.value)
	if dict.has("fog_density") and fog_density_spin:
		fog_density_spin.value = float(dict["fog_density"])
		if env_node and env_node.environment:
			env_node.environment.fog_density = fog_density_spin.value
			env_node.environment.fog_enabled = (fog_density_spin.value > 0.00001)
	
	if dict.has("wave_amplitude") and wave_amp_slider:
		wave_amp_slider.value = float(dict["wave_amplitude"])
	if dict.has("show_water") and toggle_water_chk:
		toggle_water_chk.button_pressed = bool(dict["show_water"])
		if water_mesh_instance: water_mesh_instance.visible = toggle_water_chk.button_pressed
	if dict.has("show_vectors") and toggle_vectors_chk:
		toggle_vectors_chk.button_pressed = bool(dict["show_vectors"])
		if tectonic_gizmos_node: tectonic_gizmos_node.visible = toggle_vectors_chk.button_pressed
	if dict.has("double_sided") and toggle_wireframe_chk:
		toggle_wireframe_chk.button_pressed = bool(dict["double_sided"])
		if terrain_mesh_instance and terrain_mesh_instance.material_override and "cull_mode" in terrain_mesh_instance.material_override:
			terrain_mesh_instance.material_override.cull_mode = BaseMaterial3D.CULL_DISABLED if toggle_wireframe_chk.button_pressed else BaseMaterial3D.CULL_BACK
	if dict.has("cam_distance"): cam_distance = float(dict["cam_distance"])
	if dict.has("cam_rot_x"): cam_rot_x = float(dict["cam_rot_x"])
	if dict.has("cam_rot_y"): cam_rot_y = float(dict["cam_rot_y"])
	if dict.has("cam_target_x") and dict.has("cam_target_y") and dict.has("cam_target_z"):
		cam_target = Vector3(float(dict["cam_target_x"]), float(dict["cam_target_y"]), float(dict["cam_target_z"]))
	if camera_3d:
		_update_camera_transform()

func save_debug_config(path: String = CONFIG_PATH) -> bool:
	var cfg = get_ui_config()
	var json_str = JSON.stringify(cfg, "\t")
	var target_path = path
	var file = FileAccess.open(target_path, FileAccess.WRITE)
	if not file:
		target_path = FALLBACK_CONFIG_PATH
		file = FileAccess.open(target_path, FileAccess.WRITE)
		if not file:
			push_error("Failed to save 3D visualizer debug config: %s" % FileAccess.get_open_error())
			if status_lbl:
				status_lbl.text = "Error saving config file!"
			return false
	
	file.store_string(json_str)
	file.close()
	print("[World3DVisualizer] Config saved to %s" % target_path)
	if status_lbl:
		status_lbl.text = "Config saved to %s" % target_path
	return true

func load_debug_config(path: String = CONFIG_PATH) -> bool:
	var target_path = path
	if not FileAccess.file_exists(target_path):
		if FileAccess.file_exists(FALLBACK_CONFIG_PATH):
			target_path = FALLBACK_CONFIG_PATH
		else:
			return false
	
	var file = FileAccess.open(target_path, FileAccess.READ)
	if not file:
		push_error("Failed to open debug config file: %s" % target_path)
		return false
	
	var json_str = file.get_as_text()
	file.close()
	
	var json = JSON.new()
	var error = json.parse(json_str)
	if error != OK:
		push_error("Failed to parse debug config JSON: %s at line %d" % [json.get_error_message(), json.get_error_line()])
		return false
	
	var cfg = json.data
	if not (cfg is Dictionary):
		push_error("Debug config JSON root is not a dictionary.")
		return false
	
	_load_config_into_ui(cfg)
	print("[World3DVisualizer] Loaded debug config from %s" % target_path)
	if status_lbl:
		status_lbl.text = "Loaded debug config from %s" % target_path
	return true

func _apply_preset(preset_dict: Dictionary) -> void:
	_load_config_into_ui(preset_dict)
	generate_world(current_seed)

func _on_random_seed_pressed() -> void:
	randomize()
	var new_seed = randi() % 100000000
	current_seed = new_seed
	if seed_spin:
		seed_spin.value = new_seed
	generate_world(new_seed)

func _on_generate_pressed() -> void:
	var seed_val = int(seed_spin.value) if seed_spin else current_seed
	generate_world(seed_val)

func _setup_ui() -> void:
	ui_root = Control.new()
	ui_root.set_anchors_preset(Control.PRESET_FULL_RECT)
	ui_root.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(ui_root)
	
	# Left Sidebar Control Panel (Scrollable)
	var panel = PanelContainer.new()
	panel.anchor_top = 0.0
	panel.anchor_bottom = 1.0
	panel.offset_left = 16
	panel.offset_top = 16
	panel.offset_right = 350
	panel.offset_bottom = -36
	
	var style_box = StyleBoxFlat.new()
	style_box.bg_color = Color(0.08, 0.10, 0.14, 0.88)
	style_box.border_color = Color(0.25, 0.35, 0.45, 0.7)
	style_box.border_width_left = 1
	style_box.border_width_right = 1
	style_box.border_width_top = 1
	style_box.border_width_bottom = 1
	style_box.corner_radius_top_left = 6
	style_box.corner_radius_top_right = 6
	style_box.corner_radius_bottom_left = 6
	style_box.corner_radius_bottom_right = 6
	style_box.content_margin_left = 10
	style_box.content_margin_right = 10
	style_box.content_margin_top = 10
	style_box.content_margin_bottom = 10
	panel.add_theme_stylebox_override("panel", style_box)
	ui_root.add_child(panel)
	
	var scroll = ScrollContainer.new()
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	scroll.vertical_scroll_mode = ScrollContainer.SCROLL_MODE_AUTO
	scroll.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	panel.add_child(scroll)
	
	var vbox = VBoxContainer.new()
	vbox.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	vbox.add_theme_constant_override("separation", 6)
	scroll.add_child(vbox)
	
	# 1. Header
	var title = Label.new()
	title.text = "3D CONTINUOUS TECTONICS"
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	title.add_theme_color_override("font_color", Color(1.0, 0.88, 0.40))
	vbox.add_child(title)
	
	var subtitle = Label.new()
	subtitle.text = "Deterministic 16.16 Simulation (512×512)"
	subtitle.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	subtitle.add_theme_color_override("font_color", Color(0.70, 0.75, 0.80))
	var font = subtitle.get_theme_default_font()
	if font:
		subtitle.add_theme_font_size_override("font_size", 11)
	vbox.add_child(subtitle)
	
	# 2. Seed & Generation
	vbox.add_child(_create_section_header("SEED & GENERATION"))
	
	var seed_box = HBoxContainer.new()
	var seed_lbl = Label.new()
	seed_lbl.text = "World Seed:"
	seed_lbl.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	seed_lbl.add_theme_color_override("font_color", Color(0.85, 0.88, 0.92))
	seed_box.add_child(seed_lbl)
	
	seed_spin = SpinBox.new()
	seed_spin.min_value = 0
	seed_spin.max_value = 999999999
	seed_spin.value = current_seed
	seed_spin.custom_minimum_size = Vector2(90, 0)
	seed_box.add_child(seed_spin)
	vbox.add_child(seed_box)
	
	var seed_btn_row = HBoxContainer.new()
	var rand_btn = Button.new()
	rand_btn.text = "Random Seed [R]"
	rand_btn.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	rand_btn.pressed.connect(self._on_random_seed_pressed)
	seed_btn_row.add_child(rand_btn)
	
	var regen_btn = Button.new()
	regen_btn.text = "Regenerate"
	regen_btn.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	regen_btn.pressed.connect(self._on_generate_pressed)
	seed_btn_row.add_child(regen_btn)
	vbox.add_child(seed_btn_row)
	
	# Debug Configuration File
	vbox.add_child(_create_section_header("DEBUG CONFIGURATION FILE"))
	var cfg_btn_row = HBoxContainer.new()
	var save_cfg_btn = Button.new()
	save_cfg_btn.text = "Save Config [Ctrl+S]"
	save_cfg_btn.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	save_cfg_btn.pressed.connect(func():
		save_debug_config()
	)
	cfg_btn_row.add_child(save_cfg_btn)
	
	var load_cfg_btn = Button.new()
	load_cfg_btn.text = "Load Config [Ctrl+L]"
	load_cfg_btn.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	load_cfg_btn.pressed.connect(func():
		if load_debug_config():
			generate_world(current_seed)
	)
	cfg_btn_row.add_child(load_cfg_btn)
	vbox.add_child(cfg_btn_row)

	# OpenTopography & Geomorphology
	vbox.add_child(_create_section_header("OPENTOPO & GEOMORPHOLOGY"))
	var mode_box = HBoxContainer.new()
	var mode_lbl = Label.new()
	mode_lbl.text = "World Gen Mode:"
	mode_lbl.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	mode_lbl.add_theme_color_override("font_color", Color(0.85, 0.88, 0.92))
	mode_box.add_child(mode_lbl)
	world_gen_mode_opt = OptionButton.new()
	world_gen_mode_opt.add_item("Tectonic Voronoi (0)", 0)
	world_gen_mode_opt.add_item("Synthesized OpenTopo (1)", 1)
	world_gen_mode_opt.selected = 1
	mode_box.add_child(world_gen_mode_opt)
	vbox.add_child(mode_box)

	var preset_box = HBoxContainer.new()
	var preset_lbl = Label.new()
	preset_lbl.text = "DEM Dataset:"
	preset_lbl.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	preset_lbl.add_theme_color_override("font_color", Color(0.85, 0.88, 0.92))
	preset_box.add_child(preset_lbl)
	opentopo_preset_opt = OptionButton.new()
	opentopo_preset_opt.add_item("Alpine Basin (Rainier)", 0)
	opentopo_preset_opt.add_item("River Valley (Columbia)", 1)
	opentopo_preset_opt.add_item("Rolling Plains (Palouse)", 2)
	opentopo_preset_opt.add_item("Coastal Fjord (Puget)", 3)
	opentopo_preset_opt.selected = 0
	preset_box.add_child(opentopo_preset_opt)
	vbox.add_child(preset_box)

	sea_level_offset_spin = _create_spin_row(vbox, "Sea Level Offset:", -30.0, 30.0, 0.5, 0.0)
	sea_buffer_spin = _create_spin_row(vbox, "Perimeter Sea Buffer:", 0.0, 64.0, 4.0, 32.0)
	apply_erosion_chk = _create_check_row(vbox, "Fluvial & Talus Erosion", true)
	apply_coastal_erosion_chk = _create_check_row(vbox, "Wave Coastal Erosion", true)
	coastal_erosion_passes_spin = _create_spin_row(vbox, "Coastal Passes:", 0.0, 20.0, 1.0, 3.0)
	coastal_erosion_strength_spin = _create_spin_row(vbox, "Coastal Carve Strength:", 0.1, 10.0, 0.1, 2.0)
	
	# Droplet Hydraulic Erosion & River Network
	vbox.add_child(_create_section_header("DROPLET EROSION & RIVER NETWORK"))
	apply_droplet_erosion_chk = _create_check_row(vbox, "Droplet Hydraulic Erosion", true)
	droplet_count_spin = _create_spin_row(vbox, "Droplet Count:", 5000.0, 300000.0, 5000.0, 100000.0)
	droplet_erosion_rate_spin = _create_spin_row(vbox, "Droplet Erode Rate:", 0.05, 1.0, 0.05, 0.30)
	droplet_deposition_rate_spin = _create_spin_row(vbox, "Droplet Deposit Rate:", 0.05, 1.0, 0.05, 0.30)
	river_min_area_spin = _create_spin_row(vbox, "River Threshold Area:", 15.0, 500.0, 5.0, 40.0)
	river_width_mult_spin = _create_spin_row(vbox, "River Width Multiplier:", 0.5, 3.0, 0.1, 1.0)
	river_carve_depth_spin = _create_spin_row(vbox, "River Carve Depth (m):", 0.5, 10.0, 0.5, 4.5)
	show_rivers_chk = _create_check_row(vbox, "Show River Ribbons", true)
	show_rivers_chk.toggled.connect(func(t: bool):
		if river_mesh_instance:
			river_mesh_instance.visible = t
			if t and river_mesh_instance.mesh == null and not elevation_data.is_empty():
				_build_river_ribbons()
		if terrain_mat:
			terrain_mat.set_shader_parameter("show_rivers", t)
	)
	show_river_splines_chk = _create_check_row(vbox, "Show River Splines", true)
	show_river_splines_chk.toggled.connect(func(t: bool):
		if river_spline_instance:
			river_spline_instance.visible = t
			if t and river_spline_instance.mesh == null and not elevation_data.is_empty():
				_build_river_splines()
	)
	
	# 3. Tectonic Plates & Warping
	vbox.add_child(_create_section_header("TECTONIC PLATES & WARPING"))
	num_plates_spin = _create_spin_row(vbox, "Plates Count:", 2, 40, 1, 16)
	center_bias_spin = _create_spin_row(vbox, "Velocity Center Bias:", -1.0, 1.0, 0.05, 0.0)
	warp_scale_spin = _create_spin_row(vbox, "Domain Warp Scale:", 0, 150, 1, 75)
	fold_belt_width_spin = _create_spin_row(vbox, "Fold Belt Width:", 10, 100, 1, 40)
	
	# 4. Orogeny & Mountain Elevation
	vbox.add_child(_create_section_header("OROGENY & MOUNTAINS"))
	collision_uplift_spin = _create_spin_row(vbox, "Collision Uplift:", 0, 80, 1, 38)
	rift_graben_spin = _create_spin_row(vbox, "Rift Graben Depth:", 0, 50, 1, 22)
	rift_shoulder_spin = _create_spin_row(vbox, "Rift Shoulder:", 0, 30, 1, 12)
	cont_base_elev_spin = _create_spin_row(vbox, "Continental Base Elev:", 0, 50, 1, 26)
	
	# 5. Terrain Detail & Noise
	vbox.add_child(_create_section_header("TERRAIN DETAIL & NOISE"))
	detail_noise_spin = _create_spin_row(vbox, "Detail Noise Scale:", 0, 30, 0.5, 14.0)
	noise_octaves_spin = _create_spin_row(vbox, "Noise Octaves:", 1, 8, 1, 6)
	
	var apply_knobs_btn = Button.new()
	apply_knobs_btn.text = "Apply Parameters & Regenerate [Space]"
	apply_knobs_btn.pressed.connect(self._on_generate_pressed)
	vbox.add_child(apply_knobs_btn)
	
	# 6. Continental Presets
	vbox.add_child(_create_section_header("CONTINENTAL PRESETS"))
	
	var preset_std_btn = Button.new()
	preset_std_btn.text = "Preset: Major Continents (Default)"
	preset_std_btn.pressed.connect(func(): _apply_preset({
		"num_plates": 16, "warp_scale": 75.0, "fold_belt_width": 40.0,
		"collision_uplift": 38.0, "rift_graben": 22.0, "rift_shoulder": 12.0,
		"continental_base_elevation": 26.0, "detail_noise_scale": 14.0
	}))
	vbox.add_child(preset_std_btn)
	
	var preset_alpine_btn = Button.new()
	preset_alpine_btn.text = "Preset: Alpine Orogeny & Peaks"
	preset_alpine_btn.pressed.connect(func(): _apply_preset({
		"num_plates": 16, "warp_scale": 85.0, "fold_belt_width": 50.0,
		"collision_uplift": 55.0, "continental_base_elevation": 28.0,
		"detail_noise_scale": 18.0
	}))
	vbox.add_child(preset_alpine_btn)
	
	var preset_pangea_btn = Button.new()
	preset_pangea_btn.text = "Preset: Pangea Supercontinent"
	preset_pangea_btn.pressed.connect(func(): _apply_preset({
		"num_plates": 8, "warp_scale": 95.0, "fold_belt_width": 55.0,
		"collision_uplift": 45.0, "continental_base_elevation": 24.0,
		"detail_noise_scale": 16.0
	}))
	vbox.add_child(preset_pangea_btn)
	
	var preset_islands_btn = Button.new()
	preset_islands_btn.text = "Preset: Fractured Microplates"
	preset_islands_btn.pressed.connect(func(): _apply_preset({
		"num_plates": 24, "warp_scale": 90.0, "fold_belt_width": 30.0,
		"collision_uplift": 30.0, "rift_graben": 28.0, "detail_noise_scale": 14.0
	}))
	vbox.add_child(preset_islands_btn)
	
	var preset_lowlands_btn = Button.new()
	preset_lowlands_btn.text = "Preset: Rolling Lowlands & Hills"
	preset_lowlands_btn.pressed.connect(func(): _apply_preset({
		"num_plates": 10, "warp_scale": 55.0, "fold_belt_width": 45.0,
		"collision_uplift": 18.0, "continental_base_elevation": 18.0,
		"detail_noise_scale": 10.0
	}))
	vbox.add_child(preset_lowlands_btn)
	
	var preset_rifts_btn = Button.new()
	preset_rifts_btn.text = "Preset: Deep Rifts & Canyons"
	preset_rifts_btn.pressed.connect(func(): _apply_preset({
		"num_plates": 16, "warp_scale": 80.0, "fold_belt_width": 40.0,
		"collision_uplift": 32.0, "rift_graben": 36.0, "rift_shoulder": 20.0,
		"detail_noise_scale": 16.0
	}))
	vbox.add_child(preset_rifts_btn)
	
	var reset_knobs_btn = Button.new()
	reset_knobs_btn.text = "Reset Knobs to Engine Defaults"
	reset_knobs_btn.pressed.connect(func(): _apply_preset({
		"seed": 1337,
		"num_plates": 16,
		"warp_scale": 75.0, "fold_belt_width": 40.0, "collision_uplift": 38.0,
		"rift_graben": 22.0, "rift_shoulder": 12.0, "continental_base_elevation": 26.0,
		"detail_noise_scale": 14.0, "noise_octaves": 6
	}))
	vbox.add_child(reset_knobs_btn)
	
	# 7. Wave & Water Controls
	vbox.add_child(_create_section_header("BOUNDARY WAVES & WATER"))
	
	var amp_box = HBoxContainer.new()
	var amp_title = Label.new()
	amp_title.text = "Wave Amplitude:"
	amp_title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	amp_title.add_theme_color_override("font_color", Color(0.85, 0.88, 0.92))
	amp_box.add_child(amp_title)
	
	wave_amp_slider = HSlider.new()
	wave_amp_slider.min_value = 0.0
	wave_amp_slider.max_value = 3.0
	wave_amp_slider.step = 0.1
	wave_amp_slider.value = 0.7
	wave_amp_slider.custom_minimum_size = Vector2(90, 0)
	wave_amp_slider.value_changed.connect(func(v):
		if wave_driver and not wave_driver.waves.is_empty():
			for i in range(wave_driver.waves.size()):
				wave_driver.waves[i].amplitude = (2.0 / float(i + 1)) * (v / 0.7)
			wave_driver.update_waves()
	)
	amp_box.add_child(wave_amp_slider)
	vbox.add_child(amp_box)
	
	toggle_water_chk = CheckBox.new()
	toggle_water_chk.text = "Show Water Surface"
	toggle_water_chk.button_pressed = true
	toggle_water_chk.toggled.connect(func(t): water_mesh_instance.visible = t)
	vbox.add_child(toggle_water_chk)
	
	toggle_vectors_chk = CheckBox.new()
	toggle_vectors_chk.text = "Show Plate Vectors"
	toggle_vectors_chk.button_pressed = true
	toggle_vectors_chk.toggled.connect(func(t): tectonic_gizmos_node.visible = t)
	vbox.add_child(toggle_vectors_chk)
	
	toggle_wireframe_chk = CheckBox.new()
	toggle_wireframe_chk.text = "Double-Sided Culling"
	toggle_wireframe_chk.button_pressed = false
	toggle_wireframe_chk.toggled.connect(func(t):
		if terrain_mesh_instance.material_override and "cull_mode" in terrain_mesh_instance.material_override:
			terrain_mesh_instance.material_override.cull_mode = BaseMaterial3D.CULL_DISABLED if t else BaseMaterial3D.CULL_BACK
	)
	vbox.add_child(toggle_wireframe_chk)
	
	toggle_trees_chk = CheckBox.new()
	toggle_trees_chk.text = "Show Procedural Trees"
	toggle_trees_chk.button_pressed = true
	toggle_trees_chk.toggled.connect(func(t):
		if tree_chunks_node:
			tree_chunks_node.visible = t
		if t and loaded_tree_chunks.is_empty():
			_update_active_tree_chunks(true)
	)
	vbox.add_child(toggle_trees_chk)

	tree_load_all_chk = CheckBox.new()
	tree_load_all_chk.text = "Load All Chunks (Full World)"
	tree_load_all_chk.button_pressed = true
	tree_load_all_chk.toggled.connect(func(t):
		tree_load_all = t
		_clear_tree_chunks()
		_update_active_tree_chunks(true)
	)
	vbox.add_child(tree_load_all_chk)
	
	var rad_box = HBoxContainer.new()
	var rad_lbl = Label.new()
	rad_lbl.text = "Tree Stream Radius:"
	rad_lbl.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	rad_lbl.add_theme_color_override("font_color", Color(0.85, 0.88, 0.92))
	rad_box.add_child(rad_lbl)
	
	tree_radius_slider = HSlider.new()
	tree_radius_slider.min_value = 2
	tree_radius_slider.max_value = 32
	tree_radius_slider.step = 1
	tree_radius_slider.value = 16
	tree_radius_slider.custom_minimum_size = Vector2(90, 0)
	tree_radius_slider.value_changed.connect(func(v):
		tree_chunk_radius = int(v)
		if not tree_load_all:
			_update_active_tree_chunks(true)
	)
	rad_box.add_child(tree_radius_slider)
	vbox.add_child(rad_box)
	
	# Interactive Wave Particle Emitters
	var part_btn_row = HBoxContainer.new()
	var drop_btn = Button.new()
	drop_btn.text = "Drop Ripple [E]"
	drop_btn.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	drop_btn.pressed.connect(func():
		if wave_driver:
			wave_driver.spawn_ripple(Vector2(cam_target.x, cam_target.z), 18, 2.4, 30.0, 24.0)
	)
	part_btn_row.add_child(drop_btn)
	
	var swell_btn = Button.new()
	swell_btn.text = "Emit Swell Train"
	swell_btn.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	swell_btn.pressed.connect(func():
		if wave_driver:
			wave_driver.spawn_ocean_swell(Vector2(0.85, 0.48), 10, 2.0, 60.0, 25.0)
	)
	part_btn_row.add_child(swell_btn)
	vbox.add_child(part_btn_row)
	
	# 8. Advanced Shader & Visual Fidelity Controls
	vbox.add_child(_create_section_header("ADVANCED SHADERS & FIDELITY"))
	
	water_clarity_spin = _create_spin_row(vbox, "Water Clarity (Density):", 0.01, 0.50, 0.01, 0.08)
	water_clarity_spin.value_changed.connect(func(v):
		if water_mat: water_mat.set_shader_parameter("depth_density", v)
	)
	
	micro_ripple_spin = _create_spin_row(vbox, "Micro Ripple Glints:", 0.0, 1.0, 0.05, 0.40)
	micro_ripple_spin.value_changed.connect(func(v):
		if water_mat: water_mat.set_shader_parameter("micro_ripple_strength", v)
	)

	shore_foam_dist_spin = _create_spin_row(vbox, "Shoreline Foam Reach (m):", 0.2, 5.0, 0.1, 1.4)
	shore_foam_dist_spin.value_changed.connect(func(v):
		if water_mat: water_mat.set_shader_parameter("shore_foam_distance", v)
	)

	crest_foam_spin = _create_spin_row(vbox, "Wave Crest Whitecaps:", 0.0, 2.0, 0.05, 1.15)
	crest_foam_spin.value_changed.connect(func(v):
		if water_mat: water_mat.set_shader_parameter("crest_foam_intensity", v)
	)
	
	cliff_threshold_spin = _create_spin_row(vbox, "Cliff Rock Slope Threshold:", 0.20, 0.90, 0.02, 0.62)
	cliff_threshold_spin.value_changed.connect(func(v):
		if terrain_mat: terrain_mat.set_shader_parameter("cliff_slope_threshold", v)
	)
	
	snow_threshold_spin = _create_spin_row(vbox, "Glacial Snow Elevation:", 20.0, 90.0, 1.0, 60.0)
	snow_threshold_spin.value_changed.connect(func(v):
		if terrain_mat: terrain_mat.set_shader_parameter("snow_elevation_threshold", v)
	)
	
	fog_density_spin = _create_spin_row(vbox, "Atmospheric Fog Density:", 0.0, 0.005, 0.0001, 0.0008)
	fog_density_spin.value_changed.connect(func(v):
		if env_node and env_node.environment:
			env_node.environment.fog_density = v
			env_node.environment.fog_enabled = (v > 0.00001)
	)
	
	# 11. Switch Scene
	vbox.add_child(_create_section_header("SCENE NAVIGATION"))
	
	var scene_2d_btn = Button.new()
	scene_2d_btn.text = "Switch to 2D Map Gen Visualizer"
	scene_2d_btn.pressed.connect(func(): get_tree().change_scene_to_file("res://scenes/visualizers/map_gen_visualizer.tscn"))
	vbox.add_child(scene_2d_btn)
	
	var main_scene_btn = Button.new()
	main_scene_btn.text = "Return to Main Map Visualizer"
	main_scene_btn.pressed.connect(func(): get_tree().change_scene_to_file("res://scenes/visualizers/map_visualizer.tscn"))
	vbox.add_child(main_scene_btn)
	
	# 12. Controls Reference
	vbox.add_child(_create_section_header("KEYBOARD & MOUSE"))
	var help_lbl = Label.new()
	help_lbl.text = "[E] Drop Ripple at Focus\n[LMB] Probe & Ripple Water\n[Space] Apply Knobs & Regenerate\n[Ctrl+S] Save Debug Config\n[Ctrl+L] Load Debug Config\n[R] Random Seed\n[RMB Drag] Orbit Camera\n[MMB Drag] Pan Camera\n[Mouse Wheel] Zoom In/Out\n[WASD / QE] Fly Camera"
	help_lbl.add_theme_color_override("font_color", Color(0.70, 0.75, 0.80))
	var hfont = help_lbl.get_theme_default_font()
	if hfont:
		help_lbl.add_theme_font_size_override("font_size", 11)
	vbox.add_child(help_lbl)
	
	# Probe Inspector Panel (Top-Right)
	var probe_panel = PanelContainer.new()
	probe_panel.anchor_left = 1.0
	probe_panel.anchor_right = 1.0
	probe_panel.offset_left = -300
	probe_panel.offset_right = -20
	probe_panel.offset_top = 20
	probe_panel.custom_minimum_size = Vector2(280, 0)
	
	var p_style = StyleBoxFlat.new()
	p_style.bg_color = Color(0.08, 0.10, 0.14, 0.88)
	p_style.border_color = Color(0.25, 0.35, 0.45, 0.7)
	p_style.border_width_left = 1
	p_style.border_width_right = 1
	p_style.border_width_top = 1
	p_style.border_width_bottom = 1
	p_style.corner_radius_top_left = 6
	p_style.corner_radius_top_right = 6
	p_style.corner_radius_bottom_left = 6
	p_style.corner_radius_bottom_right = 6
	p_style.content_margin_left = 10
	p_style.content_margin_right = 10
	p_style.content_margin_top = 10
	p_style.content_margin_bottom = 10
	probe_panel.add_theme_stylebox_override("panel", p_style)
	ui_root.add_child(probe_panel)
	
	var pvbox = VBoxContainer.new()
	probe_panel.add_child(pvbox)
	
	var ptitle = Label.new()
	ptitle.text = "TECTONIC PROBE (Click Ground)"
	ptitle.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	pvbox.add_child(ptitle)
	pvbox.add_child(HSeparator.new())
	
	probe_lbl = Label.new()
	probe_lbl.text = "Click anywhere on 3D terrain\nto inspect continuous elevation,\nplate velocity, and boundary physics."
	pvbox.add_child(probe_lbl)
	
	# Status bar (Bottom)
	status_lbl = Label.new()
	status_lbl.anchor_top = 1.0
	status_lbl.anchor_bottom = 1.0
	status_lbl.offset_left = 365
	status_lbl.offset_top = -32
	status_lbl.offset_right = -20
	status_lbl.text = "Controls: RMB=Orbit, MMB=Pan, Wheel=Zoom, LMB=Probe, Space=Apply Knobs, Ctrl+S=Save, Ctrl+L=Load"
	ui_root.add_child(status_lbl)
	
	# Load engine config into UI controls
	_load_config_into_ui(bridge.get_config())

func _update_status_bar() -> void:
	if status_lbl:
		var tree_cnt = bridge.get_tree_count() if bridge else 0
		var loaded_cnt = loaded_tree_chunks.size()
		var grid_sz = bridge.get_chunk_grid_size() if bridge else Vector2i(32, 32)
		var total_grid_chunks = grid_sz.x * grid_sz.y
		status_lbl.text = "Seed: %d | Size: %dx%d | Plates: %d | Trees: %d (Chunks %d/%d) | RMB=Orbit, MMB=Pan, Wheel=Zoom, LMB=Probe, Space=Apply, Ctrl+S=Save, Ctrl+L=Load" % [
			current_seed, map_width, map_height, int(num_plates_spin.value if num_plates_spin else 16), tree_cnt, loaded_cnt, total_grid_chunks
		]
