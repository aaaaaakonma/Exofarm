extends SceneTree

func _init() -> void:
	print("[QC] Initializing visual terrain renderer...")
	var vis_scene = load("res://scenes/visualizers/world_3d_visualizer.tscn")
	if not vis_scene:
		printerr("[QC] Failed to load world_3d_visualizer.tscn")
		quit(1)
		return
	
	var vis = vis_scene.instantiate()
	root.add_child(vis)
	
	# Wait for procedural generation and several frames of rendering
	for i in range(30):
		await process_frame
	
	var vp = root.get_viewport()
	if vp:
		# 1. Capture Overview
		var tex = vp.get_texture()
		if tex and tex.get_image():
			tex.get_image().save_png("qc_terrain_overview.png")
			print("[QC] Overview saved to qc_terrain_overview.png")
		
		# 2. Hide UI and focus close-up on river valley
		if "ui_root" in vis and vis.ui_root:
			vis.ui_root.visible = false
		
		if "bridge" in vis and vis.bridge:
			print("[QC] Config from bridge: ", vis.bridge.get_config())
			var reaches = vis.bridge.get_parametric_river_reaches()
			print("[QC] Total river reaches extracted: ", reaches.size())
			for i in range(reaches.size()):
				var r = reaches[i]
				var w_arr: PackedVector2Array = r.get("waypoints", PackedVector2Array())
				var b_arr: PackedFloat32Array = r.get("bed_elevations", PackedFloat32Array())
				var w_elevs: PackedFloat32Array = r.get("water_elevations", PackedFloat32Array())
				var p0 = w_arr[0] if not w_arr.is_empty() else Vector2.ZERO
				var p1 = w_arr[-1] if not w_arr.is_empty() else Vector2.ZERO
				print("   Reach #", r.get("id"), " wps: ", w_arr.size(), " len: ", r.get("total_length"), "m | from: ", p0, " to: ", p1, " Zbed: ", (b_arr[0] if not b_arr.is_empty() else 0), "->", (b_arr[-1] if not b_arr.is_empty() else 0))
			var longest_reach = null
			var max_len = 0.0
			for r in reaches:
				var l = float(r.get("total_length", 0.0))
				if l > max_len:
					max_len = l
					longest_reach = r
			if longest_reach:
				var wps: PackedVector2Array = longest_reach.get("waypoints", PackedVector2Array())
				var elevs: PackedFloat32Array = longest_reach.get("water_elevations", PackedFloat32Array())
				print("[QC] Longest reach length: ", max_len, " waypoints: ", wps.size())
				if not wps.is_empty():
					var mid_idx = wps.size() / 2
					var mid_p = wps[mid_idx]
					var mid_y = elevs[mid_idx] * vis.elevation_scale if mid_idx < elevs.size() else 10.0
					print("[QC] Focusing camera on river midpoint: ", mid_p, " y: ", mid_y)
					vis.cam_target = Vector3(mid_p.x, mid_y, mid_p.y)
					vis.cam_distance = 110.0
					vis.cam_rot_x = -32.0
					vis.cam_rot_y = 50.0
					vis._update_camera_transform()
		elif "cam_distance" in vis:
			vis.cam_target = Vector3(500.0, 12.0, 520.0)
			vis.cam_distance = 160.0
			vis.cam_rot_x = -20.0
			vis.cam_rot_y = 75.0
			vis._update_camera_transform()
		
		for i in range(10):
			await process_frame
			
		tex = vp.get_texture()
		if tex and tex.get_image():
			tex.get_image().save_png("qc_river_closeup.png")
			print("[QC] River close-up saved to qc_river_closeup.png")
	
	print("[QC] Visual QC capture finished.")
	quit(0)
