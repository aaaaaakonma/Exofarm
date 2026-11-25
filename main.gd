extends Node3D

# ----------------------------------------------------------------------
# ExoFarm: Main Scene & Deterministic 3D Traversal Controller
# ----------------------------------------------------------------------
# - 16.16 Fixed-Point Deterministic Physics Authority via SimBridge
# - Continuous 3D Tectonic Heightmap & Boundary-Aware Wave Surface
# - Smooth Camera-Relative WASD Traversal with Tangent Sliding
# - Interpolated Avatar Visual with Ground Shadow & HUD Telemetry
# ----------------------------------------------------------------------

const CONFIG_PATH: String = "res://world_3d_config.json"
const BotanicalTreeMaterials = preload("res://scripts/botanical_tree_materials.gd")

@onready var bridge: SimBridge = SimBridge.new()
var wave_baker: WaveCageBaker = WaveCageBaker.new()
var wave_driver: WaveComputeDriver = WaveComputeDriver.new()
var tree_bridge = null

# World Parameters (1:1 Unified 3D Simulation Grid)
var current_seed: int = 1337
var map_width: int = 512
var map_height: int = 512
var cell_size: float = 2.0 # 3D units per cell (meters)
var elevation_scale: float = 0.4
var sea_level: float = 0.0
var loaded_config: Dictionary = {}

# Droplet Erosion & River Network State (1:1 Synchronized with visualizer config)
var apply_droplet_erosion: bool = true
var droplet_count: int = 30000
var droplet_erosion_rate: float = 0.3
var droplet_deposition_rate: float = 0.3
var river_min_area: int = 110
var river_width_multiplier: float = 1.0
var river_carve_depth: float = 2.0
var show_rivers: bool = true
var river_lbl: Label

# 3D Node References
var terrain_mesh_instance: MeshInstance3D
var water_mesh_instance: MeshInstance3D
var river_mesh_instance: MeshInstance3D
var tree_chunks_node: Node3D
var sun_light: DirectionalLight3D
var env_node: WorldEnvironment

# Chunked Tree & Procedural Vegetation State
var loaded_tree_chunks: Dictionary = {} # Vector2i -> Node3D
var tree_bark_material: StandardMaterial3D
var current_chunk_coords: Vector2i = Vector2i(-9999, -9999)
const TREE_CHUNK_RADIUS: int = 3 # 7x7 active chunk window around player

# Player Avatar & Visuals
var player_id: int = 0
var player_visual: Node3D
var player_model: MeshInstance3D
var player_visor: MeshInstance3D
var player_shadow: MeshInstance3D
var starter_spawn: Vector2i = Vector2i.ZERO

# Player Simulation & Interpolation State
var sim_target_x: float = 0.0
var sim_target_z: float = 0.0
var sim_vel_x: float = 0.0
var sim_vel_z: float = 0.0
var visual_pos: Vector3 = Vector3.ZERO
var player_speed_kmh: float = 0.0

# Valheim-Style 3rd Person Camera Rig
var camera_rig: Node3D
var camera_3d: Camera3D
var cam_target: Vector3 = Vector3.ZERO
var target_cam_distance: float = 5.5
var current_cam_distance: float = 5.5
var min_cam_distance: float = 1.6
var max_cam_distance: float = 16.0

var cam_rot_x: float = -16.0 # Pitch angle in degrees (-65° looking down to +32° looking up)
var cam_rot_y: float = 0.0   # Yaw angle in degrees
var min_cam_pitch: float = -65.0
var max_cam_pitch: float = 32.0

var mouse_sensitivity: float = 0.16
var is_rmb_down: bool = false
var camera_head_offset: float = 1.45 # Eye/upper torso pivot height above ground
var camera_collision_clearance: float = 0.35

# Cached Data & Textures
var elevation_data: PackedInt32Array
var biome_data: PackedByteArray
var surface_data: PackedByteArray
var hm_tex: ImageTexture
var biome_tex: ImageTexture
var water_depth_tex: ImageTexture
var terrain_mat: ShaderMaterial
var water_mat: ShaderMaterial

# UI / HUD
var ui_root: CanvasLayer
var status_lbl: Label
var coords_lbl: Label
var biome_lbl: Label
var speed_lbl: Label
var help_panel: PanelContainer
var hud_visible: bool = true

# Terraforming & Terrain Manipulation
var terraform_delta: int = 65536 # 1.0 meter fixed-point (16.16)
var targeted_tile: Vector2i = Vector2i(-1, -1)
var target_marker: MeshInstance3D
var target_marker_mat: StandardMaterial3D
var feedback_lbl: Label
var feedback_timer: float = 0.0
var terraform_panel: PanelContainer
var raise_btn: Button
var lower_btn: Button

const BIOME_NAMES: Array[String] = [
	"Ocean",
	"Tundra",
	"Desert",
	"Grassland",
	"Continental Forest",
	"Tropical Rainforest",
	"Swamp & Wetlands",
	"High Mountain",
	"Coastline Beach"
]

const SURFACE_NAMES: Array[String] = [
	"Dry Soil (100% Speed)",
	"Tilled Soil (100% Speed)",
	"Mud Bank (80% Speed Drag)",
	"Shallow Water (65% Speed Wading)",
	"Deep Water (Barrier)",
	"Rock Cliff (Sheer Wall)",
	"Nitrogen Ore Deposit",
	"Phosphorus Ore Deposit",
	"Potassium Ore Deposit"
]

func _ready() -> void:
	add_child(bridge)
	add_child(wave_baker)
	add_child(wave_driver)
	
	_load_config()
	_setup_environment()
	_setup_camera()
	_setup_world_nodes()
	_setup_player_visual()
	_setup_hud()
	
	# Start with captured mouse for authentic Valheim 3rd person mouse-look
	Input.mouse_mode = Input.MOUSE_MODE_CAPTURED
	
	generate_world(current_seed)

