extends Node2D

# ----------------------------------------------------------------------
# ExoFarm: High-Performance Ground-Level Map & Micro River Visualizer
# Optimized:
# - GPU Texture-Backed Blitting via Image & ImageTexture (0 draw-command bloat)
# - Viewport-Culled Local Overlays (Only zoomed-in visible tiles)
# - Zero Allocation Churn (Cached PackedArrays, Flat Vectors)
# - Comprehensive Telemetry & Interactive Testing Suite
# ----------------------------------------------------------------------

var bridge: SimBridge
var map_width: int = 0
var map_height: int = 0

# Factorio Scale: 1 Tile = 32.0 Pixels
var cell_size: float = 32.0

# Visual Analysis Modes:
# 0 = Micro Ground View (Rich ground art & animated river currents)
# 1 = Hydrology & Flow Vectors (D8 downhill flow direction arrows & pipe IDs)
# 2 = Bathymetry & Water Depth (Riverbed elevation & water depth contours)
# 3 = River Health & Drought (Water level % / Starvation heat map)
# 4 = Water Table Seepage (Groundwater diffusion into farmland)
# 5 = Collision & Passability (Solid impassable barriers vs wading water)
# 6 = Macro Continental Overview (DF/Caves of Qud world map)
var view_mode: int = 0
var view_mode_names = [
	"Micro Ground (Animated Water)",
	"Flow Vectors & Pipe IDs",
	"Bathymetry & Depth",
	"River Health & Drought",
	"Water Table Seepage",
	"Collision & Passability",
	"Macro Continental Map"
]

# Map Simulation Buffers
var surface_types: PackedByteArray
var mineral_types: PackedByteArray
var biomes: PackedByteArray
var elevation: PackedInt32Array
var rivers: PackedByteArray
var flow_targets: PackedInt32Array
var lakes: PackedByteArray
var structures: PackedByteArray
var crop_types: PackedByteArray
var crop_growth: PackedInt64Array
var water_table: PackedInt64Array
var soil_n: PackedInt64Array
var soil_p: PackedInt64Array
var soil_k: PackedInt64Array
var soil_compact: PackedInt64Array

# Fast GPU Texture Backing (Ultra-low RAM)
var map_image: Image
var map_texture: ImageTexture
var map_texture_dirty: bool = true

# River Network & Hydrology Graph Metadata
var flow_directions: PackedVector2Array # Flat C-level memory (no Variant heap allocations)
var river_pipe_indices: Array[int] = []
var current_nav_pipe_idx: int = 0

# Starter Site
var starter_spawn: Vector2i = Vector2i.ZERO
var current_seed: int = 42

# Player & Camera State
var player_id: int = 0
var player_pos: Vector2 = Vector2.ZERO
var player_vel: Vector2 = Vector2.ZERO
var player_facing: Vector2 = Vector2.DOWN
var camera_follow_player: bool = true
var is_world_map_open: bool = false
var show_grid: bool = true
var show_flow_vectors: bool = true
var show_telemetry: bool = true
var saved_local_cam_pos: Vector2 = Vector2.ZERO
var saved_local_zoom: Vector2 = Vector2(1.0, 1.0)

# Hover & Inspector State
var hovered_tile: Vector2i = Vector2i(-1, -1)
var hovered_world_pos: Vector2 = Vector2.ZERO

# UI Controls
var camera: Camera2D
var telemetry_panel: PanelContainer
var inspector_panel: PanelContainer
var nav_panel: PanelContainer
var quick_help_panel: PanelContainer

# Telemetry Labels
var fps_lbl: Label
var frame_time_lbl: Label
var memory_lbl: Label
var culling_lbl: Label
var hydro_summary_lbl: Label
var river_health_lbl: Label
var inspector_lbl: Label
var mode_btn: Button
var cam_btn: Button
var tele_btn: Button
var grid_btn: Button
var vec_btn: Button

# Telemetry Performance Tracking
var tick_counter: int = 0
var sim_step_time_us: float = 0.0
var rendered_tile_count: int = 0

func _ready() -> void:
	texture_filter = CanvasItem.TEXTURE_FILTER_NEAREST
	bridge = SimBridge.new()
	add_child(bridge)
	
	camera = Camera2D.new()
	camera.zoom = Vector2(1.0, 1.0)
	add_child(camera)
	
	_setup_ui()
	generate_new_map(current_seed)
	focus_on_spawn()

