shader_type spatial;
render_mode unshaded, blend_add, cull_disabled, depth_draw_never;

void fragment() {
    float radius = length(2.0 * UV - 1.0);
    float glow = 1.0 - smoothstep(0.08, 1.0, radius);
    ALBEDO = COLOR.rgb * mix(vec3(1.0, 0.24, 0.04), vec3(1.0, 0.94, 0.55), glow * glow);
    ALPHA = glow * COLOR.a;
}
