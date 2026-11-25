@tool
class_name WaveComputeDriver
extends Node

## Hosts the Yuksel (2007) Wave Particle Simulation on Jeschke Boundary Wave Cages.
## Discrete 2D wave packet particles propagate across the ocean, shoal and slow down
## in shallow waters, reflect off coastal boundaries, and deform the water grid.

@export var static_cage_texture: Texture2D
@export var heightmap_texture: Texture2D
@export var grid_size: int = 512
@export var cage_softness: float = 6.0  ## Jeschke §4 tanh sharpness — must be >> 1 to preserve displacement inside cage
@export var world_size: float = 1024.0
@export var height_scale_factor: float = 1.0
@export var sea_level_normalized: float = 0.0

@export var target_material: ShaderMaterial

# Maximum simultaneous wave particles in GPU storage buffer
const MAX_PARTICLES: int = 512
const PARTICLE_STRIDE: int = 64 # bytes per WaveParticle struct in std430

# Output Texture2DRD objects exposed to the spatial water shader
var displacement_texture: Texture2DRD
var normal_texture: Texture2DRD

# Vulkan / RenderingDevice resources
var rd: RenderingDevice
var shader: RID
var pipeline: RID

var out_disp_tex: RID
var out_norm_tex: RID
var cage_tex_rid: RID
var hm_tex_rid: RID
var sampler_rid: RID

var uniform_set_0: RID # output images
var uniform_set_1: RID # cage tex + heightmap tex
var uniform_set_2: RID # params UBO
var uniform_set_3: RID # particle SSBO

var params_buffer: RID
var wave_buffer: RID

var time: float = 0.0
var global_amplitude_mult: float = 1.0

# Cached CPU-side image for instant boundary lookups (reflections & shoaling)
var cached_cage_image: Image
var cached_hm_image: Image

# ---------- Wave Particle Definition ----------
class WaveParticle:
	var pos: Vector2 = Vector2.ZERO         # world x, z
	var dir: Vector2 = Vector2(1.0, 0.0)    # normalized direction
	var speed: float = 24.0                 # propagation velocity (m/s)
	var amplitude: float = 1.6              # wave packet height (m)
	var radius: float = 30.0                # kernel influence radius (m)
	var wavelength: float = 20.0            # carrier wavelength (m)
	var age: float = 0.0                    # current lifetime (s)
	var lifetime: float = 14.0              # total lifespan (s)
	var steepness: float = 0.5              # Gerstner Q horizontal choppiness
	var active: bool = false
	var is_ambient: bool = false

var particles: Array[WaveParticle] = []
var ambient_timer: float = 0.0

# Compatibility mock for existing wave controls
class MockWave:
	var amplitude: float = 1.0
var waves: Array[MockWave] = []

func _ready() -> void:
	_init_particles()
	if static_cage_texture and heightmap_texture:
		_init_compute()

func _init_particles() -> void:
	particles.clear()
	for i in range(MAX_PARTICLES):
		particles.append(WaveParticle.new())
	
	# Compatibility mock
	waves.clear()
	for i in range(4):
		waves.append(MockWave.new())
	
	# Seed initial ambient ocean swell particles across the map
	_seed_ambient_swells()

func _seed_ambient_swells() -> void:
	# Populate open ocean regions with rolling swell particle wavefronts
	var main_dir = Vector2(0.85, 0.52).normalized()
	for row in range(5):
		var base_dist = float(row) * (world_size * 0.22)
		for col in range(8):
			var perp = Vector2(-main_dir.y, main_dir.x)
			var offset = (float(col) - 3.5) * 110.0
			var spawn_p = main_dir * base_dist + perp * offset
			spawn_p.x = fposmod(spawn_p.x, world_size)
			spawn_p.y = fposmod(spawn_p.y, world_size)
			
			var p = _alloc_particle()
			if p:
				p.pos = spawn_p
				p.dir = main_dir
				p.speed = 22.0 + randf_range(-2.0, 2.0)
				p.amplitude = 1.4 * global_amplitude_mult
				p.radius = 45.0 + randf_range(-5.0, 10.0)
				p.wavelength = 28.0 + randf_range(-4.0, 4.0)
				p.age = randf_range(0.0, 8.0)
				p.lifetime = 24.0
				p.steepness = 0.55
				p.active = true
				p.is_ambient = true