func _setup_ui() -> void:
	var canvas = CanvasLayer.new()
	canvas.name = "TelemetryCanvas"
	add_child(canvas)
	
	# -------------------------------------------------------------
	# 1. Left Toolbar: Mode, Navigation, and Action Controls
	# -------------------------------------------------------------
	var left_panel = PanelContainer.new()
	left_panel.position = Vector2(16, 16)
	canvas.add_child(left_panel)
	
	var left_vbox = VBoxContainer.new()
	left_vbox.custom_minimum_size = Vector2(280, 0)
	left_panel.add_child(left_vbox)
	
	var header = Label.new()
	header.text = "ExoFarm: Ground River Gen"
	header.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	left_vbox.add_child(header)
	
	var seed_lbl = Label.new()
	seed_lbl.name = "SeedLabel"
	seed_lbl.text = "Seed: %d" % current_seed
	left_vbox.add_child(seed_lbl)
	
	mode_btn = Button.new()
	mode_btn.text = "Mode: " + view_mode_names[view_mode]
	mode_btn.pressed.connect(self._on_cycle_mode_pressed)
	left_vbox.add_child(mode_btn)
	
	cam_btn = Button.new()
	cam_btn.text = "Camera: Follow Player [C]"
	cam_btn.pressed.connect(self._on_toggle_camera_pressed)
	left_vbox.add_child(cam_btn)
	
	tele_btn = Button.new()
	tele_btn.text = "Telemetry HUD: On [T]"
	tele_btn.pressed.connect(self._on_toggle_telemetry_pressed)
	left_vbox.add_child(tele_btn)
	
	grid_btn = Button.new()
	grid_btn.text = "Tile Grid: On [G]"
	grid_btn.pressed.connect(self._on_toggle_grid_pressed)
	left_vbox.add_child(grid_btn)
	
	vec_btn = Button.new()
	vec_btn.text = "Flow Vectors: On [V]"
	vec_btn.pressed.connect(self._on_toggle_flow_vectors_pressed)
	left_vbox.add_child(vec_btn)
	
	var sep1 = HSeparator.new()
	left_vbox.add_child(sep1)
	
	# River Navigation Jump Buttons
	var jump_lbl = Label.new()
	jump_lbl.text = "River Navigation Hotspots:"
	left_vbox.add_child(jump_lbl)
	
	var btn_next_riv = Button.new()
	btn_next_riv.text = "Next River Pipe [Tab]"
	btn_next_riv.pressed.connect(self.jump_to_next_river_pipe)
	left_vbox.add_child(btn_next_riv)
	
	var btn_spring = Button.new()
	btn_spring.text = "Jump to River Source [1]"
	btn_spring.pressed.connect(self.jump_to_river_source)
	left_vbox.add_child(btn_spring)
	
	var btn_confl = Button.new()
	btn_confl.text = "Jump to River Confluence [2]"
	btn_confl.pressed.connect(self.jump_to_river_confluence)
	left_vbox.add_child(btn_confl)
	
	var btn_mouth = Button.new()
	btn_mouth.text = "Jump to River Delta [3]"
	btn_mouth.pressed.connect(self.jump_to_river_mouth)
	left_vbox.add_child(btn_mouth)
	
	var btn_pond = Button.new()
	btn_pond.text = "Jump to Starter Base [4]"
	btn_pond.pressed.connect(self.focus_on_spawn)
	left_vbox.add_child(btn_pond)
	
	var btn_char = Button.new()
	btn_char.text = "Center on Character [F]"
	btn_char.pressed.connect(self.focus_on_player)
	left_vbox.add_child(btn_char)
	
	var sep2 = HSeparator.new()
	left_vbox.add_child(sep2)
	
	var pump_btn = Button.new()
	pump_btn.text = "Place/Remove Pump [P]"
	pump_btn.pressed.connect(self._on_toggle_pump_at_cursor)
	left_vbox.add_child(pump_btn)
	
	var bridge_btn = Button.new()
	bridge_btn.text = "Place/Remove Bridge [B]"
	bridge_btn.pressed.connect(self._on_toggle_bridge_at_cursor)
	left_vbox.add_child(bridge_btn)
	
	var regen_btn = Button.new()
	regen_btn.text = "Generate New Seed [R]"
	regen_btn.pressed.connect(self._on_regenerate_pressed)
	left_vbox.add_child(regen_btn)
	
	var switch_scene_btn = Button.new()
	switch_scene_btn.text = "Open Main Map Visualizer"
	switch_scene_btn.pressed.connect(self._on_switch_to_macro_scene)
	left_vbox.add_child(switch_scene_btn)

	var switch_map_gen_btn = Button.new()
	switch_map_gen_btn.text = "Open Map Generator Visualizer"
	switch_map_gen_btn.pressed.connect(func(): get_tree().change_scene_to_file("res://map_gen_visualizer.tscn"))
	left_vbox.add_child(switch_map_gen_btn)

	# -------------------------------------------------------------
	# 2. Right Telemetry Dashboard (Performance & Hydrology Telemetry)
	# -------------------------------------------------------------
	telemetry_panel = PanelContainer.new()
	telemetry_panel.position = Vector2(get_viewport_rect().size.x - 360, 16)
	telemetry_panel.set_anchors_preset(Control.PRESET_TOP_RIGHT)
	canvas.add_child(telemetry_panel)
	
	var tele_vbox = VBoxContainer.new()
	tele_vbox.custom_minimum_size = Vector2(330, 0)
	telemetry_panel.add_child(tele_vbox)
	
	var tele_title = Label.new()
	tele_title.text = "Usage & Engine Telemetry"
	tele_title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	tele_vbox.add_child(tele_title)
	
	var tele_sep1 = HSeparator.new()
	tele_vbox.add_child(tele_sep1)
	
	fps_lbl = Label.new()
	fps_lbl.text = "FPS: 60 | Frame: 16.6 ms"
	tele_vbox.add_child(fps_lbl)
	
	frame_time_lbl = Label.new()
	frame_time_lbl.text = "Sim Tick: 0 | Sim Time: 0 us"
	tele_vbox.add_child(frame_time_lbl)
	
	memory_lbl = Label.new()
	memory_lbl.text = "RAM: 0 MB | Peak: 0 MB"
	tele_vbox.add_child(memory_lbl)
	
	culling_lbl = Label.new()
	culling_lbl.text = "Render Mode: GPU Texture (1 Draw Call)"
	tele_vbox.add_child(culling_lbl)
	
	var tele_sep2 = HSeparator.new()
	tele_vbox.add_child(tele_sep2)
	
	var hydro_title = Label.new()
	hydro_title.text = "River Network Telemetry:"
	tele_vbox.add_child(hydro_title)
	
	hydro_summary_lbl = Label.new()
	hydro_summary_lbl.text = "River Pipes: 0 | Total Flow: 0.0"
	tele_vbox.add_child(hydro_summary_lbl)
	
	river_health_lbl = Label.new()
	river_health_lbl.text = "Hydrology Health: 100.0% Flowing"
	tele_vbox.add_child(river_health_lbl)
	
	var tele_sep3 = HSeparator.new()
	tele_vbox.add_child(tele_sep3)
	
	var insp_title = Label.new()
	insp_title.text = "Live Micro-Tile Inspector:"
	tele_vbox.add_child(insp_title)
	
	inspector_lbl = Label.new()
	inspector_lbl.text = "Hover over any tile to inspect..."
	inspector_lbl.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	tele_vbox.add_child(inspector_lbl)
	
	# -------------------------------------------------------------
	# 3. Bottom Hotkey Help Bar
	# -------------------------------------------------------------
	var help_panel = PanelContainer.new()
	help_panel.position = Vector2(16, get_viewport_rect().size.y - 44)
	help_panel.set_anchors_preset(Control.PRESET_BOTTOM_LEFT)
	canvas.add_child(help_panel)
	
	var help_lbl = Label.new()
	help_lbl.text = "[WASD] Move Mecha | [Wheel] Zoom | [Tab/1..4] Jump River | [P] Toggle Pump | [B] Place Bridge | [T] Telemetry | [V] Vectors | [R] New Seed"
	help_panel.add_child(help_lbl)

