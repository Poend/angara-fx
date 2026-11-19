#include "raster_pass.h"
#include "lighting_volume.h"
#include "geometry_cache.h"
#include "render_policy.h"
#include "particle_lights.h"
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/world3d.hpp>
#include <godot_cpp/classes/multi_mesh_instance3d.hpp>
#include <godot_cpp/classes/multi_mesh.hpp>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/texture3d.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <cmath>
#include <chrono>
namespace godot {
namespace {
void collect_sampler(const Ref<ShaderMaterial> &material, const Dictionary &sampler, Dictionary &textures) {
    String name = sampler["name"];
    Variant value = material->get_shader_parameter(name);
    if (value.get_type() != Variant::OBJECT)
        return;
    Object *object = value.get_validated_object();
    if ((String(sampler["type"]) == "sampler3D" && Object::cast_to<Texture3D>(object)) ||
        (String(sampler["type"]) == "sampler2D" && Object::cast_to<Texture2D>(object)))
        textures[name] = value;
}
} // namespace
String NBNativeRasterPass::set_view_parameters(double exposure, double limit, const Vector4 &fog) {
    if (!std::isfinite(exposure) || exposure <= 0. || !std::isfinite(limit) || limit <= 0. ||
        !fog.is_finite() || fog.w < 0. || fog.w > 1. ||
        std::min(double(fog.x), std::min(double(fog.y), double(fog.z))) < 0.)
        return "Native view inputs require positive finite exposure/limit and nonnegative integrated fog "
               "with transmittance in [0,1].";
    std::lock_guard<std::mutex> guard(lock);
    view.clear();
    view["nb_view_pre_exposure"] = exposure;
    view["nb_view_color_limit"] = limit;
    view["nb_integrated_fog"] = fog;
    return String();
}
TypedArray<String> NBNativeRasterPass::set_height_fog(const Ref<Resource> &settings) {
    TypedArray<String> errors;
    if (settings.is_valid())
        errors.assign(settings->call("validate"));
    if (!errors.is_empty())
        return errors;
    Dictionary values;
    if (settings.is_valid()) {
        values = settings->call("uniforms");
        values["nb_integrated_fog"] = Vector4(0, 0, 0, 1);
    } else
        values["nb_height_fog_enabled"] = false;
    std::lock_guard<std::mutex> guard(lock);
    height_fog = values;
    return errors;
}
TypedArray<String> NBNativeRasterPass::prepare_player_view(Camera3D *camera) {
    TypedArray<String> errors;
    Dictionary settings;
    {
        std::lock_guard<std::mutex> guard(lock);
        settings = options.duplicate();
        if (disposed)
            errors.append("Native particle rendering requires a live Forward+ camera.");
    }
    auto *server = RenderingServer::get_singleton();
    if (!errors.is_empty() || !camera || server->get_current_rendering_method() != "forward_plus" ||
        !server->get_rendering_device()) {
        if (errors.is_empty())
            errors.append("Native particle rendering requires a live Forward+ camera.");
        return errors;
    }
    Ref<Resource> fog = settings["source_height_fog"], context = settings["source_scene_context"];
    Dictionary sort;
    {
        std::lock_guard<std::mutex> guard(lock);
        sort_override = sort;
    }
    if (context.is_valid()) {
        errors.append_array(context->call("validate"));
        if (!errors.is_empty())
            return errors;
        sort["policy"] = context->get("component_sort_policy");
        sort["axis"] = context->get("component_sort_axis");
        fog = context->get("height_fog");
    }
    {
        std::lock_guard<std::mutex> guard(lock);
        sort_override = sort;
    }
    Ref<Resource> environment = camera->get("environment");
    if (environment.is_null())
        environment = camera->get_world_3d()->get("environment");
    if (fog.is_null() && environment.is_valid() &&
        (bool(environment->get("fog_enabled")) || bool(environment->get("volumetric_fog_enabled"))))
        errors.append("Godot scene fog has no exported native fog context; retain spatial rendering.");
    int64_t policy = settings["component_sort_policy"];
    Vector3 axis = settings["component_sort_axis"];
    if (context.is_null() &&
        (policy < 0 || policy > 2 || !axis.is_finite() || (policy == 2 && !axis.is_normalized())))
        errors.append("Native component sort policy/axis is invalid.");
    String problem = set_view_parameters(settings["source_pre_exposure"], settings["source_color_limit"],
                                         Vector4(0, 0, 0, 1));
    if (!problem.is_empty())
        errors.append(problem);
    errors.append_array(set_height_fog(fog));

    Dictionary config = NBNativeLightingVolume::configuration(context);
    {
        std::lock_guard<std::mutex> guard(lock);
        lighting_config = config;
    }
    errors.append_array(statistics()["errors"]);
    return errors;
}
TypedArray<String> NBNativeRasterPass::submit_surface(Node *owner, MeshInstance3D *instance,
                                                      const Dictionary &sort_context) {
    TypedArray<String> errors;
    {
        std::lock_guard<std::mutex> guard(lock);
        if (disposed)
            errors.append("Native raster submission requires a live owner and mesh surface.");
    }
    if (!errors.is_empty() || !owner || !instance || instance->get_mesh().is_null()) {
        if (owner)
            remove_surface(owner);
        if (errors.is_empty())
            errors.append("Native raster submission requires a live owner and mesh surface.");
        return errors;
    }
    Array draws;
    Ref<Mesh> mesh = instance->get_mesh();
    Ref<Script> system = dependencies["system"], material_program = dependencies["material"],
                raster = dependencies["raster"], builder = dependencies["builder"],
                cache = dependencies["geometry"];
    for (int surface = 0; surface < mesh->get_surface_count(); ++surface) {
        Ref<Material> selected = instance->get_material_override();
        if (selected.is_null())
            selected = instance->get_active_material(surface);
        Ref<ShaderMaterial> material = selected;
        Ref<ArrayMesh> array_mesh = mesh;
        // ArrayMesh already owns the primitive metadata. Requesting the entire
        // RenderingServer surface here also retrieves its vertex/index buffers.
        // Other Mesh implementations retain the generic metadata fallback.
        int64_t primitive = array_mesh.is_valid() ? int64_t(array_mesh->surface_get_primitive_type(surface))
                                                  : int64_t(RenderingServer::get_singleton()
                                                                ->mesh_get_surface(mesh->get_rid(), surface)
                                                                .get("primitive", -1));
        if (material.is_null() || primitive != Mesh::PRIMITIVE_TRIANGLES) {
            errors.append("Native raster requires sealed triangle ShaderMaterials.");
            continue;
        }
        bool batch = material->has_meta("niagara_bridge_system_material");
        Ref<Script> program_type = batch ? system : material_program;
        Array invalid = program_type->call("cached_validate", material);
        if (!invalid.is_empty()) {
            errors.append_array(invalid);
            continue;
        }
        String key = raster->call("source_key", material);
        if (!programs.has(key))
            programs[key] = builder->call("build", material);
        Dictionary program = programs[key];
        Array program_errors = program["errors"];
        if (!program_errors.is_empty()) {
            errors.append_array(program_errors);
            continue;
        }
        if (bool(program.get("requires_lighting_volume", false))) {
            Array lighting_errors;
            {
                std::lock_guard<std::mutex> guard(lock);
                lighting_errors = Array(lighting_config["errors"]).duplicate();
            }
            if (!lighting_errors.is_empty()) {
                errors.append_array(lighting_errors);
                continue;
            }
        }
        Dictionary values = program_type->call("current_values", material), textures;
        Array fields = program["fields"], samplers = program["samplers"];
        for (int n = 0; n < fields.size(); ++n) {
            String name = Dictionary(fields[n])["name"];
            Variant live = material->get_shader_parameter(name);
            if (live.get_type() != Variant::NIL)
                values[name] = live;
        }
        for (int n = 0; n < samplers.size(); ++n)
            collect_sampler(material, samplers[n], textures);
        Dictionary packet = NBNativeGeometryCache::packet(mesh, surface);
        PackedInt32Array indices = packet["indices"];
        if (indices.is_empty())
            continue;
        values["nb_instance_color"] = Color(1, 1, 1, 1);
        values["INSTANCE_CUSTOM"] = Color(0, 0, 0, 0);
        values["INSTANCE_ID"] = 0;
        Dictionary draw;
        draw["key"] = key;
        draw["program"] = program;
        draw["values"] = values;
        draw["textures"] = textures;
        draw["model"] = instance->get_global_transform();
        draw["geometry"] = packet["geometry"];
        draw["indices"] = indices;
        draws.append(draw);
    }
    std::lock_guard<std::mutex> guard(lock);
    if (errors.is_empty()) {
        Dictionary group;
        group["owner"] = UtilityFunctions::weakref(owner);
        group["draws"] = draws;
        group["sort"] = sort_context.duplicate();
        group["identity"] = owner->get_instance_id();
        submissions[owner->get_instance_id()] = group;
    } else
        submissions.erase(owner->get_instance_id());
    return errors;
}
void NBNativeRasterPass::remove_surface(Node *owner) {
    if (owner) {
        std::lock_guard<std::mutex> guard(lock);
        submissions.erase(owner->get_instance_id());
    }
}
TypedArray<String> NBNativeRasterPass::submit_particle_lights(Node *owner, const Array &records,
                                                              double exposure) {
    Dictionary prepared = NBNativeParticleLights::translucency_lights(records, exposure);
    Array errors = prepared["errors"];
    std::lock_guard<std::mutex> guard(lock);
    if (disposed || !owner)
        errors.append("Particle-light submission requires a live owner and compositor.");
    if (owner) {
        Array lights = prepared["lights"];
        if (errors.is_empty() && !lights.is_empty()) {
            Dictionary group;
            group["owner"] = UtilityFunctions::weakref(owner);
            group["lights"] = lights;
            light_submissions[owner->get_instance_id()] = group;
        } else
            light_submissions.erase(owner->get_instance_id());
    }
    TypedArray<String> result;
    result.assign(errors);
    return result;
}
TypedArray<String> NBNativeRasterPass::submit_player_particle_lights(Node *owner, const Array &records,
                                                                     Camera3D *camera) {
    TypedArray<String> errors = prepare_player_view(camera);

    Dictionary settings;
    {
        std::lock_guard<std::mutex> guard(lock);
        settings = options.duplicate();
    }
    Dictionary config = NBNativeLightingVolume::configuration(settings["source_scene_context"]);
    errors.append_array(config["errors"]);
    bool needs_exposure = false;
    for (int n = 0; n < records.size(); ++n)
        needs_exposure |= double(Dictionary(records[n]).get("inverse_exposure_blend", 0.)) != 0.;
    if (needs_exposure && (!bool(settings["source_eye_adaptation_exposure_observed"]) ||
                           !std::isfinite(double(settings["source_eye_adaptation_exposure"]))))
        errors.append("Particle lights with inverse exposure require an observed source eye-adaptation "
                      "exposure; view pre-exposure is a separate input.");
    if (!errors.is_empty()) {
        remove_particle_lights(owner);
        return errors;
    }
    return submit_particle_lights(owner, records,
                                  needs_exposure ? double(settings["source_eye_adaptation_exposure"]) : 1.);
}
void NBNativeRasterPass::remove_particle_lights(Node *owner) {
    if (owner) {
        std::lock_guard<std::mutex> guard(lock);
        light_submissions.erase(owner->get_instance_id());
    }
}
void NBNativeRasterPass::submit_player(Node *owner, const Array &instances) {
    const bool profile=owner && bool(owner->get_meta("niagara_bridge_profile_renderer",false));
    Dictionary phases;
    auto phase_start=profile ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point();
    auto mark=[&](const char *name) {
        if(!profile) return;
        auto now=std::chrono::steady_clock::now();
        phases[name]=double(phases.get(name,0.))+std::chrono::duration<double,std::micro>(now-phase_start).count();phase_start=now;
    };
    Dictionary settings;
    {
        std::lock_guard<std::mutex> guard(lock);
        if (disposed)
            return;
        settings = options.duplicate();
    }
    if (!owner)
        return;
    Ref<Script> policy_type = dependencies["policy"], builder = dependencies["builder"],
                raster = dependencies["raster"], cache = dependencies["geometry"];
    Dictionary policy;
    if (bool(settings["use_imported_policy"]))
        policy = NBNativeRenderPolicy::for_effect(owner->get("effect"));
    else {
        policy["supported"] = true;
        policy["verified"] = false;
        policy["enabled"] = true;
        policy["high_precision_offsets"] = settings["high_precision_offsets"];
        policy["key"] = "enabled:" + UtilityFunctions::str(settings["high_precision_offsets"]);
        policy["errors"] = Array();
        policy["limitations"] = Array();
    }
    mark("policy_setup");
    Array draws;
    TypedArray<String> errors;
    int64_t refractive_surfaces = 0;
    for (int n = 0; n < instances.size(); ++n) {
        auto *instance = Object::cast_to<GeometryInstance3D>(instances[n].get_validated_object());
        if (!instance || !instance->is_inside_tree() || !instance->is_visible_in_tree())
            continue;
        auto *single = Object::cast_to<MeshInstance3D>(instance);
        auto *multi = Object::cast_to<MultiMeshInstance3D>(instance);
        Ref<MultiMesh> multimesh = multi ? multi->get_multimesh() : Ref<MultiMesh>();
        Ref<Mesh> mesh = single                 ? single->get_mesh()
                         : multimesh.is_valid() ? multimesh->get_mesh()
                                                : Ref<Mesh>();
        if (mesh.is_null())
            continue;
        for (int surface = 0; surface < mesh->get_surface_count(); ++surface) {
            Ref<Material> selected = instance->get_material_override();
            if (selected.is_null())
                selected = mesh->surface_get_material(surface);
            if (instance->get_material_override().is_null() && single &&
                single->get_surface_override_material(surface).is_valid())
                selected = single->get_surface_override_material(surface);
            Ref<ShaderMaterial> material = selected;
            if (material.is_null() || material->get_shader().is_null() ||
                material->get_shader_parameter("nb_deferred_refraction").get_type() == Variant::NIL)
                continue;
            ++refractive_surfaces;
            if (!bool(policy["supported"])) {
                errors.append_array(policy["errors"]);
                errors.append_array(policy["limitations"]);
                material->set_shader_parameter("nb_deferred_refraction", false);
                continue;
            }
            mark("selection");
            Array invalid = builder->call("validate", material);
            mark("validation");
            if (!bool(policy["enabled"])) {
                errors.append_array(invalid);
                material->set_shader_parameter("nb_deferred_refraction", invalid.is_empty());
                continue;
            }
            if (!invalid.is_empty()) {
                errors.append_array(invalid);
                continue;
            }
            String key = raster->call("source_key", material);
            if (!programs.has(key))
                programs[key] = builder->call("build", material);
            Dictionary program = programs[key];
            Array program_errors = program["errors"];
            if (!program_errors.is_empty()) {
                errors.append_array(program_errors);
                continue;
            }
            mark("source_key_build");
            material->set_shader_parameter("nb_deferred_refraction", true);
            Dictionary values = builder->call("current_values", material), textures;
            Array fields = program["fields"], samplers = program["samplers"];
            for (int index = 0; index < fields.size(); ++index) {
                String name = Dictionary(fields[index])["name"];
                Variant value = material->get_shader_parameter(name);
                if (value.get_type() != Variant::NIL)
                    values[name] = value;
            }
            for (int index = 0; index < samplers.size(); ++index)
                collect_sampler(material, samplers[index], textures);
            mark("live_values");
            Dictionary packet = NBNativeGeometryCache::packet(mesh, surface);
            mark("geometry_packet");
            if (Array(packet["arrays"]).is_empty())
                continue;
            int64_t count = multimesh.is_null() ? 1
                            : multimesh->get_visible_instance_count() < 0
                                ? multimesh->get_instance_count()
                                : multimesh->get_visible_instance_count();
            for (int64_t index = 0; index < count; ++index) {
                Transform3D model = instance->get_global_transform();
                Dictionary instance_values = values.duplicate();
                instance_values["INSTANCE_ID"] = index;
                instance_values["nb_instance_color"] = Color(1, 1, 1, 1);
                instance_values["INSTANCE_CUSTOM"] = Color(0, 0, 0, 0);
                if (multimesh.is_valid()) {
                    model = model * multimesh->get_instance_transform(index);
                    if (multimesh->is_using_colors())
                        instance_values["nb_instance_color"] = multimesh->get_instance_color(index);
                    if (multimesh->is_using_custom_data())
                        instance_values["INSTANCE_CUSTOM"] = multimesh->get_instance_custom_data(index);
                }
                Dictionary draw;
                draw["key"] = key;
                draw["program"] = program;
                draw["geometry"] = packet["geometry"];
                draw["indices"] = packet["indices"];
                draw["values"] = instance_values;
                draw["textures"] = textures;
                draw["model"] = model;
                draws.append(draw);
            }
            mark("draw_records");
        }
    }
    mark("tail");
    if(profile) owner->set_meta("niagara_bridge_refraction_timings_usec",phases);
    std::lock_guard<std::mutex> guard(lock);
    Dictionary group;
    group["owner"] = UtilityFunctions::weakref(owner);
    group["draws"] = draws;
    group["policy"] = policy;
    group["refractive_surfaces"] = refractive_surfaces;
    players[owner->get_instance_id()] = group;
    Array previous_errors = report["errors"];
    for (int n = 0; n < errors.size(); ++n)
        if (!previous_errors.has(errors[n]))
            previous_errors.append(errors[n]);
}
void NBNativeRasterPass::remove_player(Node *owner) {
    if (!owner)
        return;
    std::lock_guard<std::mutex> guard(lock);
    players.erase(owner->get_instance_id());
    if (players.is_empty()) {
        Array errors = report["errors"];
        errors.clear();
    }
}
} // namespace godot
