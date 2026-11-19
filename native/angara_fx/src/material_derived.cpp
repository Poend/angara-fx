#include "material_derived.h"
#include "material_authoring.h"
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <cmath>
namespace godot {
using namespace nbmaterial;
TypedArray<String> NBNativeMaterialDerived::validate(const Ref<ShaderMaterial> &material,
                                                     const Ref<Script> &schema) {
    return material.is_valid() && material->has_meta("angara_fx_system_program")
               ? NBNativeMaterialPrograms::cached_validate_system(material, schema)
               : NBNativeMaterialPrograms::cached_validate(material, schema);
}
Dictionary NBNativeMaterialDerived::current_values(const Ref<ShaderMaterial> &material) {
    return material->has_meta("angara_fx_system_program")
               ? NBNativeMaterialPrograms::system_values(material)
               : NBNativeMaterialPrograms::current_values(material);
}
Dictionary NBNativeMaterialDerived::distortion(const Ref<ShaderMaterial> &material,
                                               const Ref<Script> &schema) {
    TypedArray<String> errors = validate(material, schema);
    if (!errors.is_empty())
        return dict({{"errors", errors}});
    bool batch = material->has_meta("angara_fx_system_program");
    Dictionary compiled;
    if (batch) {
        compiled = NBNativeMaterialComposer::build(NBNativeMaterialPrograms::source_contexts(material),
                                                   "distortion", schema);
        errors.append_array(compiled["errors"]);
        if (!bool(compiled["supported"]))
            return dict({{"errors", errors}});
    } else {
        Dictionary sealed = material->get_meta("angara_fx_material_program"), trace;
        compiled = NBNativeMaterialCompiler::compile(
            sealed["graph"], Dictionary(),
            dict({{"embedded", true},
                  {"values", current_values(material)},
                  {"native_texture_formats", sealed["native_texture_formats"]},
                  {"render_pass", "distortion"}}),
            trace);
        Array diagnostics = compiled["diagnostics"];
        for (int64_t i = 0; i < diagnostics.size(); ++i) {
            Dictionary d = diagnostics[i];
            if (d["severity"] == Variant("error"))
                errors.append(d["message"]);
        }
    }
    if (text(compiled, "shader_code").is_empty())
        return dict({{"errors", errors}});
    return NBNativeRasterProgram::lower(compiled, batch, "out_color=vec4(ALBEDO,ALPHA);", "", "");
}
Dictionary NBNativeMaterialDerived::surface(const Ref<ShaderMaterial> &material, const Ref<Script> &schema,
                                            const Ref<Script> &fog_schema) {
    bool batch = material.is_valid() && material->has_meta("angara_fx_system_program");
    TypedArray<String> errors = batch ? NBNativeMaterialPrograms::validate_system(material, schema)
                                      : NBNativeMaterialPrograms::validate(material, schema);
    if (!errors.is_empty())
        return dict({{"errors", errors}});
    TypedArray<Dictionary> contexts;
    if (batch)
        contexts = NBNativeMaterialPrograms::source_contexts(material);
    else
        contexts.append(dict({{"material", material}}));
    TypedArray<String> lighting_cases;
    String lighting_vertex;
    for (int64_t index = 0; index < contexts.size(); ++index) {
        Dictionary context = contexts[index];
        Ref<ShaderMaterial> source = context["material"];
        Dictionary sealed = source->get_meta("angara_fx_material_program");
        Ref<Resource> graph = sealed["graph"];
        Dictionary properties = graph->get("material_properties");
        String shading = text(properties, "ShadingModel", "MSM_DefaultLit");
        if (shading == "MSM_DefaultLit") {
            String mode = text(properties, "TranslucencyLightingMode", "TLM_VolumetricNonDirectional");
            PackedStringArray supported =
                String("TLM_VolumetricNonDirectional,TLM_VolumetricDirectional,TLM_"
                       "VolumetricPerVertexNonDirectional,TLM_VolumetricPerVertexDirectional,TLM_Surface")
                    .split(",");
            if (!supported.has(mode)) {
                errors.append("Native surface lighting stage is not implemented for " + mode);
                continue;
            }
            String condition = batch ? "nb_context_slot == " + String::num_int64(index) : String("true");
            bool per_vertex = mode.contains("PerVertex"),
                 directional = mode != "TLM_VolumetricNonDirectional" &&
                               mode != "TLM_VolumetricPerVertexNonDirectional";
            double intensity = properties.get("TranslucencyDirectionalLightingIntensity", 1.0);
            if (!std::isfinite(intensity) || intensity < 0) {
                errors.append("Invalid source translucency directional intensity.");
                continue;
            }
            String sample =
                per_vertex
                    ? "vec4 nb_lit_a=nb_tlv_vertex_ambient; vec3 nb_lit_d=nb_tlv_vertex_directional;"
                    : "vec4 nb_lit_a; vec3 nb_lit_d; "
                      "nb_tlv_sample((INV_VIEW_MATRIX*vec4(VERTEX,1.0)).xyz,vec3(0.0),nb_lit_a,nb_lit_d);";
            lighting_cases.append("if (" + condition + ") { " + sample +
                                  " ALBEDO=EMISSION+ALBEDO*(1.0-METALLIC)*nb_tlv_diffuse(nb_lit_a,nb_lit_d,"
                                  "normalize(mat3(INV_VIEW_MATRIX)*NORMAL)*(FRONT_FACING ? 1.0 : -1.0)," +
                                  Variant(intensity).stringify() + "," +
                                  String(directional ? "true" : "false") + "); }");
            if (per_vertex)
                lighting_vertex += "if (" + condition +
                                   ") { "
                                   "nb_tlv_sample((MODEL_MATRIX*vec4(VERTEX,1.0)).xyz,vec3(0.0),nb_tlv_"
                                   "vertex_ambient,nb_tlv_vertex_directional); }\n";
        } else if (shading != "MSM_Unlit")
            errors.append("Native surface lighting stage is not implemented for this shading model; no unlit "
                          "substitution is applied.");
        String blend = text(properties, "BlendMode", "BLEND_Opaque");
        if (blend != "BLEND_Translucent" && blend != "BLEND_Additive" && blend != "BLEND_AlphaComposite")
            errors.append("Native transparent surface pass cannot replace a depth-writing opaque pass.");
    }
    if (!errors.is_empty())
        return dict({{"errors", errors}});
    Dictionary compiled;
    if (batch) {
        compiled = NBNativeMaterialComposer::build(contexts, "native_surface", schema);
        errors.append_array(compiled["errors"]);
    } else {
        Dictionary sealed = material->get_meta("angara_fx_material_program"), trace;
        compiled = NBNativeMaterialCompiler::compile(
            sealed["graph"], Dictionary(),
            dict({{"embedded", true},
                  {"values", NBNativeMaterialPrograms::current_values(material)},
                  {"native_texture_formats", sealed["native_texture_formats"]},
                  {"render_pass", "native_surface"}}),
            trace);
        Array diagnostics = compiled["diagnostics"];
        for (int64_t i = 0; i < diagnostics.size(); ++i) {
            Dictionary d = diagnostics[i];
            if (d["severity"] == Variant("error"))
                errors.append(d["message"]);
        }
    }
    if (!errors.is_empty() || text(compiled, "shader_code").is_empty()) {
        if (errors.is_empty())
            errors.append("Native surface graph did not compile.");
        return dict({{"errors", errors}});
    }
    String header = "uniform float nb_view_pre_exposure = 1.0;\nuniform float nb_view_color_limit = "
                    "32256.0;\nuniform vec4 nb_integrated_fog = vec4(0.0,0.0,0.0,1.0);\n";
    Dictionary parts = compiled["parts"], support = parts["support"], uniforms = compiled["uniforms"];
    if (!text(support, "header").contains("uniform sampler2D nb_depth_texture"))
        header += "uniform sampler2D nb_depth_texture : filter_nearest, repeat_disable;\n";
    uniforms.merge(dict({{"nb_view_pre_exposure", 1.0},
                         {"nb_view_color_limit", 32256.0},
                         {"nb_integrated_fog", Vector4(0, 0, 0, 1)}}),
                   true);
    Ref<Resource> fog = fog_schema->call("new");
    fog->set("enabled", false);
    uniforms.merge(NBNativeMaterialAuthoring::height_fog_uniforms(fog), true);
    header +=
        FileAccess::get_file_as_string("res://addons/angara_fx/materials/native_height_fog.gdshaderinc");
    if (!lighting_cases.is_empty()) {
        header += FileAccess::get_file_as_string(
                      "res://addons/angara_fx/materials/native_lighting_volume.gdshaderinc") +
                  "\nvarying vec4 nb_tlv_vertex_ambient;\nvarying vec3 nb_tlv_vertex_directional;\n";
        lighting_vertex =
            "nb_tlv_vertex_ambient=vec4(0.0); nb_tlv_vertex_directional=vec3(0.0);\n" + lighting_vertex;
    }
    TypedArray<String> additive, composite, disabled, pixel;
    for (int64_t index = 0; index < contexts.size(); ++index) {
        Dictionary context = contexts[index];
        Ref<ShaderMaterial> source = context["material"];
        Dictionary sealed = source->get_meta("angara_fx_material_program");
        Ref<Resource> graph = sealed["graph"];
        Dictionary properties = graph->get("material_properties");
        String condition = batch ? "nb_context_slot == " + String::num_int64(index) : String("true");
        if (properties["BlendMode"] == Variant("BLEND_Additive"))
            additive.append(condition);
        if (properties["BlendMode"] == Variant("BLEND_AlphaComposite"))
            composite.append(condition);
        if (!bool(properties.get("bUseTranslucencyVertexFog", true)))
            disabled.append(condition);
        else if (bool(properties.get("bComputeFogPerPixel", false)))
            pixel.append(condition);
    }
    auto conditions = [](const Array &values) {
        return values.is_empty() ? String("false") : "(" + join(values, " || ") + ")";
    };
    String is_additive = conditions(additive), is_disabled = conditions(disabled),
           is_pixel = conditions(pixel);
    String fog_code =
               "vec4 nb_fog = vec4(0.0,0.0,0.0,1.0);\nif (!(" + is_disabled + ")) { nb_fog = " + is_pixel +
               " ? (nb_height_fog_enabled ? nb_evaluate_height_fog((INV_VIEW_MATRIX*vec4(VERTEX,1.0)).xyz) : "
               "nb_integrated_fog) : nb_height_fog_vertex; }\n",
           fog_vertex =
               "nb_height_fog_vertex = nb_integrated_fog;\nif (nb_height_fog_enabled && !(" + is_disabled +
               ") && !(" + is_pixel +
               ")) { nb_height_fog_vertex = nb_evaluate_height_fog((MODEL_MATRIX*vec4(VERTEX,1.0)).xyz); }";
    String epilogue = join(lighting_cases, "\n") + "\n" + fog_code + "bool nb_additive = " + is_additive +
                      ";\nbool nb_alpha_composite = " + conditions(composite) +
                      ";\nvec3 nb_final_color = ALBEDO * nb_fog.a;\nif (nb_additive) { nb_final_color *= ALPHA; }"
                      " else { nb_final_color += nb_fog.rgb * (nb_alpha_composite ? ALPHA : 1.0); }\n"
                      "nb_final_color = min(nb_final_color * nb_view_pre_exposure,vec3(nb_view_color_limit));\n"
                      "out_color = nb_additive ? vec4(nb_final_color,0.0) : "
                      "vec4(nb_final_color * (nb_alpha_composite ? 1.0 : ALPHA),ALPHA);";
    Dictionary result =
        NBNativeRasterProgram::lower(compiled, batch, epilogue, header, fog_vertex + "\n" + lighting_vertex);
    result["requires_lighting_volume"] = !lighting_cases.is_empty();
    result["requires"] = list({"source_height_fog_context_or_integrated_fog", "source_view_pre_exposure",
                               "ordered_premultiplied_blend", "separate_refraction_accumulation"});
    return result;
}
void NBNativeMaterialDerived::_bind_methods() {
    ClassDB::bind_static_method("NBNativeMaterialDerived", D_METHOD("validate", "material", "schema"),
                                &NBNativeMaterialDerived::validate);
    ClassDB::bind_static_method("NBNativeMaterialDerived", D_METHOD("current_values", "material"),
                                &NBNativeMaterialDerived::current_values);
    ClassDB::bind_static_method("NBNativeMaterialDerived", D_METHOD("distortion", "material", "schema"),
                                &NBNativeMaterialDerived::distortion);
    ClassDB::bind_static_method("NBNativeMaterialDerived",
                                D_METHOD("surface", "material", "schema", "fog_schema"),
                                &NBNativeMaterialDerived::surface);
}
} // namespace godot