func generate_new_map(seed_val: int) -> void:
	current_seed = seed_val
	bridge.set_seed(current_seed)
	
	# Generate Ground-Level Micro Map (256x144, cell_size_fixed = 64 * 65536)
	bridge.generate_player_map(current_seed, 256, 144, 64 * 65536)
	
	map_width = bridge.get_map_width()
	map_height = bridge.get_map_height()
	starter_spawn = bridge.get_starter_spawn()
	is_world_map_open = false
	current_nav_pipe_idx = 0
	
	# Spawn Player Entity (Type 0)
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
	_compute_flow_vectors()
	_collect_river_pipes()
	_update_map_texture()
	_update_telemetry_hud()
	
	var seed_lbl = get_node_or_null("TelemetryCanvas/PanelContainer/VBoxContainer/SeedLabel") as Label
	if seed_lbl:
		seed_lbl.text = "Seed: %d" % current_seed
		
	queue_redraw()

func _refresh_data() -> void:
	surface_types = bridge.get_surface_types()
	mineral_types = bridge.get_mineral_types()
	biomes = bridge.get_biome()
	elevation = bridge.get_elevation()
	rivers = bridge.get_rivers()
	flow_targets = bridge.get_flow_targets()
	lakes = bridge.get_lakes()
	structures = bridge.get_structure_types()
	crop_types = bridge.get_crop_types()
	crop_growth = bridge.get_crop_growth()
	water_table = bridge.get_water_table()
	soil_n = bridge.get_soil_nitrogen()
	soil_p = bridge.get_soil_phosphorus()
	soil_k = bridge.get_soil_potassium()
	soil_compact = bridge.get_soil_compactness()

# Bakes tile colors into a single lightweight GPU texture (147 KB total memory)
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

# Computes smooth flow vectors using flat C-level memory (PackedVector2Array)
func _compute_flow_vectors() -> void:
	flow_directions.resize(map_width * map_height)
	
	for y in range(map_height):
		for x in range(map_width):
			var idx = y * map_width + x
			var dir = Vector2.ZERO
			
			if not flow_targets.is_empty():
				var tgt = flow_targets[idx]
				if tgt >= 0 and tgt < map_width * map_height and tgt != idx:
					var tx = tgt % map_width
					var ty = tgt / map_width
					dir = Vector2(tx - x, ty - y).normalized()
			
			flow_directions[idx] = dir

