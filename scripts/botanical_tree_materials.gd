class_name BotanicalTreeMaterials
extends RefCounted

# ==============================================================================
# ExoFarm: Botanical Tree Species Presets, Foliage Shaders & Collision Helpers
# ==============================================================================
# Shared presentation/material layer paired with the C++ BotanicalTreeBridge.
# ==============================================================================

enum Species {
	CONIFER = 0,
	BROADLEAF_OAK = 1,
	SLENDER_BIRCH = 2,
	DEAD_SNAG = 3
}

static var _cached_textures: Dictionary = {}
static var _cached_foliage_shader: Shader = null
static var _cached_foliage_materials: Dictionary = {}

static func get_species_name(species_id: int) -> String:
	match species_id:
		Species.CONIFER:
			return "Conifer (Pine / Spruce)"
		Species.BROADLEAF_OAK:
			return "Broadleaf Oak"
		Species.SLENDER_BIRCH:
			return "Slender Birch"
		Species.DEAD_SNAG:
			return "Dead Snag"
		_:
			return "Conifer (Pine / Spruce)"

static func get_species_colors(species_id: int) -> Dictionary:
	match species_id:
		Species.CONIFER:
			return {
				"stem": Color(0.32, 0.22, 0.14, 1.0),
				"leaf": Color(0.16, 0.40, 0.16, 1.0)
			}
		Species.BROADLEAF_OAK:
			return {
				"stem": Color(0.38, 0.25, 0.15, 1.0),
				"leaf": Color(0.24, 0.54, 0.16, 1.0)
			}
		Species.SLENDER_BIRCH:
			return {
				"stem": Color(0.42, 0.35, 0.28, 1.0),
				"leaf": Color(0.32, 0.62, 0.20, 1.0)
			}
		Species.DEAD_SNAG:
			return {
				"stem": Color(0.45, 0.38, 0.30, 1.0),
				"leaf": Color(0.48, 0.40, 0.28, 0.8)
			}
		_:
			return get_species_colors(Species.CONIFER)

static func create_procedural_card_texture(species_id: int) -> ImageTexture:
	var width = 768
	var height = 256
	var img = Image.create(width, height, false, Image.FORMAT_RGBA8)
	img.fill(Color(0.0, 0.0, 0.0, 0.0))

	var colors = get_species_colors(species_id)
	var leaf_c: Color = colors["leaf"]
	var stem_c: Color = colors["stem"]

	for slot in range(3):
		var cx = slot * 256 + 128
		var slot_leaf_col = leaf_c
		if slot == 0:
			slot_leaf_col = leaf_c.lightened(0.12)
		elif slot == 2:
			slot_leaf_col = leaf_c.darkened(0.08)

		for y in range(25, 256):
			var y_f = float(y) / 256.0
			var spine_w = int(lerp(6.0, 2.5, 1.0 - y_f)) if slot != 0 else int(lerp(4.5, 2.0, 1.0 - y_f))
			var curve = sin(y_f * 5.0) * (6.0 if slot == 2 else 3.0)
			var sx = cx + int(curve)
			for x in range(sx - spine_w, sx + spine_w + 1):
				if x >= slot * 256 and x < (slot + 1) * 256:
					img.set_pixel(x, y, stem_c)

		var num_clusters = 14 if slot == 0 else (18 if slot == 1 else 16)
		for i in range(num_clusters):
			var cy = int(lerp(35.0, 235.0, float(i) / float(num_clusters)))
			var y_ratio = float(i) / float(num_clusters)

			for side in [-1, 1]:
				var b_len = lerp(68.0, 32.0, y_ratio)
				if slot == 1:
					b_len *= 1.35
				elif slot == 2:
					b_len *= 1.15

				var angle_rad = deg_to_rad(32.0) if slot != 2 else deg_to_rad(-22.0)
				var dir_x = float(side) * cos(angle_rad)
				var dir_y = -sin(angle_rad) if slot != 2 else 0.45

				var num_sub = 4
				for s in range(num_sub):
					var t = float(s + 1) / float(num_sub)
					var center_x = cx + int(dir_x * (b_len * t))
					var center_y = cy + int(dir_y * (b_len * t))

					var rad_x = int(lerp(14.0, 8.0, 1.0 - t))
					var rad_y = int(lerp(10.0, 6.0, 1.0 - t))

					for dy in range(-rad_y, rad_y + 1):
						for dx in range(-rad_x, rad_x + 1):
							var px = center_x + dx
							var py = center_y + dy
							if px >= slot * 256 and px < (slot + 1) * 256 and py >= 0 and py < 256:
								var d_sq = (float(dx * dx) / float(rad_x * rad_x)) + (float(dy * dy) / float(rad_y * rad_y))
								if d_sq <= 1.0:
									var a = clamp((1.0 - d_sq) * 3.5, 0.0, 1.0)
									var prev_c = img.get_pixel(px, py)
									if a > prev_c.a:
										var clr = slot_leaf_col
										if (dx + dy) % 3 == 0:
											clr = clr.lightened(0.06)
										clr.a = a
										img.set_pixel(px, py, clr)

		var apex_rad = 12
		for dy in range(-apex_rad, apex_rad + 1):
			for dx in range(-apex_rad, apex_rad + 1):
				var px = cx + dx
				var py = 25 + dy
				if px >= slot * 256 and px < (slot + 1) * 256 and py >= 0 and py < 256:
					var d_sq = float(dx * dx + dy * dy) / float(apex_rad * apex_rad)
					if d_sq <= 1.0:
						var a = clamp((1.0 - d_sq) * 3.0, 0.0, 1.0)
						var prev_c = img.get_pixel(px, py)
						if a > prev_c.a:
							var clr = slot_leaf_col.lightened(0.15)
							clr.a = a
							img.set_pixel(px, py, clr)

	var dilated = Image.create(width, height, false, Image.FORMAT_RGBA8)
	dilated.copy_from(img)
	for y in range(height):
		for x in range(width):
			if img.get_pixel(x, y).a < 0.05:
				var found = false
				for oy in [-1, 0, 1]:
					if found:
						break
					for ox in [-1, 0, 1]:
						var nx = x + ox
						var ny = y + oy
						if nx >= 0 and nx < width and ny >= 0 and ny < height:
							var nc = img.get_pixel(nx, ny)
							if nc.a > 0.3:
								dilated.set_pixel(x, y, Color(nc.r, nc.g, nc.b, 0.0))
								found = true
								break
	img = dilated
	img.generate_mipmaps()
	return ImageTexture.create_from_image(img)

