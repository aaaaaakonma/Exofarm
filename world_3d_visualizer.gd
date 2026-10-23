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
var status_lbl: Label
var probe_lbl: Label

# Cached Data
var elevation_data: PackedInt32Array
var biome_data: PackedByteArray
var water_mat: ShaderMaterial

func _ready() -> void:
	add_child(bridge)
	add_child(wave_baker)
	add_child(wave_driver)
	
	_setup_environment()
	_setup_camera()
	_setup_nodes()
	_setup_ui()
	
	if OS.is_debug_build() or FileAccess.file_exists(CONFIG_PATH) or FileAccess.file_exists(FALLBACK_CONFIG_PATH):
		load_debug_config()
	
	generate_world(current_seed)

func _setup_environment() -> void:
	# Directional Sun Light
	sun_light = DirectionalLight3D.new()
	sun_light.rotation_degrees = Vector3(-50, 45, 0)
	sun_light.light_color = Color(1.0, 0.96, 0.9)
	sun_light.light_energy = 1.2
	sun_light.shadow_enabled = true
	add_child(sun_light)
	
	# Sky Environment
	var env = Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.12, 0.15, 0.22)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.4, 0.45, 0.55)
	env.ambient_light_energy = 0.8
	env.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	env.glow_enabled = true
	env.glow_intensity = 0.5
	
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
	
	tectonic_gizmos_node = Node3D.new()
	add_child(tectonic_gizmos_node)

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
	
	# 3. Build Tectonic Plate Vector Gizmos
	_build_tectonic_gizmos()
	
	_update_status_bar()

