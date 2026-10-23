extends Node2D

# ----------------------------------------------------------------------
# ExoFarm: Continental Tectonic Map Generator & Visualizer
# Optimized:
# - GPU Texture Blitting via Image & ImageTexture (0 draw-command bloat)
# - Viewport-Culled Local Overlays (Only zoomed-in visible tiles)
# - Comprehensive World Generation Telemetry, Presets & Layer Modes
# - Clean, categorized, scrollable control dashboard
# ----------------------------------------------------------------------

var bridge: SimBridge
var map_width: int = 0
var map_height: int = 0

# Factorio-esque Scale: 1 Tile = 32.0 Pixels
var cell_size: float = 32.0

# Visualization Modes:
# 0 = Micro Surface (Local Terrain & Detail)
# 1 = Macro Biomes (Continental Biome Map)
# 2 = Minerals N-P-K (Factorio-style resource patches)
# 3 = Heightmap (Continental Elevation)
# 4 = Hydrology & Water Table
var view_mode: int = 0
var view_mode_names = [
	"Micro Surface (Terrain)",
	"Macro Biomes (Continental)",
	"Minerals (N-P-K Patches)",
	"Heightmap Elevation",
	"Hydrology & Water Table"
]

var surface_types: PackedByteArray
var mineral_types: PackedByteArray
var biomes: PackedByteArray
var elevation: PackedInt32Array
var rivers: PackedByteArray
var lakes: PackedByteArray
var structures: PackedByteArray
var crop_types: PackedByteArray
var crop_growth: PackedInt64Array
var water_table: PackedInt64Array

# Fast GPU Texture Backing (Ultra-low RAM)
var map_image: Image
var map_texture: ImageTexture

var starter_spawn: Vector2i = Vector2i.ZERO
var current_seed: int = 42

# Player entity & movement state
var player_id: int = 0
var player_pos: Vector2 = Vector2.ZERO
var player_vel: Vector2 = Vector2.ZERO
var player_facing: Vector2 = Vector2.DOWN
var camera_follow_player: bool = true
var is_world_map_open: bool = false
var show_grid: bool = true
var saved_local_cam_pos: Vector2 = Vector2.ZERO
var saved_local_zoom: Vector2 = Vector2(1.0, 1.0)

var camera: Camera2D
var tick_counter: int = 0

# UI Controls
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

var mode_opt: OptionButton
var view_btn: Button
var cam_btn: Button
var map_btn: Button
var grid_btn: Button
var status_lbl: Label
var stats_lbl: Label

func _ready() -> void:
	texture_filter = CanvasItem.TEXTURE_FILTER_NEAREST
	bridge = SimBridge.new()
	add_child(bridge)
	
	camera = Camera2D.new()
	camera.zoom = Vector2(1.0, 1.0)
	add_child(camera)
	
	_setup_ui()
	_load_config_into_ui(bridge.get_config())
	generate_new_map(current_seed)
	focus_on_spawn()

func _create_section_header(title_text: String) -> Control:
	var container = VBoxContainer.new()
	container.add_theme_constant_override("separation", 3)
	
	var sep = HSeparator.new()
	sep.add_theme_constant_override("separation", 6)
	container.add_child(sep)
	
	var lbl = Label.new()
	lbl.text = "──  " + title_text + "  ──"
	lbl.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	lbl.add_theme_color_override("font_color", Color(0.60, 0.78, 1.0))
	container.add_child(lbl)
	
	return container

func _create_spin_row(parent: VBoxContainer, label_text: String, min_v: float, max_v: float, step_v: float, default_v: float) -> SpinBox:
	var hb = HBoxContainer.new()
	var lbl = Label.new()
	lbl.text = label_text
	lbl.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	var spin = SpinBox.new()
	spin.min_value = min_v
	spin.max_value = max_v
	spin.step = step_v
	spin.value = default_v
	spin.custom_minimum_size = Vector2(90, 0)
	hb.add_child(lbl)
	hb.add_child(spin)
	parent.add_child(hb)
	return spin

func _create_check_row(parent: VBoxContainer, label_text: String, default_v: bool) -> CheckBox:
	var chk = CheckBox.new()
	chk.text = label_text
	chk.button_pressed = default_v
	parent.add_child(chk)
	return chk

