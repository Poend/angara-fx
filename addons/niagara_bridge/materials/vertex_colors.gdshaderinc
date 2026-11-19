// Optional transient float colors for generated geometry. Authored mesh colors
// remain unchanged when disabled; no custom dynamic-parameter channel is used.
uniform sampler2D nb_vertex_colors : filter_nearest, repeat_disable;
uniform bool nb_vertex_colors_enabled = false;
vec4 nb_read_vertex_color(int vertex_id) {
	int width = max(textureSize(nb_vertex_colors, 0).x, 1);
	return texelFetch(nb_vertex_colors, ivec2(vertex_id % width, vertex_id / width), 0);
}
