@tool
class_name WaveCageBaker
extends Node

## GPU Compute-based Baker for wave cage parameters (Jeschke et al. 2020).
##
## Offloads the heavy multi-source BFS and raymarching calculations to the GPU
## using the Jump Flooding Algorithm (JFA). This prevents framerate hitches
## when generating maps dynamically at runtime.

@export var heightmap: Texture2D

@export_group("World Parameters")
@export var sea_level_normalized: float = 0.5
@export var height_scale_factor: float = 100.0
@export var physical_size: float = 512.0
@export var max_cage_radius: float = 100.0

@export_group("Baker Settings")
@export var grid_size: int = 512
@export var smoothing_passes: int = 3
@export var output_path: String = ""

@export var bake_cages: bool = false:
	set(value):
		if value:
			_bake()
			bake_cages = false


func _bake() -> ImageTexture:
	if not heightmap:
		printerr("WaveCageBaker: No heightmap assigned.")
		return null

	print("WaveCageBaker: Starting GPU bake...")
	var t0 := Time.get_ticks_msec()

	var rd := RenderingServer.get_rendering_device()
	if not rd:
		printerr("WaveCageBaker: Compute shaders not supported.")
		return null

	# ---- 1. Load Compute Shader ----
	var shader_file = load("res://assets/shaders/wave_cage_baker.glsl")
	var spirv: RDShaderSPIRV = shader_file.get_spirv()
	var shader := rd.shader_create_from_spirv(spirv)
	var pipeline := rd.compute_pipeline_create(shader)

	# ---- 2. Prepare Heightmap Input ----
	var hm_img := heightmap.get_image()
	hm_img.decompress()
	hm_img.convert(Image.FORMAT_RGBAF)

	var hm_fmt := RDTextureFormat.new()
	hm_fmt.width = hm_img.get_width()
	hm_fmt.height = hm_img.get_height()
	hm_fmt.format = RenderingDevice.DATA_FORMAT_R32G32B32A32_SFLOAT
	hm_fmt.usage_bits = RenderingDevice.TEXTURE_USAGE_SAMPLING_BIT | RenderingDevice.TEXTURE_USAGE_CAN_UPDATE_BIT
	var hm_tex := rd.texture_create(hm_fmt, RDTextureView.new(), [hm_img.get_data()])

	var sampler_state := RDSamplerState.new()
	sampler_state.min_filter = RenderingDevice.SAMPLER_FILTER_NEAREST
	sampler_state.mag_filter = RenderingDevice.SAMPLER_FILTER_NEAREST
	var sampler := rd.sampler_create(sampler_state)

	# ---- 3. Create Ping-Pong Buffers ----
	var io_fmt := RDTextureFormat.new()
	io_fmt.width = grid_size
	io_fmt.height = grid_size
	io_fmt.format = RenderingDevice.DATA_FORMAT_R32G32B32A32_SFLOAT
	io_fmt.usage_bits = RenderingDevice.TEXTURE_USAGE_STORAGE_BIT | RenderingDevice.TEXTURE_USAGE_CAN_COPY_FROM_BIT | RenderingDevice.TEXTURE_USAGE_CAN_UPDATE_BIT

	var tex_jfa_a: RID = rd.texture_create(io_fmt, RDTextureView.new())
	var tex_jfa_b: RID = rd.texture_create(io_fmt, RDTextureView.new())
	var tex_radii_orig: RID = rd.texture_create(io_fmt, RDTextureView.new())
	var tex_radii_a: RID = rd.texture_create(io_fmt, RDTextureView.new())
	var tex_radii_b: RID = rd.texture_create(io_fmt, RDTextureView.new())

	# Uniform set generator helper
	var create_set = func(in_t: RID, out_t: RID, orig_t: RID) -> RID:
		var u_hm := RDUniform.new()
		u_hm.uniform_type = RenderingDevice.UNIFORM_TYPE_SAMPLER_WITH_TEXTURE
		u_hm.binding = 0
		u_hm.add_id(sampler)
		u_hm.add_id(hm_tex)

		var u_in := RDUniform.new()
		u_in.uniform_type = RenderingDevice.UNIFORM_TYPE_IMAGE
		u_in.binding = 1
		u_in.add_id(in_t)

		var u_out := RDUniform.new()
		u_out.uniform_type = RenderingDevice.UNIFORM_TYPE_IMAGE
		u_out.binding = 2
		u_out.add_id(out_t)

		var u_orig := RDUniform.new()
		u_orig.uniform_type = RenderingDevice.UNIFORM_TYPE_IMAGE
		u_orig.binding = 3
		u_orig.add_id(orig_t)

		return rd.uniform_set_create([u_hm, u_in, u_out, u_orig], shader, 0)

	# ---- 4. Setup Compute Execution ----
	var cl := rd.compute_list_begin()
	rd.compute_list_bind_compute_pipeline(cl, pipeline)

	var groups := int(ceil(float(grid_size) / 16.0))
	var sea_h := 0.0 # 0.0m sea level
	var world_dx := physical_size / float(grid_size - 1)
	var max_steps := int(max_cage_radius / world_dx) + 2

	# Pass 0: Init JFA seeds
	var set_init: RID = create_set.call(tex_jfa_a, tex_jfa_a, tex_radii_orig)
	rd.compute_list_bind_uniform_set(cl, set_init, 0)
	rd.compute_list_set_push_constant(cl, _encode_pc(0, 0, max_steps, grid_size, sea_h, height_scale_factor, physical_size, max_cage_radius, world_dx), 36)
	rd.compute_list_dispatch(cl, groups, groups, 1)
	rd.compute_list_add_barrier(cl)

	# Pass 1: Jump Flooding Algorithm (JFA)
	var cur_jfa: RID = tex_jfa_a
	var next_jfa: RID = tex_jfa_b
	var step := int(ceil(float(grid_size) / 2.0))
	
	while step >= 1:
		var set_jfa: RID = create_set.call(cur_jfa, next_jfa, tex_radii_orig)
		rd.compute_list_bind_uniform_set(cl, set_jfa, 0)
		rd.compute_list_set_push_constant(cl, _encode_pc(1, step, max_steps, grid_size, sea_h, height_scale_factor, physical_size, max_cage_radius, world_dx), 36)
		rd.compute_list_dispatch(cl, groups, groups, 1)
		rd.compute_list_add_barrier(cl)

		var tmp: RID = cur_jfa
		cur_jfa = next_jfa
		next_jfa = tmp
		step /= 2

	# Pass 2: Calculate Initial Radii and Angle
	var set_radii: RID = create_set.call(cur_jfa, tex_radii_orig, tex_radii_orig)
	rd.compute_list_bind_uniform_set(cl, set_radii, 0)
	rd.compute_list_set_push_constant(cl, _encode_pc(2, 0, max_steps, grid_size, sea_h, height_scale_factor, physical_size, max_cage_radius, world_dx), 36)
	rd.compute_list_dispatch(cl, groups, groups, 1)
	rd.compute_list_add_barrier(cl)

	# Pass 3: Box Blur (Inequality Constrained)
	var cur_radii: RID = tex_radii_orig
	var next_radii: RID = tex_radii_a
	
	for i in range(smoothing_passes):
		var set_blur: RID = create_set.call(cur_radii, next_radii, tex_radii_orig)
		rd.compute_list_bind_uniform_set(cl, set_blur, 0)
		rd.compute_list_set_push_constant(cl, _encode_pc(3, 0, max_steps, grid_size, sea_h, height_scale_factor, physical_size, max_cage_radius, world_dx), 36)
		rd.compute_list_dispatch(cl, groups, groups, 1)
		rd.compute_list_add_barrier(cl)

		if cur_radii == tex_radii_orig:
			cur_radii = tex_radii_a
			next_radii = tex_radii_b
		else:
			var tmp: RID = cur_radii
			cur_radii = next_radii
			next_radii = tmp

	rd.compute_list_end()
	
	# Execute and Wait
	rd.submit()
	rd.sync()
	
	print("  GPU compute done (%.2fs)" % ((Time.get_ticks_msec() - t0) / 1000.0))

	# ---- 5. Retrieve Results and Save ----
	var byte_data := rd.texture_get_data(cur_radii, 0)

	# Clean up manually since this script runs synchronously
	rd.free_rid(hm_tex)
	rd.free_rid(sampler)
	rd.free_rid(tex_jfa_a)
	rd.free_rid(tex_jfa_b)
	rd.free_rid(tex_radii_orig)
	rd.free_rid(tex_radii_a)
	rd.free_rid(tex_radii_b)
	rd.free_rid(pipeline)
	rd.free_rid(shader)

	var out_img := Image.create_from_data(grid_size, grid_size, false, Image.FORMAT_RGBAF, byte_data)
	if output_path != "":
		out_img.save_exr(output_path)
	
	print("WaveCageBaker: Finished entirely in %.2fs — saved to %s" % [(Time.get_ticks_msec() - t0) / 1000.0, output_path])
	if Engine.is_editor_hint() and output_path != "":
		ResourceLoader.load(output_path)
	
	return ImageTexture.create_from_image(out_img)

## Public API to bake wave cages directly in-memory from a heightmap
func bake_from_heightmap(hm_tex: Texture2D) -> ImageTexture:
	heightmap = hm_tex
	if hm_tex:
		grid_size = hm_tex.get_width()
	return _bake()


func _encode_pc(pass_idx: int, step_s: int, max_s: int, gs: int, sl: float, hs: float, ps: float, mr: float, wdx: float) -> PackedByteArray:
	# Packs 4 ints and 5 floats = 36 bytes. 
	var pb := PackedByteArray()
	pb.resize(36)
	pb.encode_s32(0, pass_idx)
	pb.encode_s32(4, step_s)
	pb.encode_s32(8, max_s)
	pb.encode_s32(12, gs)
	pb.encode_float(16, sl)
	pb.encode_float(20, hs)
	pb.encode_float(24, ps)
	pb.encode_float(28, mr)
	pb.encode_float(32, wdx)
	return pb