func _setup_ui() -> void:
	var canvas = CanvasLayer.new()
	canvas.name = "MapGenCanvas"
	add_child(canvas)
	
	# Left Sidebar Panel
	var panel = PanelContainer.new()
	panel.set_anchors_preset(Control.PRESET_LEFT_WIDE)
	panel.offset_left = 14
	panel.offset_top = 14
	panel.offset_right = 364
	panel.offset_bottom = -14
	
	var style_box = StyleBoxFlat.new()
	style_box.bg_color = Color(0.08, 0.10, 0.14, 0.94)
	style_box.border_color = Color(0.22, 0.30, 0.42, 0.85)
	style_box.border_width_left = 1
	style_box.border_width_right = 1
	style_box.border_width_top = 1
	style_box.border_width_bottom = 1
	style_box.corner_radius_top_left = 8
	style_box.corner_radius_top_right = 8
	style_box.corner_radius_bottom_left = 8
	style_box.corner_radius_bottom_right = 8
	style_box.content_margin_left = 10
	style_box.content_margin_right = 10
	style_box.content_margin_top = 10
	style_box.content_margin_bottom = 10
	panel.add_theme_stylebox_override("panel", style_box)
	canvas.add_child(panel)
	
	# ScrollContainer for clean, smooth vertical scrolling
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
	
	# -------------------------------------------------------------
	# 1. Header
	# -------------------------------------------------------------
	var title_lbl = Label.new()
	title_lbl.text = "Map Generator Visualizer"
	title_lbl.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	title_lbl.add_theme_color_override("font_color", Color(1.0, 0.88, 0.40))
	vbox.add_child(title_lbl)
	
	var subtitle_lbl = Label.new()
	subtitle_lbl.text = "Continental & Tectonic Synthesis (256×144)"
	subtitle_lbl.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	subtitle_lbl.add_theme_color_override("font_color", Color(0.70, 0.75, 0.80))
	vbox.add_child(subtitle_lbl)
	
	status_lbl = Label.new()
	status_lbl.text = "Player: (0, 0) | Grassland"
	status_lbl.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	status_lbl.add_theme_color_override("font_color", Color(0.55, 0.85, 0.65))
	vbox.add_child(status_lbl)
	
	# -------------------------------------------------------------
	# 2. Seed & Generation
	# -------------------------------------------------------------
	vbox.add_child(_create_section_header("SEED & GENERATION"))
	
	var seed_hb = HBoxContainer.new()
	var seed_label = Label.new()
	seed_label.text = "Seed:"
	seed_spin = SpinBox.new()
	seed_spin.min_value = 0
	seed_spin.max_value = 999999999
	seed_spin.value = current_seed
	seed_spin.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	seed_hb.add_child(seed_label)
	seed_hb.add_child(seed_spin)
	vbox.add_child(seed_hb)
	
	var seed_btn_row = HBoxContainer.new()
	
	var regen_btn = Button.new()
	regen_btn.text = "Random Seed [R]"
	regen_btn.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	regen_btn.pressed.connect(self._on_random_seed_pressed)
	seed_btn_row.add_child(regen_btn)
	
	var apply_seed_btn = Button.new()
	apply_seed_btn.text = "Generate"
	apply_seed_btn.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	apply_seed_btn.pressed.connect(self._on_generate_pressed)
	seed_btn_row.add_child(apply_seed_btn)
	vbox.add_child(seed_btn_row)
	
	# -------------------------------------------------------------
	# 3. Tectonic Plates & Warping
	# -------------------------------------------------------------
	vbox.add_child(_create_section_header("TECTONIC PLATES & WARPING"))
	num_plates_spin = _create_spin_row(vbox, "Plates Count:", 2, 40, 1, 14)
	continent_chance_spin = _create_spin_row(vbox, "Continent %:", 0, 100, 1, 60)
	island_arc_chance_spin = _create_spin_row(vbox, "Island Arc %:", 0, 100, 1, 15)
	warp_scale_spin = _create_spin_row(vbox, "Domain Warp Scale:", 0, 150, 1, 75)
	fold_belt_width_spin = _create_spin_row(vbox, "Fold Belt Width:", 10, 100, 1, 40)

	# -------------------------------------------------------------
	# 4. Orogeny & Mountain Elevation
	# -------------------------------------------------------------
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

	# -------------------------------------------------------------
	# 5. Terrain Detail & Noise
	# -------------------------------------------------------------
	vbox.add_child(_create_section_header("TERRAIN DETAIL & NOISE"))
	detail_noise_spin = _create_spin_row(vbox, "Detail Noise Scale:", 0, 30, 0.5, 14.0)
	noise_octaves_spin = _create_spin_row(vbox, "Noise Octaves:", 1, 8, 1, 6)

	var apply_knobs_btn = Button.new()
	apply_knobs_btn.text = "Apply Parameters & Generate [Space]"
	apply_knobs_btn.pressed.connect(self._on_generate_pressed)
	vbox.add_child(apply_knobs_btn)

	# -------------------------------------------------------------
	# 6. Continental Presets
	# -------------------------------------------------------------
	vbox.add_child(_create_section_header("CONTINENTAL PRESETS"))

	var preset_std_btn = Button.new()
	preset_std_btn.text = "Preset: Major Continents (Default)"
	preset_std_btn.pressed.connect(func(): _apply_preset({
		"num_plates": 14, "continent_chance": 60, "island_arc_chance": 15,
		"warp_scale": 75.0, "fold_belt_width": 40.0, "collision_uplift": 38.0,
		"subduction_arc": 24.0, "subduction_trench": 22.0, "detail_noise_scale": 14.0
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
	reset_knobs_btn.pressed.connect(func(): _load_config_into_ui(bridge.get_config()))
	vbox.add_child(reset_knobs_btn)
	
	# -------------------------------------------------------------
	# 5. Display Layers & Modes
	# -------------------------------------------------------------
	vbox.add_child(_create_section_header("LAYER MODES"))
	
	mode_opt = OptionButton.new()
	for i in range(view_mode_names.size()):
		mode_opt.add_item(view_mode_names[i], i)
	mode_opt.selected = view_mode
	mode_opt.item_selected.connect(self._on_mode_selected)
	vbox.add_child(mode_opt)
	
	view_btn = Button.new()
	view_btn.text = "Next Layer Mode [1..5]"
	view_btn.pressed.connect(self._on_cycle_mode_pressed)
	vbox.add_child(view_btn)
	
	# -------------------------------------------------------------
	# 6. View & Camera Controls
	# -------------------------------------------------------------
	vbox.add_child(_create_section_header("VIEW & CAMERA"))
	
	map_btn = Button.new()
	map_btn.text = "Toggle World Overview [M]"
	map_btn.pressed.connect(self._on_toggle_world_map_pressed)
	vbox.add_child(map_btn)
	
	cam_btn = Button.new()
	cam_btn.text = "Camera: Follow Player [C]"
	cam_btn.pressed.connect(self._on_toggle_camera_pressed)
	vbox.add_child(cam_btn)
	
	grid_btn = Button.new()
	grid_btn.text = "Tile Grid: On [G]"
	grid_btn.pressed.connect(self._on_toggle_grid_pressed)
	vbox.add_child(grid_btn)
	
	var focus_btn = Button.new()
	focus_btn.text = "Center on Spawn [F]"
	focus_btn.pressed.connect(self.focus_on_spawn)
	vbox.add_child(focus_btn)
	
	# -------------------------------------------------------------
	# 7. World Generation Metrics / Telemetry
	# -------------------------------------------------------------
	vbox.add_child(_create_section_header("GENERATION METRICS"))
	
	stats_lbl = Label.new()
	stats_lbl.text = "Calculating metrics..."
	stats_lbl.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	vbox.add_child(stats_lbl)
	
	# -------------------------------------------------------------
	# 8. Scene Switcher
	# -------------------------------------------------------------
	vbox.add_child(_create_section_header("SWITCH SCENE"))
	
	var scene_3d_btn = Button.new()
	scene_3d_btn.text = "Open 3D Tectonics & Wave Visualizer"
	scene_3d_btn.pressed.connect(func(): get_tree().change_scene_to_file("res://world_3d_visualizer.tscn"))
	vbox.add_child(scene_3d_btn)

	var micro_scene_btn = Button.new()
	micro_scene_btn.text = "Open Ground River Visualizer"
	micro_scene_btn.pressed.connect(func(): get_tree().change_scene_to_file("res://river_micro_visualizer.tscn"))
	vbox.add_child(micro_scene_btn)
	
	var main_scene_btn = Button.new()
	main_scene_btn.text = "Return to Main Map Visualizer"
	main_scene_btn.pressed.connect(func(): get_tree().change_scene_to_file("res://map_visualizer.tscn"))
	vbox.add_child(main_scene_btn)
	
	# -------------------------------------------------------------
	# 9. Keybindings Reference
	# -------------------------------------------------------------
	vbox.add_child(_create_section_header("CONTROLS"))
	
	var help_lbl = Label.new()
	help_lbl.text = "[Space] Generate / Apply Knobs\n[WASD / Arrows] Move Player\n[I, J, K, L] Freecam Pan\n[M] Toggle World Map\n[1..5] Select Layer Mode\n[G] Toggle Tile Grid\n[C] Toggle Camera Mode\n[F] Center on Spawn\n[R] Random Seed\n[Mouse Wheel] Zoom Map"
	help_lbl.add_theme_color_override("font_color", Color(0.70, 0.75, 0.80))
	vbox.add_child(help_lbl)

func get_ui_config() -> Dictionary:
	var cfg = {}
	cfg["seed"] = current_seed
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
	
	return cfg

func _load_config_into_ui(dict: Dictionary) -> void:
	if dict.is_empty():
		return
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
	

func _apply_preset(preset_dict: Dictionary) -> void:
	_load_config_into_ui(preset_dict)
	generate_new_map(current_seed)

func _on_random_seed_pressed() -> void:
	randomize()
	var new_seed = randi() % 100000000
	if seed_spin:
		seed_spin.value = new_seed
	generate_new_map(new_seed)

func _on_generate_pressed() -> void:
	var seed_to_use = current_seed
	if seed_spin:
		seed_to_use = int(seed_spin.value)
	generate_new_map(seed_to_use)

func _on_mode_selected(idx: int) -> void:
	view_mode = idx
	if mode_opt:
		mode_opt.selected = view_mode
	_update_map_texture()
	queue_redraw()

func _on_cycle_mode_pressed() -> void:
	view_mode = (view_mode + 1) % view_mode_names.size()
	if mode_opt:
		mode_opt.selected = view_mode
	_update_map_texture()
	queue_redraw()

func generate_new_map(seed_val: int) -> void:
	current_seed = seed_val
	if seed_spin:
		seed_spin.value = current_seed
	
	var cfg = get_ui_config()
	cfg["seed"] = current_seed
	bridge.set_config(cfg)
	
	bridge.generate_player_map(current_seed, 256, 144, 64 * 65536)
	
	map_width = bridge.get_map_width()
	map_height = bridge.get_map_height()
	starter_spawn = bridge.get_starter_spawn()
	is_world_map_open = false
	
	bridge.clear_entities()
	var cell_size_fixed: int = 64 * 65536
	var spawn_x_fixed: int = int((starter_spawn.x + 0.5) * cell_size_fixed)
	var spawn_y_fixed: int = int((starter_spawn.y + 0.5) * cell_size_fixed)
	var player_radius_fixed: int = 20 * 65536
	player_id = bridge.spawn_entity(0, spawn_x_fixed, spawn_y_fixed, player_radius_fixed, 100, 0)
	
	player_pos = Vector2((starter_spawn.x + 0.5) * cell_size, (starter_spawn.y + 0.5) * cell_size)
	player_vel = Vector2.ZERO
	player_facing = Vector2.DOWN
	
	_refresh_data()
	_update_map_texture()
	_update_stats_ui()
	_update_status_bar()
	
	queue_redraw()

func _refresh_data() -> void:
	surface_types = bridge.get_surface_types()
	mineral_types = bridge.get_mineral_types()
	biomes = bridge.get_biome()
	elevation = bridge.get_elevation()
	rivers = bridge.get_rivers()
	lakes = bridge.get_lakes()
	structures = bridge.get_structure_types()
	crop_types = bridge.get_crop_types()
	crop_growth = bridge.get_crop_growth()
	water_table = bridge.get_water_table()

# Bakes tile colors into a single lightweight GPU texture
func _update_map_texture() -> void:
	if map_width <= 0 or map_height <= 0 or surface_types.is_empty():
		return
		
	var byte_data = PackedByteArray()
	byte_data.resize(map_width * map_height * 4)
	
	var out_idx = 0
	for y in range(map_height):
		for x in range(map_width):
			var col = _get_tile_draw_color(x, y)
			byte_data[out_idx + 0] = int(clamp(col.r * 255.0, 0.0, 255.0))
			byte_data[out_idx + 1] = int(clamp(col.g * 255.0, 0.0, 255.0))
			byte_data[out_idx + 2] = int(clamp(col.b * 255.0, 0.0, 255.0))
			byte_data[out_idx + 3] = int(clamp(col.a * 255.0, 0.0, 255.0))
			out_idx += 4
			
	if map_image == null or map_image.get_width() != map_width or map_image.get_height() != map_height:
		map_image = Image.create_from_data(map_width, map_height, false, Image.FORMAT_RGBA8, byte_data)
		map_texture = ImageTexture.create_from_image(map_image)
	else:
		map_image.set_data(map_width, map_height, false, Image.FORMAT_RGBA8, byte_data)
		map_texture.update(map_image)

func _get_biome_name(b: int) -> String:
	match b:
		0: return "Ocean"
		1: return "Tundra / Glacier"
		2: return "Desert"
		3: return "Grassland / Plains"
		4: return "Temperate Forest"
		5: return "Tropical Rainforest"
		6: return "Swamp / Wetlands"
		7: return "Mountain Range"
		_: return "Wilderness"

func _calculate_world_stats() -> Dictionary:
	var total_tiles = map_width * map_height
	if total_tiles == 0 or biomes.is_empty():
		return {}
	
	var biome_counts = {}
	for i in range(8):
		biome_counts[i] = 0
		
	var ocean_count = 0
	var land_count = 0
	var river_tiles = 0
	var min_el = 999999
	var max_el = -999999
	
	var mineral_counts = {1: 0, 2: 0, 3: 0} # 1: N, 2: P, 3: K
	
	for y in range(map_height):
		for x in range(map_width):
			var idx = y * map_width + x
			var b = biomes[idx]
			if biome_counts.has(b):
				biome_counts[b] += 1
			if b == 0:
				ocean_count += 1
			else:
				land_count += 1
				
			if not rivers.is_empty() and rivers[idx] > 0:
				river_tiles += 1
				
			if not mineral_types.is_empty():
				var m = mineral_types[idx]
				if mineral_counts.has(m):
					mineral_counts[m] += 1
					
			if not elevation.is_empty():
				var el = elevation[idx] / 65536
				if el < min_el: min_el = el
				if el > max_el: max_el = el
				
	return {
		"total_tiles": total_tiles,
		"ocean_count": ocean_count,
		"land_count": land_count,
		"land_pct": (float(land_count) / float(total_tiles)) * 100.0,
		"ocean_pct": (float(ocean_count) / float(total_tiles)) * 100.0,
		"biome_counts": biome_counts,
		"river_tiles": river_tiles,
		"river_pct": (float(river_tiles) / float(total_tiles)) * 100.0,
		"min_el": min_el,
		"max_el": max_el,
		"minerals": mineral_counts,
		"river_count": bridge.get_river_count()
	}

func _update_stats_ui() -> void:
	if stats_lbl == null:
		return
	var stats = _calculate_world_stats()
	if stats.is_empty():
		return
		
	var b_spawn_name = "Grassland"
	if starter_spawn.x >= 0 and starter_spawn.x < map_width and starter_spawn.y >= 0 and starter_spawn.y < map_height:
		var s_idx = starter_spawn.y * map_width + starter_spawn.x
		if not biomes.is_empty():
			b_spawn_name = _get_biome_name(biomes[s_idx])
			
	var biome_breakdown = ""
	var b_names = [
		"Ocean", "Tundra", "Desert", "Grassland",
		"Forest", "Rainforest", "Swamp", "Mountain"
	]
	for i in range(8):
		var cnt = stats["biome_counts"].get(i, 0)
		var pct = (float(cnt) / float(stats["total_tiles"])) * 100.0
		if pct >= 0.5:
			biome_breakdown += "  • %s: %.1f%%\n" % [b_names[i], pct]
			
	stats_lbl.text = "Spawn: (%d, %d) [%s]\nLand: %.1f%% | Ocean: %.1f%%\nElevation: %d to %d\nRivers: %d active (%d tiles, %.1f%%)\nOre Patches: N:%d  P:%d  K:%d\n\nBiome Coverage:\n%s" % [
		starter_spawn.x, starter_spawn.y, b_spawn_name,
		stats["land_pct"], stats["ocean_pct"],
		stats["min_el"], stats["max_el"],
		stats["river_count"], stats["river_tiles"], stats["river_pct"],
		stats["minerals"][1], stats["minerals"][2], stats["minerals"][3],
		biome_breakdown.strip_edges()
	]

func _update_status_bar() -> void:
	if status_lbl == null:
		return
	var tile_x = int(player_pos.x / cell_size)
	var tile_y = int(player_pos.y / cell_size)
	var b_name = "Grassland"
	if tile_x >= 0 and tile_x < map_width and tile_y >= 0 and tile_y < map_height:
		var idx = tile_y * map_width + tile_x
		if not biomes.is_empty():
			b_name = _get_biome_name(biomes[idx])
	status_lbl.text = "Player: (%d, %d) | %s" % [tile_x, tile_y, b_name]

func focus_on_spawn() -> void:
	if map_width > 0 and map_height > 0:
		camera.position = Vector2((starter_spawn.x + 0.5) * cell_size, (starter_spawn.y + 0.5) * cell_size)

func focus_on_player() -> void:
	if is_world_map_open:
		_on_toggle_world_map_pressed()
	camera.position = player_pos

func _on_toggle_camera_pressed() -> void:
	camera_follow_player = not camera_follow_player
	cam_btn.text = "Camera: " + ("Follow Player [C]" if camera_follow_player else "Freecam Pan [C]")

func _on_toggle_grid_pressed() -> void:
	show_grid = not show_grid
	grid_btn.text = "Tile Grid: " + ("On [G]" if show_grid else "Off [G]")
	queue_redraw()

func _on_toggle_world_map_pressed() -> void:
	is_world_map_open = not is_world_map_open
	if is_world_map_open:
		saved_local_cam_pos = camera.position
		saved_local_zoom = camera.zoom
		view_mode = 1
		if mode_opt: mode_opt.selected = 1
		camera.position = Vector2(map_width * cell_size / 2.0, map_height * cell_size / 2.0)
		var fit_zoom_x = 1800.0 / (map_width * cell_size)
		var fit_zoom_y = 1000.0 / (map_height * cell_size)
		camera.zoom = Vector2(min(fit_zoom_x, fit_zoom_y), min(fit_zoom_x, fit_zoom_y))
	else:
		view_mode = 0
		if mode_opt: mode_opt.selected = 0
		camera.position = saved_local_cam_pos if saved_local_cam_pos != Vector2.ZERO else player_pos
		camera.zoom = saved_local_zoom
	_update_map_texture()
	queue_redraw()

func _physics_process(delta: float) -> void:
	var move_dir = Vector2.ZERO
	if Input.is_key_pressed(KEY_A) or Input.is_key_pressed(KEY_LEFT): move_dir.x -= 1.0
	if Input.is_key_pressed(KEY_D) or Input.is_key_pressed(KEY_RIGHT): move_dir.x += 1.0
	if Input.is_key_pressed(KEY_W) or Input.is_key_pressed(KEY_UP): move_dir.y -= 1.0
	if Input.is_key_pressed(KEY_S) or Input.is_key_pressed(KEY_DOWN): move_dir.y += 1.0
		
	if move_dir.length_squared() > 1.0:
		move_dir = move_dir.normalized()
		
	var move_x_fixed: int = int(round(move_dir.x * 65536.0))
	var move_y_fixed: int = int(round(move_dir.y * 65536.0))
	
	var mouse_world = get_global_mouse_position()
	var aim_sim_x: int = int(round((mouse_world.x / cell_size) * 64.0 * 65536.0))
	var aim_sim_y: int = int(round((mouse_world.y / cell_size) * 64.0 * 65536.0))
	
	var actions: int = 0
	if Input.is_mouse_button_pressed(MOUSE_BUTTON_LEFT):
		actions |= 1
		
	bridge.step(move_x_fixed, move_y_fixed, aim_sim_x, aim_sim_y, actions)
	
	var render_states = bridge.get_render_states()
	var num_entities = render_states.size() / 8
	for i in range(num_entities):
		var offset = i * 8
		var e_id = int(render_states[offset + 0])
		var e_type = int(render_states[offset + 1])
		if e_type == 0 or e_id == player_id:
			var sim_x = render_states[offset + 2]
			var sim_y = render_states[offset + 3]
			var sim_vx = render_states[offset + 4]
			var sim_vy = render_states[offset + 5]
			
			player_pos = Vector2(sim_x / 64.0 * cell_size, sim_y / 64.0 * cell_size)
			player_vel = Vector2(sim_vx, sim_vy)
			if move_dir.length_squared() > 0.01:
				player_facing = move_dir
			elif (mouse_world - player_pos).length_squared() > 10.0:
				player_facing = (mouse_world - player_pos).normalized()
			break
			
	if not is_world_map_open:
		if camera_follow_player:
			camera.position = camera.position.lerp(player_pos, 12.0 * delta)
		else:
			var pan_speed = 600.0 / camera.zoom.x
			var pan_move = Vector2.ZERO
			if Input.is_key_pressed(KEY_J): pan_move.x -= 1.0
			if Input.is_key_pressed(KEY_L): pan_move.x += 1.0
			if Input.is_key_pressed(KEY_I): pan_move.y -= 1.0
			if Input.is_key_pressed(KEY_K): pan_move.y += 1.0
			if pan_move != Vector2.ZERO:
				camera.position += pan_move.normalized() * pan_speed * delta
				
	tick_counter += 1
	if tick_counter % 20 == 0:
		_update_status_bar()
		
	queue_redraw()

func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventKey and event.is_pressed() and not event.is_echo():
		if event.keycode == KEY_M:
			_on_toggle_world_map_pressed()
		elif event.keycode == KEY_G:
			_on_toggle_grid_pressed()
		elif event.keycode == KEY_1:
			_on_mode_selected(0)
		elif event.keycode == KEY_2:
			_on_mode_selected(1)
		elif event.keycode == KEY_3:
			_on_mode_selected(2)
		elif event.keycode == KEY_4:
			_on_mode_selected(3)
		elif event.keycode == KEY_5:
			_on_mode_selected(4)
		elif event.keycode == KEY_C:
			_on_toggle_camera_pressed()
		elif event.keycode == KEY_F:
			focus_on_spawn()
		elif event.keycode == KEY_R:
			_on_random_seed_pressed()
		elif event.keycode == KEY_SPACE:
			_on_generate_pressed()
	elif event is InputEventMouseButton:
		if event.button_index == MOUSE_BUTTON_WHEEL_UP:
			var new_z = clamp(camera.zoom.x * 1.15, 0.15, 4.0)
			camera.zoom = Vector2(new_z, new_z)
		elif event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			var new_z = clamp(camera.zoom.x * 0.85, 0.15, 4.0)
			camera.zoom = Vector2(new_z, new_z)

func _get_tile_draw_color(x: int, y: int) -> Color:
	var idx = y * map_width + x
	if view_mode == 0:
		var st = surface_types[idx]
		var struct_t = structures[idx] if not structures.is_empty() else 0
		if struct_t == 6: return Color(0.55, 0.35, 0.15)
		elif struct_t == 2: return Color(0.95, 0.95, 0.20)
		elif struct_t == 4 or struct_t == 5: return Color(0.00, 0.90, 0.90)
			
		match st:
			0:
				var b = biomes[idx]
				match b:
					1: return Color(0.85, 0.90, 0.95)
					2: return Color(0.88, 0.76, 0.45)
					3: return Color(0.42, 0.72, 0.28)
					4: return Color(0.20, 0.50, 0.24)
					5: return Color(0.00, 0.38, 0.16)
					6: return Color(0.32, 0.40, 0.26)
					7: return Color(0.55, 0.55, 0.55)
					_: return Color(0.35, 0.55, 0.25)
			1: return Color(0.35, 0.25, 0.15)
			2: return Color(0.30, 0.22, 0.14)
			3: return Color(0.15, 0.65, 0.90)
			4: return Color(0.04, 0.15, 0.38)
			5: return Color(0.25, 0.25, 0.25)
			6: return Color(0.25, 0.88, 0.82)
			7: return Color(0.95, 0.55, 0.10)
			8: return Color(0.75, 0.35, 0.85)
			_: return Color.BLACK
	elif view_mode == 1:
		var b_type = biomes[idx]
		var color = Color.BLACK
		match b_type:
			0: color = Color(0.08, 0.18, 0.45)
			1: color = Color(0.85, 0.90, 0.95)
			2: color = Color(0.92, 0.78, 0.46)
			3: color = Color(0.55, 0.78, 0.38)
			4: color = Color(0.20, 0.50, 0.25)
			5: color = Color(0.00, 0.40, 0.18)
			6: color = Color(0.30, 0.38, 0.25)
			7: color = Color(0.50, 0.50, 0.50)
		var el_val = float(elevation[idx]) / 65536.0
		var factor = 0.75 + 0.50 * clamp(el_val / 80.0, 0.0, 1.0) if el_val > 0.0 else 0.50
		color.r = clamp(color.r * factor, 0.0, 1.0)
		color.g = clamp(color.g * factor, 0.0, 1.0)
		color.b = clamp(color.b * factor, 0.0, 1.0)
		if not lakes.is_empty() and lakes[idx] != 0:
			color = Color(0.05, 0.40, 0.70)
		elif rivers[idx] == 2:
			color = Color(0.04, 0.25, 0.75)
		elif rivers[idx] == 1:
			color = Color(0.20, 0.70, 0.95)
		return color
	elif view_mode == 2:
		var m_type = mineral_types[idx]
		if m_type == 1: return Color(0.25, 0.88, 0.82)
		elif m_type == 2: return Color(0.95, 0.55, 0.10)
		elif m_type == 3: return Color(0.75, 0.35, 0.85)
		else:
			var el = float(elevation[idx]) / 65536.0
			return Color(0.05, 0.10, 0.25) if el <= 0 else Color(0.18, 0.20, 0.18)
	elif view_mode == 3:
		var el_val = float(elevation[idx]) / 65536.0
		var norm = clamp(el_val / 120.0, 0.0, 1.0)
		if el_val <= 0: return Color(0.0, 0.0, norm * 0.3 + 0.2)
		elif not lakes.is_empty() and lakes[idx] != 0: return Color(0.05, 0.40, 0.70)
		elif rivers[idx] == 2: return Color(0.0, 0.55, 0.95)
		elif rivers[idx] == 1: return Color(0.20, 0.80, 1.0)
		else: return Color(norm, norm, norm)
	elif view_mode == 4:
		var wt_val = float(water_table[idx]) / 65536.0 if not water_table.is_empty() else 0.0
		var wt_norm = clamp(wt_val / 100.0, 0.0, 1.0)
		if surface_types[idx] == 4: return Color(0.05, 0.20, 0.60)
		elif surface_types[idx] == 3: return Color(0.10, 0.50, 0.85)
		elif surface_types[idx] == 2: return Color(0.40, 0.30, 0.15)
		else: return Color(0.8 - wt_norm * 0.6, 0.7 + wt_norm * 0.2, 0.4 + wt_norm * 0.5)
	return Color.BLACK

# -------------------------------------------------------------
# High-Performance GPU Texture Drawing
# -------------------------------------------------------------
func _draw() -> void:
	if map_width <= 0 or map_height <= 0 or map_texture == null:
		return
		
	# 1. Single Draw Call for whole map texture
	draw_texture_rect(map_texture, Rect2(0, 0, map_width * cell_size, map_height * cell_size), false)
	
	var vp_size = get_viewport_rect().size
	var half_vp_world = (vp_size / 2.0) / camera.zoom
	var top_left = camera.position - half_vp_world
	var bot_right = camera.position + half_vp_world
	
	var min_tx = int(clamp(floor(top_left.x / cell_size) - 1, 0, map_width - 1))
	var max_tx = int(clamp(ceil(bot_right.x / cell_size) + 1, 0, map_width - 1))
	var min_ty = int(clamp(floor(top_left.y / cell_size) - 1, 0, map_height - 1))
	var max_ty = int(clamp(ceil(bot_right.y / cell_size) + 1, 0, map_height - 1))
	
	var zoom_x = camera.zoom.x
	var is_detail_lod = zoom_x >= 0.75 and not is_world_map_open

	# 2. Viewport-Culled Local Overlays
	if is_detail_lod:
		for y in range(min_ty, max_ty + 1):
			for x in range(min_tx, max_tx + 1):
				var rect = Rect2(x * cell_size, y * cell_size, cell_size, cell_size)
				if show_grid and zoom_x >= 0.9:
					draw_rect(rect, Color(0, 0, 0, 0.12), false, 1.0)

	# 3. Draw Starter Site Reticle
	var sp_center = Vector2((starter_spawn.x + 0.5) * cell_size, (starter_spawn.y + 0.5) * cell_size)
	draw_arc(sp_center, cell_size * 2.5, 0, TAU, 32, Color(1.0, 0.9, 0.2, 0.7), 2.5)
	draw_circle(sp_center, 4.0, Color(1.0, 0.9, 0.2))

	# 4. If World Map is Open, draw continental border & pulsing location pin
	if is_world_map_open:
		var map_rect = Rect2(0, 0, map_width * cell_size, map_height * cell_size)
		draw_rect(map_rect, Color(1.0, 0.85, 0.2, 0.8), false, 4.0)
		var pulse = (sin(Time.get_ticks_msec() * 0.008) * 0.5 + 0.5)
		var pin_rad = cell_size * (2.5 + pulse * 1.5)
		draw_circle(player_pos, cell_size * 0.9, Color(1.0, 0.2, 0.2))
		draw_arc(player_pos, pin_rad, 0, TAU, 32, Color(1.0, 0.9, 0.2), 3.0)
		return

	# 5. Draw Entities (Player)
	var render_states = bridge.get_render_states()
	var num_entities = render_states.size() / 8
	for i in range(num_entities):
		var offset = i * 8
		var e_id = int(render_states[offset + 0])
		var e_type = int(render_states[offset + 1])
		if e_type == 0 or e_id == player_id:
			var sim_x = render_states[offset + 2]
			var sim_y = render_states[offset + 3]
			var sim_vx = render_states[offset + 4]
			var sim_vy = render_states[offset + 5]
			var sim_radius = render_states[offset + 6]
			
			var v_pos = Vector2(sim_x / 64.0 * cell_size, sim_y / 64.0 * cell_size)
			var v_rad = (sim_radius / 64.0) * cell_size
			if v_rad < 10.0: v_rad = 10.0
			
			draw_circle(v_pos + Vector2(0, v_rad * 0.4), v_rad * 0.95, Color(0, 0, 0, 0.45))
			var speed = Vector2(sim_vx, sim_vy).length()
			if speed > 0.1:
				var trail_pos = v_pos - player_facing * (v_rad * 0.85)
				var pulse = (sin(Time.get_ticks_msec() * 0.02) * 0.5 + 0.5) * 0.4 + 0.6
				draw_circle(trail_pos, v_rad * 0.45 * pulse, Color(0.2, 0.9, 1.0, 0.7))
				draw_circle(trail_pos, v_rad * 0.25 * pulse, Color.WHITE)
			draw_circle(v_pos, v_rad, Color(0.12, 0.15, 0.22))
			draw_arc(v_pos, v_rad, 0, TAU, 24, Color(0.15, 0.85, 0.95), 2.5)
			var s_left = v_pos + player_facing.rotated(1.57) * (v_rad * 0.75)
			var s_right = v_pos + player_facing.rotated(-1.57) * (v_rad * 0.75)
			draw_circle(s_left, v_rad * 0.35, Color(0.18, 0.22, 0.32))
			draw_circle(s_right, v_rad * 0.35, Color(0.18, 0.22, 0.32))
			draw_circle(v_pos, v_rad * 0.5, Color(0.20, 0.75, 0.95))
			draw_circle(v_pos, v_rad * 0.25, Color.WHITE)
			var v_tip = v_pos + player_facing * (v_rad * 1.25)
			var v_l = v_pos + player_facing.rotated(2.35) * (v_rad * 0.65)
			var v_r = v_pos + player_facing.rotated(-2.35) * (v_rad * 0.65)
			draw_colored_polygon([v_tip, v_l, v_r], Color(1.0, 0.88, 0.20))
			break