func _collect_river_pipes() -> void:
	river_pipe_indices.clear()
	var count = bridge.get_river_count()
	for i in range(count):
		river_pipe_indices.append(i)

func _physics_process(delta: float) -> void:
	var start_time_us = Time.get_ticks_usec()
	
	# 1. Player Input Frame
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
		
	# 2. Step Simulation Bridge
	bridge.step(move_x_fixed, move_y_fixed, aim_sim_x, aim_sim_y, actions)
	sim_step_time_us = float(Time.get_ticks_usec() - start_time_us)
	
	# 3. Read Player Entity Render State
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
			
	# 4. Camera Tracking
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

	# 5. Low-Frequency Telemetry & Inspector Update (Avoid continuous allocation churn)
	tick_counter += 1
	if tick_counter % 12 == 0:
		_update_hovered_tile()
		_update_telemetry_hud()
		
	queue_redraw()

func _update_hovered_tile() -> void:
	hovered_world_pos = get_global_mouse_position()
	var tx = int(floor(hovered_world_pos.x / cell_size))
	var ty = int(floor(hovered_world_pos.y / cell_size))
	if tx >= 0 and tx < map_width and ty >= 0 and ty < map_height:
		hovered_tile = Vector2i(tx, ty)
	else:
		hovered_tile = Vector2i(-1, -1)

func _update_telemetry_hud() -> void:
	if not show_telemetry or not telemetry_panel.visible:
		return
		
	# Performance Metrics
	var fps = Engine.get_frames_per_second()
	var dt_ms = (1.0 / fps) * 1000.0 if fps > 0 else 0.0
	fps_lbl.text = "FPS: %d (%.1f ms) | Tick: %d" % [fps, dt_ms, bridge.current_tick()]
	
	if fps >= 55:
		fps_lbl.modulate = Color.GREEN
	elif fps >= 30:
		fps_lbl.modulate = Color.YELLOW
	else:
		fps_lbl.modulate = Color.RED
		
	var hash_val = bridge.get_state_hash()
	frame_time_lbl.text = "Sim Step: %.0f us | Hash: 0x%X" % [sim_step_time_us, hash_val]
	
	var static_mem = OS.get_static_memory_usage() / (1024.0 * 1024.0)
	var peak_mem = OS.get_static_memory_peak_usage() / (1024.0 * 1024.0)
	memory_lbl.text = "RAM: %.1f MB | Peak: %.1f MB (GPU Texture)" % [static_mem, peak_mem]
	
	culling_lbl.text = "Render: GPU Texture (1 Draw Call) + LOD (%d tiles)" % rendered_tile_count
	
	# Hydrology Network Metrics
	var riv_count = bridge.get_river_count()
	var flow_rates = bridge.get_river_flow_rates()
	var water_levels = bridge.get_river_water_levels()
	var pump_rates = bridge.get_river_pump_rates()
	
	var total_flow: float = 0.0
	var total_pump: float = 0.0
	var flowing_count: int = 0
	
	for i in range(riv_count):
		var flow = float(flow_rates[i]) / 65536.0 if not flow_rates.is_empty() else 0.0
		var lvl = float(water_levels[i]) / 65536.0 if not water_levels.is_empty() else 100.0
		var p_rate = float(pump_rates[i]) / 65536.0 if not pump_rates.is_empty() else 0.0
		total_flow += flow
		total_pump += p_rate
		if lvl > 10.0:
			flowing_count += 1
			
	var health_pct = (float(flowing_count) / float(riv_count) * 100.0) if riv_count > 0 else 100.0
	hydro_summary_lbl.text = "Active Rivers: %d Pipes | Total Flow: %.1f u/t\nExtraction: %.1f u/t" % [
		riv_count, total_flow, total_pump
	]
	
	river_health_lbl.text = "Network Health: %.1f%% Flowing (%d / %d)" % [
		health_pct, flowing_count, riv_count
	]
	if health_pct > 80.0:
		river_health_lbl.modulate = Color.CYAN
	elif health_pct > 40.0:
		river_health_lbl.modulate = Color.YELLOW
	else:
		river_health_lbl.modulate = Color.TOMATO
		
	# Live Inspector for Hovered Tile
	if hovered_tile.x >= 0 and hovered_tile.y >= 0:
		var idx = hovered_tile.y * map_width + hovered_tile.x
		var st_name = _get_surface_name(surface_types[idx] if not surface_types.is_empty() else 0)
		var b_name = _get_biome_name(biomes[idx] if not biomes.is_empty() else 0)
		var el_val = float(elevation[idx]) / 65536.0 if not elevation.is_empty() else 0.0
		var riv_t = rivers[idx] if not rivers.is_empty() else 0
		var riv_str = "None"
		if riv_t == 1: riv_str = "Shallow Brook"
		elif riv_t == 2: riv_str = "Major Deep River"
		elif not lakes.is_empty() and lakes[idx] != 0: riv_str = "Lake Basin"
		
		var struct_str = "None"
		if not structures.is_empty():
			match structures[idx]:
				1: struct_str = "Well"
				2: struct_str = "Mechanical Pump"
				3: struct_str = "Channel"
				4: struct_str = "Sprinkler (Small)"
				5: struct_str = "Sprinkler (Pivot)"
				6: struct_str = "Wooden Bridge"
				
		var wt = float(water_table[idx]) / 65536.0 if not water_table.is_empty() else 0.0
		var flow_v = flow_directions[idx] if not flow_directions.is_empty() else Vector2.ZERO
		var flow_angle = rad_to_deg(flow_v.angle())
		if flow_angle < 0: flow_angle += 360.0
		
		inspector_lbl.text = "Tile: (%d, %d)\nSurface: %s\nBiome: %s | Elevation: %.1f m\nRiver Reach: %s\nStructure: %s\nWater Table: %.1f%%\nFlow Vector: (%.2f, %.2f) [%.0f°]" % [
			hovered_tile.x, hovered_tile.y, st_name, b_name, el_val, riv_str, struct_str, wt, flow_v.x, flow_v.y, flow_angle
		]
	else:
		inspector_lbl.text = "Hover over any tile to inspect micro details."

