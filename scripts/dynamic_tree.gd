@tool
class_name DynamicTree
extends Node3D

# ==============================================================================
# ExoFarm: DynamicTree Scene Node
# ==============================================================================
# Godot Node3D component for procedural botanical trees:
# - Real-time non-uniform growth scrubbing in editor and runtime
# - SpeedTree-like foliage cards mesh + bark mesh
# - Dynamic light exposure field with emergent self-pruning (cladoptosis)
# - Integrated ground, self, and obstacle collision checks
# - StaticBody3D collision shape generation for gameplay
# - Foliage socket and card access for gameplay interactions
# ==============================================================================

const BotanicalTreeMaterials = preload("res://scripts/botanical_tree_materials.gd")

@export_range(0.0, 1.0, 0.01) var growth: float = 1.0:
	set(val):
		growth = clamp(val, 0.0, 1.0)
		if is_node_ready():
			regenerate()

@export var seed_val: int = 1337:
	set(val):
		seed_val = val
		if is_node_ready():
			regenerate()

@export_enum("Conifer", "Broadleaf Oak", "Slender Birch", "Dead Snag") var tree_type: int = 0:
	set(val):
		tree_type = val
		if is_node_ready():
			_update_foliage_material()
			regenerate()

@export var use_optimized_foliage_shader: bool = true:
	set(val):
		use_optimized_foliage_shader = val
		if is_node_ready():
			_update_foliage_material()
			regenerate()

@export var enable_cards: bool = true:
	set(val):
		enable_cards = val
		if is_node_ready():
			if foliage_mesh_instance:
				foliage_mesh_instance.visible = enable_cards
			regenerate()

@export var enable_self_pruning: bool = true:
	set(val):
		enable_self_pruning = val
		if is_node_ready():
			regenerate()

@export_range(0.05, 0.50, 0.01) var cladoptosis_threshold: float = 0.22:
	set(val):
		cladoptosis_threshold = val
		if is_node_ready():
			regenerate()

@export_range(0.0, 1.0, 0.01) var lateral_wander_bias: float = -1.0:
	set(val):
		lateral_wander_bias = val
		if is_node_ready():
			regenerate()

@export_range(0.0, 1.0, 0.01) var vertical_wander_bias: float = -1.0:
	set(val):
		vertical_wander_bias = val
		if is_node_ready():
			regenerate()

@export var sun_direction: Vector3 = Vector3(0.5, 0.707, 0.5):
	set(val):
		sun_direction = val
		if is_node_ready():
			regenerate()

@export var enable_collision: bool = true:
	set(val):
		enable_collision = val
		if is_node_ready() and static_body:
			static_body.visible = enable_collision
			if collision_shape_node:
				collision_shape_node.disabled = not enable_collision

@export var bark_material: Material:
	set(val):
		bark_material = val
		if mesh_instance:
			mesh_instance.material_override = bark_material

@export var foliage_material: Material:
	set(val):
		foliage_material = val
		if foliage_mesh_instance:
			foliage_mesh_instance.material_override = foliage_material

# Child Nodes
var mesh_instance: MeshInstance3D
var foliage_mesh_instance: MeshInstance3D
var static_body: StaticBody3D
var collision_shape_node: CollisionShape3D

# C++ Bridge & Cached Result
var _bridge = null
var current_result: Dictionary = {}

func _ready() -> void:
	if ClassDB.class_exists("BotanicalTreeBridge"):
		_bridge = ClassDB.instantiate("BotanicalTreeBridge")
	_setup_nodes()
	regenerate()

func _setup_nodes() -> void:
	if not mesh_instance:
		mesh_instance = MeshInstance3D.new()
		mesh_instance.name = "BarkMesh"
		add_child(mesh_instance)

	if not foliage_mesh_instance:
		foliage_mesh_instance = MeshInstance3D.new()
		foliage_mesh_instance.name = "FoliageMesh"
		add_child(foliage_mesh_instance)

	if not static_body:
		static_body = StaticBody3D.new()
		static_body.name = "CollisionBody"
		add_child(static_body)

		collision_shape_node = CollisionShape3D.new()
		collision_shape_node.name = "TrunkCollisionShape"
		static_body.add_child(collision_shape_node)

	if not bark_material:
		var default_mat = StandardMaterial3D.new()
		default_mat.albedo_color = Color(0.38, 0.26, 0.18)
		default_mat.roughness = 0.88
		default_mat.metallic_specular = 0.15
		default_mat.cull_mode = BaseMaterial3D.CULL_BACK
		bark_material = default_mat

	mesh_instance.material_override = bark_material

	if not foliage_material:
		_update_foliage_material()
	else:
		foliage_mesh_instance.material_override = foliage_material

func _update_foliage_material() -> void:
	if use_optimized_foliage_shader:
		foliage_material = BotanicalTreeMaterials.get_foliage_shader_material(tree_type)
	else:
		foliage_material = BotanicalTreeMaterials.get_default_foliage_material(tree_type)
	if foliage_mesh_instance:
		foliage_mesh_instance.material_override = foliage_material

func regenerate() -> void:
	if not mesh_instance:
		_setup_nodes()
	if not _bridge and ClassDB.class_exists("BotanicalTreeBridge"):
		_bridge = ClassDB.instantiate("BotanicalTreeBridge")
	if not _bridge:
		return

	var params: Dictionary = {
		"ground_pos": Vector3.ZERO,
		"ground_normal": Vector3.UP,
		"sun_dir": sun_direction,
		"enable_self_pruning": enable_self_pruning,
		"cladoptosis_threshold": cladoptosis_threshold
	}
	if lateral_wander_bias >= 0.0:
		params["lateral_bias"] = lateral_wander_bias
	if vertical_wander_bias >= 0.0:
		params["vertical_bias"] = vertical_wander_bias

	current_result = _bridge.generate_tree(seed_val, tree_type, growth, params)

	var bark_mesh: ArrayMesh = current_result.get("bark_mesh")
	if bark_mesh:
		mesh_instance.mesh = bark_mesh
		mesh_instance.visible = true
	else:
		mesh_instance.mesh = null

	var foliage_mesh: ArrayMesh = current_result.get("foliage_mesh")
	if foliage_mesh and enable_cards:
		foliage_mesh_instance.mesh = foliage_mesh
		foliage_mesh_instance.visible = true
	else:
		foliage_mesh_instance.mesh = null
		foliage_mesh_instance.visible = false

	if enable_collision and collision_shape_node:
		var col_shape = BotanicalTreeMaterials.build_collision_shape_from_stats(current_result.get("stats", {}))
		collision_shape_node.shape = col_shape
		collision_shape_node.position = Vector3(0.0, col_shape.height * 0.5, 0.0)
		collision_shape_node.disabled = false
	elif collision_shape_node:
		collision_shape_node.disabled = true

func get_tree_result() -> Dictionary:
	return current_result

func get_stats() -> Dictionary:
	return current_result.get("stats", {})
