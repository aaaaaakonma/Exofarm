@tool
class_name WaveComputeDriver
extends Node

## Hosts the wave cage compute pipeline (Jeschke et al. 2020).
## On _ready() it compiles the shader and sets up all Vulkan resources.
## On _process() it updates time + wave params and dispatches the compute pass.
##
## Assign static_cage_texture (baked by wave_cage_baker.gd) and
## heightmap_texture (same heightmap used by the terrain shader) before entering
## the scene tree, or the compute pass will be skipped with an error.

@export var static_cage_texture: Texture2D
@export var heightmap_texture: Texture2D   ## Same heightmap used by the terrain — needed for terrain-normal reconstruction in the shader.
@export var grid_size: int = 512
@export var cage_softness: float = 0.5
@export var world_size: float = 512.0
@export var height_scale_factor: float = 100.0   ## Must match wave_cage_baker.gd / terrain shader.
@export var sea_level_normalized: float = 0.5    ## Must match wave_cage_baker.gd.

@export var target_material: ShaderMaterial

# Output Texture2DRD objects exposed to the spatial shader.
var displacement_texture: Texture2DRD
var normal_texture: Texture2DRD

# ---------- Vulkan / RD resources ----------
var rd: RenderingDevice
var shader: RID
var pipeline: RID

var out_disp_tex: RID
var out_norm_tex: RID
var cage_tex_rid: RID   # tracked for cleanup (was leaked before)
var hm_tex_rid: RID     # heightmap RD texture (tracked for cleanup)
var sampler_rid: RID    # shared linear sampler (tracked for cleanup)

var uniform_set_0: RID  # output image2Ds
var uniform_set_1: RID  # cage tex + heightmap tex
var uniform_set_2: RID  # params UBO
var uniform_set_3: RID  # wave SSBO

var params_buffer: RID
var wave_buffer: RID

var time: float = 0.0

# ---------- Wave definition ----------
class WaveDef:
	var dir: Vector2
	var amplitude: float
	var frequency: float
	var speed: float
	var phase: float
	var steepness: float = 0.5   # Gerstner Q parameter, was missing before

var waves: Array[WaveDef] = []


func _ready() -> void:
	_init_waves()
	if static_cage_texture and heightmap_texture:
		_init_compute()


func _init_waves() -> void:
	# Four overlapping Gerstner waves at varied directions and frequencies.
	for i in range(4):
		var w := WaveDef.new()
		w.dir       = Vector2(cos(i * 0.7 + 0.3), sin(i * 0.7 + 0.3)).normalized()
		w.amplitude = 2.0 / (i + 1)
		w.frequency = 0.1 * (i + 1)
		w.speed     = 1.0 * (i + 1)
		w.phase     = i * 0.5
		w.steepness = 0.5
		waves.append(w)