func _load_config() -> void:
	if not FileAccess.file_exists(CONFIG_PATH):
		return
	var file = FileAccess.open(CONFIG_PATH, FileAccess.READ)
	if not file:
		return
	var json = JSON.new()
	if json.parse(file.get_as_text()) == OK and json.data is Dictionary:
		loaded_config = json.data
		if loaded_config.has("seed"): current_seed = int(loaded_config["seed"])
		if loaded_config.has("map_width"): map_width = int(loaded_config["map_width"])
		if loaded_config.has("map_height"): map_height = int(loaded_config["map_height"])
		if loaded_config.has("cell_size"): cell_size = float(loaded_config["cell_size"])
		if loaded_config.has("elevation_scale"): elevation_scale = float(loaded_config["elevation_scale"])
		if loaded_config.has("sea_level_offset"): sea_level = float(loaded_config["sea_level_offset"])
		
		# Synchronize Droplet Erosion & River Network parameters
		if loaded_config.has("apply_droplet_erosion"): apply_droplet_erosion = bool(loaded_config["apply_droplet_erosion"])
		if loaded_config.has("droplet_count"): droplet_count = int(loaded_config["droplet_count"])
		if loaded_config.has("droplet_erosion_rate"): droplet_erosion_rate = float(loaded_config["droplet_erosion_rate"])
		if loaded_config.has("droplet_deposition_rate"): droplet_deposition_rate = float(loaded_config["droplet_deposition_rate"])
		if loaded_config.has("river_min_area"): river_min_area = int(loaded_config["river_min_area"])
		if loaded_config.has("river_width_multiplier"): river_width_multiplier = float(loaded_config["river_width_multiplier"])
		if loaded_config.has("river_carve_depth"): river_carve_depth = float(loaded_config["river_carve_depth"])
		if loaded_config.has("show_rivers"): show_rivers = bool(loaded_config["show_rivers"])
		
		bridge.set_config(loaded_config)

func _setup_environment() -> void:
	sun_light = DirectionalLight3D.new()
	sun_light.rotation_degrees = Vector3(-50, 45, 0)
	sun_light.light_color = Color(1.0, 0.98, 0.93)
	sun_light.light_energy = 1.3
	sun_light.shadow_enabled = true
	sun_light.shadow_blur = 1.0
	sun_light.directional_shadow_mode = DirectionalLight3D.SHADOW_PARALLEL_4_SPLITS
	sun_light.directional_shadow_max_distance = 600.0
	add_child(sun_light)
	
	var sky_mat = ProceduralSkyMaterial.new()
	sky_mat.sky_top_color = Color(0.20, 0.44, 0.78)
	sky_mat.sky_horizon_color = Color(0.68, 0.78, 0.88)
	sky_mat.sky_curve = 0.12
	sky_mat.ground_bottom_color = Color(0.12, 0.15, 0.20)
	sky_mat.ground_horizon_color = Color(0.55, 0.65, 0.75)
	
	var sky = Sky.new()
	sky.sky_material = sky_mat
	
	var env = Environment.new()
	env.background_mode = Environment.BG_SKY
	env.sky = sky
	env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	env.ambient_light_energy = 0.65
	env.reflected_light_source = Environment.REFLECTION_SOURCE_SKY
	env.tonemap_mode = Environment.TONE_MAPPER_ACES
	env.tonemap_exposure = 1.05
	env.glow_enabled = true
	env.glow_intensity = 0.35
	env.glow_bloom = 0.1
	
	env.fog_enabled = true
	env.fog_light_color = Color(0.68, 0.78, 0.88)
	env.fog_light_energy = 0.8
	env.fog_density = 0.001
	env.fog_aerial_perspective = 0.4
	
	env_node = WorldEnvironment.new()
	env_node.environment = env
	add_child(env_node)

func _setup_camera() -> void:
	camera_rig = Node3D.new()
	add_child(camera_rig)
	
	camera_3d = Camera3D.new()
	camera_3d.fov = 70.0 # Valheim standard wide third-person FOV
	camera_3d.near = 0.1 # Prevents near-plane clipping on close zoom
	camera_3d.far = 1500.0
	camera_rig.add_child(camera_3d)
	_update_camera_transform(0.0)

func _setup_world_nodes() -> void:
	terrain_mesh_instance = MeshInstance3D.new()
	add_child(terrain_mesh_instance)
	
	water_mesh_instance = MeshInstance3D.new()
	add_child(water_mesh_instance)

	river_mesh_instance = MeshInstance3D.new()
	add_child(river_mesh_instance)

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

	# Targeted Tile Indicator Mesh
	target_marker = MeshInstance3D.new()
	var box = BoxMesh.new()
	box.size = Vector3(cell_size * 0.95, 0.15, cell_size * 0.95)
	target_marker.mesh = box
	target_marker_mat = StandardMaterial3D.new()
	target_marker_mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	target_marker_mat.albedo_color = Color(0.2, 0.9, 1.0, 0.45)
	target_marker_mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	target_marker.material_override = target_marker_mat
	target_marker.visible = false
	add_child(target_marker)

func _setup_player_visual() -> void:
	player_visual = Node3D.new()
	add_child(player_visual)
	
	# 1. Main Capsule Mesh (Suit Body)
	player_model = MeshInstance3D.new()
	var capsule = CapsuleMesh.new()
	capsule.radius = 0.45
	capsule.height = 1.8
	player_model.mesh = capsule
	player_model.position = Vector3(0, 0.9, 0)
	
	var suit_mat = StandardMaterial3D.new()
	suit_mat.albedo_color = Color(0.92, 0.93, 0.95)
	suit_mat.roughness = 0.4
	suit_mat.metallic = 0.1
	player_model.material_override = suit_mat
	player_visual.add_child(player_model)
	
	# 2. Visor / Helmet Face
	player_visor = MeshInstance3D.new()
	var visor_box = BoxMesh.new()
	visor_box.size = Vector3(0.48, 0.28, 0.22)
	player_visor.mesh = visor_box
	player_visor.position = Vector3(0, 1.35, 0.36)
	
	var visor_mat = StandardMaterial3D.new()
	visor_mat.albedo_color = Color(0.1, 0.2, 0.3)
	visor_mat.emission_enabled = true
	visor_mat.emission = Color(0.15, 0.75, 1.0)
	visor_mat.emission_energy_multiplier = 1.8
	visor_mat.roughness = 0.15
	visor_mat.metallic = 0.85
	player_visor.material_override = visor_mat
	player_visual.add_child(player_visor)
	
	# 3. Ground Shadow Decal / Disc
	player_shadow = MeshInstance3D.new()
	var shadow_mesh = CylinderMesh.new()
	shadow_mesh.top_radius = 0.65
	shadow_mesh.bottom_radius = 0.65
	shadow_mesh.height = 0.04
	player_shadow.mesh = shadow_mesh
	player_shadow.position = Vector3(0, 0.02, 0)
	
	var shadow_mat = StandardMaterial3D.new()
	shadow_mat.albedo_color = Color(0.0, 0.0, 0.0, 0.55)
	shadow_mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	shadow_mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	player_shadow.material_override = shadow_mat
	player_visual.add_child(player_shadow)