func _alloc_particle() -> WaveParticle:
	for p in particles:
		if not p.active:
			return p
	# Recycle the oldest particle if pool is exhausted
	var oldest: WaveParticle = particles[0]
	for p in particles:
		if p.age > oldest.age:
			oldest = p
	return oldest

func spawn_particle(
	pos: Vector2,
	dir: Vector2,
	speed: float,
	amp: float,
	radius: float,
	wavelength: float,
	lifetime: float = 12.0,
	steepness: float = 0.5,
	is_ambient: bool = false
) -> WaveParticle:
	var p = _alloc_particle()
	if p:
		p.pos = pos
		p.dir = dir.normalized()
		p.speed = speed
		p.amplitude = amp * global_amplitude_mult
		p.radius = radius
		p.wavelength = wavelength
		p.age = 0.0
		p.lifetime = lifetime
		p.steepness = steepness
		p.active = true
		p.is_ambient = is_ambient
	return p

## Spawns a radial disturbance of wave particles expanding outward in a circular ring
func spawn_ripple(
	center_pos: Vector2,
	count: int = 16,
	amplitude: float = 1.8,
	radius: float = 24.0,
	speed: float = 22.0,
	wavelength: float = 14.0,
	lifetime: float = 9.0
) -> void:
	var angle_step = TAU / float(count)
	for i in range(count):
		var angle = float(i) * angle_step + randf_range(-0.1, 0.1)
		var dir = Vector2(cos(angle), sin(angle))
		var start_p = center_pos + dir * 4.0
		spawn_particle(start_p, dir, speed, amplitude, radius, wavelength, lifetime, 0.5, false)

## Spawns a planar wavefront of ocean swell wave particles traveling in dir
func spawn_ocean_swell(
	dir: Vector2 = Vector2(0.8, 0.6),
	count: int = 10,
	amplitude: float = 2.0,
	radius: float = 55.0,
	speed: float = 24.0,
	wavelength: float = 35.0
) -> void:
	var norm_dir = dir.normalized()
	var perp = Vector2(-norm_dir.y, norm_dir.x)
	var start_base = Vector2.ZERO if norm_dir.x > 0 else Vector2(world_size, 0)
	for i in range(count):
		var offset = float(i) * (world_size / float(count))
		var p_pos = start_base + perp * offset
		p_pos.x = fposmod(p_pos.x, world_size)
		p_pos.y = fposmod(p_pos.y, world_size)
		spawn_particle(p_pos, norm_dir, speed, amplitude, radius, wavelength, 25.0, 0.55, true)

func update_waves() -> void:
	if not waves.is_empty():
		global_amplitude_mult = waves[0].amplitude / 2.0
		for p in particles:
			if p.active and p.is_ambient:
				p.amplitude = 1.4 * global_amplitude_mult

