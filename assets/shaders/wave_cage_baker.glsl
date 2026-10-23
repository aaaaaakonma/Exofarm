#[compute]
#version 450

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D heightmap_tex;
layout(rgba32f, set = 0, binding = 1) uniform image2D image_in;
layout(rgba32f, set = 0, binding = 2) uniform image2D image_out;
layout(rgba32f, set = 0, binding = 3) uniform image2D image_orig;

layout(push_constant, std430) uniform Params {
	int pass_index; // 0: init, 1: jfa, 2: radii, 3: blur
	int step_size;
	int max_steps;
	int grid_size;
	float sea_level;
	float height_scale;
	float physical_size;
	float max_radius;
	float world_dx;
} pc;

float get_depth(ivec2 p) {
	if (p.x < 0 || p.x >= pc.grid_size || p.y < 0 || p.y >= pc.grid_size) return 100.0;
	vec2 uv = (vec2(p) + 0.5) / float(pc.grid_size);
	float h = texture(heightmap_tex, uv).r; // Direct physical meters
	return -h; // Sea level is 0.0m, water depth is -h
}

void pass_init(ivec2 pos) {
	float d = get_depth(pos);
	if (d <= 0.0) {
		// Land. Check if it's a shoreline (adjacent to water).
		bool is_shore = false;
		if (get_depth(pos + ivec2(1, 0)) > 0.0) is_shore = true;
		if (get_depth(pos + ivec2(-1, 0)) > 0.0) is_shore = true;
		if (get_depth(pos + ivec2(0, 1)) > 0.0) is_shore = true;
		if (get_depth(pos + ivec2(0, -1)) > 0.0) is_shore = true;

		if (is_shore) {
			// XY = seed coords, Z = distance
			imageStore(image_out, pos, vec4(vec2(pos), 0.0, 1.0));
		} else {
			imageStore(image_out, pos, vec4(-1.0, -1.0, 999999.0, 0.0));
		}
	} else {
		// Water pixels start with no seed.
		imageStore(image_out, pos, vec4(-1.0, -1.0, 999999.0, 0.0));
	}
}

void pass_jfa(ivec2 pos) {
	vec4 best_data = imageLoad(image_in, pos);
	float best_dist = best_data.z;
	vec2 best_seed = best_data.xy;

	// Check 9 neighbors at JFA step distance
	for (int y = -1; y <= 1; y++) {
		for (int x = -1; x <= 1; x++) {
			ivec2 n_pos = pos + ivec2(x, y) * pc.step_size;
			if (n_pos.x >= 0 && n_pos.x < pc.grid_size && n_pos.y >= 0 && n_pos.y < pc.grid_size) {
				vec4 n_data = imageLoad(image_in, n_pos);
				if (n_data.x >= 0.0) { // Valid seed
					vec2 seed = n_data.xy;
					float dist = length(vec2(pos) - seed);
					if (dist < best_dist) {
						best_dist = dist;
						best_seed = seed;
					}
				}
			}
		}
	}
	imageStore(image_out, pos, vec4(best_seed, best_dist, 1.0));
}

float raycast(vec2 start_pos, vec2 dir) {
	vec2 p = start_pos + 0.5; // Start at pixel center
	for (int i = 1; i <= pc.max_steps; i++) {
		p += dir;
		ivec2 ip = ivec2(p);
		if (ip.x < 0 || ip.x >= pc.grid_size || ip.y < 0 || ip.y >= pc.grid_size) {
			return float(pc.max_steps) * pc.world_dx;
		}
		if (get_depth(ip) <= 0.0) {
			return float(i) * pc.world_dx;
		}
	}
	return float(pc.max_steps) * pc.world_dx;
}

void pass_radii(ivec2 pos) {
	float d = get_depth(pos);
	if (d <= 0.0) {
		// Land / waterline Dirichlet condition: radii = 0
		imageStore(image_out, pos, vec4(0.0));
		return;
	}

	vec4 jfa_data = imageLoad(image_in, pos);
	vec2 seed = jfa_data.xy;
	float dist_grid = jfa_data.z;

	// r1: distance to nearest shore
	float r1 = clamp(dist_grid * pc.world_dx, 0.0, pc.max_radius);

	// Calculate shore direction from seed
	float a1_gx = seed.x - float(pos.x);
	float a1_gz = seed.y - float(pos.y);
	float angle = 0.0;
	if (dist_grid > 0.01 && seed.x >= 0.0) {
		angle = atan(a1_gz, a1_gx);
	} else {
		a1_gx = 1.0; a1_gz = 0.0;
	}

	// r2: perpendicular raycast to find narrowing straits/channels
	vec2 perp = vec2(-a1_gz, a1_gx);
	float len = length(perp);
	if (len > 0.001) perp /= len; else perp = vec2(0.0, 1.0);

	float r2p = raycast(vec2(pos), perp);
	float r2n = raycast(vec2(pos), -perp);
	float r2 = clamp(min(r2p, r2n), 0.0, pc.max_radius);

	// r3: depth constraint
	float r3 = clamp(d, 0.0, pc.max_radius);

	imageStore(image_out, pos, vec4(r1, r2, r3, angle));
}

void pass_blur(ivec2 pos) {
	float d = get_depth(pos);
	if (d <= 0.0) {
		imageStore(image_out, pos, vec4(0.0));
		return;
	}

	// Keep boundaries pinned
	if (pos.x == 0 || pos.y == 0 || pos.x == pc.grid_size - 1 || pos.y == pc.grid_size - 1) {
		imageStore(image_out, pos, imageLoad(image_orig, pos));
		return;
	}

	vec4 sum = vec4(0.0);
	float count = 0.0;

	// 3x3 Box blur
	for (int y = -1; y <= 1; y++) {
		for (int x = -1; x <= 1; x++) {
			sum += imageLoad(image_in, pos + ivec2(x, y));
			count += 1.0;
		}
	}
	vec4 avg = sum / count;
	vec4 orig = imageLoad(image_orig, pos);

	// Jeschke 2020 inequality constraint: smoothed <= original_fitted
	vec4 res;
	res.r = min(avg.r, orig.r);
	res.g = min(avg.g, orig.g);
	res.b = min(avg.b, orig.b);
	res.a = orig.a; // Do not blur the angle!

	imageStore(image_out, pos, res);
}

void main() {
	ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
	if (pos.x >= pc.grid_size || pos.y >= pc.grid_size) return;

	if (pc.pass_index == 0) pass_init(pos);
	else if (pc.pass_index == 1) pass_jfa(pos);
	else if (pc.pass_index == 2) pass_radii(pos);
	else if (pc.pass_index == 3) pass_blur(pos);
}