func _setup_hud() -> void:
	ui_root = CanvasLayer.new()
	add_child(ui_root)
	
	var telemetry_panel = PanelContainer.new()
	telemetry_panel.position = Vector2(24, 24)
	ui_root.add_child(telemetry_panel)
	
	var style = StyleBoxFlat.new()
	style.bg_color = Color(0.08, 0.10, 0.14, 0.85)
	style.corner_radius_top_left = 8
	style.corner_radius_top_right = 8
	style.corner_radius_bottom_left = 8
	style.corner_radius_bottom_right = 8
	style.content_margin_left = 16.0
	style.content_margin_right = 16.0
	style.content_margin_top = 14.0
	style.content_margin_bottom = 14.0
	telemetry_panel.add_theme_stylebox_override("panel", style)
	
	var vbox = VBoxContainer.new()
	vbox.add_theme_constant_override("separation", 6)
	telemetry_panel.add_child(vbox)
	
	var title = Label.new()
	title.text = "EXOFARM // 3D CONTINENTAL ENGINE"
	title.add_theme_color_override("font_color", Color(0.35, 0.85, 1.0))
	title.add_theme_font_size_override("font_size", 14)
	vbox.add_child(title)
	
	status_lbl = Label.new()
	status_lbl.text = "Simulation: 60Hz Lockstep Fixed-Point"
	status_lbl.add_theme_font_size_override("font_size", 12)
	vbox.add_child(status_lbl)
	
	coords_lbl = Label.new()
	coords_lbl.text = "Position: X: 0.0m | Z: 0.0m | Elev: 0.0m"
	coords_lbl.add_theme_font_size_override("font_size", 12)
	vbox.add_child(coords_lbl)
	
	biome_lbl = Label.new()
	biome_lbl.text = "Biome: Grassland"
	biome_lbl.add_theme_font_size_override("font_size", 12)
	vbox.add_child(biome_lbl)
	
	speed_lbl = Label.new()
	speed_lbl.text = "Surface: Dry Soil (100% Speed)"
	speed_lbl.add_theme_font_size_override("font_size", 12)
	vbox.add_child(speed_lbl)
	
	river_lbl = Label.new()
	river_lbl.text = "River: None"
	river_lbl.add_theme_font_size_override("font_size", 12)
	river_lbl.add_theme_color_override("font_color", Color(0.4, 0.75, 0.95))
	vbox.add_child(river_lbl)
	
	# Valheim-Style Center Reticle / Crosshair Dot
	var crosshair_center = CenterContainer.new()
	crosshair_center.set_anchors_preset(Control.PRESET_FULL_RECT)
	crosshair_center.mouse_filter = Control.MOUSE_FILTER_IGNORE
	ui_root.add_child(crosshair_center)
	
	var reticle = ColorRect.new()
	reticle.custom_minimum_size = Vector2(4, 4)
	reticle.color = Color(1.0, 1.0, 1.0, 0.75)
	reticle.mouse_filter = Control.MOUSE_FILTER_IGNORE
	crosshair_center.add_child(reticle)
	
	# Controls overlay on bottom right
	help_panel = PanelContainer.new()
	help_panel.set_anchors_preset(Control.PRESET_BOTTOM_RIGHT)
	help_panel.grow_horizontal = Control.GROW_DIRECTION_BEGIN
	help_panel.grow_vertical = Control.GROW_DIRECTION_BEGIN
	help_panel.offset_left = -340
	help_panel.offset_top = -210
	help_panel.offset_right = -24
	help_panel.offset_bottom = -24
	ui_root.add_child(help_panel)
	help_panel.add_theme_stylebox_override("panel", style)
	
	var help_vbox = VBoxContainer.new()
	help_panel.add_child(help_vbox)
	
	var help_title = Label.new()
	help_title.text = "VALHEIM 3RD PERSON CONTROLS"
	help_title.add_theme_color_override("font_color", Color(1.0, 0.8, 0.3))
	help_title.add_theme_font_size_override("font_size", 13)
	help_vbox.add_child(help_title)
	
	var help_text = Label.new()
	help_text.text = "[Mouse] Look / Orbit Camera\n[W/A/S/D] Camera-Relative Traversal\n[T / G] Raise / Lower Terrain (+/-1m)\n[Mouse Wheel] Zoom Distance (1.6m - 16m)\n[Esc / Tab] Toggle Cursor Lock\n[Q / E] Quick Rotate Yaw\n[F] Align Camera Behind Character\n[R] Regenerate Seed | [H] Toggle HUD"
	help_text.add_theme_font_size_override("font_size", 11)
	help_vbox.add_child(help_text)

	# Terraforming & Action Feedback Banner
	feedback_lbl = Label.new()
	feedback_lbl.text = ""
	feedback_lbl.set_anchors_preset(Control.PRESET_CENTER_TOP)
	feedback_lbl.offset_top = 28.0
	feedback_lbl.grow_horizontal = Control.GROW_DIRECTION_BOTH
	feedback_lbl.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	feedback_lbl.add_theme_font_size_override("font_size", 16)
	feedback_lbl.add_theme_color_override("font_color", Color(1.0, 1.0, 1.0))
	ui_root.add_child(feedback_lbl)

	# Terraforming Controls Panel
	terraform_panel = PanelContainer.new()
	terraform_panel.set_anchors_preset(Control.PRESET_BOTTOM_LEFT)
	terraform_panel.offset_left = 24.0
	terraform_panel.offset_bottom = -24.0
	terraform_panel.offset_top = -110.0
	terraform_panel.offset_right = 300.0
	terraform_panel.add_theme_stylebox_override("panel", style)
	ui_root.add_child(terraform_panel)

	var t_vbox = VBoxContainer.new()
	t_vbox.add_theme_constant_override("separation", 6)
	terraform_panel.add_child(t_vbox)

	var t_title = Label.new()
	t_title.text = "TERRAFORMING CONTROLS"
	t_title.add_theme_color_override("font_color", Color(0.4, 0.9, 0.6))
	t_title.add_theme_font_size_override("font_size", 12)
	t_vbox.add_child(t_title)

	var btn_hbox = HBoxContainer.new()
	btn_hbox.add_theme_constant_override("separation", 10)
	t_vbox.add_child(btn_hbox)

	raise_btn = Button.new()
	raise_btn.text = "Raise [T] (+1m)"
	raise_btn.focus_mode = Control.FOCUS_NONE
	raise_btn.pressed.connect(func(): modify_targeted_terrain(terraform_delta))
	btn_hbox.add_child(raise_btn)

	lower_btn = Button.new()
	lower_btn.text = "Lower [G] (-1m)"
	lower_btn.focus_mode = Control.FOCUS_NONE
	lower_btn.pressed.connect(func(): modify_targeted_terrain(-terraform_delta))
	btn_hbox.add_child(lower_btn)