func _get_surface_name(st: int) -> String:
	match st:
		0: return "Dry Soil (100% Speed)"
		1: return "Tilled Soil (100% Speed)"
		2: return "Mud Bank (80% Speed)"
		3: return "Shallow Water (65% Speed, Wadeable)"
		4: return "Deep Water (Wall Barrier, Impassable)"
		5: return "Rock Cliff (Wall Barrier, Impassable)"
		6: return "Nitrogen Deposit (Guano)"
		7: return "Phosphorus Deposit (Apatite)"
		8: return "Potassium Deposit (Potash)"
		_: return "Terrain"

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

# -------------------------------------------------------------
# River Navigation Jump Actions
# -------------------------------------------------------------
func jump_to_next_river_pipe() -> void:
	if river_pipe_indices.is_empty():
		return
	current_nav_pipe_idx = (current_nav_pipe_idx + 1) % river_pipe_indices.size()
	for y in range(map_height):
		for x in range(map_width):
			var idx = y * map_width + x
			if rivers[idx] > 0 and (lakes.is_empty() or lakes[idx] == 0):
				camera.position = Vector2((x + 0.5) * cell_size, (y + 0.5) * cell_size)
				return

func jump_to_river_source() -> void:
	var highest_el: int = -999999
	var best_pos: Vector2 = Vector2.ZERO
	for y in range(map_height):
		for x in range(map_width):
			var idx = y * map_width + x
			if rivers[idx] > 0 and elevation[idx] > highest_el:
				highest_el = elevation[idx]
				best_pos = Vector2((x + 0.5) * cell_size, (y + 0.5) * cell_size)
	if best_pos != Vector2.ZERO:
		camera.position = best_pos

func jump_to_river_confluence() -> void:
	var best_pos = Vector2.ZERO
	var max_adj = 0
	var dxs = [1, -1, 0, 0, 1, -1, 1, -1]
	var dys = [0, 0, 1, -1, 1, 1, -1, -1]
	
	for y in range(1, map_height - 1):
		for x in range(1, map_width - 1):
			var idx = y * map_width + x
			if rivers[idx] > 0:
				var adj = 0
				for n in range(8):
					var ni = (y + dys[n]) * map_width + (x + dxs[n])
					if rivers[ni] > 0: adj += 1
				if adj > max_adj:
					max_adj = adj
					best_pos = Vector2((x + 0.5) * cell_size, (y + 0.5) * cell_size)
	if best_pos != Vector2.ZERO:
		camera.position = best_pos

func jump_to_river_mouth() -> void:
	var lowest_el = 999999
	var best_pos = Vector2.ZERO
	for y in range(map_height):
		for x in range(map_width):
			var idx = y * map_width + x
			if rivers[idx] > 0 and elevation[idx] > 0 and elevation[idx] < lowest_el:
				lowest_el = elevation[idx]
				best_pos = Vector2((x + 0.5) * cell_size, (y + 0.5) * cell_size)
	if best_pos != Vector2.ZERO:
		camera.position = best_pos

func focus_on_spawn() -> void:
	camera.position = Vector2((starter_spawn.x + 0.5) * cell_size, (starter_spawn.y + 0.5) * cell_size)

func focus_on_player() -> void:
	camera.position = player_pos

# -------------------------------------------------------------
# Interactive River Editing Actions (Pump & Bridge)
# -------------------------------------------------------------
func _on_toggle_pump_at_cursor() -> void:
	if hovered_tile.x >= 0 and hovered_tile.y >= 0:
		var idx = hovered_tile.y * map_width + hovered_tile.x
		if not structures.is_empty() and structures[idx] == 2:
			bridge.remove_structure(hovered_tile.x, hovered_tile.y)
		else:
			bridge.place_structure(hovered_tile.x, hovered_tile.y, 2)
		_refresh_data()
		_update_map_texture()
		_update_telemetry_hud()
		queue_redraw()