func _init_compute() -> void:
	if not static_cage_texture or not heightmap_texture:
		return

	rd = RenderingServer.get_rendering_device()
	if rd == null:
		printerr("WaveComputeDriver: Compute shaders not supported on this renderer.")
		return

	# ---- 1. Compile shader + create pipeline ----
	var shader_file  = load("res://assets/shaders/wave_compute.glsl")
	var spirv: RDShaderSPIRV = shader_file.get_spirv()
	var shader_res   = rd.shader_create_from_spirv(spirv)
	shader = shader_res
	pipeline = rd.compute_pipeline_create(shader)

	# ---- 2. Output textures (displacement + normal) ----
	var fmt := RDTextureFormat.new()
	fmt.width  = grid_size
	fmt.height = grid_size
	fmt.format = RenderingDevice.DATA_FORMAT_R32G32B32A32_SFLOAT
	fmt.usage_bits = (RenderingDevice.TEXTURE_USAGE_CAN_UPDATE_BIT
					| RenderingDevice.TEXTURE_USAGE_STORAGE_BIT
					| RenderingDevice.TEXTURE_USAGE_CAN_COPY_FROM_BIT
					| RenderingDevice.TEXTURE_USAGE_SAMPLING_BIT)

	out_disp_tex = rd.texture_create(fmt, RDTextureView.new())
	out_norm_tex = rd.texture_create(fmt, RDTextureView.new())

	displacement_texture = Texture2DRD.new()
	displacement_texture.texture_rd_rid = out_disp_tex

	normal_texture = Texture2DRD.new()
	normal_texture.texture_rd_rid = out_norm_tex

	if target_material:
		target_material.set_shader_parameter("displacement_tex", displacement_texture)
		target_material.set_shader_parameter("normal_tex",       normal_texture)
		# Also wire the heightmap so the spatial shader can do terrain clipping.
		target_material.set_shader_parameter("heightmap_tex", heightmap_texture)

	# ---- 3. Uniform Set 0 — output image2Ds ----
	var u_disp := RDUniform.new()
	u_disp.uniform_type = RenderingDevice.UNIFORM_TYPE_IMAGE
	u_disp.binding = 0
	u_disp.add_id(out_disp_tex)

	var u_norm := RDUniform.new()
	u_norm.uniform_type = RenderingDevice.UNIFORM_TYPE_IMAGE
	u_norm.binding = 1
	u_norm.add_id(out_norm_tex)

	uniform_set_0 = rd.uniform_set_create([u_disp, u_norm], shader, 0)

	# ---- 4. Shared linear sampler (clamp-to-edge) ----
	var ss := RDSamplerState.new()
	ss.mag_filter = RenderingDevice.SAMPLER_FILTER_LINEAR
	ss.min_filter = RenderingDevice.SAMPLER_FILTER_LINEAR
	ss.repeat_u   = RenderingDevice.SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE
	ss.repeat_v   = RenderingDevice.SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE
	sampler_rid = rd.sampler_create(ss)

	# ---- 5. Cage texture (set=1, binding=0) ----
	var cage_img := static_cage_texture.get_image()
	cage_img.convert(Image.FORMAT_RGBAF)
	var cage_fmt := RDTextureFormat.new()
	cage_fmt.width  = cage_img.get_width()
	cage_fmt.height = cage_img.get_height()
	cage_fmt.format = RenderingDevice.DATA_FORMAT_R32G32B32A32_SFLOAT
	cage_fmt.usage_bits = (RenderingDevice.TEXTURE_USAGE_CAN_UPDATE_BIT
						 | RenderingDevice.TEXTURE_USAGE_SAMPLING_BIT)
	cage_tex_rid = rd.texture_create(cage_fmt, RDTextureView.new(), [cage_img.get_data()])

	var u_cage := RDUniform.new()
	u_cage.uniform_type = RenderingDevice.UNIFORM_TYPE_SAMPLER_WITH_TEXTURE
	u_cage.binding = 0
	u_cage.add_id(sampler_rid)
	u_cage.add_id(cage_tex_rid)

	# ---- 6. Heightmap texture (set=1, binding=1) ----
	var hm_img := heightmap_texture.get_image()
	hm_img.decompress()
	hm_img.convert(Image.FORMAT_RGBAF)
	var hm_fmt := RDTextureFormat.new()
	hm_fmt.width  = hm_img.get_width()
	hm_fmt.height = hm_img.get_height()
	hm_fmt.format = RenderingDevice.DATA_FORMAT_R32G32B32A32_SFLOAT
	hm_fmt.usage_bits = (RenderingDevice.TEXTURE_USAGE_CAN_UPDATE_BIT
					   | RenderingDevice.TEXTURE_USAGE_SAMPLING_BIT)
	hm_tex_rid = rd.texture_create(hm_fmt, RDTextureView.new(), [hm_img.get_data()])

	var u_hm := RDUniform.new()
	u_hm.uniform_type = RenderingDevice.UNIFORM_TYPE_SAMPLER_WITH_TEXTURE
	u_hm.binding = 1
	u_hm.add_id(sampler_rid)
	u_hm.add_id(hm_tex_rid)

	uniform_set_1 = rd.uniform_set_create([u_cage, u_hm], shader, 1)

	# ---- 7. Params UBO (set=2) — 32 bytes, 8 floats ----
	# Layout matches the WaveParams uniform block in wave_compute.glsl.
	var pb := PackedByteArray()
	pb.resize(32)
	params_buffer = rd.uniform_buffer_create(pb.size(), pb)

	var u_params := RDUniform.new()
	u_params.uniform_type = RenderingDevice.UNIFORM_TYPE_UNIFORM_BUFFER
	u_params.binding = 0
	u_params.add_id(params_buffer)
	uniform_set_2 = rd.uniform_set_create([u_params], shader, 2)

	# ---- 8. Wave SSBO (set=3) — 32 bytes per Wave struct ----
	# Layout: vec2 dir (8), float amplitude (4), frequency (4), speed (4),
	#         phase (4), steepness (4), padding (4) = 32 bytes.
	const WAVE_STRIDE := 32
	var wave_bytes := PackedByteArray()
	wave_bytes.resize(waves.size() * WAVE_STRIDE)  # zero-initialised (padding safe)

	for i in range(waves.size()):
		var w   := waves[i]
		var off := i * WAVE_STRIDE
		wave_bytes.encode_float(off + 0,  w.dir.x)
		wave_bytes.encode_float(off + 4,  w.dir.y)
		wave_bytes.encode_float(off + 8,  w.amplitude)
		wave_bytes.encode_float(off + 12, w.frequency)
		wave_bytes.encode_float(off + 16, w.speed)
		wave_bytes.encode_float(off + 20, w.phase)
		wave_bytes.encode_float(off + 24, w.steepness)
		# off + 28 = padding, already 0 from resize()

	wave_buffer = rd.storage_buffer_create(wave_bytes.size(), wave_bytes)

	var u_waves := RDUniform.new()
	u_waves.uniform_type = RenderingDevice.UNIFORM_TYPE_STORAGE_BUFFER
	u_waves.binding = 0
	u_waves.add_id(wave_buffer)
	uniform_set_3 = rd.uniform_set_create([u_waves], shader, 3)