func generate_world(seed_val: int) -> void:
	current_seed = seed_val
	bridge.set_seed(current_seed)
	
	var cfg = loaded_config.duplicate()
	cfg["seed"] = current_seed
	cfg["apply_droplet_erosion"] = apply_droplet_erosion
	cfg["droplet_count"] = droplet_count
	cfg["droplet_erosion_rate"] = droplet_erosion_rate
	cfg["droplet_deposition_rate"] = droplet_deposition_rate
	cfg["river_min_area"] = river_min_area
	cfg["river_width_multiplier"] = river_width_multiplier
	cfg["river_carve_depth"] = river_carve_depth
	cfg["show_rivers"] = show_rivers
	bridge.set_config(cfg)
	
	var cell_size_fixed = int(cell_size * 65536.0)
	bridge.generate_player_map(seed_val, map_width, map_height, cell_size_fixed)
	
	elevation_data = bridge.get_elevation()
	biome_data = bridge.get_biome()
	surface_data = bridge.get_surface_types()
	starter_spawn = bridge.get_starter_spawn()
	
	_build_terrain_mesh()
	_build_water_surface()
	_build_river_ribbons()
	_spawn_player()
	
	_clear_tree_chunks()
	_update_active_tree_chunks(true)
	
	status_lbl.text = "Seed: %d | Sim Tick: %d | Trees: %d" % [current_seed, bridge.current_tick(), bridge.get_tree_count()]

func _ensure_world_textures() -> void:
	if elevation_data.is_empty():
		return
	var num_pixels = map_width * map_height

	# Query 4-channel wave cage & estuary data from bridge:
	# R: Elevation (m), G: Boundary/Estuary Mask [0,1], B: Plume Vx (m/s), A: Plume Vy (m/s)
	var cage_floats = bridge.get_wave_cage_data(sea_level if "sea_level" in self else 0.0)
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
	var chunk_size = 128
	var chunks_x = (map_width + chunk_size - 1) / chunk_size
	var chunks_y = (map_height + chunk_size - 1) / chunk_size
	
	var shader = load("res://assets/shaders/world_terrain_3d.gdshader")
	if shader:
		terrain_mat = ShaderMaterial.new()
		terrain_mat.shader = shader
		terrain_mat.set_shader_parameter("heightmap_tex", hm_tex)
		terrain_mat.set_shader_parameter("biome_tex", biome_tex)
		terrain_mat.set_shader_parameter("world_size", Vector2(map_width * cell_size, map_height * cell_size))
		terrain_mat.set_shader_parameter("height_scale", elevation_scale)
		terrain_mat.set_shader_parameter("sea_level", sea_level)
		terrain_mat.set_shader_parameter("cliff_slope_threshold", 0.55)
		terrain_mat.set_shader_parameter("snow_elevation_threshold", 60.0)
		terrain_mat.set_shader_parameter("rock_elevation_threshold", 35.0)
		terrain_mat.set_shader_parameter("detail_normal_strength", 0.45)
		terrain_mat.set_shader_parameter("strata_frequency", 1.1)
		terrain_mat.set_shader_parameter("curvature_strength", 0.45)
		terrain_mat.set_shader_parameter("soil_depth_scale", 28.0)
		terrain_mat.set_shader_parameter("height_blend_contrast", 0.16)
		terrain_mat.set_shader_parameter("use_texture_arrays", false)
		terrain_mat.set_shader_parameter("show_rivers", show_rivers)
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

func _build_water_surface() -> void:
	_ensure_world_textures()

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
		water_mat.set_shader_parameter("depth_density", 0.08)
		water_mat.set_shader_parameter("normal_strength", 1.0)
		water_mat.set_shader_parameter("micro_ripple_strength", 0.40)
		water_mat.set_shader_parameter("micro_ripple_scale", 0.85)
		water_mat.set_shader_parameter("refraction_strength", 0.12)
		water_mat.set_shader_parameter("caustics_strength", 0.28)
		water_mat.set_shader_parameter("sss_color", Color(0.12, 0.85, 0.65))
		water_mat.set_shader_parameter("sss_strength", 0.70)
		water_mat.set_shader_parameter("shore_foam_distance", 1.4)
		water_mat.set_shader_parameter("shore_foam_intensity", 0.95)
		water_mat.set_shader_parameter("crest_foam_intensity", 1.15)
		water_mat.set_shader_parameter("crest_foam_threshold", 0.45)
		water_mat.set_shader_parameter("heightmap_tex", hm_tex)
		water_mesh_instance.material_override = water_mat
	
	wave_baker.physical_size = map_width * cell_size
	wave_baker.grid_size = map_width
	wave_baker.sea_level_normalized = 0.0
	wave_baker.height_scale_factor = 1.0
	var cage_texture = wave_baker.bake_from_heightmap(hm_tex)
	if cage_texture:
		wave_driver.grid_size = map_width
		wave_driver.world_size = map_width * cell_size
		wave_driver.height_scale_factor = 1.0
		wave_driver.target_material = water_mat
		wave_driver.update_textures(cage_texture, hm_tex)

func _build_river_ribbons() -> void:
	if not river_mesh_instance:
		return
		
	if not show_rivers:
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
		
		# Coordinate scale: 1.0 if waypoints are already in world meters (coords > map_width), else cell_size
		var wp_max = 0.0
		for wp in wps:
			wp_max = max(wp_max, max(wp.x, wp.y))
		var coord_scale = 1.0 if wp_max > float(map_width) else cell_size
		
		# Build smooth cross-sections along reach waypoints
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
			
			var center_wx = p.x
			var center_wz = p.y
			var left_wx = left_pt.x
			var left_wz = left_pt.y
			var right_wx = right_pt.x
			var right_wz = right_pt.y
			
			# 3D Evaluated Water Surface Elevation
			var water_y: float
			if not water_elevs.is_empty() and i < water_elevs.size():
				water_y = float(water_elevs[i]) * elevation_scale
			else:
				var center_ground = sample_ground_height(center_wx, center_wz)
				if not bed_elevs.is_empty() and i < bed_elevs.size():
					center_ground = min(center_ground, float(bed_elevs[i]) * elevation_scale)
				var water_depth = 0.25 * (elevation_scale / 0.4)
				if i == 0:
					water_depth = 0.08
				water_y = center_ground + water_depth
				
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
		river_mesh_instance.material_override = r_mat
	else:
		var std_mat = StandardMaterial3D.new()
		std_mat.albedo_color = Color(0.18, 0.62, 0.88, 0.85)
		std_mat.roughness = 0.12
		std_mat.metallic = 0.05
		std_mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		river_mesh_instance.material_override = std_mat

