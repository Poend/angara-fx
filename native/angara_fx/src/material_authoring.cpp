#include "material_authoring.h"
#include "component_bounds.h"
#include "text_saver.h"
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/resource_saver.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <filesystem>
#include <cmath>
#include <godot_cpp/variant/utility_functions.hpp>
namespace godot {
namespace {
using namespace nbmaterial;
void issue(TypedArray<Dictionary> diagnostics, const String &message, const String &location) {
    diagnostics.append(dict({{"severity", "error"},
                             {"code", "material_context"},
                             {"message", message},
                             {"location", location},
                             {"blocks_execution", true}}));
}
bool inside(const String &path, const String &root) {
    std::error_code ec;
    auto resolve = [&](const String &p) {
        CharString u = ProjectSettings::get_singleton()->globalize_path(p).utf8();
        return std::filesystem::weakly_canonical(std::filesystem::u8path(u.get_data()), ec);
    };
    auto base = resolve(root);
    if (ec)
        return false;
    auto target = resolve(path);
    if (ec)
        return false;
    auto b = base.begin(), t = target.begin();
    for (; b != base.end(); ++b, ++t)
        if (t == target.end() || *b != *t)
            return false;
    return true;
}
} // namespace
Ref<Material> NBNativeMaterialAuthoring::bind(const Ref<Material> &material, const Ref<Mesh> &mesh, int kind,
                                              TypedArray<Dictionary> diagnostics, const String &location,
                                              int surface, const Variant &component_bounds) {
    Ref<ShaderMaterial> shader_material = material;
    if (shader_material.is_null() || shader_material->get_shader().is_null())
        return material;
    Variant raw = material->get_meta("angara_fx_conversion", Dictionary());
    if (raw.get_type() != Variant::DICTIONARY || Dictionary(raw).is_empty())
        return material;
    Dictionary conversion = raw;
    Array requirements = conversion.get("requirements", Array());
    Dictionary uniforms;
    TypedArray<Dictionary> list = shader_material->get_shader()->get_shader_uniform_list();
    for (int64_t i = 0; i < list.size(); ++i) {
        Dictionary uniform = list[i];
        uniforms[Variant(uniform["name"]).stringify()] = true;
    }
    if (mesh.is_valid()) {
        for (int index = 0; index < mesh->get_surface_count(); ++index) {
            if (surface >= 0 && index != surface)
                continue;
            Array arrays = mesh->surface_get_arrays(index);
            for (int channel = 0; channel < 2; ++channel) {
                if (!requirements.has("uv_channel_" + String::num_int64(channel)))
                    continue;
                Variant values = arrays[channel == 0 ? Mesh::ARRAY_TEX_UV : Mesh::ARRAY_TEX_UV2];
                PackedVector3Array vertices = arrays[Mesh::ARRAY_VERTEX];
                if (values.get_type() != Variant::PACKED_VECTOR2_ARRAY ||
                    PackedVector2Array(values).size() != vertices.size())
                    issue(diagnostics,
                          "Required source mesh UV channel is absent: " + String::num_int64(channel),
                          location);
            }
            if (requirements.has("ParticleColor_uses_COLOR_requires_white_mesh_vertex_colors")) {
                Variant values = arrays[Mesh::ARRAY_COLOR];
                if (values.get_type() == Variant::PACKED_COLOR_ARRAY) {
                    PackedColorArray colors = values;
                    for (int64_t i = 0; i < colors.size(); ++i)
                        if (!colors[i].is_equal_approx(Color(1, 1, 1, 1))) {
                            issue(diagnostics,
                                  "Independent ParticleColor is required for a mesh with nonwhite vertex "
                                  "colors.",
                                  location);
                            break;
                        }
                }
            }
        }
    } else if (kind == 2 && requirements.has("uv_channel_1"))
        issue(diagnostics, "Ribbon TexCoord1 has no verified renderer mapping.", location);
    bool needs = requirements.has("primitive_local_bounds_uniforms_must_match_the_scene_component") ||
                 requirements.has("mesh_local_bounds_uniforms_must_match_the_assigned_mesh");
    if (needs && mesh.is_null() && component_bounds.get_type() != Variant::AABB) {
        issue(diagnostics, "Material requires the actual mesh local bounds.", location);
        return material;
    }
    if (kind == 3 && requirements.has("mesh_object_position_policy_missing"))
        issue(diagnostics,"Re-export the source mesh material with its ObjectPositionWS renderer policy.",location);
    bool object_center = uniforms.has("nb_object_world_center");
    if (!needs && !object_center && !uniforms.has("nb_renderer_kind"))
        return material;
    Ref<ShaderMaterial> authored = shader_material->duplicate();
    if (object_center && component_bounds.get_type()==Variant::AABB)
        authored->set_shader_parameter("nb_object_world_center",AABB(component_bounds).get_center());
    if (uniforms.has("nb_renderer_kind"))
        authored->set_shader_parameter("nb_renderer_kind", kind);
    if (needs) {
        AABB bounds =
            component_bounds.get_type() == Variant::AABB ? AABB(component_bounds) : mesh->get_aabb();
        authored->set_shader_parameter("nb_mesh_bounds_min", bounds.position);
        authored->set_shader_parameter("nb_mesh_bounds_size", bounds.size);
    }
    Dictionary context = dict(
        {{"renderer_kind", kind},
         {"mesh_source", mesh.is_valid() ? mesh->get_meta("source_path", mesh->get_path()) : Variant("")}});
    if (component_bounds.get_type() == Variant::AABB)
        context["bounds_scope"] = "scene_component";
    authored->set_meta("angara_fx_context", context);
    return authored;
}
void NBNativeMaterialAuthoring::apply(const Ref<Resource> &effect, TypedArray<Dictionary> diagnostics) {
    Variant initial_bounds;
    Ref<NBNativeComponentBounds> helper;
    helper.instantiate();
    Dictionary configured = helper->configure(effect->get("source_renderer_order"));
    if (bool(configured.get("verified", false)))
        initial_bounds = helper->step(Array(), Transform3D(), 0.0, false, false)["world_bounds_godot"];
    Array emitters = effect->get("emitters");
    for (int64_t i = 0; i < emitters.size(); ++i) {
        Ref<Resource> emitter = emitters[i];
        Array renderers = emitter->get("renderers");
        for (int64_t j = 0; j < renderers.size(); ++j) {
            Ref<Resource> renderer = renderers[j];
            String type = renderer->get("renderer_type");
            int kind = type == "MeshRenderer"
                           ? 3
                           : (type == "SpriteRenderer" ? 1 : (type == "RibbonRenderer" ? 2 : -1));
            if (kind < 0)
                continue;
            Ref<Mesh> mesh;
            Array entries = renderer->get("mesh_entries");
            if (kind == 3 && entries.size() == 1) {
                Ref<Resource> entry = entries[0];
                mesh = entry->get("mesh");
            }
            Variant bounds = initial_bounds;
            if (bounds.get_type() == Variant::NIL && mesh.is_null())
                bounds = AABB(Vector3(-0.01, -0.01, -0.01), Vector3(0.02, 0.02, 0.02));
            String location = String(emitter->get("display_name")) + "/" + type;
            renderer->set("material",
                          bind(renderer->get("material"), mesh, kind, diagnostics, location, -1, bounds));
            Array materials = renderer->get("surface_materials");
            for (int64_t slot = 0; slot < materials.size(); ++slot)
                materials[slot] = bind(materials[slot], mesh, kind, diagnostics,
                                       location + "/slot" + String::num_int64(slot), int(slot), bounds);
            Array all;
            all.append(renderer->get("material"));
            all.append_array(materials);
            for (int64_t k = 0; k < all.size(); ++k) {
                Ref<ShaderMaterial> material = all[k];
                if (material.is_null() || material->get_shader().is_null())
                    continue;
                TypedArray<Dictionary> uniforms = material->get_shader()->get_shader_uniform_list();
                for (int64_t u = 0; u < uniforms.size(); ++u) {
                    Dictionary uniform = uniforms[u];
                    String name = uniform["name"];
                    if (name == "nb_subimage_size") {
                        Array grid = renderer->call("get_input", "SubImageSize", nbmaterial::list({1, 1}));
                        material->set_shader_parameter(name, Vector2(double(grid[0]), double(grid[1])));
                    } else if (name == "nb_subimage_blend")
                        material->set_shader_parameter(
                            name, bool(renderer->call("get_input", "bSubImageBlend", false)));
                }
            }
        }
    }
}
Dictionary NBNativeMaterialAuthoring::report(const Dictionary &result) {
    Ref<Resource> graph = result["graph"];
    return nbdocument::dict({{"schema_version", 1},
                           {"source_material", graph->get("source_material")},
                           {"root_material", graph->get("root_material")},
                           {"source_hashes", Dictionary(graph->get("source_hashes")).duplicate(true)},
                           {"requirements", Array(result["requirements"]).duplicate()},
                           {"used_operations", Array(result["used_operations"]).duplicate()},
                           {"parameter_bindings", Array(result["parameter_bindings"]).duplicate(true)},
                           {"diagnostics", Array(result["diagnostics"]).duplicate(true)},
                           {"visual_parity", result["visual_parity"]},
                           {"status", result.get("status", "generated")}});
}
Dictionary NBNativeMaterialAuthoring::build(const String &manifest, const String &material_path,
                                            const PackedStringArray &evidence, const Dictionary &textures,
                                            const Ref<Script> &schema) {
    Ref<Resource> graph = NBNativeMaterialGraphLoader::load_graph(manifest, material_path, schema);
    NBNativeTextureBuilder::apply_evidence(graph, evidence);
    Dictionary trace;
    Dictionary result = NBNativeMaterialCompiler::compile(graph, textures, Dictionary(), trace);
    result["graph"] = graph;
    result["material"] = Variant();
    String shader_code = result["shader_code"];
    result["status"] = shader_code.is_empty() ? "blocked" : "generated";
    if (shader_code.is_empty())
        return result;
    Ref<Shader> shader;
    shader.instantiate();
    shader->set_code(shader_code);
    Ref<ShaderMaterial> material;
    material.instantiate();
    material->set_shader(shader);
    Dictionary uniforms = result["uniforms"];
    Array keys = uniforms.keys();
    for (int64_t i = 0; i < keys.size(); ++i)
        material->set_shader_parameter(keys[i], uniforms[keys[i]]);
    material->set_meta("angara_fx_conversion", report(result));
    material->set_meta("angara_fx_material_program", NBNativeMaterialPrograms::seal(result));
    result["material"] = material;
    return result;
}
Error NBNativeMaterialAuthoring::save(const Dictionary &result, const String &directory,
                                      const String &basename) {
    Ref<ShaderMaterial> material = result.get("material", Variant());
    if (material.is_null() || result.get("status", Variant()) != Variant("generated"))
        return ERR_INVALID_DATA;
    if (!directory.begins_with("res://") || basename.is_empty() || basename != basename.validate_filename())
        return ERR_INVALID_PARAMETER;
    String shader = directory.path_join(basename + String(".gdshader")),
           graph_path = directory.path_join(basename + String(".graph.tres")),
           material_path = directory.path_join(basename + String(".tres")),
           report_path = directory.path_join(basename + String(".report.tres"));
    for (const String &path : {directory, shader, graph_path, material_path, report_path})
        if (!inside(path, "res://"))
            return ERR_INVALID_PARAMETER;
    Error error =
        DirAccess::make_dir_recursive_absolute(ProjectSettings::get_singleton()->globalize_path(directory));
    if (error != OK)
        return error;
    error = ResourceSaver::get_singleton()->save(material->get_shader(), shader);
    if (error != OK)
        return error;
    material->get_shader()->take_over_path(shader);
    Ref<Resource> graph = result["graph"];
    error = Error(NBNativeTextSaver::save(graph, graph_path));
    if (error != OK)
        return error;
    graph->take_over_path(graph_path);
    error = Error(NBNativeTextSaver::save(material, material_path));
    if (error != OK)
        return error;
    Ref<Resource> report_resource;
    report_resource.instantiate();
    report_resource->set_meta("conversion", report(result));
    return Error(NBNativeTextSaver::save(report_resource, report_path));
}
TypedArray<String> NBNativeMaterialAuthoring::validate_height_fog(const Ref<Resource> &fog) {
    TypedArray<String> errors;
    Array features = fog->get("unsupported_source_features");
    for (int64_t i = 0; i < features.size(); ++i)
        errors.append("Native height fog requires an unimplemented source feature: " +
                      Variant(features[i]).stringify());
    for (const char *name :
         {"density", "height_falloff", "second_density", "second_height_falloff", "start_distance_cm",
          "end_distance_cm", "cutoff_distance_cm", "camera_to_target_distance_cm", "directional_exponent",
          "directional_start_distance_cm"}) {
        double value = fog->get(name);
        if (!std::isfinite(value) || value < 0) {
            errors.append(
                "Native height-fog densities, falloffs and distances must be finite and nonnegative.");
            break;
        }
    }
    double height = fog->get("height_cm"), offset = fog->get("second_height_offset_cm"),
           opacity = fog->get("maximum_opacity");
    if (!std::isfinite(height) || !std::isfinite(offset) || !std::isfinite(opacity) || opacity < 0 ||
        opacity > 1)
        errors.append("Native fog height/opacity is invalid.");
    for (const char *name : {"scattering_color", "directional_color"}) {
        Color color = fog->get(name);
        if (!Vector3(color.r, color.g, color.b).is_finite() || MIN(color.r, MIN(color.g, color.b)) < 0)
            errors.append("Native fog colors must be finite nonnegative linear RGB.");
    }
    Vector3 direction = fog->get("direction_to_light");
    if (bool(fog->get("directional_enabled")) &&
        (!direction.is_finite() || !UtilityFunctions::is_equal_approx(direction.length_squared(), 1.0)))
        errors.append("Native directional fog requires a normalized source light direction.");
    return errors;
}
Dictionary NBNativeMaterialAuthoring::height_fog_uniforms(const Ref<Resource> &fog) {
    double density = fog->get("density"), second = fog->get("second_density"),
           falloff = fog->get("height_falloff"), second_falloff = fog->get("second_height_falloff"),
           height = fog->get("height_cm"), offset = fog->get("second_height_offset_cm"),
           opacity = fog->get("maximum_opacity");
    Color scattering = fog->get("scattering_color"), directional = fog->get("directional_color");
    Vector3 direction = fog->get("direction_to_light");
    return dict({{"nb_height_fog_enabled", bool(fog->get("enabled")) && density + second > 0 && opacity > 0},
                 {"nb_fog_layer0", Vector4(density * .001, falloff * .001, height, 0)},
                 {"nb_fog_layer1", Vector4(second * .001, second_falloff * .001, height + offset, 0)},
                 {"nb_fog_distances",
                  Vector4(double(fog->get("start_distance_cm")), double(fog->get("end_distance_cm")),
                          double(fog->get("cutoff_distance_cm")), opacity)},
                 {"nb_fog_scattering", Vector3(scattering.r, scattering.g, scattering.b)},
                 {"nb_fog_ortho_adjustment", fog->get("orthographic_height_adjustment")},
                 {"nb_fog_camera_target_distance", fog->get("camera_to_target_distance_cm")},
                 {"nb_fog_direction", Vector4(direction.x, direction.y, direction.z,
                                              bool(fog->get("directional_enabled"))
                                                  ? double(fog->get("directional_start_distance_cm"))
                                                  : -1.0)},
                 {"nb_fog_directional_color", Vector4(directional.r, directional.g, directional.b,
                                                      double(fog->get("directional_exponent")))}});
}
void NBNativeMaterialAuthoring::_bind_methods() {
    ClassDB::bind_static_method("NBNativeMaterialAuthoring",
                                D_METHOD("bind_material", "material", "mesh", "kind", "diagnostics",
                                         "location", "surface", "component_bounds"),
                                &NBNativeMaterialAuthoring::bind);
    ClassDB::bind_static_method("NBNativeMaterialAuthoring", D_METHOD("apply", "effect", "diagnostics"),
                                &NBNativeMaterialAuthoring::apply);
    ClassDB::bind_static_method("NBNativeMaterialAuthoring",
                                D_METHOD("build", "manifest", "material", "evidence", "textures", "schema"),
                                &NBNativeMaterialAuthoring::build);
    ClassDB::bind_static_method("NBNativeMaterialAuthoring",
                                D_METHOD("save", "result", "directory", "basename"),
                                &NBNativeMaterialAuthoring::save);
    ClassDB::bind_static_method("NBNativeMaterialAuthoring", D_METHOD("report", "result"),
                                &NBNativeMaterialAuthoring::report);
    ClassDB::bind_static_method("NBNativeMaterialAuthoring", D_METHOD("validate_height_fog", "fog"),
                                &NBNativeMaterialAuthoring::validate_height_fog);
    ClassDB::bind_static_method("NBNativeMaterialAuthoring", D_METHOD("height_fog_uniforms", "fog"),
                                &NBNativeMaterialAuthoring::height_fog_uniforms);
}
} // namespace godot