func update_waves() -> void:
	if not rd or not wave_buffer.is_valid():
		return
	const WAVE_STRIDE := 32
	var wave_bytes := PackedByteArray()
	wave_bytes.resize(waves.size() * WAVE_STRIDE)
	for i in range(waves.size()):
		var w   := waves[i]
		var off := i * WAVE_STRIDE
		wave_bytes.encode_float(off + 0,  w.dir.x)
		wave_bytes.encode_float(off + 4,  w.dir.y)
		wave_bytes.encode_float(off + 8,  w.amplitude)
		wave_bytes.encode_float(off + 12, w.frequency)
		wave_bytes.encode_float(off + 16, w.speed)
		wave_bytes.encode_float(off + 20, w.phase)
		wave_bytes.encode_float(off + 24, w.steepness)
	rd.buffer_update(wave_buffer, 0, wave_bytes.size(), wave_bytes)


func _process(delta: float) -> void:
	if not rd or not pipeline.is_valid():
		return

	time += delta

	# Update params UBO — 32 bytes matching WaveParams in wave_compute.glsl.
	var pb := PackedByteArray()
	pb.resize(32)
	pb.encode_float(0,  time)
	pb.encode_float(4,  cage_softness)
	pb.encode_s32(8,    waves.size())
	pb.encode_float(12, world_size)
	pb.encode_float(16, height_scale_factor)
	pb.encode_float(20, sea_level_normalized)
	# bytes 24-31 = padding, left as 0
	rd.buffer_update(params_buffer, 0, pb.size(), pb)

	# Dispatch.
	var compute_list := rd.compute_list_begin()
	rd.compute_list_bind_compute_pipeline(compute_list, pipeline)
	rd.compute_list_bind_uniform_set(compute_list, uniform_set_0, 0)
	rd.compute_list_bind_uniform_set(compute_list, uniform_set_1, 1)
	rd.compute_list_bind_uniform_set(compute_list, uniform_set_2, 2)
	rd.compute_list_bind_uniform_set(compute_list, uniform_set_3, 3)
	# Fix: ceil() returns float in GDScript — cast to int for the RD API.
	var groups_x := int(ceil(float(grid_size) / 16.0))
	var groups_y := int(ceil(float(grid_size) / 16.0))
	rd.compute_list_dispatch(compute_list, groups_x, groups_y, 1)
	rd.compute_list_end()


func update_textures(new_cage_tex: Texture2D, new_heightmap_tex: Texture2D) -> void:
	static_cage_texture = new_cage_tex
	heightmap_texture = new_heightmap_tex
	if new_cage_tex:
		grid_size = new_cage_tex.get_width()
	_cleanup_resources()
	_init_compute()

func _cleanup_resources() -> void:
	if rd:
		if out_disp_tex.is_valid(): rd.free_rid(out_disp_tex)
		if out_norm_tex.is_valid(): rd.free_rid(out_norm_tex)
		if cage_tex_rid.is_valid(): rd.free_rid(cage_tex_rid)
		if hm_tex_rid.is_valid():   rd.free_rid(hm_tex_rid)
		if sampler_rid.is_valid():  rd.free_rid(sampler_rid)
		if params_buffer.is_valid(): rd.free_rid(params_buffer)
		if wave_buffer.is_valid():  rd.free_rid(wave_buffer)
		if pipeline.is_valid():     rd.free_rid(pipeline)
		if shader.is_valid():       rd.free_rid(shader)

func _notification(what: int) -> void:
	if what == NOTIFICATION_PREDELETE:
		_cleanup_resources()
		rd = null