func _init_compute() -> void:
	if not static_cage_texture or not heightmap_texture:
		return

	rd = RenderingServer.get_rendering_device()
	if rd == null:
		printerr("WaveComputeDriver: Compute shaders not supported on this renderer.")
		return

	cached_cage_image = static_cage_texture.get_image()

	# 1. Compile Wave Particle Compute Shader
	var shader_file = load("res://assets/shaders/wave_compute.glsl")
	var spirv: RDShaderSPIRV = shader_file.get_spirv()
	var shader_res = rd.shader_create_from_spirv(spirv)
	if not shader_res.is_valid():
		var err = spirv.get_stage_compile_error(RenderingDevice.SHADER_STAGE_COMPUTE)
		printerr("WaveComputeDriver: Compute shader compilation failed: ", err)
		return
	shader = shader_res
	pipeline = rd.compute_pipeline_create(shader)

	# 2. Output Textures (Displacement + Normal)
	var fmt := RDTextureFormat.new()
	fmt.width = grid_size
	fmt.height = grid_size
	fmt.format = RenderingDevice.DATA_FORMAT_R32G32B32A32_SFLOAT
	fmt.usage_bits = (
		RenderingDevice.TEXTURE_USAGE_CAN_UPDATE_BIT
		| RenderingDevice.TEXTURE_USAGE_STORAGE_BIT
		| RenderingDevice.TEXTURE_USAGE_CAN_COPY_FROM_BIT
		| RenderingDevice.TEXTURE_USAGE_SAMPLING_BIT
	)

	out_disp_tex = rd.texture_create(fmt, RDTextureView.new())
	out_norm_tex = rd.texture_create(fmt, RDTextureView.new())

	displacement_texture = Texture2DRD.new()
	displacement_texture.texture_rd_rid = out_disp_tex

	normal_texture = Texture2DRD.new()
	normal_texture.texture_rd_rid = out_norm_tex

	if target_material:
		target_material.set_shader_parameter("displacement_tex", displacement_texture)
		target_material.set_shader_parameter("normal_tex", normal_texture)
		target_material.set_shader_parameter("heightmap_tex", heightmap_texture)

	# 3. Uniform Set 0 — Output image2Ds
	var u_disp := RDUniform.new()
	u_disp.uniform_type = RenderingDevice.UNIFORM_TYPE_IMAGE
	u_disp.binding = 0
	u_disp.add_id(out_disp_tex)

	var u_norm := RDUniform.new()
	u_norm.uniform_type = RenderingDevice.UNIFORM_TYPE_IMAGE
	u_norm.binding = 1
	u_norm.add_id(out_norm_tex)

	uniform_set_0 = rd.uniform_set_create([u_disp, u_norm], shader, 0)

	# 4. Shared Linear Sampler
	var ss := RDSamplerState.new()
	ss.mag_filter = RenderingDevice.SAMPLER_FILTER_LINEAR
	ss.min_filter = RenderingDevice.SAMPLER_FILTER_LINEAR
	ss.repeat_u = RenderingDevice.SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE
	ss.repeat_v = RenderingDevice.SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE
	sampler_rid = rd.sampler_create(ss)

	# 5. Cage Texture (Set 1, Binding 0)
	var cage_img := static_cage_texture.get_image()
	cage_img.convert(Image.FORMAT_RGBAF)
	var cage_fmt := RDTextureFormat.new()
	cage_fmt.width = cage_img.get_width()
	cage_fmt.height = cage_img.get_height()
	cage_fmt.format = RenderingDevice.DATA_FORMAT_R32G32B32A32_SFLOAT
	cage_fmt.usage_bits = (
		RenderingDevice.TEXTURE_USAGE_CAN_UPDATE_BIT
		| RenderingDevice.TEXTURE_USAGE_SAMPLING_BIT
	)
	cage_tex_rid = rd.texture_create(cage_fmt, RDTextureView.new(), [cage_img.get_data()])

	var u_cage := RDUniform.new()
	u_cage.uniform_type = RenderingDevice.UNIFORM_TYPE_SAMPLER_WITH_TEXTURE
	u_cage.binding = 0
	u_cage.add_id(sampler_rid)
	u_cage.add_id(cage_tex_rid)

	# 6. Heightmap Texture (Set 1, Binding 1)
	var hm_img := heightmap_texture.get_image()
	hm_img.decompress()
	hm_img.convert(Image.FORMAT_RF)
	var hm_fmt := RDTextureFormat.new()
	hm_fmt.width = hm_img.get_width()
	hm_fmt.height = hm_img.get_height()
	hm_fmt.format = RenderingDevice.DATA_FORMAT_R32_SFLOAT
	hm_fmt.usage_bits = (
		RenderingDevice.TEXTURE_USAGE_CAN_UPDATE_BIT
		| RenderingDevice.TEXTURE_USAGE_SAMPLING_BIT
	)
	hm_tex_rid = rd.texture_create(hm_fmt, RDTextureView.new(), [hm_img.get_data()])

	var u_hm := RDUniform.new()
	u_hm.uniform_type = RenderingDevice.UNIFORM_TYPE_SAMPLER_WITH_TEXTURE
	u_hm.binding = 1
	u_hm.add_id(sampler_rid)
	u_hm.add_id(hm_tex_rid)

	uniform_set_1 = rd.uniform_set_create([u_cage, u_hm], shader, 1)

	# 7. Params UBO (Set 2) — 32 bytes
	var pb := PackedByteArray()
	pb.resize(32)
	params_buffer = rd.uniform_buffer_create(pb.size(), pb)

	var u_params := RDUniform.new()
	u_params.uniform_type = RenderingDevice.UNIFORM_TYPE_UNIFORM_BUFFER
	u_params.binding = 0
	u_params.add_id(params_buffer)
	uniform_set_2 = rd.uniform_set_create([u_params], shader, 2)

	# 8. Wave Particle SSBO (Set 3) — 64 bytes per Particle
	var particle_bytes := PackedByteArray()
	particle_bytes.resize(MAX_PARTICLES * PARTICLE_STRIDE)
	wave_buffer = rd.storage_buffer_create(particle_bytes.size(), particle_bytes)

	var u_part := RDUniform.new()
	u_part.uniform_type = RenderingDevice.UNIFORM_TYPE_STORAGE_BUFFER
	u_part.binding = 0
	u_part.add_id(wave_buffer)
	uniform_set_3 = rd.uniform_set_create([u_part], shader, 3)