func _on_toggle_bridge_at_cursor() -> void:
	if hovered_tile.x >= 0 and hovered_tile.y >= 0:
		var idx = hovered_tile.y * map_width + hovered_tile.x
		if not structures.is_empty() and structures[idx] == 6:
			bridge.remove_structure(hovered_tile.x, hovered_tile.y)
		else:
			bridge.place_structure(hovered_tile.x, hovered_tile.y, 6)
		_refresh_data()
		_update_map_texture()
		_update_telemetry_hud()
		queue_redraw()

func _on_cycle_mode_pressed() -> void:
	view_mode = (view_mode + 1) % view_mode_names.size()
	mode_btn.text = "Mode: " + view_mode_names[view_mode]
	_update_map_texture()
	queue_redraw()

func _on_toggle_camera_pressed() -> void:
	camera_follow_player = not camera_follow_player
	cam_btn.text = "Camera: " + ("Follow Player [C]" if camera_follow_player else "Freecam Pan [C]")

func _on_toggle_telemetry_pressed() -> void:
	show_telemetry = not show_telemetry
	telemetry_panel.visible = show_telemetry
	tele_btn.text = "Telemetry HUD: " + ("On [T]" if show_telemetry else "Off [T]")

func _on_toggle_grid_pressed() -> void:
	show_grid = not show_grid
	grid_btn.text = "Tile Grid: " + ("On [G]" if show_grid else "Off [G]")
	queue_redraw()

func _on_toggle_flow_vectors_pressed() -> void:
	show_flow_vectors = not show_flow_vectors
	vec_btn.text = "Flow Vectors: " + ("On [V]" if show_flow_vectors else "Off [V]")
	queue_redraw()

func _on_regenerate_pressed() -> void:
	randomize()
	var new_seed = randi() % 1000000
	generate_new_map(new_seed)
	focus_on_spawn()

func _on_switch_to_macro_scene() -> void:
	get_tree().change_scene_to_file("res://map_visualizer.tscn")

# -------------------------------------------------------------
# Input Handling
# -------------------------------------------------------------
func _input(event: InputEvent) -> void:
	if event is InputEventKey and event.is_pressed() and not event.is_echo():
		if event.keycode == KEY_TAB:
			jump_to_next_river_pipe()
		elif event.keycode == KEY_1:
			jump_to_river_source()
		elif event.keycode == KEY_2:
			jump_to_river_confluence()
		elif event.keycode == KEY_3:
			jump_to_river_mouth()
		elif event.keycode == KEY_4:
			focus_on_spawn()
		elif event.keycode == KEY_F:
			focus_on_player()
		elif event.keycode == KEY_C:
			_on_toggle_camera_pressed()
		elif event.keycode == KEY_T:
			_on_toggle_telemetry_pressed()
		elif event.keycode == KEY_G:
			_on_toggle_grid_pressed()
		elif event.keycode == KEY_V:
			_on_toggle_flow_vectors_pressed()
		elif event.keycode == KEY_P:
			_on_toggle_pump_at_cursor()
		elif event.keycode == KEY_B:
			_on_toggle_bridge_at_cursor()
		elif event.keycode == KEY_R:
			_on_regenerate_pressed()
		elif event.keycode == KEY_M:
			view_mode = 6 if view_mode != 6 else 0
			mode_btn.text = "Mode: " + view_mode_names[view_mode]
			_update_map_texture()
			queue_redraw()
	elif event is InputEventMouseButton:
		if event.button_index == MOUSE_BUTTON_WHEEL_UP:
			camera.zoom = (camera.zoom * 1.15).clamp(Vector2(0.15, 0.15), Vector2(4.0, 4.0))
		elif event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			camera.zoom = (camera.zoom * 0.85).clamp(Vector2(0.15, 0.15), Vector2(4.0, 4.0))
		elif event.button_index == MOUSE_BUTTON_RIGHT and event.is_pressed():
			_on_toggle_pump_at_cursor()

