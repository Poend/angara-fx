#pragma once
// Static GLSL templates from the existing material compiler; no runtime script calls.
namespace godot::nbmaterial::shader_text {
// Rational approximation of Planckian chromaticity in CIE 1960 UCS.
inline constexpr const char *blackbody = R"NB_SHADER(vec3 nb_blackbody(float kelvin) {
    vec3 powers = vec3(1.0, kelvin, kelvin * kelvin);
    float u = dot(powers, vec3(0.860117757, 0.000154118254, 1.28641212e-7))
            / dot(powers, vec3(1.0, 0.000842420235, 7.08145163e-7));
    float v = dot(powers, vec3(0.317398726, 4.22806245e-5, 4.20481691e-8))
            / dot(powers, vec3(1.0, -2.89741816e-5, 1.61456053e-7));
    vec3 xyz = vec3(3.0*u, 2.0*v, 4.0-u-10.0*v) / (2.0*v);
    vec3 rgb = vec3(dot(xyz, vec3(3.240969942,-1.537383178,-0.498610760)),
                    dot(xyz, vec3(-0.969243636,1.875967502,0.041555057)),
                    dot(xyz, vec3(0.055630080,-0.203976959,1.056971514)));
    float intensity = kelvin * 0.0004;
    intensity *= intensity;
    return max(rgb, vec3(0.0)) * intensity * intensity;
})NB_SHADER";
inline constexpr const char *unlit_limit = R"NB_SHADER(vec3 nb_limit_unlit_color(vec3 color, float opacity) { return opacity > 0.0 ? min(color, vec3(32256.0) / opacity) : vec3(0.0); })NB_SHADER";
inline constexpr const char *particle_include = R"NB_SHADER(#include "res://addons/niagara_bridge/materials/particle_attributes.gdshaderinc")NB_SHADER";
inline constexpr const char *dynamic_uniforms = R"NB_SHADER(uniform bool nb_use_vertex_custom = false;
varying vec4 nb_dynamic_values;)NB_SHADER";
inline constexpr const char *sprite_uv = R"NB_SHADER(vec2 nb_source_sprite_uv(vec2 uv,int channel) { vec2 grid=max(nb_subimage_size,vec2(1.0)); float frame=floor(nb_attributes.x)+0.5+float(channel); float x=floor(frame-grid.x*trunc(frame/grid.x)); float y=floor(frame/grid.x); return (vec2(x,y)+nb_sprite_uv(uv))/grid; })NB_SHADER";
inline constexpr const char *source_subuv = R"NB_SHADER(vec2 nb_source_subuv(vec2 uv,int frame_offset) { if(nb_renderer_kind == 1) return nb_source_sprite_uv(uv,frame_offset); if(nb_renderer_kind == 3) { vec2 grid=max(nb_subimage_size,vec2(1.0)); float frame=floor(nb_attributes.x)+float(frame_offset); return (vec2(frame-grid.x*trunc(frame/grid.x),floor(frame/grid.x))+uv)/grid; } return nb_renderer_kind == 2 ? vec2(0.0) : uv; })NB_SHADER";
inline constexpr const char *subuv_lerp = R"NB_SHADER(float nb_source_subuv_lerp() { return (nb_renderer_kind == 1 || nb_renderer_kind == 3) && nb_subimage_blend ? fract(nb_attributes.x) : 0.0; })NB_SHADER";
inline constexpr const char *particle_color_read = R"NB_SHADER(nb_particle_color = nb_read_attribute_record(int(CUSTOM1.y),4);)NB_SHADER";
inline constexpr const char *particle_color_assign = R"NB_SHADER(if (nb_renderer_kind != 3) { COLOR = nb_particle_color; })NB_SHADER";
inline constexpr const char *vertex_color_include = R"NB_SHADER(#include "res://addons/niagara_bridge/materials/vertex_colors.gdshaderinc")NB_SHADER";
inline constexpr const char *vertex_color_assign = R"NB_SHADER(if (nb_vertex_colors_enabled) { COLOR = nb_read_vertex_color(VERTEX_ID); })NB_SHADER";
inline constexpr const char *relative_time_header = R"NB_SHADER(uniform int nb_particle_relative_time_record = %d;
varying float nb_particle_relative_time;)NB_SHADER";
inline constexpr const char *relative_time_vertex = R"NB_SHADER(nb_particle_relative_time = nb_particle_attributes_enabled && nb_particle_attribute_stride > nb_particle_relative_time_record ? nb_read_attribute_record(%s,nb_particle_relative_time_record).x : 0.0;)NB_SHADER";
inline constexpr const char *geometry_transform = R"NB_SHADER(mat4 nb_geometry_local_to_world = mat4(nb_read_attribute_record(int(CUSTOM1.y),5),nb_read_attribute_record(int(CUSTOM1.y),6),nb_read_attribute_record(int(CUSTOM1.y),7),nb_read_attribute_record(int(CUSTOM1.y),8));)NB_SHADER";
inline constexpr const char *simulation_time = R"NB_SHADER(uniform float simulation_time = 0.0;)NB_SHADER";
inline constexpr const char *temporal_view = R"NB_SHADER(uniform float nb_temporal_sample_index = 0.0;
uniform float nb_temporal_sample_count = 1.0;
uniform vec2 nb_temporal_sample_offset = vec2(0.0);)NB_SHADER";
inline constexpr const char *project_depth = R"NB_SHADER(float nb_project_depth(float axial_depth,mat4 projection) { vec4 projected=projection*vec4(0.0,0.0,-axial_depth,1.0); return projected.z/projected.w; })NB_SHADER";
inline constexpr const char *source_exposure = R"NB_SHADER(uniform float nb_source_exposure = 1.0;)NB_SHADER";
inline constexpr const char *world_helpers = R"NB_SHADER(varying vec3 nb_original_world_position;
varying vec3 nb_original_world_normal;
vec3 nb_to_ue_direction(vec3 g) { return vec3(-g.z,g.x,g.y); }
vec3 nb_to_godot_direction(vec3 u) { return vec3(u.y,u.z,-u.x); })NB_SHADER";
inline constexpr const char *world_position_vertex = R"NB_SHADER(nb_original_world_position = (MODEL_MATRIX * vec4(VERTEX,1.0)).xyz;)NB_SHADER";
inline constexpr const char *world_normal_vertex = R"NB_SHADER(nb_original_world_normal = normalize(MODEL_NORMAL_MATRIX * NORMAL);)NB_SHADER";
inline constexpr const char *instance_row_header = R"NB_SHADER(varying flat vec4 nb_instance_row%d;)NB_SHADER";
inline constexpr const char *instance_row_vertex = R"NB_SHADER(nb_instance_row%d = vec4(nb_transported_instance_matrix[0][%d],nb_transported_instance_matrix[1][%d],nb_transported_instance_matrix[2][%d],nb_transported_instance_matrix[3][%d]);)NB_SHADER";
inline constexpr const char *instance_matrix = R"NB_SHADER(mat4 nb_instance_matrix() { return mat4(vec4(nb_instance_row0.x,nb_instance_row1.x,nb_instance_row2.x,0.0),vec4(nb_instance_row0.y,nb_instance_row1.y,nb_instance_row2.y,0.0),vec4(nb_instance_row0.z,nb_instance_row1.z,nb_instance_row2.z,0.0),vec4(nb_instance_row0.w,nb_instance_row1.w,nb_instance_row2.w,1.0)); })NB_SHADER";
inline constexpr const char *tangent_header = R"NB_SHADER(varying vec3 nb_view_tangent;
varying vec3 nb_view_binormal;
varying vec3 nb_view_normal;)NB_SHADER";
inline constexpr const char *tangent_vertex = R"NB_SHADER(nb_view_tangent = normalize((MODELVIEW_MATRIX * vec4(TANGENT,0.0)).xyz);)NB_SHADER";
inline constexpr const char *binormal_vertex = R"NB_SHADER(nb_view_binormal = normalize((MODELVIEW_MATRIX * vec4(BINORMAL,0.0)).xyz);)NB_SHADER";
inline constexpr const char *normal_vertex = R"NB_SHADER(nb_view_normal = normalize(MODELVIEW_NORMAL_MATRIX * NORMAL);)NB_SHADER";
// Sprite geometry maps Niagara's increasing V to local -Y on the Godot quad.
// QuadMesh's authored binormal is local +Y. Mesh and ribbon bases are independent.
inline constexpr const char *sprite_tangent_sign = R"NB_SHADER(if (nb_renderer_kind == 1) { nb_view_tangent *= nb_attributes.z < 0.0 ? -1.0 : 1.0; nb_view_binormal *= nb_attributes.w < 0.0 ? 1.0 : -1.0; })NB_SHADER";
inline constexpr const char *bounds = R"NB_SHADER(uniform vec3 nb_mesh_bounds_min = vec3(0.0);
uniform vec3 nb_mesh_bounds_size = vec3(0.0);)NB_SHADER";
inline constexpr const char *normal_decode = R"NB_SHADER(vec4 nb_decode_normal(vec4 t,bool flip) { vec2 xy=t.rg*2.0-1.0; if(flip){xy.y=-xy.y;} return vec4(xy,sqrt(clamp(1.0-dot(xy,xy),0.0,1.0)),1.0); })NB_SHADER";
inline constexpr const char *positive_power = R"NB_SHADER(float nb_positive_power(float b,float e) { return b <= 0.00000002980233 ? 0.0 : pow(b,e); })NB_SHADER";
inline constexpr const char *round_even = R"NB_SHADER(float nb_round_even(float x) { float f=floor(x); float r=x-f; return r<0.5 ? f : (r>0.5 ? f+1.0 : (mod(f,2.0)==0.0 ? f : f+1.0)); })NB_SHADER";
inline constexpr const char *safe_divisor = R"NB_SHADER(float nb_safe_divisor(float d) { return abs(d)<0.00001 ? (d<0.0 ? -0.00001 : 0.00001) : d; })NB_SHADER";
inline constexpr const char *rotate_axis = R"NB_SHADER(vec3 nb_rotate_axis(vec3 axis,float angle,vec3 pivot,vec3 position) { vec3 closest=pivot+axis*dot(axis,position-pivot); vec3 u=position-closest; vec3 v=cross(axis,u); float a=angle*6.28318530718; return closest+u*cos(a)+v*sin(a)-position; })NB_SHADER";
inline constexpr const char *pseudorandom = R"NB_SHADER(float nb_pseudorandom(vec2 xy) { vec2 p=fract(xy/128.0)*128.0+vec2(-64.340622,-72.465622); return fract(dot(p.xyx*p.xyy,vec3(20.390625,60.703125,2.4281209))); })NB_SHADER";
inline constexpr const char *refraction = R"NB_SHADER(uniform bool nb_deferred_refraction = false;
uniform sampler2D nb_scene_texture : hint_screen_texture, filter_linear, repeat_disable;)NB_SHADER";
inline constexpr const char *depth_sampler = R"NB_SHADER(uniform sampler2D nb_depth_texture : hint_depth_texture, filter_nearest, repeat_disable;)NB_SHADER";
inline constexpr const char *view_depth = R"NB_SHADER(float nb_view_depth(vec2 uv, mat4 inv_projection) { float d=textureLod(nb_depth_texture,uv,0.0).r; vec3 ndc=vec3(uv*2.0-1.0,d);
#if CURRENT_RENDERER == RENDERER_COMPATIBILITY
ndc.z=d*2.0-1.0;
#endif
vec4 p=inv_projection*vec4(ndc,1.0); return -p.z/p.w; })NB_SHADER";
}
