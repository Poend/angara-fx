shader_type spatial;
render_mode unshaded, cull_disabled;
uniform vec4 grid_color : source_color = vec4(0.25, 0.28, 0.32, 1.0);
uniform vec4 ground_color : source_color = vec4(0.075, 0.085, 0.1, 1.0);
varying vec3 world_position;
void vertex() { world_position = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz; }
void fragment() {
	vec2 p = world_position.xz * 2.0;
	vec2 line = abs(fract(p - 0.5) - 0.5) / max(fwidth(p), vec2(0.0001));
	float grid = 1.0 - min(min(line.x, line.y), 1.0);
	ALBEDO = mix(ground_color.rgb, grid_color.rgb, grid * 0.6);
}