func get_river_info_near(world_pos: Vector2) -> Dictionary:
	# 1. First attempt authoritative C++ 16.16 spatial sample from SimBridge
	if bridge and bridge.has_method("get_nearest_river_sample"):
		var sample = bridge.get_nearest_river_sample(world_pos.x, world_pos.y)
		if sample.get("found", false):
			var tier = int(sample.get("tier", 1))
			var tier_name = "Brook / Headwater" if tier == 1 else ("River Channel" if tier == 2 else "Trunk River")
			sample["tier_name"] = tier_name
			var w = float(sample.get("width", 2.0))
			sample["channel_width"] = w
			sample["near_channel"] = float(sample.get("dist_to_center", 999.0)) <= (w * 0.5 + 6.0)
			return sample

	# 2. Query water depth and river presence directly from world grid
	var grid_x = world_pos.x / cell_size
	var grid_y = world_pos.y / cell_size
	var gx = int(clamp(grid_x, 0, map_width - 1))
	var gy = int(clamp(grid_y, 0, map_height - 1))
	var g_idx = gy * map_width + gx
	
	if bridge and bridge.has_method("get_water_depth"):
		var depths = bridge.get_water_depth()
		if not depths.is_empty() and g_idx < depths.size() and depths[g_idx] > 0.05:
			var d = depths[g_idx]
			var tier = 3 if d >= 2.0 else (2 if d >= 0.5 else 1)
			var tier_name = "Brook / Headwater" if tier == 1 else ("River Channel" if tier == 2 else "Trunk River")
			return {
				"found": true,
				"in_channel": true,
				"near_channel": true,
				"dist_to_center": 0.0,
				"channel_width": 4.0 * tier,
				"depth": d,
				"max_depth": d,
				"flow_rate": 10.0 * d,
				"tier": tier,
				"tier_name": tier_name,
				"reach_id": 1
			}
	
	var rivers = bridge.get_rivers() if bridge else PackedByteArray()
	if not rivers.is_empty() and g_idx < rivers.size() and rivers[g_idx] > 0:
		var tier = int(rivers[g_idx])
		var tier_name = "Brook / Headwater" if tier == 1 else ("River Channel" if tier == 2 else "Trunk River")
		return {
			"found": true,
			"in_channel": true,
			"near_channel": true,
			"dist_to_center": 0.0,
			"channel_width": 4.0 * tier,
			"depth": 1.2 * tier,
			"max_depth": 1.5 * tier,
			"flow_rate": 15.0 * tier,
			"tier": tier,
			"tier_name": tier_name,
			"reach_id": 1
		}
	return {}

func _spawn_player() -> void:
	bridge.clear_entities()
	
	var cell_size_fixed: int = int(cell_size * 65536.0)
	var spawn_sim_x: int = int((starter_spawn.x + 0.5) * cell_size_fixed)
	var spawn_sim_y: int = int((starter_spawn.y + 0.5) * cell_size_fixed)
	var player_radius_fixed: int = int(0.5 * 65536.0)
	
	player_id = bridge.spawn_entity(0, spawn_sim_x, spawn_sim_y, player_radius_fixed, 100, 0)
	
	sim_target_x = (starter_spawn.x + 0.5) * cell_size
	sim_target_z = (starter_spawn.y + 0.5) * cell_size
	sim_vel_x = 0.0
	sim_vel_z = 0.0
	
	var start_y = sample_ground_height(sim_target_x, sim_target_z)
	visual_pos = Vector3(sim_target_x, start_y, sim_target_z)
	player_visual.global_position = visual_pos
	cam_target = visual_pos + Vector3(0, camera_head_offset, 0)
	current_cam_distance = target_cam_distance
	_update_camera_transform(0.0)

func _clear_tree_chunks() -> void:
	current_chunk_coords = Vector2i(-9999, -9999)
	for cnode in loaded_tree_chunks.values():
		if is_instance_valid(cnode):
			cnode.queue_free()
	loaded_tree_chunks.clear()

func _update_active_tree_chunks(force: bool = false) -> void:
	if not bridge:
		return
	var chunk_size_world = 16.0 * cell_size
	var pcx: int = int(floor(visual_pos.x / chunk_size_world))
	var pcy: int = int(floor(visual_pos.z / chunk_size_world))
	var new_chunk_coords = Vector2i(pcx, pcy)

	if not force and new_chunk_coords == current_chunk_coords:
		return
	current_chunk_coords = new_chunk_coords

	var grid_size = bridge.get_chunk_grid_size()
	var required_chunks: Dictionary = {}

	for dy in range(-TREE_CHUNK_RADIUS, TREE_CHUNK_RADIUS + 1):
		for dx in range(-TREE_CHUNK_RADIUS, TREE_CHUNK_RADIUS + 1):
			var cx = pcx + dx
			var cy = pcy + dy
			if cx >= 0 and cx < grid_size.x and cy >= 0 and cy < grid_size.y:
				var ckey = Vector2i(cx, cy)
				required_chunks[ckey] = true
				if not loaded_tree_chunks.has(ckey):
					_load_tree_chunk(cx, cy)

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

		# Add physical trunk collision body
		var col_body = StaticBody3D.new()
		var col_shape = CollisionShape3D.new()
		var cyl = CylinderShape3D.new()
		cyl.radius = 0.35
		cyl.height = 3.5
		col_shape.shape = cyl
		col_shape.position = Vector3(0, 1.75, 0)
		col_body.add_child(col_shape)
		tree_node.add_child(col_body)

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

