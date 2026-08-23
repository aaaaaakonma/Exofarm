extends Node2D

# ----------------------------------------------------------------------
# ExoFarm: Factorio-Scale Continental Sim & Map Visualizer
# Optimized:
# - GPU Texture Blitting via Image & ImageTexture (0 draw-command bloat)
# - Viewport-Culled Local Overlays (Only zoomed-in visible tiles)
# - Zero Allocation Churn (Cached PackedArrays)
# ----------------------------------------------------------------------

var bridge: SimBridge
var map_width: int = 0
var map_height: int = 0

# Factorio-esque Scale: 1 Tile = 32.0 Pixels
var cell_size: float = 32.0

# Visualization Modes:
# 0 = Micro Surface (Local Player Gameplay & Collisions)
# 1 = Macro Biomes (Continental Biome Map)
# 2 = Minerals N-P-K (Factorio-style resource patches)
# 3 = Heightmap (Continental Elevation)
# 4 = Hydrology & Water Table
var view_mode: int = 0
var view_mode_names = [
	"Micro Surface (Gameplay)",
	"Macro Biomes",
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
var is_pumping: bool = false
var pump_placed: bool = false

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
var status_lbl: Label
var player_lbl: Label
var biome_lbl: Label
var pond_lbl: Label
var view_btn: Button
var cam_btn: Button
var map_btn: Button
var grid_btn: Button

var tick_counter: int = 0

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
	add_child(canvas)
	
	var panel = PanelContainer.new()
	panel.position = Vector2(20, 20)
	canvas.add_child(panel)
	
	var vbox = VBoxContainer.new()
	vbox.custom_minimum_size = Vector2(310, 0)
	panel.add_child(vbox)
	
	var title_lbl = Label.new()
	title_lbl.text = "ExoFarm: Factorio-Scale Sim"
	title_lbl.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	vbox.add_child(title_lbl)
	
	var seed_lbl = Label.new()
	seed_lbl.name = "SeedLabel"
	seed_lbl.text = "Seed: " + str(current_seed)
	vbox.add_child(seed_lbl)
	
	player_lbl = Label.new()
	player_lbl.text = "Player: (0.0, 0.0) | Speed: 0.0"
	vbox.add_child(player_lbl)
	
	biome_lbl = Label.new()
	biome_lbl.text = "Region Biome: Grassland"
	vbox.add_child(biome_lbl)
	
	pond_lbl = Label.new()
	pond_lbl.text = "Starter Pond: 30,000 / 30,000 (100.0%)"
	vbox.add_child(pond_lbl)
	
	status_lbl = Label.new()
	status_lbl.text = "Spawn: (0, 0)"
	vbox.add_child(status_lbl)
	
	map_btn = Button.new()
	map_btn.text = "Toggle World Map [M]"
	map_btn.pressed.connect(self._on_toggle_world_map_pressed)
	vbox.add_child(map_btn)
	
	view_btn = Button.new()
	view_btn.text = "Mode: " + view_mode_names[view_mode]
	view_btn.pressed.connect(self._on_toggle_view_pressed)
	vbox.add_child(view_btn)
	
	cam_btn = Button.new()
	cam_btn.text = "Camera: Follow Player [C]"
	cam_btn.pressed.connect(self._on_toggle_camera_pressed)
	vbox.add_child(cam_btn)
	
	grid_btn = Button.new()
	grid_btn.text = "Tile Grid: On [G]"
	grid_btn.pressed.connect(self._on_toggle_grid_pressed)
	vbox.add_child(grid_btn)
	
	var pump_btn = Button.new()
	pump_btn.text = "Toggle Pump on Pond [P]"
	pump_btn.pressed.connect(self._on_toggle_pump_pressed)
	vbox.add_child(pump_btn)
	
	var riv_pump_btn = Button.new()
	riv_pump_btn.text = "Toggle Pump on River [N]"
	riv_pump_btn.pressed.connect(self._on_toggle_river_pump_pressed)
	vbox.add_child(riv_pump_btn)
	
	var bridge_btn = Button.new()
	bridge_btn.text = "Place Bridge on River [B]"
	bridge_btn.pressed.connect(self._on_place_bridge_pressed)
	vbox.add_child(bridge_btn)
	
	var focus_btn = Button.new()
	focus_btn.text = "Center on Player [F]"
	focus_btn.pressed.connect(self.focus_on_player)
	vbox.add_child(focus_btn)
	
	var regen_btn = Button.new()
	regen_btn.text = "Generate New Seed [R]"
	regen_btn.pressed.connect(self._on_regenerate_pressed)
	vbox.add_child(regen_btn)
	
	var micro_scene_btn = Button.new()
	micro_scene_btn.text = "Open Ground River Visualizer"
	micro_scene_btn.pressed.connect(func(): get_tree().change_scene_to_file("res://river_micro_visualizer.tscn"))
	vbox.add_child(micro_scene_btn)
	
	var map_gen_scene_btn = Button.new()
	map_gen_scene_btn.text = "Open Map Generator Visualizer"
	map_gen_scene_btn.pressed.connect(func(): get_tree().change_scene_to_file("res://map_gen_visualizer.tscn"))
	vbox.add_child(map_gen_scene_btn)
	
	var help_lbl = Label.new()
	help_lbl.text = "\nControls:\n[WASD / Arrows] Move Character\n[M] Toggle World Map\n[G] Toggle Tile Grid\n[C] Toggle Camera Follow\n[1..5] Switch View Modes\n[P] Toggle Pond Pump\n[N] Toggle River Pump\n[B] Place Bridge\n[F] Center on Player\n[R] Random Seed\n[Mouse Wheel] Zoom In/Out"
	vbox.add_child(help_lbl)

func generate_new_map(seed_val: int) -> void:
	current_seed = seed_val
	bridge.set_seed(current_seed)
	
	bridge.generate_player_map(current_seed, 256, 144, 64 * 65536)
	
	map_width = bridge.get_map_width()
	map_height = bridge.get_map_height()
	starter_spawn = bridge.get_starter_spawn()
	pump_placed = false
	is_pumping = false
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
	
	var seed_lbl = get_node_or_null("CanvasLayer/PanelContainer/VBoxContainer/SeedLabel") as Label
	if seed_lbl:
		seed_lbl.text = "Seed: " + str(current_seed)
	
	status_lbl.text = "Spawn: (%d, %d)" % [starter_spawn.x, starter_spawn.y]
	_update_pond_label()
	
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

func _update_pond_label() -> void:
	var cur = bridge.get_starter_pond_volume() / 65536
	var max_v = bridge.get_starter_pond_max() / 65536
	var pct = bridge.get_starter_pond_percent()
	var state_str = " (Pumping)" if is_pumping else ""
	var num_rivers = bridge.get_river_count()
	var riv_levels = bridge.get_river_water_levels()
	var avg_riv_pct = 100.0
	if num_rivers > 0 and not riv_levels.is_empty():
		var total_pct = 0.0
		for lvl in riv_levels:
			total_pct += float(lvl) / 65536.0
		avg_riv_pct = total_pct / float(num_rivers)
	pond_lbl.text = "Starter Pond: %s / %s (%.1f%%)%s\nRiver Pipes: %d | Avg Water Level: %.1f%%" % [
		str(cur), str(max_v), pct, state_str, num_rivers, avg_riv_pct
	]

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
		view_btn.text = "Mode: Macro World Map"
		camera.position = Vector2(map_width * cell_size / 2.0, map_height * cell_size / 2.0)
		var fit_zoom_x = 1800.0 / (map_width * cell_size)
		var fit_zoom_y = 1000.0 / (map_height * cell_size)
		camera.zoom = Vector2(min(fit_zoom_x, fit_zoom_y), min(fit_zoom_x, fit_zoom_y))
	else:
		view_mode = 0
		view_btn.text = "Mode: " + view_mode_names[view_mode]
		camera.position = saved_local_cam_pos if saved_local_cam_pos != Vector2.ZERO else player_pos
		camera.zoom = saved_local_zoom
	_update_map_texture()
	queue_redraw()

func _on_toggle_view_pressed() -> void:
	view_mode = (view_mode + 1) % view_mode_names.size()
	view_btn.text = "Mode: " + view_mode_names[view_mode]
	_update_map_texture()
	queue_redraw()

func _on_toggle_pump_pressed() -> void:
	var pond_tile_x = starter_spawn.x + 6
	var pond_tile_y = starter_spawn.y + 6
	
	if not pump_placed:
		bridge.place_structure(pond_tile_x, pond_tile_y, 2)
		pump_placed = true
		is_pumping = true
	else:
		if is_pumping:
			bridge.remove_structure(pond_tile_x, pond_tile_y)
			pump_placed = false
			is_pumping = false
		else:
			bridge.place_structure(pond_tile_x, pond_tile_y, 2)
			pump_placed = true
			is_pumping = true
			
	_refresh_data()
	_update_map_texture()
	_update_pond_label()
	queue_redraw()

func _on_toggle_river_pump_pressed() -> void:
	var placed = false
	for r in range(1, 40):
		for dy in range(-r, r + 1):
			for dx in range(-r, r + 1):
				var nx = starter_spawn.x + dx
				var ny = starter_spawn.y + dy
				if nx >= 0 and nx < map_width and ny >= 0 and ny < map_height:
					var idx = ny * map_width + nx
					if rivers[idx] > 0 and (lakes.is_empty() or lakes[idx] == 0):
						if structures[idx] == 2:
							bridge.remove_structure(nx, ny)
							placed = true
							break
						elif structures[idx] == 0:
							bridge.place_structure(nx, ny, 2)
							placed = true
							break
			if placed: break
		if placed: break
		
	_refresh_data()
	_update_map_texture()
	_update_pond_label()
	queue_redraw()

func _on_place_bridge_pressed() -> void:
	var placed = false
	for r in range(1, 20):
		for dy in range(-r, r + 1):
			for dx in range(-r, r + 1):
				var nx = starter_spawn.x + dx
				var ny = starter_spawn.y + dy
				if nx >= 0 and nx < map_width and ny >= 0 and ny < map_height:
					var idx = ny * map_width + nx
					if surface_types[idx] == 4 and structures[idx] == 0:
						bridge.place_structure(nx, ny, 6)
						placed = true
						break
			if placed: break
		if placed: break
		
	_refresh_data()
	_update_map_texture()
	queue_redraw()

func _on_regenerate_pressed() -> void:
	randomize()
	var new_seed = randi() % 1000000
	generate_new_map(new_seed)
	focus_on_spawn()

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
	if tick_counter % 15 == 0:
		_update_pond_label()
		_update_player_ui()
		
	queue_redraw()

func _update_player_ui() -> void:
	var tile_x = int(player_pos.x / cell_size)
	var tile_y = int(player_pos.y / cell_size)
	
	var surf_name = "Dry Soil (100% Speed)"
	var b_name = "Grassland"
	if tile_x >= 0 and tile_x < map_width and tile_y >= 0 and tile_y < map_height:
		var idx = tile_y * map_width + tile_x
		if not biomes.is_empty():
			match biomes[idx]:
				0: b_name = "Ocean"
				1: b_name = "Tundra / Glacier"
				2: b_name = "Desert"
				3: b_name = "Grassland / Plains"
				4: b_name = "Temperate Forest"
				5: b_name = "Tropical Rainforest"
				6: b_name = "Swamp / Wetlands"
				7: b_name = "Mountain Range"
				
		if not structures.is_empty() and structures[idx] == 6:
			surf_name = "Wooden Bridge (110% Speed)"
		elif not surface_types.is_empty():
			var st = surface_types[idx]
			match st:
				1: surf_name = "Tilled Soil (100% Speed)"
				2: surf_name = "Mud Bank (80% Speed)"
				3: surf_name = "Shallow Water (65% Speed)"
				4: surf_name = "Deep Water (Wall Barrier)"
				5: surf_name = "Rock Cliff (Wall Barrier)"
				6: surf_name = "Nitrogen Deposit (Guano)"
				7: surf_name = "Phosphorus Deposit (Apatite)"
				8: surf_name = "Potassium Deposit (Potash)"
				
	biome_lbl.text = "Region Biome: %s" % b_name
	player_lbl.text = "Player: (%d, %d)\nSurface: %s\nVelocity: (%.1f, %.1f)" % [
		tile_x, tile_y, surf_name, player_vel.x, player_vel.y
	]

func _input(event: InputEvent) -> void:
	if event is InputEventKey and event.is_pressed() and not event.is_echo():
		if event.keycode == KEY_M:
			_on_toggle_world_map_pressed()
		elif event.keycode == KEY_G:
			_on_toggle_grid_pressed()
		elif event.keycode == KEY_1:
			view_mode = 0
			is_world_map_open = false
			view_btn.text = "Mode: " + view_mode_names[view_mode]
			_update_map_texture()
			queue_redraw()
		elif event.keycode == KEY_2:
			view_mode = 1
			view_btn.text = "Mode: " + view_mode_names[view_mode]
			_update_map_texture()
			queue_redraw()
		elif event.keycode == KEY_3:
			view_mode = 2
			view_btn.text = "Mode: " + view_mode_names[view_mode]
			_update_map_texture()
			queue_redraw()
		elif event.keycode == KEY_4:
			view_mode = 3
			view_btn.text = "Mode: " + view_mode_names[view_mode]
			_update_map_texture()
			queue_redraw()
		elif event.keycode == KEY_5:
			view_mode = 4
			view_btn.text = "Mode: " + view_mode_names[view_mode]
			_update_map_texture()
			queue_redraw()
		elif event.keycode == KEY_C:
			_on_toggle_camera_pressed()
		elif event.keycode == KEY_P:
			_on_toggle_pump_pressed()
		elif event.keycode == KEY_N:
			_on_toggle_river_pump_pressed()
		elif event.keycode == KEY_B:
			_on_place_bridge_pressed()
		elif event.keycode == KEY_F:
			focus_on_player()
		elif event.keycode == KEY_R:
			_on_regenerate_pressed()
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

	# 2. Viewport-Culled Local Overlays (Only visible tiles when zoomed in)
	if is_detail_lod:
		for y in range(min_ty, max_ty + 1):
			for x in range(min_tx, max_tx + 1):
				var idx = y * map_width + x
				var rect = Rect2(x * cell_size, y * cell_size, cell_size, cell_size)
				
				if view_mode == 0:
					var struct_t = structures[idx] if not structures.is_empty() else 0
					if struct_t == 6:
						draw_line(Vector2(rect.position.x, rect.position.y + 8), Vector2(rect.position.x + cell_size, rect.position.y + 8), Color(0.35, 0.20, 0.08), 1.5)
						draw_line(Vector2(rect.position.x, rect.position.y + 16), Vector2(rect.position.x + cell_size, rect.position.y + 16), Color(0.35, 0.20, 0.08), 1.5)
					elif struct_t == 2:
						draw_circle(rect.position + rect.size * 0.5, cell_size * 0.35, Color(0.85, 0.75, 0.10))
					if show_grid and zoom_x >= 0.9:
						draw_rect(rect, Color(0, 0, 0, 0.12), false, 1.0)

	# 3. Draw Starter Site Reticle
	var sp_center = Vector2((starter_spawn.x + 0.5) * cell_size, (starter_spawn.y + 0.5) * cell_size)
	draw_arc(sp_center, cell_size * 2.5, 0, TAU, 32, Color(1.0, 0.9, 0.2, 0.6), 2.0)

	# 4. If World Map is Open, draw continental border & pulsing location pin
	if is_world_map_open:
		var map_rect = Rect2(0, 0, map_width * cell_size, map_height * cell_size)
		draw_rect(map_rect, Color(1.0, 0.85, 0.2, 0.8), false, 4.0)
		var pulse = (sin(Time.get_ticks_msec() * 0.008) * 0.5 + 0.5)
		var pin_rad = cell_size * (2.5 + pulse * 1.5)
		draw_circle(player_pos, cell_size * 0.9, Color(1.0, 0.2, 0.2))
		draw_arc(player_pos, pin_rad, 0, TAU, 32, Color(1.0, 0.9, 0.2), 3.0)
		return

	# 5. Draw Entities (Player, Bots, Enemies)
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
