extends Node3D

const BotanicalTreeMaterials = preload("res://scripts/botanical_tree_materials.gd")

# ==============================================================================
# ExoFarm: Dynamic Tree Generator Interactive Test Scene
# ==============================================================================
# Interactive inspection & validation tool for dynamic procedural trees:
# - Continuous scrubbable non-uniform growth (0.0 sprout to 1.0 mature tree)
# - SpeedTree-like placeholder foliage cards with volumetric spherical normals
# - Dynamic canopy light field & emergent self-pruning (cladoptosis)
# - Integrated ground, self, and obstacle collision checks
# - Species profile switcher (Conifer, Broadleaf Oak, Slender Birch, Dead Snag)
# - Camera orbit, pan, and zoom controls
# ==============================================================================

# Time & Playback State
var current_growth: float = 1.0
var is_playing: bool = false
var playback_speed: float = 0.25

# Tree Configuration
var current_seed: int = 1337
var current_tree_type: int = BotanicalTreeMaterials.Species.CONIFER
var enable_self_pruning: bool = true
var cladoptosis_threshold: float = 0.22
var lateral_wander_bias: float = 0.50
var vertical_wander_bias: float = 0.50

# Environment Parameters
var sun_azimuth_deg: float = 45.0
var sun_elevation_deg: float = 50.0
var terrain_slope_deg: float = 0.0
var terrain_slope_heading_deg: float = 0.0

# Visual Settings
var show_bark: bool = true
var show_cards: bool = true
var use_optimized_foliage_shader: bool = true

# Scene Nodes
var camera_rig: Node3D
var camera_3d: Camera3D
var cpp_bridge = null
var sun_light: DirectionalLight3D
var sun_gizmo_node: Node3D
var terrain_slab: MeshInstance3D
var tree_root_node: Node3D
var bark_mesh_instance: MeshInstance3D
var foliage_mesh_instance: MeshInstance3D

# Materials
var bark_material: StandardMaterial3D
var foliage_material: Material
var ground_material: StandardMaterial3D

# Grove Stand Settings
var is_grove_mode: bool = false
var grove_spacing: float = 4.5
var grove_grid_dim: int = 3
var grove_instances: Array = []
var grove_btn: Button

# Camera Controls
var cam_target: Vector3 = Vector3(0.0, 5.0, 0.0)
var cam_distance: float = 18.0
var cam_rot_x: float = -12.0
var cam_rot_y: float = 35.0
var is_dragging_orbit: bool = false
var is_dragging_pan: bool = false
var last_mouse_pos: Vector2 = Vector2.ZERO

# UI Controls
var ui_root: Control
var growth_slider: HSlider
var growth_lbl: Label
var play_btn: Button
var speed_btn: Button
var hud_lbl: Label
var species_opt: OptionButton

func _ready() -> void:
	if ClassDB.class_exists("BotanicalTreeBridge"):
		cpp_bridge = ClassDB.instantiate("BotanicalTreeBridge")
		print("TreeGrowthVisualizer: C++ BotanicalTreeBridge is active and ready.")
	_setup_environment()
	_setup_camera()
	_setup_materials()
	_setup_scene_nodes()
	_setup_ui()
	_update_sun_transform()
	_update_terrain_slope()
	_rebuild_tree()

func _setup_environment() -> void:
	sun_light = DirectionalLight3D.new()
	sun_light.light_color = Color(1.0, 0.98, 0.92)
	sun_light.light_energy = 1.4
	sun_light.shadow_enabled = true
	sun_light.shadow_blur = 1.0
	sun_light.directional_shadow_mode = DirectionalLight3D.SHADOW_PARALLEL_4_SPLITS
	sun_light.directional_shadow_max_distance = 60.0
	add_child(sun_light)
	
	var sky_mat = ProceduralSkyMaterial.new()
	sky_mat.sky_top_color = Color(0.24, 0.48, 0.82)
	sky_mat.sky_horizon_color = Color(0.70, 0.80, 0.90)
	sky_mat.ground_bottom_color = Color(0.14, 0.16, 0.20)
	sky_mat.ground_horizon_color = Color(0.55, 0.65, 0.72)
	sky_mat.sun_angle_max = 25.0
	
	var sky = Sky.new()
	sky.sky_material = sky_mat
	
	var env = Environment.new()
	env.background_mode = Environment.BG_SKY
	env.sky = sky
	env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	env.ambient_light_energy = 0.55
	env.reflected_light_source = Environment.REFLECTION_SOURCE_SKY
	env.tonemap_mode = Environment.TONE_MAPPER_ACES
	env.tonemap_exposure = 1.05
	
	var env_node = WorldEnvironment.new()
	env_node.environment = env
	add_child(env_node)