func _process(delta: float) -> void:
	if not rd or not pipeline.is_valid():
		return

	time += delta
	ambient_timer += delta

	# Periodically respawn ambient swell wavefronts from ocean borders
	if ambient_timer > 6.0:
		ambient_timer = 0.0
		spawn_ocean_swell(Vector2(0.85, 0.45), 6, 1.8, 55.0, 24.0, 32.0)

	# 1. Physical Wave Particle Simulation Step (Advection, Shoaling & Coastal Reflection)
	_step_particles(delta)

	# 2. Pack Active Particle Buffer to GPU SSBO
	var particle_bytes := PackedByteArray()
	particle_bytes.resize(MAX_PARTICLES * PARTICLE_STRIDE)
	var active_count: int = 0

	for i in range(MAX_PARTICLES):
		var p = particles[i]
		var off = i * PARTICLE_STRIDE
		if p.active:
			active_count += 1
			particle_bytes.encode_float(off + 0,  p.pos.x)
			particle_bytes.encode_float(off + 4,  p.pos.y)
			particle_bytes.encode_float(off + 8,  p.dir.x)
			particle_bytes.encode_float(off + 12, p.dir.y)
			particle_bytes.encode_float(off + 16, p.speed)
			particle_bytes.encode_float(off + 20, p.amplitude)
			particle_bytes.encode_float(off + 24, p.radius)
			particle_bytes.encode_float(off + 28, p.wavelength)
			particle_bytes.encode_float(off + 32, p.age)
			particle_bytes.encode_float(off + 36, p.lifetime)
			particle_bytes.encode_float(off + 40, p.steepness)
			particle_bytes.encode_float(off + 44, 1.0)
		else:
			particle_bytes.encode_float(off + 44, 0.0)

	rd.buffer_update(wave_buffer, 0, particle_bytes.size(), particle_bytes)

	# 3. Update Params UBO
	var pb := PackedByteArray()
	pb.resize(32)
	pb.encode_float(0,  time)
	pb.encode_float(4,  cage_softness)
	pb.encode_s32(8,    active_count)
	pb.encode_float(12, world_size)
	pb.encode_float(16, height_scale_factor)
	pb.encode_float(20, sea_level_normalized)
	rd.buffer_update(params_buffer, 0, pb.size(), pb)

	# 4. Dispatch Wave Particle Raster / Splatting Compute Pass
	var compute_list := rd.compute_list_begin()
	rd.compute_list_bind_compute_pipeline(compute_list, pipeline)
	rd.compute_list_bind_uniform_set(compute_list, uniform_set_0, 0)
	rd.compute_list_bind_uniform_set(compute_list, uniform_set_1, 1)
	rd.compute_list_bind_uniform_set(compute_list, uniform_set_2, 2)
	rd.compute_list_bind_uniform_set(compute_list, uniform_set_3, 3)

	var groups_x := int(ceil(float(grid_size) / 16.0))
	var groups_y := int(ceil(float(grid_size) / 16.0))
	rd.compute_list_dispatch(compute_list, groups_x, groups_y, 1)
	rd.compute_list_end()

