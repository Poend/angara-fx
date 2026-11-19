#include "material_compiler.h"
#include "material_shader_text.h"
namespace godot::nbmaterial {
Dictionary Context::shader_support() {
    TypedArray<String> header, vertex;
    bool batch = bool(options.get("batch", false));
    if (features.has("blackbody"))
        header.append(shader_text::blackbody);
    if (features.has("native_unlit_color_limit"))
        header.append(shader_text::unlit_limit);
    if (features.has("tangent_frame"))
        feature("particle");
    if (features.has("particle")) {
        uniforms.merge(dict({{"nb_particle_attributes_enabled", batch},
                             {"nb_particle_attribute_stride", batch ? 10 : 1},
                             {"nb_use_vertex_custom", batch}}));
        if (!batch)
            uniforms.merge(dict({{"nb_dynamic_parameter_valid_mask", 0},
                                 {"nb_subimage_size", Vector2(1, 1)},
                                 {"nb_subimage_blend", false},
                                 {"nb_renderer_kind", 0}}));
        if (batch)
            header.append("#define NB_PARTICLE_CONTEXT_FROM_GEOMETRY");
        header.append(shader_text::particle_include);
        header.append(shader_text::dynamic_uniforms);
        for (int i = 1; i < 4; ++i)
            header.append("varying vec4 nb_dynamic_values" + String::num_int64(i) + ";");
        header.append(batch ? "varying vec4 nb_particle_color;" : "uniform int nb_renderer_kind = 0;");
        header.append(shader_text::sprite_uv);
        if (features.has("particle_subuv")) {
            header.append(shader_text::source_subuv);
            header.append(shader_text::subuv_lerp);
        }
        vertex.append(batch ? "nb_attributes = nb_read_attributes(int(CUSTOM1.y));"
                            : "nb_attributes = nb_read_attributes(INSTANCE_ID);");
        vertex.append(batch ? "nb_dynamic_values = CUSTOM0;"
                            : "nb_dynamic_values = nb_use_vertex_custom ? CUSTOM0 : INSTANCE_CUSTOM;");
        for (int i = 1; i < 4; ++i) {
            String field = String::num_int64(i);
            vertex.append("nb_dynamic_values" + field + " = " +
                          (batch ? "nb_read_attribute_record(int(CUSTOM1.y)," + field + ")"
                                 : "(nb_use_vertex_custom ? CUSTOM" + field +
                                       " : nb_read_attribute_record(INSTANCE_ID," + field + "))") +
                          ";");
        }
        if (batch) {
            vertex.append(shader_text::particle_color_read);
            vertex.append(shader_text::particle_color_assign);
        } else {
            uniforms["nb_vertex_colors_enabled"] = false;
            header.append(shader_text::vertex_color_include);
            vertex.append(shader_text::vertex_color_assign);
        }
    }
    if (features.has("particle_relative_time")) {
        int record = batch ? 9 : 4;
        uniforms["nb_particle_relative_time_record"] = record;
        header.append(String(shader_text::relative_time_header).replace("%d", String::num_int64(record)));
        String index = batch ? "int(CUSTOM1.y)" : "(nb_renderer_kind == 2 ? VERTEX_ID : INSTANCE_ID)";
        vertex.append(String(shader_text::relative_time_vertex).replace("%s", index));
    }
    if (batch && features.has("instance_transform"))
        vertex.append(shader_text::geometry_transform);
    if (features.has("time")) {
        uniforms["simulation_time"] = 0.0;
        header.append(shader_text::simulation_time);
    }
    if (features.has("temporal_view")) {
        uniforms.merge(dict({{"nb_temporal_sample_index", 0.0},
                             {"nb_temporal_sample_count", 1.0},
                             {"nb_temporal_sample_offset", Vector2()}}));
        header.append(shader_text::temporal_view);
    }
    if (features.has("pixel_depth_offset"))
        header.append(shader_text::project_depth);
    if (features.has("exposure")) {
        uniforms["nb_source_exposure"] = 1.0;
        header.append(shader_text::source_exposure);
    }
    if (features.has("world")) {
        header.append(shader_text::world_helpers);
        vertex.append(shader_text::world_position_vertex);
        vertex.append(shader_text::world_normal_vertex);
    }
    if (features.has("fragment_world_to_local") || features.has("fragment_local_to_world")) {
        bool forward = features.has("fragment_local_to_world");
        String matrix = batch ? "nb_geometry_local_to_world" : "MODEL_MATRIX";
        vertex.append(
            "mat4 nb_transported_instance_matrix = " + (forward ? matrix : "inverse(" + matrix + ")") + ";");
        for (int row = 0; row < 3; ++row) {
            String index = String::num_int64(row);
            header.append(String(shader_text::instance_row_header).replace("%d", index));
            vertex.append(String(shader_text::instance_row_vertex).replace("%d", index));
        }
        header.append(shader_text::instance_matrix);
        if (features.has("fragment_world_to_local"))
            header.append(String("#define nb_instance_world_to_local ") +
                          (forward ? "inverse(nb_instance_matrix())" : "nb_instance_matrix()"));
        if (forward)
            header.append("#define nb_instance_local_to_world nb_instance_matrix()");
    }
    if (features.has("tangent_frame")) {
        header.append(shader_text::tangent_header);
        vertex.append(shader_text::tangent_vertex);
        vertex.append(shader_text::binormal_vertex);
        vertex.append(shader_text::normal_vertex);
        vertex.append(shader_text::sprite_tangent_sign);
    }
    if (features.has("bounds")) {
        uniforms["nb_mesh_bounds_min"] = Vector3();
        uniforms["nb_mesh_bounds_size"] = Vector3();
        header.append(shader_text::bounds);
    }
    if (features.has("object_position")) {
        uniforms["nb_object_world_center"] = Vector3();
        header.append("uniform vec3 nb_object_world_center = vec3(0.0);");
    }
    if (features.has("normal_decode"))
        header.append(shader_text::normal_decode);
    if (features.has("positive_power")) {
        header.append(shader_text::positive_power);
        for (int width = 2; width < 5; ++width) {
            TypedArray<String> components;
            for (int index = 0; index < width; ++index) {
                String field = String("xyzw").substr(index, 1);
                components.append("nb_positive_power(b." + field + ",e." + field + ")");
            }
            String w = String::num_int64(width);
            header.append("vec" + w + " nb_positive_power(vec" + w + " b,vec" + w + " e) { return vec" + w +
                          "(" + join(components, ",") + "); }");
        }
    }
    for (const char *name : {"round_even", "safe_divisor"})
        if (features.has(name)) {
            bool round = String(name) == "round_even";
            header.append(round ? shader_text::round_even : shader_text::safe_divisor);
            for (int width = 2; width < 5; ++width) {
                TypedArray<String> parts;
                for (int index = 0; index < width; ++index)
                    parts.append(String(round ? "nb_round_even(x." : "nb_safe_divisor(d.") +
                                 String("xyzw").substr(index, 1) + ")");
                String w = String::num_int64(width);
                header.append("vec" + w + " " + String(round ? "nb_round_even" : "nb_safe_divisor") + "(vec" +
                              w + " " + String(round ? "x" : "d") + ") { return vec" + w + "(" +
                              join(parts, ",") + "); }");
            }
        }
    if (features.has("rotate_axis"))
        header.append(shader_text::rotate_axis);
    if (features.has("pseudorandom"))
        header.append(shader_text::pseudorandom);
    if (features.has("pno")) {
        uniforms["nb_deferred_refraction"] = false;
        header.append(shader_text::refraction);
    }
    if (features.has("depth")) {
        header.append(shader_text::depth_sampler);
        header.append(shader_text::view_depth);
    }
    return dict({{"header", join(header, "\n")}, {"vertex", vertex}});
}
} // namespace godot::nbmaterial