func _setup_camera() -> void:
	camera_rig = Node3D.new()
	add_child(camera_rig)
	
	camera_3d = Camera3D.new()
	camera_3d.fov = 52.0
	camera_3d.near = 0.15
	camera_3d.far = 200.0
	camera_rig.add_child(camera_3d)
	_update_camera_transform()

func _setup_materials() -> void:
	bark_material = StandardMaterial3D.new()
	bark_material.albedo_color = Color(0.38, 0.26, 0.18)
	bark_material.roughness = 0.88
	bark_material.metallic_specular = 0.15
	bark_material.cull_mode = BaseMaterial3D.CULL_BACK
	bark_material.depth_draw_mode = BaseMaterial3D.DEPTH_DRAW_ALWAYS
	
	foliage_material = _get_current_foliage_material()
	
	ground_material = StandardMaterial3D.new()
	ground_material.albedo_color = Color(0.24, 0.28, 0.22)
	ground_material.roughness = 0.94

func _setup_scene_nodes() -> void:
	terrain_slab = MeshInstance3D.new()
	var box_mesh = BoxMesh.new()
	box_mesh.size = Vector3(32.0, 1.0, 32.0)
	terrain_slab.mesh = box_mesh
	terrain_slab.material_override = ground_material
	terrain_slab.position = Vector3(0.0, -0.5, 0.0)
	add_child(terrain_slab)
	
	tree_root_node = Node3D.new()
	add_child(tree_root_node)
	
	bark_mesh_instance = MeshInstance3D.new()
	bark_mesh_instance.material_override = bark_material
	tree_root_node.add_child(bark_mesh_instance)
	
	foliage_mesh_instance = MeshInstance3D.new()
	foliage_mesh_instance.material_override = foliage_material
	tree_root_node.add_child(foliage_mesh_instance)

	# Initialize 3x3 Grove Stand mesh instances
	for r in range(grove_grid_dim):
		for c in range(grove_grid_dim):
			var b_inst = MeshInstance3D.new()
			b_inst.material_override = bark_material
			b_inst.visible = false
			tree_root_node.add_child(b_inst)
			
			var f_inst = MeshInstance3D.new()
			f_inst.material_override = foliage_material
			f_inst.visible = false
			tree_root_node.add_child(f_inst)
			
			grove_instances.append({ "b": b_inst, "f": f_inst, "r": r, "c": c })
	
	sun_gizmo_node = Node3D.new()
	add_child(sun_gizmo_node)
	var sun_arrow_mesh = CylinderMesh.new()
	sun_arrow_mesh.top_radius = 0.12
	sun_arrow_mesh.bottom_radius = 0.0
	sun_arrow_mesh.height = 1.6
	var arrow_inst = MeshInstance3D.new()
	arrow_inst.mesh = sun_arrow_mesh
	var sun_arrow_mat = StandardMaterial3D.new()
	sun_arrow_mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	sun_arrow_mat.albedo_color = Color(1.0, 0.85, 0.2)
	arrow_inst.material_override = sun_arrow_mat
	arrow_inst.rotation_degrees = Vector3(90, 0, 0)
	arrow_inst.position = Vector3(0, 0, -1.0)
	sun_gizmo_node.add_child(arrow_inst)
	sun_gizmo_node.position = Vector3(0.0, 16.0, 0.0)

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

func _get_sun_vector() -> Vector3:
	var az = deg_to_rad(sun_azimuth_deg)
	var el = deg_to_rad(sun_elevation_deg)
	return Vector3(sin(az) * cos(el), sin(el), cos(az) * cos(el)).normalized()