func _step_particles(delta: float) -> void:
	var cage_w = cached_cage_image.get_width() if cached_cage_image else 0
	var cage_h = cached_cage_image.get_height() if cached_cage_image else 0

	for p in particles:
		if not p.active:
			continue

		p.age += delta
		if p.age >= p.lifetime or abs(p.amplitude) < 0.01:
			if p.is_ambient:
				# Respawn ambient ocean swell at deep water border
				p.pos = Vector2(randf_range(0.0, world_size * 0.3), randf_range(0.0, world_size))
				p.dir = Vector2(0.85, 0.52).normalized()
				p.amplitude = 1.4 * global_amplitude_mult
				p.speed = 22.0 + randf_range(-2.0, 2.0)
				p.age = 0.0
			else:
				p.active = false
				continue

		# Position Advection
		p.pos += p.dir * p.speed * delta

		# Toroidal World Boundary Wrapping
		if p.pos.x < 0.0: p.pos.x += world_size
		elif p.pos.x >= world_size: p.pos.x -= world_size
		if p.pos.y < 0.0: p.pos.y += world_size
		elif p.pos.y >= world_size: p.pos.y -= world_size

		# Coastal Reflection, Estuary Outflow Plume & Shallow Water Shoaling
		if cage_w > 0 and cage_h > 0:
			var u = clamp(p.pos.x / world_size, 0.0, 1.0)
			var v = clamp(p.pos.y / world_size, 0.0, 1.0)
			var px = int(u * float(cage_w - 1))
			var py = int(v * float(cage_h - 1))
			var cage_sample = cached_cage_image.get_pixel(px, py)

			var r1 = cage_sample.r # Distance to nearest shore [metres]
			var r3 = cage_sample.b # Water depth [metres]
			var shore_angle = cage_sample.a # Angle toward shore

			# Estuary Plume & Fluvial Barrier Sample
			if cached_hm_image:
				var hx = clampi(int(u * float(cached_hm_image.get_width() - 1)), 0, cached_hm_image.get_width() - 1)
				var hy = clampi(int(v * float(cached_hm_image.get_height() - 1)), 0, cached_hm_image.get_height() - 1)
				var hm_sample = cached_hm_image.get_pixel(hx, hy)
				var boundary_mask = hm_sample.g
				var plume_vel = Vector2(hm_sample.b, hm_sample.a)

				# 1. Estuary Absorbing Gate: If wave packet enters an inland river channel or estuary boundary
				if boundary_mask > 0.5:
					p.amplitude *= exp(-18.0 * delta)
					if abs(p.amplitude) < 0.02:
						p.active = false
						continue

				# 2. Wave-Current Interaction in River Plume:
				var plume_speed = plume_vel.length()
				if plume_speed > 0.05:
					# Opposing current (plume flows seaward against wave heading landward)
					var opp_flow = -p.dir.dot(plume_vel.normalized()) * plume_speed
					if opp_flow > 0.1:
						# Opposing flow retards wave velocity and steepens wave profile
						p.speed = max(4.0, p.speed - opp_flow * 2.5 * delta)
						p.steepness = min(1.0, p.steepness + opp_flow * 0.4 * delta)
						# Whitecap wave breaking dissipation on the estuary bar
						p.amplitude *= exp(-opp_flow * 1.6 * delta)

			if r3 < 0.15:
				# Reached dry beach or land: rapidly dissipate wave packet
				p.amplitude *= exp(-9.0 * delta)
			else:
				# Shallow water shoaling: velocity decreases with sqrt(depth)
				var depth_clamped = clamp(r3 / 18.0, 0.12, 1.0)
				var target_speed = 24.0 * sqrt(depth_clamped)
				p.speed = lerp(p.speed, target_speed, 4.0 * delta)

				# Coastal Shoaling & Boundary Cage Absorption (Jeschke 2020)
				# Waves do NOT bounce off coast like billiard balls; they shoal, refract toward the coast,
				# and their displacement is absorbed and smoothly damped by the boundary cage into the shoreline.
				if r1 < p.radius * 0.70:
					var shore_normal = Vector2(-cos(shore_angle), -sin(shore_angle))
					var coast_dir = -shore_normal # points directly toward the coast
					p.dir = p.dir.lerp(coast_dir, 2.8 * delta).normalized()
					# Boundary cage absorption and dissipation in surf zone
					var surf_prox = 1.0 - (r1 / (p.radius * 0.70))
					p.amplitude *= exp(-2.5 * surf_prox * delta)

		# Gradual natural wave packet damping
		p.amplitude *= exp(-0.06 * delta)

func update_textures(new_cage_tex: Texture2D, new_heightmap_tex: Texture2D) -> void:
	static_cage_texture = new_cage_tex
	heightmap_texture = new_heightmap_tex
	if new_cage_tex:
		grid_size = new_cage_tex.get_width()
		cached_cage_image = new_cage_tex.get_image()
	if new_heightmap_tex:
		cached_hm_image = new_heightmap_tex.get_image()
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
