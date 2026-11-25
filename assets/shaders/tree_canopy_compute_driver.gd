@tool
class_name TreeCanopyComputeDriver
extends RefCounted

## GPU-accelerated canopy cluster light exposure evaluator using Godot's RenderingDevice.
## Evaluates direct solar beam attenuation and sky zenith ambient occlusion in parallel across
## thousands of foliage clusters in active forest chunks.

var rd: RenderingDevice
var shader_rid: RID
var pipeline_rid: RID
var is_ready: bool = false

func initialize() -> bool:
	if is_ready:
		return true

	rd = RenderingServer.get_rendering_device()
	if rd == null:
		# Fall back to local rendering device if main server device is unavailable in headless
		rd = RenderingServer.create_local_rendering_device()
	if rd == null:
		push_warning("TreeCanopyComputeDriver: No RenderingDevice available for GPU compute.")
		return false

	var shader_file = load("res://assets/shaders/tree_canopy_compute.glsl")
	if not shader_file:
		push_error("TreeCanopyComputeDriver: Could not load tree_canopy_compute.glsl")
		return false

	var spirv: RDShaderSPIRV = shader_file.get_spirv()
	if not spirv:
		push_error("TreeCanopyComputeDriver: Failed to retrieve SPIRV bytecode.")
		return false

	shader_rid = rd.shader_create_from_spirv(spirv)
	if not shader_rid.is_valid():
		var err = spirv.get_stage_compile_error(RenderingDevice.SHADER_STAGE_COMPUTE)
		push_error("TreeCanopyComputeDriver: Compute shader compile error: ", err)
		return false

	pipeline_rid = rd.compute_pipeline_create(shader_rid)
	if not pipeline_rid.is_valid():
		push_error("TreeCanopyComputeDriver: Failed to create compute pipeline.")
		return false

	is_ready = true
	return true

## Dispatches GPU compute shader to evaluate light exposure across all clusters.
## Returns PackedFloat32Array of exposure values [0.05..1.0] matching cluster order.
func evaluate_canopy_shadows_gpu(clusters_packed: PackedFloat32Array, sun_dir: Vector3 = Vector3(0.5, 0.707, 0.5)) -> PackedFloat32Array:
	if not is_ready:
		if not initialize():
			return PackedFloat32Array()

	var num_clusters = clusters_packed.size() / 8
	if num_clusters == 0:
		return PackedFloat32Array()

	var cluster_bytes = clusters_packed.to_byte_array()
	var cluster_buffer = rd.storage_buffer_create(cluster_bytes.size(), cluster_bytes)

	var output_bytes = PackedByteArray()
	output_bytes.resize(num_clusters * 4) # 4 bytes per float
	var exposure_buffer = rd.storage_buffer_create(output_bytes.size(), output_bytes)

	# UBO: 32 bytes (std140)
	var sun_norm = sun_dir.normalized()
	if sun_norm.length_squared() < 0.01:
		sun_norm = Vector3(0.5, 0.707, 0.5).normalized()

	var ubo_bytes = PackedByteArray()
	ubo_bytes.resize(32)
	# vec4 sun_dir (xyz = dir, w = 0.70)
	ubo_bytes.encode_float(0, sun_norm.x)
	ubo_bytes.encode_float(4, sun_norm.y)
	ubo_bytes.encode_float(8, sun_norm.z)
	ubo_bytes.encode_float(12, 0.70)
	# vec4 zenith_dir (xyz = (0,1,0), w = 0.30)
	ubo_bytes.encode_float(16, 0.0)
	ubo_bytes.encode_float(20, 1.0)
	ubo_bytes.encode_float(24, 0.0)
	ubo_bytes.encode_float(28, 0.30)

	var ubo_buffer = rd.uniform_buffer_create(ubo_bytes.size(), ubo_bytes)

	# Build Uniform Set
	var u_cluster := RDUniform.new()
	u_cluster.uniform_type = RenderingDevice.UNIFORM_TYPE_STORAGE_BUFFER
	u_cluster.binding = 0
	u_cluster.add_id(cluster_buffer)

	var u_exposure := RDUniform.new()
	u_exposure.uniform_type = RenderingDevice.UNIFORM_TYPE_STORAGE_BUFFER
	u_exposure.binding = 1
	u_exposure.add_id(exposure_buffer)

	var u_params := RDUniform.new()
	u_params.uniform_type = RenderingDevice.UNIFORM_TYPE_UNIFORM_BUFFER
	u_params.binding = 2
	u_params.add_id(ubo_buffer)

	var uniform_set = rd.uniform_set_create([u_cluster, u_exposure, u_params], shader_rid, 0)

	# Dispatch compute shader: 64 threads per workgroup
	var groups_x = int(ceil(float(num_clusters) / 64.0))
	var compute_list = rd.compute_list_begin()
	rd.compute_list_bind_compute_pipeline(compute_list, pipeline_rid)
	rd.compute_list_bind_uniform_set(compute_list, uniform_set, 0)
	rd.compute_list_dispatch(compute_list, groups_x, 1, 1)
	rd.compute_list_end()

	rd.submit()
	rd.sync()

	var result_bytes = rd.buffer_get_data(exposure_buffer)
	var exposures = result_bytes.to_float32_array()

	# Cleanup temporary buffers
	rd.free_rid(cluster_buffer)
	rd.free_rid(exposure_buffer)
	rd.free_rid(ubo_buffer)

	return exposures