func _update_sun_transform() -> void:
	sun_light.rotation_degrees = Vector3(-sun_elevation_deg, sun_azimuth_deg, 0.0)
	if sun_gizmo_node:
		sun_gizmo_node.rotation_degrees = Vector3(-sun_elevation_deg, sun_azimuth_deg, 0.0)

func _update_terrain_slope() -> void:
	var slope_rad = deg_to_rad(terrain_slope_deg)
	var heading_rad = deg_to_rad(terrain_slope_heading_deg)
	var tilt_axis = Vector3(cos(heading_rad), 0.0, sin(heading_rad)).cross(Vector3.UP).normalized()
	terrain_slab.transform = Transform3D(Basis(tilt_axis, slope_rad), Vector3(0.0, -0.5, 0.0))

func _get_ground_normal() -> Vector3:
	return terrain_slab.transform.basis.y.normalized()

func _rebuild_tree() -> void:
	if not cpp_bridge:
		return
	var g_norm = _get_ground_normal()
	var s_vec = _get_sun_vector()

	if not is_grove_mode:
		for gi in grove_instances:
			gi.b.visible = false
			gi.f.visible = false

		var t0 = Time.get_ticks_usec()
		var params: Dictionary = {
			"ground_pos": Vector3.ZERO,
			"ground_normal": g_norm,
			"sun_dir": s_vec,
			"enable_self_pruning": enable_self_pruning,
			"cladoptosis_threshold": cladoptosis_threshold,
			"lateral_bias": lateral_wander_bias,
			"vertical_bias": vertical_wander_bias
		}
		var res_dict = cpp_bridge.generate_tree(current_seed, current_tree_type, current_growth, params)
		var elapsed_ms = (Time.get_ticks_usec() - t0) / 1000.0

		bark_mesh_instance.visible = show_bark
		bark_mesh_instance.mesh = res_dict.get("bark_mesh")

		foliage_material = _get_current_foliage_material()
		foliage_mesh_instance.material_override = foliage_material
		foliage_mesh_instance.visible = show_cards and (res_dict.get("foliage_mesh") != null)
		foliage_mesh_instance.mesh = res_dict.get("foliage_mesh")

		_update_telemetry_dict(res_dict.get("stats", {}), elapsed_ms)
	else:
		bark_mesh_instance.visible = false
		foliage_mesh_instance.visible = false

		var t0 = Time.get_ticks_usec()
		foliage_material = _get_current_foliage_material()

		var positions = PackedVector3Array()
		var growths = PackedFloat32Array()
		var stand_seeds = []
		for gi in grove_instances:
			var ox = (float(gi.c) - float(grove_grid_dim - 1) * 0.5) * grove_spacing
			var oz = (float(gi.r) - float(grove_grid_dim - 1) * 0.5) * grove_spacing
			positions.append(Vector3(ox, 0.0, oz))
			growths.append(current_growth)
			stand_seeds.append(current_seed + gi.r * 100 + gi.c * 10)

		var params = {
			"ground_pos": Vector3.ZERO,
			"ground_normal": g_norm,
			"sun_dir": s_vec,
			"enable_self_pruning": enable_self_pruning,
			"cladoptosis_threshold": cladoptosis_threshold,
			"lateral_bias": lateral_wander_bias,
			"vertical_bias": vertical_wander_bias,
			"seeds": stand_seeds
		}
		var stand_res = cpp_bridge.generate_stand(current_seed, current_tree_type, positions, growths, params)
		var trees: Array = stand_res.get("trees", [])
		var elapsed_ms = (Time.get_ticks_usec() - t0) / 1000.0

		var total_cards = 0
		var total_limbs = 0
		var total_pruned = 0
		var total_tris = 0
		var total_card_tris = 0

		for idx in range(min(grove_instances.size(), trees.size())):
			var gi = grove_instances[idx]
			var t_dict: Dictionary = trees[idx]
			var stats: Dictionary = t_dict.get("stats", {})

			gi.b.visible = show_bark
			gi.b.mesh = t_dict.get("bark_mesh")
			gi.f.visible = show_cards and (t_dict.get("foliage_mesh") != null)
			gi.f.material_override = foliage_material
			gi.f.mesh = t_dict.get("foliage_mesh")

			total_cards += stats.get("active_cards_count", 0)
			total_limbs += stats.get("living_branches", 0)
			total_pruned += stats.get("pruned_branches", 0)
			total_tris += stats.get("triangle_count", 0)
			total_card_tris += stats.get("foliage_triangle_count", 0)

		if hud_lbl:
			var species_name = BotanicalTreeMaterials.get_species_name(current_tree_type)
			hud_lbl.text = "=== Forest Grove Stand (3x3 Array) [C++ GDExtension] ===
