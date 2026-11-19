#include "renderer_geometry.h"
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
namespace godot {
namespace {
Ref<RefCounted> referenced(Node *host, const char *name) {
    Ref<RefCounted> weak = host->get(name);
    return weak.is_valid() ? Ref<RefCounted>(weak->call("get_ref")) : Ref<RefCounted>();
}
Array camera_passes(Camera3D *camera) {
    Ref<Resource> compositor = camera ? Ref<Resource>(camera->get("compositor")) : Ref<Resource>();
    return compositor.is_valid() ? Array(compositor->get("compositor_effects")) : Array();
}
Ref<RefCounted> select(const Array &passes, const Ref<Script> &type) {
    for (int n = 0; n < passes.size(); ++n) {
        Ref<RefCounted> pass = passes[n];
        if (pass.is_valid() && bool(pass->get("enabled")) && Ref<Script>(pass->get_script()) == type)
            return pass;
    }
    return Ref<RefCounted>();
}
void disable_refraction(const Ref<Material> &material) {
    Ref<ShaderMaterial> shader = material;
    if (shader.is_valid() &&
        shader->get_shader_parameter("nb_deferred_refraction").get_type() != Variant::NIL)
        shader->set_shader_parameter("nb_deferred_refraction", false);
}
} // namespace
void NBNativeRendererGeometry::release_lights(Node *host) {
    Ref<RefCounted> pass = referenced(host, "_particle_light_pass");
    if (pass.is_valid())
        pass->call("remove_particle_lights", host);
    host->set("_particle_light_pass", Variant());
    Dictionary status;
    status["active"] = false;
    status["records"] = 0;
    host->set("_particle_light_status", status);
    host->set("_particle_light_error_key", 0);
}
void NBNativeRendererGeometry::synchronize_lights(Node3D *host, Camera3D *camera,
                                                  const Ref<Script> &pass_type) {
    Array records, emitters = host->get("_emitters");
    TypedArray<String> data_errors;
    for (int n = 0; n < emitters.size(); ++n) {
        Dictionary emitter = emitters[n];
        records.append_array(emitter.get("light_records", Array()));
        data_errors.append_array(emitter.get("light_errors", Array()));
    }
    if (!host->is_visible_in_tree() || !data_errors.is_empty())
        records.clear();
    Ref<RefCounted> selected =
        records.is_empty() ? Ref<RefCounted>() : select(camera_passes(camera), pass_type);
    Ref<RefCounted> previous = referenced(host, "_particle_light_pass");
    if (previous.is_valid() && previous != selected)
        previous->call("remove_particle_lights", host);
    host->set("_particle_light_pass", selected.is_valid() ? UtilityFunctions::weakref(selected) : Variant());
    TypedArray<String> errors;
    if (!records.is_empty()) {
        if (selected.is_null())
            errors.append("Active particle lights require a native surface pass and scene lighting context "
                          "on the active camera.");
        else
            errors.assign(selected->call("submit_player_particle_lights", host, records, camera));
    }
    Array combined = errors.duplicate();
    combined.append_array(data_errors);
    Dictionary status;
    status["active"] = !records.is_empty() && errors.is_empty();
    status["records"] = records.size();
    status["errors"] = combined;
    status["backend"] = "native_translucency";
    status["opaque_supported"] = false;
    status["volumetric_fog_supported"] = false;
    host->set("_particle_light_status", status);
    int64_t key = errors.is_empty() ? 0 : errors.hash();
    if (key != int64_t(host->get("_particle_light_error_key"))) {
        TypedArray<Dictionary> diagnostics;
        Array previous_diagnostics = host->get("diagnostics");
        for (int n = 0; n < previous_diagnostics.size(); ++n) {
            Dictionary row = previous_diagnostics[n];
            if (String(row.get("code", "")) != "particle_light_backend")
                diagnostics.append(row);
        }
        for (int n = 0; n < errors.size(); ++n) {
            Dictionary row;
            row["severity"] = "error";
            row["code"] = "particle_light_backend";
            row["message"] = errors[n];
            diagnostics.append(row);
        }
        host->set("diagnostics", diagnostics);
        host->set("_particle_light_error_key", key);
    }
}
void NBNativeRendererGeometry::release_surface(Node *host) {
    Ref<RefCounted> previous = referenced(host, "_native_surface_pass");
    if (previous.is_valid())
        previous->call("remove_surface", host);
    host->set("_native_surface_pass", Variant());
    Dictionary status;
    status["active"] = false;
    host->set("_native_surface_status", status);
}
void NBNativeRendererGeometry::synchronize_surface(Node3D *host, Camera3D *camera,
                                                   const Ref<Script> &pass_type) {
    Array passes = camera_passes(camera);
    Ref<RefCounted> selected = select(passes, pass_type), previous = referenced(host, "_native_surface_pass");
    if (previous.is_valid() && previous != selected)
        previous->call("remove_surface", host);
    host->set("_native_surface_pass", selected.is_valid() ? UtilityFunctions::weakref(selected) : Variant());
    Dictionary status;
    status["active"] = false;
    status["errors"] = Array();
    host->set("_native_surface_status", status);
    if (selected.is_null())
        return;
    Ref<RefCounted> composite = host->get("_composite");
    MeshInstance3D *node =
        composite.is_valid()
            ? Object::cast_to<MeshInstance3D>(static_cast<Object *>(composite->get("instance")))
            : nullptr;
    if (!node || node->get_mesh().is_null() || !node->is_visible_in_tree() ||
        (int64_t(node->get("layers")) & camera->get_cull_mask()) == 0) {
        selected->call("remove_surface", host);
        return;
    }
    TypedArray<String> errors;
    errors.assign(selected->call("prepare_player_view", camera));
    Ref<ShaderMaterial> material = composite->get("material");
    Variant deferred = material->get_shader_parameter("nb_deferred_refraction");
    if (deferred.get_type() != Variant::NIL) {
        Ref<RefCounted> distortion = referenced(host, "_refraction_pass");
        if (distortion.is_null() || passes.find(distortion) < passes.find(selected) || !bool(deferred))
            errors.append("Native particle color requires a supported distortion pass after it.");
    }
    if (errors.is_empty()) {
        AABB bounds = node->get_global_transform().xform(AABB(node->call("get_aabb")));
        Dictionary scoped = host->get("_scoped_status"), options;
        options["origin"] = bounds.get_center();
        options["priority"] = scoped["component_priority"];
        options["distance_offset_cm"] = scoped["component_distance_offset_cm"];
        errors.assign(selected->call("submit_surface", host, node, options));
    }
    if (!errors.is_empty()) {
        selected->call("remove_surface", host);
        status["errors"] = errors;
        return;
    }
    node->set_visible(false);
    status["active"] = true;
    status["native_visual_equivalence_verified"] = false;
    status["bounds_verified"] = Dictionary(host->get("_native_bounds_result")).get("verified", false);
    status["scene_wide_transparency_verified"] = false;
}
void NBNativeRendererGeometry::synchronize_refraction(Node *host, Camera3D *camera,
                                                      const Ref<Script> &pass_type) {
    Ref<RefCounted> selected = select(camera_passes(camera), pass_type),
                    previous = referenced(host, "_refraction_pass");
    if (previous.is_valid() && previous != selected)
        previous->call("remove_player", host);
    host->set("_refraction_pass", selected.is_valid() ? UtilityFunctions::weakref(selected) : Variant());
    Array instances, emitters = host->get("_emitters");
    for (int n = 0; n < emitters.size(); ++n) {
        Array renderers = Dictionary(emitters[n])["renderers"];
        for (int slot = 0; slot < renderers.size(); ++slot) {
            Dictionary renderer = renderers[slot];
            Object *node = renderer["instance"];
            if (!camera || (int64_t(node->get("layers")) & camera->get_cull_mask()) != 0)
                instances.append(node);
            if (selected.is_null()) {
                Array materials = renderer["materials"];
                for (int index = 0; index < materials.size(); ++index)
                    disable_refraction(materials[index]);
            }
        }
    }
    Ref<RefCounted> composite = host->get("_composite");
    if (composite.is_valid()) {
        Object *node = composite->get("instance");
        if (!camera || (int64_t(node->get("layers")) & camera->get_cull_mask()) != 0)
            instances.append(node);
        if (selected.is_null())
            disable_refraction(composite->get("material"));
    }
    if (selected.is_valid()) {
        selected->call("submit_player", host, instances);
        Array errors = Dictionary(selected->call("statistics"))["errors"],
              diagnostics = host->get("diagnostics");
        for (int n = 0; n < errors.size(); ++n) {
            bool found = false;
            for (int slot = 0; slot < diagnostics.size(); ++slot) {
                Dictionary row = diagnostics[slot];
                if (String(row.get("code", "")) == "refraction_pass" &&
                    row.get("message", Variant()) == errors[n]) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                Dictionary row;
                row["severity"] = "error";
                row["code"] = "refraction_pass";
                row["message"] = errors[n];
                diagnostics.append(row);
            }
        }
    }
}
} // namespace godot