# -------------------------------------------------------------
# Color Mapping for Texture Generation
# -------------------------------------------------------------
func _get_tile_draw_color(x: int, y: int) -> Color:
	var idx = y * map_width + x
	var st = surface_types[idx]
	var b = biomes[idx]
	var is_riv = rivers[idx]
	var is_lk = lakes[idx] if not lakes.is_empty() else 0
	var struct_t = structures[idx] if not structures.is_empty() else 0
	
	if view_mode == 0:
		if struct_t == 6: return Color(0.55, 0.35, 0.15)
		elif struct_t == 2: return Color(0.95, 0.85, 0.15)
		match st:
			0:
				match b:
					1: return Color(0.85, 0.90, 0.95)
					2: return Color(0.88, 0.76, 0.45)
					3: return Color(0.42, 0.72, 0.28)
					4: return Color(0.22, 0.52, 0.26)
					5: return Color(0.02, 0.40, 0.18)
					6: return Color(0.30, 0.38, 0.24)
					7: return Color(0.55, 0.55, 0.55)
					_: return Color(0.35, 0.55, 0.25)
			1: return Color(0.35, 0.25, 0.15)
			2: return Color(0.32, 0.24, 0.16)
			3: return Color(0.18, 0.70, 0.88)
			4: return Color(0.06, 0.28, 0.72)
			5: return Color(0.25, 0.25, 0.25)
			6: return Color(0.25, 0.88, 0.82)
			7: return Color(0.95, 0.55, 0.10)
			8: return Color(0.75, 0.35, 0.85)
			_: return Color.BLACK
	elif view_mode == 1:
		if is_riv == 2: return Color(0.05, 0.35, 0.85)
		elif is_riv == 1: return Color(0.20, 0.70, 0.95)
		elif is_lk != 0: return Color(0.04, 0.45, 0.75)
		elif elevation[idx] <= 0: return Color(0.04, 0.12, 0.30)
		else:
			var el_n = clamp(float(elevation[idx]) / (80.0 * 65536.0), 0.0, 1.0)
			return Color(0.15 + el_n * 0.25, 0.18 + el_n * 0.25, 0.15 + el_n * 0.25)
	elif view_mode == 2:
		var el_f = float(elevation[idx]) / 65536.0
		if el_f <= 0:
			var ocean_d = clamp(abs(el_f) / 25.0, 0.0, 1.0)
			return Color(0.0, 0.05 + ocean_d * 0.15, 0.25 + ocean_d * 0.50)
		elif is_lk != 0 or is_riv == 2: return Color(0.05, 0.25, 0.75)
		elif is_riv == 1: return Color(0.20, 0.75, 0.95)
		else:
			var h_norm = clamp(el_f / 100.0, 0.0, 1.0)
			return Color(h_norm, h_norm * 0.9, h_norm * 0.7)
	elif view_mode == 3:
		if is_riv > 0: return Color(0.05, 0.85, 0.95)
		elif is_lk != 0: return Color(0.10, 0.50, 0.90)
		elif elevation[idx] <= 0: return Color(0.05, 0.10, 0.25)
		else: return Color(0.20, 0.20, 0.20)
	elif view_mode == 4:
		var wt_val = float(water_table[idx]) / 65536.0 if not water_table.is_empty() else 0.0
		var wt_norm = clamp(wt_val / 100.0, 0.0, 1.0)
		if st == 4: return Color(0.05, 0.20, 0.60)
		elif st == 3: return Color(0.15, 0.55, 0.85)
		elif st == 2: return Color(0.35, 0.25, 0.15)
		else: return Color(0.85 - wt_norm * 0.65, 0.70 + wt_norm * 0.25, 0.35 + wt_norm * 0.60)
	elif view_mode == 5:
		if st == 4 or st == 5: return Color(0.85, 0.20, 0.20)
		elif st == 3: return Color(0.90, 0.85, 0.20)
		elif struct_t == 6: return Color(0.20, 0.85, 0.30)
		elif st == 2: return Color(0.70, 0.50, 0.20)
		else: return Color(0.25, 0.35, 0.25)
	elif view_mode == 6:
		var col = Color.BLACK
		match b:
			0: col = Color(0.08, 0.18, 0.45)
			1: col = Color(0.85, 0.90, 0.95)
			2: col = Color(0.92, 0.78, 0.46)
			3: col = Color(0.55, 0.78, 0.38)
			4: col = Color(0.20, 0.50, 0.25)
			5: col = Color(0.00, 0.40, 0.18)
			6: col = Color(0.30, 0.38, 0.25)
			7: col = Color(0.50, 0.50, 0.50)
		var el_val = float(elevation[idx]) / 65536.0
		var factor = 0.75 + 0.50 * clamp(el_val / 80.0, 0.0, 1.0) if el_val > 0.0 else 0.50
		col = col * factor
		if is_lk != 0: col = Color(0.05, 0.40, 0.70)
		elif is_riv == 2: col = Color(0.04, 0.25, 0.75)
		elif is_riv == 1: col = Color(0.20, 0.70, 0.95)
		return col
	return Color.BLACK