static func _convert_black_background_to_alpha(img: Image) -> Image:
	var out = Image.create(img.get_width(), img.get_height(), false, Image.FORMAT_RGBA8)
	for y in range(img.get_height()):
		for x in range(img.get_width()):
			var c = img.get_pixel(x, y)
			var max_c = max(c.r, max(c.g, c.b))
			var a = clamp((max_c - 0.05) / 0.12, 0.0, 1.0)
			out.set_pixel(x, y, Color(c.r, c.g, c.b, a))
	return out

static func _load_first_card_texture(png_paths: Array[String], jpg_paths: Array[String]) -> Texture2D:
	for path in png_paths:
		if ResourceLoader.exists(path):
			var res = load(path)
			if res is Texture2D:
				return res
		var global_path = ProjectSettings.globalize_path(path)
		if FileAccess.file_exists(global_path):
			var img = Image.load_from_file(global_path)
			if img != null:
				return ImageTexture.create_from_image(img)

	for path in jpg_paths:
		var global_path = ProjectSettings.globalize_path(path)
		if FileAccess.file_exists(global_path):
			var img = Image.load_from_file(global_path)
			if img != null:
				return ImageTexture.create_from_image(_convert_black_background_to_alpha(img))
	return null

static func get_card_texture_for_species(species_id: int) -> Texture2D:
	if _cached_textures.has(species_id):
		return _cached_textures[species_id]

	var tex: Texture2D = null
	if species_id == Species.SLENDER_BIRCH:
		tex = _load_first_card_texture(
			["res://assets/cards/birch_atlas.png", "res://assets/cards/birch_2.png", "res://assets/cards/birch.png", "res://assets/cards/birch_3.png"],
			["res://assets/cards/birch_2.jpg", "res://assets/cards/birch.jpg", "res://assets/cards/birch_3.jpg"]
		)
	elif species_id == Species.BROADLEAF_OAK:
		tex = _load_first_card_texture(
			["res://assets/cards/maple_atlas.png", "res://assets/cards/maple_0.png", "res://assets/cards/maple_1.png"],
			["res://assets/cards/maple_0.jpg", "res://assets/cards/maple_1.jpg"]
		)

	if tex == null:
		tex = create_procedural_card_texture(species_id)

	_cached_textures[species_id] = tex
	return tex

static func get_default_foliage_material(species_id: int) -> StandardMaterial3D:
	var mat = StandardMaterial3D.new()
	mat.cull_mode = BaseMaterial3D.CULL_DISABLED
	mat.vertex_color_use_as_albedo = true
	mat.roughness = 0.82
	mat.metallic_specular = 0.15
	mat.depth_draw_mode = BaseMaterial3D.DEPTH_DRAW_ALWAYS
	mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR
	mat.alpha_scissor_threshold = 0.25
	mat.backlight_enabled = true
	mat.backlight = Color(0.25, 0.45, 0.18)
	mat.albedo_texture = get_card_texture_for_species(species_id)
	return mat

static func get_foliage_shader() -> Shader:
	if _cached_foliage_shader == null:
		_cached_foliage_shader = load("res://assets/shaders/tree_foliage.gdshader")
	return _cached_foliage_shader

static func get_foliage_shader_material(species_id: int) -> ShaderMaterial:
	if _cached_foliage_materials.has(species_id):
		return _cached_foliage_materials[species_id]

	var mat = ShaderMaterial.new()
	mat.shader = get_foliage_shader()
	mat.set_shader_parameter("texture_albedo", get_card_texture_for_species(species_id))
	mat.set_shader_parameter("alpha_scissor_threshold", 0.25)
	mat.set_shader_parameter("roughness", 0.85)

	match species_id:
		Species.CONIFER:
			mat.set_shader_parameter("transmission_color", Color(0.25, 0.45, 0.18, 1.0))
			mat.set_shader_parameter("transmission_intensity", 0.65)
		Species.BROADLEAF_OAK:
			mat.set_shader_parameter("transmission_color", Color(0.38, 0.58, 0.20, 1.0))
			mat.set_shader_parameter("transmission_intensity", 0.85)
		Species.SLENDER_BIRCH:
			mat.set_shader_parameter("transmission_color", Color(0.42, 0.62, 0.22, 1.0))
			mat.set_shader_parameter("transmission_intensity", 0.95)
		Species.DEAD_SNAG:
			mat.set_shader_parameter("transmission_color", Color(0.35, 0.30, 0.22, 1.0))
			mat.set_shader_parameter("transmission_intensity", 0.0)

	_cached_foliage_materials[species_id] = mat
	return mat

static func build_collision_shape_from_stats(stats: Dictionary) -> CylinderShape3D:
	var shape = CylinderShape3D.new()
	var h = max(0.5, float(stats.get("height", 8.0)) * 0.75)
	var r = max(0.1, float(stats.get("base_radius", 0.25)) * 0.85)
	shape.height = h
	shape.radius = r
	return shape