func _update_camera_transform(delta: float = 0.0) -> void:
	if not camera_3d:
		return
		
	var safe_delta = min(delta, 0.1) if delta > 0.0 else 0.0
	var yaw_rad = deg_to_rad(cam_rot_y)
	var pitch_rad = deg_to_rad(cam_rot_x)
	
	# Vector pointing from target towards camera
	var cam_dir = Vector3(
		cos(pitch_rad) * sin(yaw_rad),
		-sin(pitch_rad),
		cos(pitch_rad) * cos(yaw_rad)
	).normalized()
	
	# Horizontal camera right vector for dynamic over-the-shoulder framing
	var right_dir = Vector3(cos(yaw_rad), 0.0, -sin(yaw_rad)).normalized()
	
	# Subtle over-the-shoulder offset (stronger when close, subtler when zoomed out)
	var shoulder_ratio = clamp((target_cam_distance - min_cam_distance) / (max_cam_distance - min_cam_distance), 0.0, 1.0)
	var shoulder_offset = lerp(0.38, 0.12, shoulder_ratio)
	
	var cam_pivot = cam_target + (right_dir * shoulder_offset)
	
	# Valheim-Style Terrain Collision & Obstacle Avoidance (Ray-march with continuous bisection)
	var safe_distance = target_cam_distance
	var num_steps = 16
	for i in range(1, num_steps + 1):
		var t = float(i) / float(num_steps)
		var check_dist = target_cam_distance * t
		var check_pt = cam_pivot + (cam_dir * check_dist)
		var g_height = sample_ground_height(check_pt.x, check_pt.z)
		var water_h = sea_level * elevation_scale
		var surface_y = max(g_height, water_h)
		if surface_y + camera_collision_clearance > check_pt.y:
			# Continuous bisection refinement between step (i - 1) and step i
			var t_lo = float(i - 1) / float(num_steps)
			var t_hi = t
			for _b in range(5):
				var t_mid = (t_lo + t_hi) * 0.5
				var mid_pt = cam_pivot + (cam_dir * (target_cam_distance * t_mid))
				var mid_surf = max(sample_ground_height(mid_pt.x, mid_pt.z), water_h)
				if mid_surf + camera_collision_clearance > mid_pt.y:
					t_hi = t_mid
				else:
					t_lo = t_mid
			safe_distance = max(min_cam_distance, t_lo * target_cam_distance)
			break
			
	# Smooth distance spring: unconditionally stable exponential decay (never overshoots or oscillates)
	if safe_delta > 0.0:
		var decay_rate = 25.0 if current_cam_distance > safe_distance else 8.0
		var decay_factor = 1.0 - exp(-decay_rate * safe_delta)
		current_cam_distance = lerp(current_cam_distance, safe_distance, decay_factor)
	else:
		current_cam_distance = safe_distance
		
	# Strict bounds clamp prevents negative distance under any circumstance
	current_cam_distance = clamp(current_cam_distance, min_cam_distance, max_cam_distance)
		
	var final_cam_pos = cam_pivot + (cam_dir * current_cam_distance)
	
	# Ensure camera position is never below the ground / water surface
	var cam_ground_y = max(sample_ground_height(final_cam_pos.x, final_cam_pos.z), sea_level * elevation_scale)
	if final_cam_pos.y < cam_ground_y + camera_collision_clearance:
		final_cam_pos.y = cam_ground_y + camera_collision_clearance
		
	camera_3d.global_position = final_cam_pos
	
	# Gimbal lock and zero-cross-product singularity guard
	var to_pivot = cam_pivot - final_cam_pos
	if to_pivot.length_squared() > 0.0001:
		var fwd = to_pivot.normalized()
		var up_vec = Vector3.UP
		if abs(fwd.dot(Vector3.UP)) > 0.98:
			up_vec = right_dir.cross(fwd).normalized()
			if up_vec.length_squared() < 0.001:
				up_vec = Vector3.FORWARD
		camera_3d.look_at(cam_pivot, up_vec)

func _snap_camera_behind_player() -> void:
	cam_rot_y = rad_to_deg(player_visual.rotation.y) + 180.0
	cam_rot_y = wrapf(cam_rot_y, -180.0, 180.0)
	cam_rot_x = -16.0
	cam_target = visual_pos + Vector3(0, camera_head_offset, 0)
	current_cam_distance = target_cam_distance
	_update_camera_transform(0.0)

func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseMotion:
		# Mouse look: active when mouse captured or when dragging with right-click
		if Input.mouse_mode == Input.MOUSE_MODE_CAPTURED or is_rmb_down:
			cam_rot_y -= event.relative.x * mouse_sensitivity
			cam_rot_y = wrapf(cam_rot_y, -180.0, 180.0)
			
			cam_rot_x -= event.relative.y * mouse_sensitivity
			cam_rot_x = clamp(cam_rot_x, min_cam_pitch, max_cam_pitch)
			
	elif event is InputEventMouseButton:
		if event.button_index == MOUSE_BUTTON_WHEEL_UP:
			target_cam_distance = clamp(target_cam_distance - 0.75, min_cam_distance, max_cam_distance)
		elif event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			target_cam_distance = clamp(target_cam_distance + 0.75, min_cam_distance, max_cam_distance)
		elif event.button_index == MOUSE_BUTTON_RIGHT:
			is_rmb_down = event.pressed
		elif event.button_index == MOUSE_BUTTON_LEFT and event.pressed:
			# Re-capture mouse if clicking inside the window while cursor is free
			if Input.mouse_mode != Input.MOUSE_MODE_CAPTURED:
				Input.mouse_mode = Input.MOUSE_MODE_CAPTURED
				
	elif event is InputEventKey and event.pressed:
		if event.keycode == KEY_ESCAPE or event.keycode == KEY_TAB:
			# Toggle cursor lock
			if Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:
				Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
			else:
				Input.mouse_mode = Input.MOUSE_MODE_CAPTURED
		elif event.keycode == KEY_H:
			hud_visible = not hud_visible
			ui_root.visible = hud_visible
		elif event.keycode == KEY_F:
			_snap_camera_behind_player()
		elif event.keycode == KEY_R:
			randomize()
			generate_world(randi() % 1000000)
		elif event.keycode == KEY_T or event.keycode == KEY_EQUAL or event.keycode == KEY_KP_ADD:
			modify_targeted_terrain(terraform_delta)
		elif event.keycode == KEY_G or event.keycode == KEY_MINUS or event.keycode == KEY_KP_SUBTRACT:
			modify_targeted_terrain(-terraform_delta)