# -------------------------------------------------------------
# Ultra-Low Memory GPU Texture Drawing Pipeline
# -------------------------------------------------------------
func _draw() -> void:
	if map_width <= 0 or map_height <= 0 or map_texture == null:
		return
		
	# 1. Single GPU Texture Draw Call (Renders all 36,864 tiles instantly with ZERO heap allocations)
	draw_texture_rect(map_texture, Rect2(0, 0, map_width * cell_size, map_height * cell_size), false)
	
	var vp_size = get_viewport_rect().size
	var half_vp = (vp_size / 2.0) / camera.zoom
	var top_left = camera.position - half_vp
	var bot_right = camera.position + half_vp
	
	var min_tx = int(clamp(floor(top_left.x / cell_size) - 1, 0, map_width - 1))
	var max_tx = int(clamp(ceil(bot_right.x / cell_size) + 1, 0, map_width - 1))
	var min_ty = int(clamp(floor(top_left.y / cell_size) - 1, 0, map_height - 1))
	var max_ty = int(clamp(ceil(bot_right.y / cell_size) + 1, 0, map_height - 1))
	
	rendered_tile_count = (max_tx - min_tx + 1) * (max_ty - min_ty + 1)
	var zoom_lvl = camera.zoom.x
	var is_detail_lod = zoom_lvl >= 0.75 and view_mode != 6
	var time_sec = Time.get_ticks_msec() * 0.001

	# 2. Viewport-Culled Local Overlays (ONLY executed for the few visible tiles when zoomed in)
	if is_detail_lod:
		for y in range(min_ty, max_ty + 1):
			for x in range(min_tx, max_tx + 1):
				var idx = y * map_width + x
				var rect = Rect2(x * cell_size, y * cell_size, cell_size, cell_size)
				var st = surface_types[idx]
				var struct_t = structures[idx] if not structures.is_empty() else 0
				
				if view_mode == 0:
					# Animated Water Currents
					if (st == 3 or st == 4) and struct_t != 6:
						var flow_v = flow_directions[idx] if not flow_directions.is_empty() else Vector2.ZERO
						var p1 = rect.position + Vector2(6, 6)
						var p2 = p1 + flow_v * (cell_size * 0.55)
						draw_line(p1, p2, Color(0.65, 0.90, 1.0, 0.40), 1.4)
					
					# Wooden Bridge Planks
					if struct_t == 6:
						var py = rect.position.y
						var px = rect.position.x
						draw_line(Vector2(px, py + 8), Vector2(px + cell_size, py + 8), Color(0.30, 0.18, 0.08), 2.0)
						draw_line(Vector2(px, py + 16), Vector2(px + cell_size, py + 16), Color(0.30, 0.18, 0.08), 2.0)
						draw_line(Vector2(px, py + 24), Vector2(px + cell_size, py + 24), Color(0.30, 0.18, 0.08), 2.0)
					
					# Mechanical Pump Housing
					if struct_t == 2:
						var p_center = rect.position + rect.size * 0.5
						draw_circle(p_center, cell_size * 0.38, Color(0.95, 0.85, 0.15))
						draw_circle(p_center, cell_size * 0.20, Color(0.15, 0.15, 0.15))
						
					# Grid lines
					if show_grid and zoom_lvl >= 0.9:
						draw_rect(rect, Color(0, 0, 0, 0.12), false, 1.0)
						
				elif view_mode == 1:
					if show_flow_vectors and (rivers[idx] > 0 or lakes[idx] > 0):
						var flow_v = flow_directions[idx] if not flow_directions.is_empty() else Vector2.ZERO
						if flow_v.length_squared() > 0.01:
							var c = rect.position + rect.size * 0.5
							var tip = c + flow_v * (cell_size * 0.42)
							draw_line(c, tip, Color.YELLOW, 2.0)

	# 3. Draw Hovered Tile Highlight Box
	if hovered_tile.x >= 0 and hovered_tile.y >= 0:
		var h_rect = Rect2(hovered_tile.x * cell_size, hovered_tile.y * cell_size, cell_size, cell_size)
		draw_rect(h_rect, Color(1.0, 1.0, 0.2, 0.8), false, 2.5)

	# 4. Draw Starter Base Reticle
	var sp_center = Vector2((starter_spawn.x + 0.5) * cell_size, (starter_spawn.y + 0.5) * cell_size)
	draw_arc(sp_center, cell_size * 2.5, 0, TAU, 32, Color(1.0, 0.85, 0.2, 0.6), 2.0)

	# 5. Draw Entities (Player Mecha Character)
	var render_states = bridge.get_render_states()
	var num_entities = render_states.size() / 8
	for i in range(num_entities):
		var offset = i * 8
		var e_id = int(render_states[offset + 0])
		var e_type = int(render_states[offset + 1])
		var sim_x = render_states[offset + 2]
		var sim_y = render_states[offset + 3]
		var sim_vx = render_states[offset + 4]
		var sim_vy = render_states[offset + 5]
		var sim_radius = render_states[offset + 6]
		
		var v_pos = Vector2(sim_x / 64.0 * cell_size, sim_y / 64.0 * cell_size)
		var v_rad = (sim_radius / 64.0) * cell_size
		if v_rad < 10.0: v_rad = 10.0
		
		if e_type == 0:
			draw_circle(v_pos + Vector2(0, v_rad * 0.4), v_rad * 0.95, Color(0, 0, 0, 0.45))
			var speed = Vector2(sim_vx, sim_vy).length()
			if speed > 0.1:
				var trail = v_pos - player_facing * (v_rad * 0.85)
				var pulse = (sin(time_sec * 20.0) * 0.5 + 0.5) * 0.4 + 0.6
				draw_circle(trail, v_rad * 0.45 * pulse, Color(0.2, 0.9, 1.0, 0.7))
				draw_circle(trail, v_rad * 0.25 * pulse, Color.WHITE)
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