Species: %s | Spacing: %.1f m
Growth: %.2f / 1.00 (Stand Total: 9 Trees)
Total Active Cards: %d | Total Living Limbs: %d
Total Pruned Limbs: %d
Bark Tris: %d | Card Tris: %d
Grove Generation Time: %.2f ms (Avg: %.2f ms/tree)" % [
				species_name, grove_spacing, current_growth, total_cards, total_limbs, total_pruned, total_tris, total_card_tris, elapsed_ms, elapsed_ms / 9.0
			]
		if growth_lbl:
			growth_lbl.text = "Growth: %.2f" % current_growth

func _update_telemetry_dict(stats: Dictionary, elapsed_ms: float) -> void:
	if not hud_lbl:
		return

	var species_name = BotanicalTreeMaterials.get_species_name(current_tree_type)
	var base_r: float = stats.get("base_radius", 0.0)
	var flare_cm: float = base_r * 1.5 * 2.0 * 100.0

	hud_lbl.text = "=== Botanical Tree Engine [C++ 16.16 Fixed] ===
Species: %s
Growth: %.2f / 1.00
Height: %.2f m | Crown Spread: %.2f m
Trunk Caliper: %.1f cm (Base Flare: %.1f cm)
Active Cards: %d | Pruned Cards: %d
Living Branches: %d | Pruned Branches: %d
Collisions Avoided: %d
Bark Mesh Tris: %d | Card Tris: %d
Avg Canopy Light Exposure: %.1f%%
Deterministic Hash: 0x%X
Generation Time: %.2f ms" % [
		species_name,
		current_growth,
		stats.get("height", 0.0),
		stats.get("crown_spread", 0.0),
		base_r * 2.0 * 100.0,
		flare_cm,
		stats.get("active_cards_count", 0),
		stats.get("pruned_cards_count", 0),
		stats.get("living_branches", 0),
		stats.get("pruned_branches", 0),
		stats.get("collisions_avoided", 0),
		stats.get("triangle_count", 0),
		stats.get("foliage_triangle_count", 0),
		stats.get("avg_light_exposure", 1.0) * 100.0,
		stats.get("state_hash", 0),
		elapsed_ms
	]

	if growth_lbl:
		growth_lbl.text = "Growth: %.2f" % current_growth

# ------------------------------------------------------------------------------
# Process & Playback Loop
# ------------------------------------------------------------------------------
func _process(delta: float) -> void:
	_handle_camera_input(delta)
	
	if is_playing:
		current_growth += delta * playback_speed
		if current_growth > 1.0:
			current_growth = 0.0
		if growth_slider:
			growth_slider.set_value_no_signal(current_growth)
		_rebuild_tree()

func _handle_camera_input(delta: float) -> void:
	if Input.is_key_pressed(KEY_CTRL):
		return
		
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
			cam_distance = max(2.0, cam_distance * 0.9)
			_update_camera_transform()
		elif event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			cam_distance = min(50.0, cam_distance * 1.1)
			_update_camera_transform()
	elif event is InputEventMouseMotion:
		var delta_pos = event.position - last_mouse_pos
		last_mouse_pos = event.position
		
		if is_dragging_orbit:
			cam_rot_y -= delta_pos.x * 0.35
			cam_rot_x = clamp(cam_rot_x - delta_pos.y * 0.35, -89.0, 89.0)
			_update_camera_transform()
		elif is_dragging_pan:
			var right = camera_3d.global_transform.basis.x
			var up = camera_3d.global_transform.basis.y
			cam_target -= (right * delta_pos.x - up * delta_pos.y) * (cam_distance * 0.002)
			_update_camera_transform()