func _physics_process(delta: float) -> void:
	# 1. Camera Orbit Keys (Q / E)
	if Input.is_key_pressed(KEY_Q):
		cam_rot_y += 90.0 * delta
		cam_rot_y = wrapf(cam_rot_y, -180.0, 180.0)
	if Input.is_key_pressed(KEY_E):
		cam_rot_y -= 90.0 * delta
		cam_rot_y = wrapf(cam_rot_y, -180.0, 180.0)
		
	# 2. Camera-Relative Ground Movement Unprojection (Decoupled from Camera Pitch/Collisions)
	var yaw_rad = deg_to_rad(cam_rot_y)
	var cam_fwd = Vector3(-sin(yaw_rad), 0.0, -cos(yaw_rad)).normalized()
	var cam_right = Vector3(cos(yaw_rad), 0.0, -sin(yaw_rad)).normalized()
	
	var in_dir = Vector2.ZERO
	if Input.is_key_pressed(KEY_A) or Input.is_action_pressed("ui_left") or Input.is_key_pressed(KEY_LEFT): in_dir.x -= 1.0
	if Input.is_key_pressed(KEY_D) or Input.is_action_pressed("ui_right") or Input.is_key_pressed(KEY_RIGHT): in_dir.x += 1.0
	if Input.is_key_pressed(KEY_W) or Input.is_action_pressed("ui_up") or Input.is_key_pressed(KEY_UP): in_dir.y += 1.0
	if Input.is_key_pressed(KEY_S) or Input.is_action_pressed("ui_down") or Input.is_key_pressed(KEY_DOWN): in_dir.y -= 1.0
	
	if in_dir.length_squared() > 1.0:
		in_dir = in_dir.normalized()
		
	var ground_move = (cam_right * in_dir.x) + (cam_fwd * in_dir.y)
	if ground_move.length_squared() > 1.0:
		ground_move = ground_move.normalized()
		
	# Map Ground Movement to SimWorld 16.16 Fixed Point
	# Godot World X -> SimWorld X
	# Godot World Z -> SimWorld Y
	var move_x_fixed: int = int(round(ground_move.x * 65536.0))
	var move_y_fixed: int = int(round(ground_move.z * 65536.0))
	var aim_x_fixed: int = move_x_fixed
	var aim_y_fixed: int = move_y_fixed
	if ground_move.length_squared() < 0.001:
		aim_x_fixed = int(round(cam_fwd.x * 65536.0))
		aim_y_fixed = int(round(cam_fwd.z * 65536.0))
		
	var actions: int = 0
	if Input.is_mouse_button_pressed(MOUSE_BUTTON_LEFT) and Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:
		actions |= 1
		
	bridge.step(move_x_fixed, move_y_fixed, aim_x_fixed, aim_y_fixed, actions)
	
	# Read latest deterministic simulation state
	var render_states = bridge.get_render_states()
	var count = render_states.size() / 8
	for i in range(count):
		var offset = i * 8
		var eid = int(render_states[offset + 0])
		var etype = int(render_states[offset + 1])
		if etype == 0 or eid == player_id:
			sim_target_x = float(render_states[offset + 2])
			sim_target_z = float(render_states[offset + 3])
			sim_vel_x = float(render_states[offset + 4])
			sim_vel_z = float(render_states[offset + 5])
			break

func _process(delta: float) -> void:
	var safe_delta = min(delta, 0.1)
	
	# Smoothly interpolate visual avatar towards simulation position with exponential decay
	var t_visual = 1.0 - exp(-20.0 * safe_delta)
	visual_pos.x = lerp(visual_pos.x, sim_target_x, t_visual)
	visual_pos.z = lerp(visual_pos.z, sim_target_z, t_visual)
	visual_pos.y = sample_ground_height(visual_pos.x, visual_pos.z)
	player_visual.global_position = visual_pos
	
	# Rotate player towards velocity direction smoothly (or toward camera forward when acting stationary)
	var move_vel_3d = Vector3(sim_vel_x, 0.0, sim_vel_z)
	var t_rot = 1.0 - exp(-18.0 * safe_delta)
	if move_vel_3d.length_squared() > 0.001:
		var target_yaw = atan2(move_vel_3d.x, move_vel_3d.z)
		player_visual.rotation.y = lerp_angle(player_visual.rotation.y, target_yaw, t_rot)
	elif Input.is_mouse_button_pressed(MOUSE_BUTTON_LEFT) and Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:
		var p_yaw_rad = deg_to_rad(cam_rot_y)
		var p_cam_fwd = Vector3(-sin(p_yaw_rad), 0.0, -cos(p_yaw_rad))
		if p_cam_fwd.length_squared() > 0.001:
			var target_yaw = atan2(p_cam_fwd.x, p_cam_fwd.z)
			player_visual.rotation.y = lerp_angle(player_visual.rotation.y, target_yaw, t_rot)
		
	# Smoothly follow player head/shoulder with camera target
	var head_target = visual_pos + Vector3(0, camera_head_offset, 0)
	var t_cam = 1.0 - exp(-14.0 * safe_delta)
	cam_target = cam_target.lerp(head_target, t_cam)
	_update_camera_transform(safe_delta)

	# Dynamic Chunk Loading & Streaming
	_update_active_tree_chunks()
	
	# Update Targeted Tile for Terraforming
	_update_targeted_tile()
	
	if feedback_timer > 0.0:
		feedback_timer -= safe_delta
		if feedback_timer <= 0.0 and feedback_lbl:
			feedback_lbl.text = ""

	# Update HUD Telemetry
	if hud_visible:
		_update_hud_telemetry()

func _update_hud_telemetry() -> void:
	var gx = int(clamp(visual_pos.x / cell_size, 0.0, float(map_width - 1)))
	var gz = int(clamp(visual_pos.z / cell_size, 0.0, float(map_height - 1)))
	var cell_idx = gz * map_width + gx
	
	var raw_elev = float(elevation_data[cell_idx]) / 65536.0 if not elevation_data.is_empty() else 0.0
	var target_str = "None"
	if targeted_tile.x >= 0 and targeted_tile.y >= 0:
		var can_mod = bridge.can_modify_terrain(targeted_tile.x, targeted_tile.y)
		var mod_status = "Ready" if can_mod else "Blocked"
		var target_wx = (targeted_tile.x + 0.5) * cell_size
		var target_wz = (targeted_tile.y + 0.5) * cell_size
		var t_river = get_river_info_near(Vector2(target_wx, target_wz))
		var r_tag = ""
		if t_river.get("in_channel", false):
			r_tag = " - %s (%.1fm deep)" % [t_river["tier_name"], t_river["depth"]]
		target_str = "(%d, %d) [%s%s]" % [targeted_tile.x, targeted_tile.y, mod_status, r_tag]
	coords_lbl.text = "Player: (X: %.1fm, Z: %.1fm) | Elev: %.1fm | Target: %s" % [
		visual_pos.x, visual_pos.z, raw_elev, target_str
	]
	
	var biome_code = biome_data[cell_idx] if not biome_data.is_empty() else 0
	var biome_str = BIOME_NAMES[biome_code] if biome_code < BIOME_NAMES.size() else "Unknown"
	biome_lbl.text = "Biome: %s | Plate Boundary Region" % biome_str
	
	var surf_code = surface_data[cell_idx] if not surface_data.is_empty() else 0
	var surf_str = SURFACE_NAMES[surf_code] if surf_code < SURFACE_NAMES.size() else "Standard"
	var current_speed = sqrt(sim_vel_x * sim_vel_x + sim_vel_z * sim_vel_z) * 60.0 # m/s
	speed_lbl.text = "Surface: %s | Speed: %.1f m/s" % [surf_str, current_speed]
	
	# River Telemetry (In-channel vs Nearby Bank vs Dry Uplands)
	if river_lbl:
		var river_info = get_river_info_near(Vector2(visual_pos.x, visual_pos.z))
		if river_info.get("in_channel", false):
			river_lbl.text = "River: %s (Tier %d) [In Channel] | Depth: %.1fm | Flow: %.1f m³/s | Width: %.1fm" % [
				river_info["tier_name"], river_info["tier"], river_info["depth"], river_info["flow_rate"], river_info["channel_width"]
			]
			river_lbl.add_theme_color_override("font_color", Color(0.2, 0.9, 1.0))
		elif river_info.get("near_channel", false):
			river_lbl.text = "River: %s (Tier %d) [Bank Distance: %.1fm] | Flow: %.1f m³/s" % [
				river_info["tier_name"], river_info["tier"], river_info["dist_to_center"], river_info["flow_rate"]
			]
			river_lbl.add_theme_color_override("font_color", Color(0.4, 0.8, 0.9))
		else:
			river_lbl.text = "River: None (Dry Uplands)"
			river_lbl.add_theme_color_override("font_color", Color(0.55, 0.6, 0.65))
			
	status_lbl.text = "Seed: %d | Sim Tick: %d | Trees: %d (Active Chunks: %d)" % [
		current_seed, bridge.current_tick(), bridge.get_tree_count(), loaded_tree_chunks.size()
	]