func _build_terrain_mesh() -> void:
	var array_mesh = ArrayMesh.new()
	var chunk_size = 128 # 4x4 = 16 chunk surfaces for 512x512
	var chunks_x = (map_width + chunk_size - 1) / chunk_size
	var chunks_y = (map_height + chunk_size - 1) / chunk_size
	
	var mat = StandardMaterial3D.new()
	mat.vertex_color_use_as_albedo = true
	mat.roughness = 0.85
	mat.specular = 0.2
	mat.cull_mode = BaseMaterial3D.CULL_BACK
	
	var w = map_width
	var h = map_height
	
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
					colors[v_idx] = _get_terrain_color(el, b)
					
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
	terrain_mesh_instance.material_override = mat

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
	# 1. Build Physical Heightmap Image & Texture for GPU Cage Baker & Wave Compute
	var num_pixels = map_width * map_height
	var heights = PackedFloat32Array()
	heights.resize(num_pixels)
	for i in range(num_pixels):
		heights[i] = float(elevation_data[i]) / 65536.0
	
	var hm_img = Image.create_from_data(map_width, map_height, false, Image.FORMAT_RF, heights.to_byte_array())
	var hm_tex = ImageTexture.create_from_image(hm_img)

	# 2. Setup Plane Mesh for Water Surface
	var plane_mesh = PlaneMesh.new()
	plane_mesh.size = Vector2(map_width * cell_size, map_height * cell_size)
	plane_mesh.subdivide_width = 256
	plane_mesh.subdivide_depth = 256
	
	water_mesh_instance.mesh = plane_mesh
	water_mesh_instance.position = Vector3(
		(map_width * cell_size) / 2.0 - (cell_size / 2.0),
		sea_level * elevation_scale,
		(map_height * cell_size) / 2.0 - (cell_size / 2.0)
	)
	
	# 3. Create Boundary-Aware Water Material
	var shader = load("res://assets/shaders/wave_water.gdshader")
	if shader:
		water_mat = ShaderMaterial.new()
		water_mat.shader = shader
		water_mat.set_shader_parameter("heightmap_world_size", Vector2(map_width * cell_size, map_height * cell_size))
		water_mat.set_shader_parameter("height_scale", elevation_scale)
		water_mat.set_shader_parameter("sea_level_offset", sea_level)
		water_mesh_instance.material_override = water_mat
	
	# 4. Bake Wave Cages via GPU Compute Baker (Jeschke 2020 JFA + Raymarching)
	wave_baker.physical_size = map_width * cell_size
	wave_baker.grid_size = map_width
	wave_baker.sea_level_normalized = 0.0
	wave_baker.height_scale_factor = 1.0
	var cage_texture = wave_baker.bake_from_heightmap(hm_tex)
	
	# 5. Initialize Wave Compute Driver (Gerstner waves + Ellipsoidal Caging)
	wave_driver.grid_size = map_width
	wave_driver.world_size = map_width * cell_size
	wave_driver.height_scale_factor = 1.0
	wave_driver.target_material = water_mat
	wave_driver.update_textures(cage_texture, hm_tex)

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
			var b_names = ["Ocean", "Tundra", "Desert", "Grassland", "Forest", "Rainforest", "Swamp", "Mountain"]
			var b_idx = biome_data[ty * map_width + tx] if not biome_data.is_empty() else 0
			var b_name = b_names[b_idx] if b_idx < b_names.size() else "Unknown"
			
			var btype_names = ["Interior", "Continental Collision", "Subduction", "Island Arc", "Continental Rift", "Ocean Spreading", "Transform Shear"]
			var b_type_idx = sample.get("boundary_type", 0)
			var b_type_name = btype_names[b_type_idx] if b_type_idx < btype_names.size() else "Interior"
			
			probe_lbl.text = "Tile: (%d, %d)\nElevation: %.1f m (Base: %.1f, Tectonic: %.1f)\nBiome: %s\nPlate ID: %d | Neighbor: %d\nBoundary Type: %s\nConvergence: %.2f | Shear: %.2f\nDist to Boundary: %.1f m" % [
				tx, ty,
				sample.get("elevation", 0.0),
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
		if terrain_mesh_instance and terrain_mesh_instance.material_override:
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
	
	# 3. Tectonic Plates & Warping
	vbox.add_child(_create_section_header("TECTONIC PLATES & WARPING"))
	num_plates_spin = _create_spin_row(vbox, "Plates Count:", 2, 40, 1, 16)
	continent_chance_spin = _create_spin_row(vbox, "Continent %:", 0, 100, 1, 60)
	island_arc_chance_spin = _create_spin_row(vbox, "Island Arc %:", 0, 100, 1, 15)
	warp_scale_spin = _create_spin_row(vbox, "Domain Warp Scale:", 0, 150, 1, 75)
	fold_belt_width_spin = _create_spin_row(vbox, "Fold Belt Width:", 10, 100, 1, 40)
	
	# 4. Orogeny & Mountain Elevation
	vbox.add_child(_create_section_header("OROGENY & MOUNTAINS"))
	collision_uplift_spin = _create_spin_row(vbox, "Collision Uplift:", 0, 80, 1, 38)
	subduction_arc_spin = _create_spin_row(vbox, "Volcanic Arc Peak:", 0, 60, 1, 24)
	subduction_trench_spin = _create_spin_row(vbox, "Trench Depth:", 0, 60, 1, 22)
	rift_graben_spin = _create_spin_row(vbox, "Rift Graben Depth:", 0, 50, 1, 22)
	rift_shoulder_spin = _create_spin_row(vbox, "Rift Shoulder:", 0, 30, 1, 12)
	cont_base_elev_spin = _create_spin_row(vbox, "Continental Base Elev:", 0, 50, 1, 26)
	ocean_base_elev_spin = _create_spin_row(vbox, "Oceanic Base Elev:", -60, 0, 1, -36)
	coastal_boundary_elev_spin = _create_spin_row(vbox, "Coast Boundary Elev:", -10, 10, 0.5, 2.0)
	shelf_width_spin = _create_spin_row(vbox, "Shelf Width:", 10, 150, 5, 36)
	shelf_depth_spin = _create_spin_row(vbox, "Shelf Depth:", -30, 0, 1, -8)
	
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
		"num_plates": 16, "continent_chance": 60, "island_arc_chance": 15,
		"warp_scale": 75.0, "fold_belt_width": 40.0, "collision_uplift": 38.0,
		"subduction_arc": 24.0, "subduction_trench": 22.0, "detail_noise_scale": 14.0, "coastal_boundary_elevation": 2.0
	}))
	vbox.add_child(preset_std_btn)
	
	var preset_alpine_btn = Button.new()
	preset_alpine_btn.text = "Preset: Alpine Orogeny & Peaks"
	preset_alpine_btn.pressed.connect(func(): _apply_preset({
		"num_plates": 16, "continent_chance": 75, "island_arc_chance": 10,
		"warp_scale": 85.0, "fold_belt_width": 50.0, "collision_uplift": 55.0,
		"subduction_arc": 32.0, "subduction_trench": 26.0, "detail_noise_scale": 18.0
	}))
	vbox.add_child(preset_alpine_btn)
	
	var preset_pangea_btn = Button.new()
	preset_pangea_btn.text = "Preset: Pangea Supercontinent"
	preset_pangea_btn.pressed.connect(func(): _apply_preset({
		"num_plates": 8, "continent_chance": 88, "island_arc_chance": 5,
		"warp_scale": 95.0, "fold_belt_width": 55.0, "collision_uplift": 45.0,
		"subduction_arc": 28.0, "subduction_trench": 22.0, "detail_noise_scale": 16.0
	}))
	vbox.add_child(preset_pangea_btn)
	
	var preset_islands_btn = Button.new()
	preset_islands_btn.text = "Preset: Volcanic Archipelago"
	preset_islands_btn.pressed.connect(func(): _apply_preset({
		"num_plates": 24, "continent_chance": 30, "island_arc_chance": 40,
		"warp_scale": 90.0, "fold_belt_width": 30.0, "collision_uplift": 25.0,
		"subduction_arc": 32.0, "subduction_trench": 30.0, "detail_noise_scale": 14.0
	}))
	vbox.add_child(preset_islands_btn)
	
	var preset_lowlands_btn = Button.new()
	preset_lowlands_btn.text = "Preset: Rolling Lowlands & Hills"
	preset_lowlands_btn.pressed.connect(func(): _apply_preset({
		"num_plates": 10, "continent_chance": 65, "island_arc_chance": 10,
		"warp_scale": 55.0, "fold_belt_width": 45.0, "collision_uplift": 18.0,
		"subduction_arc": 14.0, "subduction_trench": 16.0, "detail_noise_scale": 10.0
	}))
	vbox.add_child(preset_lowlands_btn)
	
	var preset_rifts_btn = Button.new()
	preset_rifts_btn.text = "Preset: Deep Rifts & Canyons"
	preset_rifts_btn.pressed.connect(func(): _apply_preset({
		"num_plates": 16, "continent_chance": 65, "island_arc_chance": 15,
		"warp_scale": 80.0, "fold_belt_width": 40.0, "rift_graben": 36.0,
		"rift_shoulder": 20.0, "subduction_trench": 32.0, "detail_noise_scale": 16.0
	}))
	vbox.add_child(preset_rifts_btn)
	
	var reset_knobs_btn = Button.new()
	reset_knobs_btn.text = "Reset Knobs to Engine Defaults"
	reset_knobs_btn.pressed.connect(func(): _apply_preset({
		"seed": 1337,
		"num_plates": 16, "continent_chance": 60, "island_arc_chance": 15,
		"warp_scale": 75.0, "fold_belt_width": 40.0, "collision_uplift": 38.0,
		"subduction_arc": 24.0, "subduction_trench": 22.0, "rift_graben": 22.0,
		"rift_shoulder": 12.0, "continental_base_elevation": 26.0,
		"oceanic_base_elevation": -36.0, "coastal_boundary_elevation": 2.0,
		"shelf_width": 36.0, "shelf_depth": -8.0, "detail_noise_scale": 14.0,
		"noise_octaves": 6
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
		if terrain_mesh_instance.material_override:
			terrain_mesh_instance.material_override.cull_mode = BaseMaterial3D.CULL_DISABLED if t else BaseMaterial3D.CULL_BACK
	)
	vbox.add_child(toggle_wireframe_chk)
	
	# 11. Switch Scene
	vbox.add_child(_create_section_header("SCENE NAVIGATION"))
	
	var scene_2d_btn = Button.new()
	scene_2d_btn.text = "Switch to 2D Map Gen Visualizer"
	scene_2d_btn.pressed.connect(func(): get_tree().change_scene_to_file("res://map_gen_visualizer.tscn"))
	vbox.add_child(scene_2d_btn)
	
	var main_scene_btn = Button.new()
	main_scene_btn.text = "Return to Main Map Visualizer"
	main_scene_btn.pressed.connect(func(): get_tree().change_scene_to_file("res://map_visualizer.tscn"))
	vbox.add_child(main_scene_btn)
	
	# 12. Controls Reference
	vbox.add_child(_create_section_header("KEYBOARD & MOUSE"))
	var help_lbl = Label.new()
	help_lbl.text = "[Space] Apply Knobs & Regenerate\n[Ctrl+S] Save Debug Config\n[Ctrl+L] Load Debug Config\n[R] Random Seed\n[RMB Drag] Orbit Camera\n[MMB Drag] Pan Camera\n[Mouse Wheel] Zoom In/Out\n[WASD / QE] Fly Camera\n[LMB] Probe Terrain Info"
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
		status_lbl.text = "Seed: %d | Size: %dx%d | Plates: %d | RMB=Orbit, MMB=Pan, Wheel=Zoom, LMB=Probe, Space=Apply, Ctrl+S=Save, Ctrl+L=Load" % [
			current_seed, map_width, map_height, int(num_plates_spin.value if num_plates_spin else 16)
		]
