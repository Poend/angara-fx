// Transient runtime data. Authored materials remain normal text ShaderMaterials.
uniform sampler2D nb_particle_attributes : filter_nearest, repeat_disable;
uniform bool nb_particle_attributes_enabled = false;
uniform int nb_particle_attribute_stride = 1;
// Source renderer's material-parameter validity, independent of attribute storage.
// Each material DynamicParameter expression retains its own defaults per channel.
#ifdef NB_PARTICLE_CONTEXT_FROM_GEOMETRY
// One flat register carries discrete renderer metadata. Keeping four separate
// scalar varyings wastes interpolator locations in mixed material programs.
varying flat ivec4 nb_context_metadata;
varying flat vec2 nb_subimage_size;
#define nb_dynamic_parameter_valid_mask nb_context_metadata.x
#define nb_subimage_blend_flag nb_context_metadata.y
#define nb_renderer_kind nb_context_metadata.z
#define nb_context_slot nb_context_metadata.w
#define nb_subimage_blend (nb_subimage_blend_flag != 0)
#else
uniform int nb_dynamic_parameter_valid_mask = 0;
uniform vec2 nb_subimage_size = vec2(1.0);
uniform bool nb_subimage_blend = false;
#endif
varying vec4 nb_attributes;

vec4 nb_dynamic_parameters_at(vec4 values, vec4 defaults, bool values_available, int parameter_index) {
	int mask = values_available ? (nb_dynamic_parameter_valid_mask >> (parameter_index * 4)) : 0;
	return vec4(
		(mask & 1) != 0 ? values.r : defaults.r,
		(mask & 2) != 0 ? values.g : defaults.g,
		(mask & 4) != 0 ? values.b : defaults.b,
		(mask & 8) != 0 ? values.a : defaults.a
	);
}

// Existing authored index0 shaders keep their original function contract.
vec4 nb_dynamic_parameters(vec4 values, vec4 defaults, bool values_available) {
	return nb_dynamic_parameters_at(values, defaults, values_available, 0);
}

vec4 nb_read_attribute_record(int index, int field) {
	if (!nb_particle_attributes_enabled) {
		return field == 0 ? vec4(0.0, 0.0, 1.0, 1.0) : vec4(1.0);
	}
	int stride = max(nb_particle_attribute_stride, 1);
	if (index < 0 || field < 0 || field >= stride) { return vec4(1.0); }
	index = index * stride + field;
	int width = max(textureSize(nb_particle_attributes, 0).x, 1);
	return texelFetch(nb_particle_attributes, ivec2(index % width, index / width), 0);
}

vec4 nb_read_attributes(int index) { return nb_read_attribute_record(index, 0); }

vec2 nb_sprite_uv(vec2 uv) {
	return (uv - vec2(0.5)) * nb_attributes.zw + vec2(0.5);
}

vec2 nb_atlas_uv(vec2 uv, float frame) {
	vec2 grid = max(nb_subimage_size, vec2(1.0));
	float index = clamp(floor(frame), 0.0, grid.x * grid.y - 1.0);
	return (uv + vec2(mod(index, grid.x), floor(index / grid.x))) / grid;
}