func modify_targeted_terrain(delta_elev: int) -> bool:
	if targeted_tile.x < 0 or targeted_tile.y < 0:
		_update_targeted_tile()
	if targeted_tile.x < 0 or targeted_tile.y < 0:
		return false
	return try_modify_terrain(targeted_tile.x, targeted_tile.y, delta_elev)

func try_modify_terrain(tx: int, ty: int, delta_elev: int) -> bool:
	if tx < 0 or tx >= map_width or ty < 0 or ty >= map_height:
		return false

	if not bridge.can_modify_terrain(tx, ty):
		var warn_msg = "Cannot modify terrain: tree or structure present"
		push_warning(warn_msg)
		print(warn_msg)
		_show_feedback(warn_msg, Color(1.0, 0.35, 0.35))
		return false

	var success = bridge.modify_terrain(tx, ty, delta_elev)
	if not success:
		var warn_msg = "Cannot modify terrain: tree or structure present"
		push_warning(warn_msg)
		print(warn_msg)
		_show_feedback(warn_msg, Color(1.0, 0.35, 0.35))
		return false

	# Successful modification: trigger dynamic update of terrain mesh, heightmap texture, and elevation arrays
	_update_terrain_visuals()
	var new_elev_m = float(elevation_data[ty * map_width + tx]) / 65536.0
	var sign_str = "+" if delta_elev > 0 else ""
	var info_msg = "Modified terrain at (%d, %d): %s%.1fm (Now: %.1fm)" % [
		tx, ty, sign_str, float(delta_elev) / 65536.0, new_elev_m
	]
	_show_feedback(info_msg, Color(0.35, 1.0, 0.45))
	return true

func _update_terrain_visuals() -> void:
	elevation_data = bridge.get_elevation()
	_ensure_world_textures()
	_build_terrain_mesh()
	if water_mat:
		water_mat.set_shader_parameter("heightmap_tex", hm_tex)
	if wave_baker and wave_driver and water_mat:
		var cage_texture = wave_baker.bake_from_heightmap(hm_tex)
		if cage_texture:
			wave_driver.update_textures(cage_texture, hm_tex)

func _show_feedback(msg: String, col: Color = Color.WHITE, duration: float = 3.0) -> void:
	if feedback_lbl:
		feedback_lbl.text = msg
		feedback_lbl.add_theme_color_override("font_color", col)
		feedback_timer = duration

func _update_targeted_tile() -> void:
	if not camera_3d or map_width <= 0 or map_height <= 0:
		targeted_tile = Vector2i(-1, -1)
		if target_marker: target_marker.visible = false
		return

	# Raycast from camera center or player head forward
	var cam_pos = camera_3d.global_position
	var cam_dir = -camera_3d.global_transform.basis.z.normalized()
	
	var hit_found = false
	var hit_cell = Vector2i(-1, -1)
	
	# Raymarch against sampled ground height from near player up to 30m
	var dist_to_player = cam_pos.distance_to(visual_pos)
	var start_d = max(0.0, dist_to_player - 2.0)
	var max_d = dist_to_player + 30.0
	var step_size = max(0.25, cell_size * 0.25)
	var d = start_d
	
	while d <= max_d:
		var p = cam_pos + cam_dir * d
		var gh = sample_ground_height(p.x, p.z)
		if p.y <= gh:
			var cx = int(floor(p.x / cell_size))
			var cz = int(floor(p.z / cell_size))
			if cx >= 0 and cx < map_width and cz >= 0 and cz < map_height:
				hit_cell = Vector2i(cx, cz)
				hit_found = true
			break
		d += step_size
		
	# Fallback if aiming at horizon/sky: cell 2.5m in front of player
	if not hit_found:
		var p_yaw = player_visual.rotation.y
		var p_fwd = Vector3(sin(p_yaw), 0.0, cos(p_yaw)).normalized()
		var p_front = visual_pos + p_fwd * (cell_size * 1.5)
		var cx = int(clamp(floor(p_front.x / cell_size), 0, map_width - 1))
		var cz = int(clamp(floor(p_front.z / cell_size), 0, map_height - 1))
		hit_cell = Vector2i(cx, cz)
		hit_found = true
		
	targeted_tile = hit_cell
	
	if target_marker:
		if hit_found:
			target_marker.visible = true
			var center_x = (targeted_tile.x + 0.5) * cell_size
			var center_z = (targeted_tile.y + 0.5) * cell_size
			var gh = sample_ground_height(center_x, center_z)
			target_marker.global_position = Vector3(center_x, gh + 0.08, center_z)
			
			var can_mod = bridge.can_modify_terrain(targeted_tile.x, targeted_tile.y)
			if can_mod:
				target_marker_mat.albedo_color = Color(0.2, 0.9, 1.0, 0.45) # cyan / ready
			else:
				target_marker_mat.albedo_color = Color(1.0, 0.25, 0.25, 0.55) # red / blocked
		else:
			target_marker.visible = false