# ------------------------------------------------------------------------------
# UI Construction (Inspector Panel & Playback Bar)
# ------------------------------------------------------------------------------
func _setup_ui() -> void:
	ui_root = Control.new()
	ui_root.set_anchors_preset(Control.PRESET_FULL_RECT)
	ui_root.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(ui_root)
	
	# 1. Telemetry HUD (Top-Left)
	var hud_panel = PanelContainer.new()
	hud_panel.position = Vector2(16, 16)
	var hud_style = StyleBoxFlat.new()
	hud_style.bg_color = Color(0.08, 0.10, 0.14, 0.90)
	hud_style.set_corner_radius_all(6)
	hud_style.content_margin_left = 12
	hud_style.content_margin_top = 10
	hud_style.content_margin_right = 12
	hud_style.content_margin_bottom = 10
	hud_panel.add_theme_stylebox_override("panel", hud_style)
	
	hud_lbl = Label.new()
	hud_lbl.add_theme_color_override("font_color", Color(0.92, 0.95, 0.98))
	hud_lbl.add_theme_font_size_override("font_size", 12)
	hud_panel.add_child(hud_lbl)
	ui_root.add_child(hud_panel)
	
	# 2. Bottom Playback Controls
	var bottom_panel = PanelContainer.new()
	bottom_panel.set_anchors_preset(Control.PRESET_BOTTOM_WIDE)
	bottom_panel.offset_top = -80
	bottom_panel.offset_left = 20
	bottom_panel.offset_right = -380
	bottom_panel.offset_bottom = -16
	
	var b_style = StyleBoxFlat.new()
	b_style.bg_color = Color(0.10, 0.12, 0.16, 0.92)
	b_style.set_corner_radius_all(8)
	b_style.content_margin_left = 16
	b_style.content_margin_top = 8
	b_style.content_margin_right = 16
	b_style.content_margin_bottom = 8
	bottom_panel.add_theme_stylebox_override("panel", b_style)
	
	var b_vbox = VBoxContainer.new()
	b_vbox.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	
	var s_row = HBoxContainer.new()
	growth_lbl = Label.new()
	growth_lbl.custom_minimum_size = Vector2(120, 0)
	growth_lbl.add_theme_color_override("font_color", Color(0.95, 0.80, 0.35))
	growth_lbl.add_theme_font_size_override("font_size", 13)
	s_row.add_child(growth_lbl)
	
	growth_slider = HSlider.new()
	growth_slider.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	growth_slider.min_value = 0.0
	growth_slider.max_value = 1.0
	growth_slider.step = 0.01
	growth_slider.value = current_growth
	growth_slider.value_changed.connect(func(v):
		current_growth = v
		_rebuild_tree()
	)
	s_row.add_child(growth_slider)
	b_vbox.add_child(s_row)
	
	var btn_row = HBoxContainer.new()
	btn_row.add_theme_constant_override("separation", 10)
	
	play_btn = Button.new()
	play_btn.text = "Play Growth"
	play_btn.custom_minimum_size = Vector2(100, 30)
	play_btn.pressed.connect(func():
		is_playing = not is_playing
		play_btn.text = "Pause" if is_playing else "Play Growth"
	)
	btn_row.add_child(play_btn)
	
	speed_btn = Button.new()
	speed_btn.text = "Speed: 1x"
	speed_btn.custom_minimum_size = Vector2(85, 30)
	speed_btn.pressed.connect(_on_cycle_speed)
	btn_row.add_child(speed_btn)
	
	var sprout_btn = Button.new()
	sprout_btn.text = "Sprout (0.0)"
	sprout_btn.pressed.connect(func():
		current_growth = 0.0
		growth_slider.set_value_no_signal(0.0)
		_rebuild_tree()
	)
	btn_row.add_child(sprout_btn)
	
	var mature_btn = Button.new()
	mature_btn.text = "Mature (1.0)"
	mature_btn.pressed.connect(func():
		current_growth = 1.0
		growth_slider.set_value_no_signal(1.0)
		_rebuild_tree()
	)
	btn_row.add_child(mature_btn)
	
	var rand_seed_btn = Button.new()
	rand_seed_btn.text = "Random Seed"
	rand_seed_btn.pressed.connect(func():
		current_seed = randi() % 999999
		_rebuild_tree()
	)
	btn_row.add_child(rand_seed_btn)
	
	grove_btn = Button.new()
	grove_btn.text = "Grove (3x3): OFF"
	grove_btn.custom_minimum_size = Vector2(120, 30)
	grove_btn.pressed.connect(func():
		is_grove_mode = not is_grove_mode
		grove_btn.text = "Grove (3x3): ON" if is_grove_mode else "Grove (3x3): OFF"
		if is_grove_mode:
			cam_distance = 28.0
			cam_target = Vector3(0.0, 6.0, 0.0)
		else:
			cam_distance = 18.0
			cam_target = Vector3(0.0, 5.0, 0.0)
		_update_camera_transform()
		_rebuild_tree()
	)
	btn_row.add_child(grove_btn)
	
	b_vbox.add_child(btn_row)
	bottom_panel.add_child(b_vbox)
	ui_root.add_child(bottom_panel)
	
	# 3. Right Sidebar Inspector
	var right_panel = PanelContainer.new()
	right_panel.set_anchors_preset(Control.PRESET_RIGHT_WIDE)
	right_panel.offset_left = -340
	right_panel.offset_top = 16
	right_panel.offset_right = -16
	right_panel.offset_bottom = -16
	
	var r_style = StyleBoxFlat.new()
	r_style.bg_color = Color(0.10, 0.12, 0.16, 0.92)
	r_style.set_corner_radius_all(8)
	r_style.content_margin_left = 14
	r_style.content_margin_top = 12
	r_style.content_margin_right = 14
	r_style.content_margin_bottom = 12
	right_panel.add_theme_stylebox_override("panel", r_style)
	
	var scroll = ScrollContainer.new()
	scroll.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	right_panel.add_child(scroll)
	
	var vbox = VBoxContainer.new()
	vbox.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	vbox.add_theme_constant_override("separation", 8)
	scroll.add_child(vbox)
	
	var title = Label.new()
	title.text = "Dynamic Tree Settings"
	title.add_theme_color_override("font_color", Color(0.95, 0.82, 0.40))
	title.add_theme_font_size_override("font_size", 14)
	vbox.add_child(title)
	
	# --- SPECIES SELECTION ---
	vbox.add_child(_create_section_header("SPECIES PRESET"))
	species_opt = OptionButton.new()
	species_opt.add_item("Conifer (Pine / Spruce)", BotanicalTreeMaterials.Species.CONIFER)
	species_opt.add_item("Broadleaf Oak", BotanicalTreeMaterials.Species.BROADLEAF_OAK)
	species_opt.add_item("Slender Birch", BotanicalTreeMaterials.Species.SLENDER_BIRCH)
	species_opt.add_item("Dead Snag", BotanicalTreeMaterials.Species.DEAD_SNAG)
	species_opt.selected = current_tree_type
	species_opt.item_selected.connect(func(idx):
		current_tree_type = species_opt.get_item_id(idx)
		_rebuild_tree()
	)
	vbox.add_child(species_opt)
	
	# --- ENVIRONMENT SECTION ---
	vbox.add_child(_create_section_header("ENVIRONMENT & LIGHTING"))
	_create_slider_row(vbox, "Sun Azimuth", 0.0, 360.0, 1.0, sun_azimuth_deg, func(v):
		sun_azimuth_deg = v
		_update_sun_transform()
		_rebuild_tree()
	)
	_create_slider_row(vbox, "Sun Elevation", 10.0, 85.0, 1.0, sun_elevation_deg, func(v):
		sun_elevation_deg = v
		_update_sun_transform()
		_rebuild_tree()
	)
	_create_slider_row(vbox, "Terrain Slope", 0.0, 50.0, 1.0, terrain_slope_deg, func(v):
		terrain_slope_deg = v
		_update_terrain_slope()
		_rebuild_tree()
	)
	
	# --- BIOLOGICAL SELF-PRUNING ---
	vbox.add_child(_create_section_header("CANOPY LIGHT & CLADOPTOSIS"))
	_create_check_row(vbox, "Enable Self-Pruning", enable_self_pruning, func(v):
		enable_self_pruning = v
		_rebuild_tree()
	)
	_create_slider_row(vbox, "Pruning Threshold", 0.05, 0.45, 0.01, cladoptosis_threshold, func(v):
		cladoptosis_threshold = v
		_rebuild_tree()
	)
	
	# --- SILHOUETTE & WANDER PHOTOTROPISM ---
	vbox.add_child(_create_section_header("SILHOUETTE & WANDER BIAS"))
	_create_slider_row(vbox, "Lateral Bias", 0.0, 1.0, 0.05, lateral_wander_bias, func(v):
		lateral_wander_bias = v
		_rebuild_tree()
	)
	_create_slider_row(vbox, "Vertical Bias", 0.0, 1.0, 0.05, vertical_wander_bias, func(v):
		vertical_wander_bias = v
		_rebuild_tree()
	)
	
	# --- VISUAL DISPLAY SECTION ---
	vbox.add_child(_create_section_header("RENDER & CARDS"))
	_create_check_row(vbox, "Show 3D Bark Mesh", show_bark, func(v):
		show_bark = v
		bark_mesh_instance.visible = v
	)
	_create_check_row(vbox, "Show Foliage Cards", show_cards, func(v):
		show_cards = v
		foliage_mesh_instance.visible = v
	)
	_create_check_row(vbox, "Optimized Foliage Shader", use_optimized_foliage_shader, func(v):
		use_optimized_foliage_shader = v
		_rebuild_tree()
	)
	
	ui_root.add_child(right_panel)

