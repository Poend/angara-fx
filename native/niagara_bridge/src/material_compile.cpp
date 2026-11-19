#include "material_compiler.h"
#include <godot_cpp/core/class_db.hpp>
#include <cmath>
namespace godot::nbmaterial {
Dictionary Context::run() {
    Dictionary result = dict({{"shader_code", ""},
                              {"uniforms", uniforms},
                              {"parameter_bindings", bindings},
                              {"requirements", requirements},
                              {"used_operations", used_ops},
                              {"diagnostics", errors},
                              {"visual_parity", "unverified"}});
    if (graph.is_null()) {
        error("material_graph", "Material graph is missing.");
        return result;
    }
    Array diagnostics = graph->get("diagnostics");
    for (int64_t i = 0; i < diagnostics.size(); ++i) {
        Dictionary item = diagnostics[i];
        if (item.get("severity", Variant()) == Variant("error"))
            errors.append(item.duplicate(true));
    }
    Dictionary properties = property("material_properties"), roots = property("outputs");
    String shading = text(properties, "ShadingModel", "MSM_DefaultLit"),
           blend = text(properties, "BlendMode", "BLEND_Opaque");
    bool alpha_composite = blend == "BLEND_AlphaComposite";
    bool transparent = blend == "BLEND_Translucent" || blend == "BLEND_Additive" || alpha_composite;
    if (text(properties, "MaterialDomain", "MD_Surface") != "MD_Surface")
        error("material_domain", "Only Surface material domain is implemented.");
    if (shading != "MSM_Unlit" && shading != "MSM_DefaultLit")
        error("shading_model", "Unsupported shading model: " + shading);
    if (blend != "BLEND_Opaque" && blend != "BLEND_Masked" && blend != "BLEND_Translucent" &&
        blend != "BLEND_Additive" && !alpha_composite)
        error("blend_mode", "Unsupported blend mode: " + blend);
    if (bool(properties.get("bUseMaterialAttributes", false)))
        error("material_attributes", "Material Attributes graph expansion is not implemented.");
    auto connected = [&](const String &name) {
        return roots.has(name) && !text(roots[name], "source").is_empty();
    };
    for (const char *name : {"MaterialAttributes", "FrontMaterial", "Displacement"})
        if (connected(name))
            error("material_output", String("Output requires a dedicated implementation: ") + name);
    Dictionary scope = dict({{"id", "root"},
                             {"nodes", node_map(graph->get("nodes"))},
                             {"bindings", Dictionary()},
                             {"function_paths", Array()},
                             {"stage", "fragment"}});
    if (connected("WorldPositionOffset")) {
        Dictionary vertex_scope = scope.duplicate();
        vertex_scope["id"] = "vertex";
        vertex_scope["stage"] = "vertex";
        Dictionary offset =
            cast(root_pin("WorldPositionOffset", constant(list({0.0, 0.0, 0.0})), vertex_scope), 3);
        body.append("VERTEX += inverse(mat3(MODEL_MATRIX)) * (vec3((" + code(offset) + ").y, (" +
                    code(offset) + ").z, -(" + code(offset) + ").x) * 0.01);");
        vertex_body.assign(body);
        body.clear();
        require("WorldPositionOffset_requires_nonsingular_model_transform");
    }
    TypedArray<String> outputs;
    String pass = text(options, "render_pass", "surface");
    bool distortion = pass == "distortion", surface = pass == "native_surface",
         premultiplied = !distortion && !surface && shading == "MSM_Unlit" && blend == "BLEND_Additive";
    bool additive_refraction = blend == "BLEND_Additive" && connected("Refraction") &&
                               text(properties, "RefractionMethod", "RM_IndexOfRefraction") == "RM_IndexOfRefraction";
    if (distortion && !connected("Refraction"))
        error("refraction_pass", "Distortion pass requires a connected refraction output.");
    if (shading == "MSM_Unlit") {
        Dictionary emitted = root_pin("EmissiveColor", constant(list({0.0, 0.0, 0.0})), scope);
        outputs.append("ALBEDO = max(" + code(cast(emitted, 3)) + ", vec3(0.0));");
    } else {
        outputs.append("ALBEDO = clamp(" +
                       code(cast(root_pin("BaseColor", constant(list({0.0, 0.0, 0.0})), scope), 3)) +
                       ", vec3(0.0), vec3(1.0));");
        const char *source[] = {"Metallic", "Specular", "Roughness", "AmbientOcclusion"},
                   *target[] = {"METALLIC", "SPECULAR", "ROUGHNESS", "AO"};
        double fallback[] = {0.0, 0.5, 0.5, 1.0};
        for (int i = 0; i < 4; ++i)
            outputs.append(String(target[i]) + " = clamp(" +
                           code(cast(root_pin(source[i], constant(fallback[i]), scope), 1)) + ", 0.0, 1.0);");
        outputs.append("EMISSION = max(" +
                       code(cast(root_pin("EmissiveColor", constant(list({0.0, 0.0, 0.0})), scope), 3)) +
                       ", vec3(0.0));");
        for (const char *name : {"Tangent", "SubsurfaceColor", "ClearCoat", "Anisotropy"})
            if (connected(name))
                error("material_output", String("Lit output requires a dedicated implementation: ") + name);
        require(surface ? "native_lighting_stage_requires_visual_comparison"
                        : "Godot_DefaultLit_BRDF_requires_visual_comparison");
    }
    if (connected("Normal")) {
        feature("tangent_frame");
        Dictionary normal = root_pin("Normal", constant(list({0.0, 0.0, 1.0})), scope);
        // MP_Normal is a float3 property. Unreal appends zero when the input is a float2.
        normal = width(normal) == 2 ? expression("vec3(" + code(normal) + ", 0.0)", 3) : cast(normal, 3);
        String value = code(normal);
        if (bool(properties.get("bTangentSpaceNormal", true)))
            outputs.append("NORMAL = normalize(nb_view_tangent * (" + value + ").x + nb_view_binormal * (" +
                           value + ").y + nb_view_normal * (" + value + ").z);");
        else
            outputs.append("NORMAL = normalize(mat3(VIEW_MATRIX) * vec3((" + value + ").y, (" + value +
                           ").z, -(" + value + ").x));");
    }
    if (transparent)
        outputs.append("ALPHA = clamp(" + code(cast(root_pin("Opacity", constant(1.0), scope), 1)) +
                       ", 0.0, 1.0);");
    else if (blend == "BLEND_Masked") {
        Variant clip = properties.get("OpacityMaskClipValue", 0.3333);
        if (!nbdocument::number(clip) || !std::isfinite(double(clip)))
            error("opacity_mask_clip", "OpacityMaskClipValue must be a finite scalar.");
        else {
            Dictionary mask = cast(root_pin("OpacityMask", constant(1.0), scope), 1);
            outputs.append("if ((" + code(mask) + ") - " + number(clip) + " < 0.0) { discard; }");
        }
    }
    if (!distortion && !surface) {
        if (shading == "MSM_Unlit") {
            feature("native_unlit_color_limit");
            outputs.append(String("ALBEDO = nb_limit_unlit_color(ALBEDO, ") +
                           (blend == "BLEND_Additive" ? "ALPHA" : "1.0") + ");");
            if (premultiplied && !additive_refraction)
                outputs.append("PREMUL_ALPHA_FACTOR = ALPHA; ALPHA = 0.0;");
            require("native_unlit_output_limit_exact_for_unit_source_pre_exposure_without_fog;final_fog_and_"
                    "view_exposure_integration_pending");
        } else
            require("native_output_limit_requires_final_lighting_stage;not_reproduced_by_lit_spatial_shader");
    }
    if (connected("Refraction")) {
        String method = text(properties, "RefractionMethod", "RM_IndexOfRefraction");
        if ((method != "RM_PixelNormalOffset" && method != "RM_IndexOfRefraction") ||
            shading != "MSM_Unlit" || (blend != "BLEND_Translucent" && !additive_refraction))
            error("refraction_mode", "Refraction supports unlit translucent PixelNormalOffset or "
                                     "IndexOfRefraction; appearance requires paired validation.");
        else {
            if (!distortion)
                feature("pno");
            feature("depth");
            feature("tangent_frame");
            require("refraction_requires_camera_accumulation_pass_and_matching_source_render_policy");
            Dictionary refraction = cast(root_pin("Refraction", constant(1.0), scope), 1);
            String normal = method == "RM_PixelNormalOffset"
                                ? "(nb_view_normal.xy - NORMAL.xy * (FRONT_FACING ? 1.0 : -1.0))"
                                : "(NORMAL.xy * (FRONT_FACING ? 1.0 : -1.0))";
            outputs.append("vec2 nb_pno = " + normal + " * (" + code(refraction) + " - 1.0);");
            outputs.append(String("if (all(lessThan(abs(nb_pno),vec2(1.0)/VIEWPORT_SIZE))) { ") +
                           (distortion ? "discard;" : "nb_pno=vec2(0.0);") + " }");
            outputs.append("float nb_projection_x=abs(PROJECTION_MATRIX[0][0]);");
            outputs.append("nb_pno *= VIEWPORT_SIZE * vec2(0.00023,-0.00023) * "
                           "vec2(nb_projection_x,VIEWPORT_SIZE.x/VIEWPORT_SIZE.y*nb_projection_x);");
            String bias = number(-double(properties.get("RefractionDepthBias", 0.0)));
            outputs.append(
                "float "
                "nb_depth_delta=(nb_view_depth(SCREEN_UV+nb_pno,INV_PROJECTION_MATRIX)+VERTEX.z)*100.0;");
            outputs.append("nb_pno *= clamp(nb_depth_delta / max(1.0,clamp(abs(" + bias +
                           "*0.5),0.0,50.0))+" + bias + ",0.0,1.0);");
            if (distortion)
                outputs.append(
                    "vec4 nb_packed_offset=vec4(max(nb_pno*4.0,vec2(0.0)),max(-nb_pno*4.0,vec2(0.0))); "
                    "ALBEDO=nb_packed_offset.rgb; ALPHA=nb_packed_offset.a;");
            else {
                outputs.append("vec2 nb_refracted_uv=SCREEN_UV+nb_pno;");
                outputs.append("if (any(lessThan(nb_refracted_uv,vec2(0.0))) || "
                               "any(greaterThan(nb_refracted_uv,vec2(1.0)))) { nb_refracted_uv=SCREEN_UV; }");
                if (!surface && additive_refraction)
                    outputs.append("if (!nb_deferred_refraction && any(notEqual(nb_refracted_uv, SCREEN_UV))) { "
                                   "ALBEDO = textureLod(nb_scene_texture,nb_refracted_uv,0.0).rgb + ALBEDO * ALPHA; "
                                   "PREMUL_ALPHA_FACTOR = 1.0; ALPHA = 1.0; "
                                   "} else { PREMUL_ALPHA_FACTOR = ALPHA; ALPHA = 0.0; }");
                else if (!surface)
                    outputs.append("if (!nb_deferred_refraction && abs(" + code(refraction) +
                                   "-1.0)>0.000001) { "
                                   "ALBEDO=ALBEDO*ALPHA+textureLod(nb_scene_texture,nb_refracted_uv,0.0).rgb*"
                                   "(1.0-ALPHA); ALPHA=1.0; }");
            }
        }
    }
    if (connected("PixelDepthOffset")) {
        String mode = text(properties, "PixelDepthOffsetMode", "PDOM_Legacy");
        if (mode != "PDOM_Legacy" && mode != "PDOM_CameraVector")
            error("pixel_depth_offset_mode", "Unsupported PixelDepthOffset mode: " + mode);
        else if (transparent)
            error("pixel_depth_offset_blend",
                  "PixelDepthOffset is implemented for opaque and masked surfaces only.");
        else {
            Dictionary offset = cast(root_pin("PixelDepthOffset", constant(0.0), scope), 1);
            feature("pixel_depth_offset");
            require("PixelDepthOffset_shadow_and_lighting_parity_requires_source_comparison");
            outputs.append("float nb_pdo = " + code(offset) + " * 0.01;");
            outputs.append(
                "float nb_axial_depth = -VERTEX.z; bool nb_ortho = PROJECTION_MATRIX[3][3] == 1.0;");
            outputs.append("vec3 nb_forward_ray = nb_ortho ? vec3(0.0,0.0,-1.0) : normalize(VERTEX);");
            if (mode == "PDOM_CameraVector")
                outputs.append("if (!nb_ortho) { nb_pdo *= -nb_forward_ray.z; }");
            outputs.append("float nb_offset_depth = nb_ortho ? max(nb_axial_depth + nb_pdo, nb_axial_depth) "
                           ": nb_axial_depth + nb_pdo;");
            outputs.append("DEPTH = min(nb_project_depth(nb_offset_depth,PROJECTION_MATRIX), FRAGCOORD.z);");
            outputs.append("float nb_world_offset = nb_ortho ? "
                           "(nb_project_depth(2.0*nb_axial_depth-nb_offset_depth,PROJECTION_MATRIX)-"
                           "FRAGCOORD.z)*0.01 : max(nb_pdo,0.0);");
            outputs.append("LIGHT_VERTEX = VERTEX + nb_forward_ray * nb_world_offset;");
        }
    }
    diagnostics = graph->get("diagnostics");
    for (int64_t i = 0; i < diagnostics.size(); ++i) {
        Dictionary item = diagnostics[i];
        if (item.get("severity", Variant()) == Variant("error") && !errors.has(item))
            errors.append(item.duplicate(true));
    }
    if (!errors.is_empty())
        return result;
    TypedArray<String> modes;
    modes.append(bool(properties.get("TwoSided", false)) ? "cull_disabled" : "cull_back");
    if (shading == "MSM_Unlit")
        modes.append("unshaded");
    if (alpha_composite) {
        modes.append("blend_premul_alpha");
        if (!surface && !distortion)
            outputs.append("PREMUL_ALPHA_FACTOR = 1.0;");
    } else if (blend == "BLEND_Additive")
        modes.append(premultiplied ? "blend_premul_alpha" : "blend_add");
    else if (blend == "BLEND_Translucent")
        modes.append("blend_mix");
    if (transparent)
        modes.append("depth_draw_never");
    Dictionary generated = shader_support();
    String light;
    if (shading == "MSM_DefaultLit" && transparent && !surface) {
        String lighting = text(properties, "TranslucencyLightingMode", "TLM_VolumetricNonDirectional");
        require("source_translucency_lighting_volume_sampling_not_reproduced:" + lighting);
        if (lighting == "TLM_VolumetricNonDirectional" ||
            lighting == "TLM_VolumetricPerVertexNonDirectional") {
            modes.append("specular_disabled");
            light = "\nvoid light() { DIFFUSE_LIGHT += LIGHT_COLOR * ATTENUATION * (0.25 / PI); }\n";
        } else if (lighting == "TLM_VolumetricDirectional" ||
                   lighting == "TLM_VolumetricPerVertexDirectional" || lighting == "TLM_Surface") {
            modes.append("specular_disabled");
            require("directional_translucency_analytic_per_light_SH_requires_volume_for_multi_light_color_"
                    "compression_and_spatial_variation");
            String intensity = number(properties.get("TranslucencyDirectionalLightingIntensity", 1.0));
            light = "\nvoid light() {\n\tvec3 nb_incident = LIGHT_COLOR * ATTENUATION;\n\tfloat "
                    "nb_ambient_luminance = dot(nb_incident * (0.282095 / PI), "
                    "vec3(0.2126390059,0.7151686788,0.0721923153));\n\tfloat nb_directional_reconstruction = "
                    "nb_ambient_luminance / (nb_ambient_luminance + 0.00001);\n\tfloat nb_transfer = "
                    "0.282095 * 0.282095 + (2.0 / 3.0) * 0.488603 * 0.488603 * dot(NORMAL,LIGHT) * " +
                    intensity +
                    " * nb_directional_reconstruction;\n\tDIFFUSE_LIGHT += nb_incident * "
                    "max(nb_transfer,0.0);\n}\n";
        }
    }
    TypedArray<String> fragment;
    fragment.append_array(body);
    fragment.append_array(outputs);
    Array vertex = Array(generated["vertex"]).duplicate();
    vertex.append_array(vertex_body);
    result["parts"] = dict({{"source_blend", blend},
                            {"native_premultiplied_additive", premultiplied},
                            {"render_modes", modes.duplicate()},
                            {"features", features.duplicate()},
                            {"declarations", declarations.duplicate()},
                            {"support", generated},
                            {"vertex", vertex_body.duplicate()},
                            {"fragment", fragment.duplicate()},
                            {"light", light.trim_prefix("\nvoid light() {").trim_suffix("}\n")},
                            {"uniform_aliases", aliases.duplicate()}});
    Dictionary formats;
    Array keys = textures.keys();
    for (int64_t i = 0; i < keys.size(); ++i) {
        Ref<Texture2D> texture = textures[keys[i]];
        Dictionary provenance =
            texture.is_valid() ? Dictionary(texture->get_meta("niagara_bridge_native_texture", Dictionary()))
                               : Dictionary();
        formats[keys[i]] = text(provenance, "native_format");
    }
    result["native_texture_formats"] = formats;
    result["shader_code"] = "shader_type spatial;\nrender_mode " + join(modes, ", ") + ";\n" +
                            text(generated, "header") + "\n" + join(declarations, "\n") +
                            "\nvoid vertex() {\n\t" + join(vertex, "\n\t") + "\n}\nvoid fragment() {\n\t" +
                            join(fragment, "\n\t") + "\n}\n" + light;
    return result;
}
} // namespace godot::nbmaterial
namespace godot {
Dictionary NBNativeMaterialCompiler::compile(const Ref<Resource> &graph, const Dictionary &textures,
                                             const Dictionary &options, Dictionary trace) {
    nbmaterial::Context context(graph, textures, options);
    Dictionary result = context.run();
    trace["body"] = context.body;
    trace["vertex"] = context.vertex_body;
    return result;
}
Dictionary NBNativeMaterialCompiler::support_for_features(const Dictionary &features, bool batch) {
    nbmaterial::Context context;
    context.features = features.duplicate();
    context.options = nbdocument::dict({{"batch", batch}});
    Dictionary support = context.shader_support();
    support["uniforms"] = context.uniforms.duplicate();
    return support;
}
void NBNativeMaterialCompiler::_bind_methods() {
    ClassDB::bind_static_method("NBNativeMaterialCompiler",
                                D_METHOD("compile", "graph", "textures", "options", "trace"),
                                &NBNativeMaterialCompiler::compile);
    ClassDB::bind_static_method("NBNativeMaterialCompiler",
                                D_METHOD("support_for_features", "features", "batch"),
                                &NBNativeMaterialCompiler::support_for_features);
    ClassDB::bind_static_method("NBNativeMaterialCompiler",
                                D_METHOD("verified_unbiased_full_chain", "descriptor", "texture"),
                                &NBNativeMaterialCompiler::verified_unbiased_full_chain);
}
} // namespace godot
