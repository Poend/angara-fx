#include "material_composer.h"
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/core/class_db.hpp>
namespace godot {
namespace {
using namespace nbmaterial;
Array filter(const Array &modes, const String &prefix, bool include) {
    Array result;
    for (int64_t i = 0; i < modes.size(); ++i) {
        String mode = modes[i];
        if (mode.begins_with(prefix) == include)
            result.append(mode);
    }
    return result;
}
Array lit_modes(const Array &modes) {
    Array result;
    if (modes.has("specular_disabled"))
        result.append("specular_disabled");
    return result;
}
Array remove_lighting(const Array &modes) {
    Array result;
    for (int64_t i = 0; i < modes.size(); ++i) {
        String mode = modes[i];
        if (mode != "unshaded" && mode != "specular_disabled")
            result.append(mode);
    }
    return result;
}
String context_label(int64_t index) {
    return "Context" + String::num_int64(index) + ": ";
}
} // namespace
Array NBNativeMaterialComposer::common_modes(const Array &modes) {
    Array result;
    for (int64_t i = 0; i < modes.size(); ++i) {
        String mode = modes[i];
        if (!mode.begins_with("cull_") && !mode.begins_with("blend_") && mode != "unshaded" &&
            mode != "specular_disabled")
            result.append(mode);
    }
    return result;
}
Dictionary NBNativeMaterialComposer::check_binding_limits(int64_t textures, int64_t samplers,
                                                          const Dictionary &reported, const String &driver) {
    TypedArray<String> errors, requirements;
    Dictionary observations,
        result = dict({{"errors", errors}, {"requirements", requirements}, {"observations", observations}});
    const RenderingDevice::Limit limits[] = {RenderingDevice::LIMIT_MAX_TEXTURES_PER_UNIFORM_SET,
                                             RenderingDevice::LIMIT_MAX_TEXTURES_PER_SHADER_STAGE,
                                             RenderingDevice::LIMIT_MAX_SAMPLERS_PER_UNIFORM_SET,
                                             RenderingDevice::LIMIT_MAX_SAMPLERS_PER_SHADER_STAGE};
    const char *names[] = {"textures_per_uniform_set", "textures_per_shader_stage",
                           "samplers_per_uniform_set", "samplers_per_shader_stage"};
    for (int i = 0; i < 4; ++i) {
        int64_t value = reported.get(int64_t(limits[i]), 0), count = i < 2 ? textures : samplers;
        String status = "finite", name = names[i];
        if (driver == "d3d12" && limits[i] == RenderingDevice::LIMIT_MAX_TEXTURES_PER_SHADER_STAGE &&
            value == -1)
            status = "unbounded_uint64_sentinel";
        else if (value <= 0) {
            status = "unknown";
            errors.append("Cannot validate composed " + name + String(": device returned ") +
                          String::num_int64(value) + ".");
        } else if (driver == "d3d12" && value == (1LL << 30)) {
            status = "engine_safe_unbounded";
            if (!requirements.has("d3d12_synthetic_limits_require_gpu_shader_validation"))
                requirements.append("d3d12_synthetic_limits_require_gpu_shader_validation");
            if (count > value)
                errors.append("Composed " + name + String(" count") + String::num_int64(count) +
                              " exceeds engine bound" + String::num_int64(value) + ".");
        } else if (count > value)
            errors.append("Composed " + name + String(" count") + String::num_int64(count) +
                          " exceeds device limit" + String::num_int64(value) + ".");
        observations[name] = dict({{"reported", value}, {"classification", status}, {"requested", count}});
    }
    return result;
}
Dictionary NBNativeMaterialComposer::build(const TypedArray<Dictionary> &contexts, const String &render_pass,
                                           const Ref<Script> &schema) {
    TypedArray<String> errors;
    Dictionary result =
        dict({{"supported", false}, {"material", Variant()}, {"errors", errors}, {"protocol_version", 1}});
    if (render_pass != "surface" && render_pass != "distortion" && render_pass != "native_surface") {
        errors.append("Unsupported system material pass.");
        return result;
    }
    TypedArray<Dictionary> aliases, compiled;
    bool refractive = false;
    if (contexts.is_empty() || contexts.size() > 64) {
        errors.append("System composition requires between1 and64 renderer contexts.");
        return result;
    }
    Dictionary features = dict({{"particle", true}}), blend_modes, uniforms,
               sampler_states = dict({{"filter_nearest, repeat_disable", true}}), global_values;
    Array modes, first_culling, lighting_modes;
    bool mixed_culling = false, light_present = false, lit_seen = false, unlit_seen = false,
         native_premultiplied = false;
    TypedArray<String> declarations, requirements;
    int64_t texture_count = 1;
    for (int64_t index = 0; index < contexts.size(); ++index) {
        Dictionary context = contexts[index];
        Ref<ShaderMaterial> material = context.get("material", Variant());
        TypedArray<String> validation = NBNativeMaterialPrograms::validate(material, schema);
        for (int64_t i = 0; i < validation.size(); ++i)
            errors.append(context_label(index) + String(validation[i]));
        if (!validation.is_empty())
            continue;
        int64_t kind = context.get("renderer_kind", -1);
        if (kind < 1 || kind > 3) {
            errors.append(context_label(index) +
                          "system composition requires sprite, ribbon or mesh geometry.");
            continue;
        }
        Variant grid_v = context.get("subimage_size", Variant());
        Vector2 grid = grid_v.get_type() == Variant::VECTOR2 ? Vector2(grid_v) : Vector2();
        if (grid_v.get_type() != Variant::VECTOR2 || !grid.is_finite() || grid.x < 1 || grid.y < 1) {
            errors.append(context_label(index) + "invalid renderer atlas grid.");
            continue;
        }
        int64_t mask = context.get("dynamic_mask", 0);
        if (mask < 0 || mask > 65535) {
            errors.append(context_label(index) + "invalid material parameter validity mask.");
            continue;
        }
        Dictionary program = material->get_meta("angara_fx_material_program"),
                   values = NBNativeMaterialPrograms::current_values(material);
        Ref<Resource> source = program["graph"], graph = source->duplicate(true);
        String prefix = "nb_m" + String::num_int64(index) + "_";
        Dictionary roots = graph->get("outputs");
        bool has_refraction = roots.has("Refraction") && !text(roots["Refraction"], "source").is_empty();
        refractive |= has_refraction;
        String component_pass =
            render_pass == "distortion" && !has_refraction ? String("surface") : render_pass;
        Dictionary trace, component = NBNativeMaterialCompiler::compile(
                              graph, Dictionary(),
                              dict({{"embedded", true},
                                    {"batch", true},
                                    {"symbol_prefix", prefix},
                                    {"values", values},
                                    {"native_texture_formats", program["native_texture_formats"]},
                                    {"render_pass", component_pass}}),
                              trace);
        Array diagnostics = component["diagnostics"];
        for (int64_t i = 0; i < diagnostics.size(); ++i) {
            Dictionary diagnostic = diagnostics[i];
            if (diagnostic.get("severity", Variant()) == Variant("error"))
                errors.append(
                    context_label(index) +
                    Variant(diagnostic.get("message", Variant(diagnostic).stringify())).stringify());
        }
        if (text(component, "shader_code").is_empty())
            continue;
        Dictionary parts = component["parts"];
        if (render_pass == "distortion" && !has_refraction)
            parts["fragment"] = list({"discard;"});
        aliases.append(Dictionary(parts["uniform_aliases"]).duplicate());
        String source_blend = parts["source_blend"],
               blending = source_blend == "BLEND_Additive"
                              ? "blend_add"
                              : (source_blend == "BLEND_Translucent" ? "blend_mix" :
                                 (source_blend == "BLEND_AlphaComposite" ? "blend_premul_alpha" : "opaque"));
        if (blending == "opaque")
            errors.append(context_label(index) + "opaque geometry belongs in the depth-writing pass.");
        blend_modes[blending] = true;
        native_premultiplied |= bool(parts["native_premultiplied_additive"]);
        Array component_modes = parts["render_modes"];
        if (index == 0) {
            modes = component_modes.duplicate();
            first_culling = filter(modes, "cull_", true);
        } else if (common_modes(component_modes) != common_modes(modes))
            errors.append(context_label(index) +
                          "incompatible render modes cannot share one ordered surface.");
        if (render_pass == "surface") {
            if (component_modes.has("unshaded"))
                unlit_seen = true;
            else {
                Array current_lit = lit_modes(component_modes);
                bool current_light = !text(parts, "light").is_empty();
                if (lit_seen && (current_lit != lighting_modes || current_light != light_present))
                    errors.append(context_label(index) +
                                  "incompatible lit paths (default/custom lighting or specular modes) "
                                  "require an explicit common implementation.");
                lit_seen = true;
                lighting_modes = current_lit;
                light_present = current_light;
            }
        }
        if (filter(component_modes, "cull_", true) != first_culling)
            mixed_culling = true;
        features.merge(parts["features"], true);
        declarations.append_array(parts["declarations"]);
        Array component_declarations = parts["declarations"];
        for (int64_t i = 0; i < component_declarations.size(); ++i) {
            String declaration = component_declarations[i];
            if (declaration.begins_with("uniform sampler")) {
                ++texture_count;
                sampler_states[declaration.get_slice(":", 1).strip_edges()] = true;
            }
        }
        Dictionary mapping = parts["uniform_aliases"], component_uniforms = component["uniforms"];
        Array keys = mapping.keys();
        for (int64_t i = 0; i < keys.size(); ++i)
            uniforms[keys[i]] = component_uniforms[keys[i]];
        for (const char *key :
             {"simulation_time", "nb_source_exposure", "nb_mesh_bounds_min", "nb_mesh_bounds_size","nb_object_world_center"}) {
            if (!component_uniforms.has(key))
                continue;
            Variant value = values.get(key, component_uniforms[key]);
            if (global_values.has(key) && global_values[key] != value)
                errors.append(context_label(index) + "shared " + key + " differs across renderer contexts.");
            global_values[key] = value;
        }
        String c = "nb_c" + String::num_int64(index);
        declarations.append("uniform vec2 " + c + "_grid = vec2(1.0);\nuniform int " + c +
                            "_mask = 0;\nuniform int " + c + "_blend = 0;\nuniform int " + c + "_kind = 1;");
        uniforms[c + "_grid"] = grid;
        uniforms[c + "_mask"] = mask;
        uniforms[c + "_blend"] = bool(context.get("subimage_blend", false)) ? 1 : 0;
        uniforms[c + "_kind"] = kind;
        Array component_requirements = component["requirements"];
        for (int64_t i = 0; i < component_requirements.size(); ++i) {
            String requirement = component_requirements[i];
            if (!requirements.has(requirement))
                requirements.append(requirement);
        }
        compiled.append(component);
    }
    if (!errors.is_empty() || compiled.size() != contexts.size())
        return result;
    if (mixed_culling) {
        modes = filter(modes, "cull_", false);
        modes.push_front("cull_disabled");
    }
    if (render_pass == "distortion" && !refractive) {
        errors.append("System distortion requires a refractive context.");
        return result;
    }
    if (!modes.has("blend_mix") && !modes.has("blend_add") && !modes.has("blend_premul_alpha")) {
        errors.append("Initial shared-surface composition requires compatible transparent render modes.");
        return result;
    }
    bool mixed_blending = blend_modes.size() > 1 && render_pass == "surface",
         premultiplied_surface = render_pass == "surface" && (mixed_blending || native_premultiplied);
    if (premultiplied_surface) {
        modes = filter(modes, "blend_", false);
        modes.append("blend_premul_alpha");
    }
    result["mixed_blending"] = mixed_blending;
    bool mixed_lighting = lit_seen && unlit_seen;
    if (mixed_lighting) {
        modes = remove_lighting(modes);
        modes.append_array(lighting_modes);
        declarations.append("uniform float nb_unlit_emission_scale = 1.0;");
        uniforms["nb_unlit_emission_scale"] = 1.0;
        requirements.append("mixed_lighting_requires_active_view_emission_normalization");
    }
    result["mixed_lighting"] = mixed_lighting;
    if (features.has("depth"))
        ++texture_count;
    if (features.has("pno")) {
        ++texture_count;
        sampler_states["filter_linear, repeat_disable"] = true;
    }
    result["texture_uniform_count"] = texture_count;
    result["sampler_state_count"] = sampler_states.size();
    RenderingDevice *device = RenderingServer::get_singleton()->get_rendering_device();
    if (device) {
        Dictionary reported;
        for (RenderingDevice::Limit limit : {RenderingDevice::LIMIT_MAX_TEXTURES_PER_UNIFORM_SET,
                                             RenderingDevice::LIMIT_MAX_TEXTURES_PER_SHADER_STAGE,
                                             RenderingDevice::LIMIT_MAX_SAMPLERS_PER_UNIFORM_SET,
                                             RenderingDevice::LIMIT_MAX_SAMPLERS_PER_SHADER_STAGE})
            reported[int64_t(limit)] = int64_t(device->limit_get(limit));
        Dictionary limits =
            check_binding_limits(texture_count, sampler_states.size(), reported,
                                 RenderingServer::get_singleton()->get_current_rendering_driver_name());
        result["binding_limits"] = limits["observations"];
        errors.append_array(limits["errors"]);
        requirements.append_array(limits["requirements"]);
        if (!errors.is_empty())
            return result;
    } else
        requirements.append("rendering_device_limits_require_validation_when_a_device_is_available");
    Dictionary support = NBNativeMaterialCompiler::support_for_features(features, true);
    uniforms.merge(support["uniforms"], true);
    uniforms.merge(global_values, true);
    TypedArray<String> vertex, fragment, light;
    vertex.append("nb_context_slot = int(CUSTOM1.x);");
    vertex.append("nb_subimage_size = vec2(1.0); nb_dynamic_parameter_valid_mask = 0; nb_subimage_blend_flag "
                  "= 0; nb_renderer_kind = -1;");
    for (int64_t index = 0; index < contexts.size(); ++index) {
        String i = String::num_int64(index), c = "nb_c" + i;
        vertex.append(String(index == 0 ? "if" : "else if") + " (nb_context_slot == " + i +
                      ") { nb_subimage_size = " + c + "_grid; nb_dynamic_parameter_valid_mask = " + c +
                      "_mask; nb_subimage_blend_flag = " + c + "_blend; nb_renderer_kind = " + c +
                      "_kind; }");
    }
    vertex.append_array(support["vertex"]);
    for (int64_t index = 0; index < compiled.size(); ++index) {
        Dictionary component = compiled[index], parts = component["parts"];
        Array lines = parts["vertex"];
        if (!lines.is_empty())
            vertex.append("if (nb_context_slot == " + String::num_int64(index) + ") {\n" + join(lines, "\n") +
                          "\n}");
    }
    fragment.append("ALBEDO = vec3(0.0); EMISSION = vec3(0.0); ALPHA = 0.0;");
    for (int64_t index = 0; index < compiled.size(); ++index) {
        Dictionary component = compiled[index], parts = component["parts"];
        Array component_modes = parts["render_modes"];
        String culling, blending, lighting;
        if (mixed_lighting && component_modes.has("unshaded"))
            lighting = "\nEMISSION = ALBEDO * nb_unlit_emission_scale; ALBEDO = vec3(0.0); SPECULAR = 0.0; "
                       "METALLIC = 0.0; ROUGHNESS = 1.0; AO = 1.0;";
        if (premultiplied_surface && !bool(parts["native_premultiplied_additive"]) &&
            parts["source_blend"] != Variant("BLEND_AlphaComposite"))
            blending = String("\nPREMUL_ALPHA_FACTOR = ALPHA;") +
                       (parts["source_blend"] == Variant("BLEND_Additive") ? " ALPHA = 0.0;" : "");
        if (mixed_culling) {
            if (component_modes.has("cull_back"))
                culling = "if (!FRONT_FACING) { discard; }\n";
            else if (component_modes.has("cull_front"))
                culling = "if (FRONT_FACING) { discard; }\n";
        }
        String condition = String(index == 0 ? "if" : "else if") +
                           " (nb_context_slot == " + String::num_int64(index) + ") {\n";
        fragment.append(condition + culling + join(parts["fragment"], "\n") + lighting + blending + "\n}");
        if (light_present)
            light.append(condition + text(parts, "light") + "\n}");
    }
    String shader_code = "shader_type spatial;\nrender_mode " + join(modes, ", ") + ";\n" +
                         text(support, "header") + "\n" + join(declarations, "\n") + "\nvoid vertex() {\n" +
                         join(vertex, "\n") + "\n}\nvoid fragment() {\n" + join(fragment, "\n") + "\n}\n";
    if (light_present)
        shader_code += "void light() {\n" + join(light, "\n") + "\n}\n";
    result["parts"] = dict({{"support", dict({{"header", support["header"]}, {"vertex", Array()}})},
                            {"declarations", declarations},
                            {"vertex", vertex},
                            {"fragment", fragment},
                            {"render_modes", modes}});
    result["uniforms"] = uniforms;
    result["shader_code"] = shader_code;
    Array source_blends;
    for (int64_t i = 0; i < compiled.size(); ++i) {
        Dictionary component = compiled[i], parts = component["parts"];
        source_blends.append(parts["source_blend"]);
    }
    result["source_blends"] = source_blends;
    if (render_pass == "distortion" || render_pass == "native_surface") {
        result["supported"] = true;
        return result;
    }
    Ref<Shader> shader;
    shader.instantiate();
    shader->set_code(shader_code);
    Dictionary declared;
    TypedArray<Dictionary> shader_uniforms = shader->get_shader_uniform_list();
    for (int64_t i = 0; i < shader_uniforms.size(); ++i) {
        Dictionary uniform = shader_uniforms[i];
        declared[Variant(uniform["name"]).stringify()] = true;
    }
    if (!declared.has("nb_particle_attributes") || !declared.has("nb_particle_attribute_stride")) {
        errors.append("Generated system shader did not pass the shader-language interface check.");
        return result;
    }
    Ref<ShaderMaterial> material;
    material.instantiate();
    material->set_shader(shader);
    Array keys = uniforms.keys();
    for (int64_t i = 0; i < keys.size(); ++i)
        material->set_shader_parameter(keys[i], uniforms[keys[i]]);
    material->set_meta("angara_fx_system_material", dict({{"schema_version", 1},
                                                               {"protocol_version", 1},
                                                               {"contexts", contexts.size()},
                                                               {"requirements", requirements},
                                                               {"mixed_lighting", mixed_lighting},
                                                               {"visual_parity", "unverified"}}));
    material->set_meta("angara_fx_system_program",
                       NBNativeMaterialPrograms::seal_system(shader_code, contexts, aliases, uniforms));
    result["material"] = material;
    result["supported"] = true;
    result["requirements"] = requirements;
    result["texture_uniform_count"] = texture_count;
    return result;
}
void NBNativeMaterialComposer::_bind_methods() {
    ClassDB::bind_static_method("NBNativeMaterialComposer",
                                D_METHOD("build", "contexts", "render_pass", "schema"),
                                &NBNativeMaterialComposer::build);
    ClassDB::bind_static_method("NBNativeMaterialComposer", D_METHOD("common_modes", "modes"),
                                &NBNativeMaterialComposer::common_modes);
    ClassDB::bind_static_method(
        "NBNativeMaterialComposer",
        D_METHOD("check_binding_limits", "textures", "samplers", "reported", "driver"),
        &NBNativeMaterialComposer::check_binding_limits);
}
} // namespace godot