# ------------------------------------------------------------------------------
# UI Helper Generators
# ------------------------------------------------------------------------------
func _create_section_header(title_text: String) -> Control:
	var c = VBoxContainer.new()
	c.add_theme_constant_override("separation", 2)
	c.add_child(HSeparator.new())
	var lbl = Label.new()
	lbl.text = title_text
	lbl.add_theme_color_override("font_color", Color(0.95, 0.75, 0.35))
	lbl.add_theme_font_size_override("font_size", 11)
	c.add_child(lbl)
	return c

func _create_slider_row(parent: VBoxContainer, label_text: String, min_v: float, max_v: float, step_v: float, def_v: float, callback: Callable) -> HSlider:
	var v_box = VBoxContainer.new()
	v_box.add_theme_constant_override("separation", 1)
	
	var h_row = HBoxContainer.new()
	var lbl = Label.new()
	lbl.text = label_text
	lbl.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	lbl.add_theme_color_override("font_color", Color(0.85, 0.88, 0.92))
	lbl.add_theme_font_size_override("font_size", 11)
	h_row.add_child(lbl)
	
	var val_lbl = Label.new()
	val_lbl.text = "%.2f" % def_v
	val_lbl.add_theme_color_override("font_color", Color(0.70, 0.75, 0.80))
	val_lbl.add_theme_font_size_override("font_size", 11)
	h_row.add_child(val_lbl)
	v_box.add_child(h_row)
	
	var slider = HSlider.new()
	slider.min_value = min_v
	slider.max_value = max_v
	slider.step = step_v
	slider.value = def_v
	slider.value_changed.connect(func(v):
		val_lbl.text = "%.2f" % v
		callback.call(v)
	)
	v_box.add_child(slider)
	
	parent.add_child(v_box)
	return slider

func _create_check_row(parent: VBoxContainer, label_text: String, def_v: bool, callback: Callable) -> CheckBox:
	var chk = CheckBox.new()
	chk.text = label_text
	chk.button_pressed = def_v
	chk.add_theme_color_override("font_color", Color(0.85, 0.88, 0.92))
	chk.add_theme_font_size_override("font_size", 11)
	chk.toggled.connect(callback)
	parent.add_child(chk)
	return chk

func _on_cycle_speed() -> void:
	if playback_speed <= 0.25:
		playback_speed = 0.5
	elif playback_speed <= 0.5:
		playback_speed = 1.0
	elif playback_speed <= 1.0:
		playback_speed = 2.0
	else:
		playback_speed = 0.25
	speed_btn.text = "Speed: %.1fx" % playback_speed

func _get_current_foliage_material() -> Material:
	if use_optimized_foliage_shader:
		return BotanicalTreeMaterials.get_foliage_shader_material(current_tree_type)
	return BotanicalTreeMaterials.get_default_foliage_material(current_tree_type)
