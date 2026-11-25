extends SceneTree

func _init() -> void:
	print("==========================================================")
	print("       EXOFARM WAVE CAGE ITERATION EXPORTER               ")
	print("==========================================================")
	
	var bridge := SimBridge.new()
	var baker := WaveCageBaker.new()
	
	var config_file := FileAccess.open("res://world_3d_config.json", FileAccess.READ)
	var cfg: Dictionary = {}
	if config_file:
		var json := JSON.new()
		if json.parse(config_file.get_as_text()) == OK and typeof(json.data) == TYPE_DICTIONARY:
			cfg = json.data
	
	var map_w: int = cfg.get("map_width", 512)
	var map_h: int = cfg.get("map_height", 512)
	var cell_size: float = cfg.get("cell_size", 2.0)
	var seed_val: int = cfg.get("seed", 75843591)
	var sea_level: float = cfg.get("sea_level_offset", 0.0)
	
	print("[1/3] Generating deterministic world map (Seed: %d, Size: %dx%d)..." % [seed_val, map_w, map_h])
	bridge.set_config(cfg)
	var cell_size_fixed := int(cell_size * 65536.0)
	bridge.generate_player_map(seed_val, map_w, map_h, cell_size_fixed)
	
	print("[2/3] Extracting 4-channel wave cage and estuary boundary data...")
	var cage_floats := bridge.get_wave_cage_data(sea_level)
	var num_pixels := map_w * map_h
	if cage_floats.size() != num_pixels * 4:
		printerr("Error: Invalid wave cage float buffer size!")
		quit(1)
		return
		
	var hm_img := Image.create_from_data(map_w, map_h, false, Image.FORMAT_RGBAF, cage_floats.to_byte_array())
	var hm_tex := ImageTexture.create_from_image(hm_img)
	
	print("[3/3] Initializing WaveCageBaker with iteration export enabled...")
	baker.heightmap = hm_tex
	baker.grid_size = map_w
	baker.physical_size = map_w * cell_size
	baker.max_cage_radius = 100.0
	baker.sea_level_normalized = sea_level
	baker.height_scale_factor = 1.0
	baker.smoothing_passes = 3
	baker.export_intermediate_iterations = true
	baker.export_directory = "res://assets/exports/baked_cages"
	
	var out_dir_abs := ProjectSettings.globalize_path(baker.export_directory)
	print("Output directory: ", out_dir_abs)
	
	var t0 := Time.get_ticks_msec()
	var res := baker._bake()
	if res:
		var elapsed := (Time.get_ticks_msec() - t0) / 1000.0
		print("==========================================================")
		print(">>> ALL WAVE CAGE ITERATIONS EXPORTED SUCCESSFULLY (%.2fs) <<<" % elapsed)
		print("Location: ", out_dir_abs)
		print("==========================================================")
		quit(0)
	else:
		printerr("Bake failed!")
		quit(1)
