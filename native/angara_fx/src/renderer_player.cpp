#include "renderer_geometry.h"
#include "raster_buffers.h"
#include "particle_lights.h"
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/multi_mesh_instance3d.hpp>
#include <godot_cpp/classes/multi_mesh.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <algorithm>
#include <chrono>

namespace godot {
void NBNativeRendererGeometry::synchronize_player(Node3D *host, const Ref<Script> &compiler,const Dictionary &builtins) {
    if (!host || !host->is_inside_tree() || !bool(host->call("_validate_temporal_view")))
        return;
    Ref<NBNativeEffectRevision> revision = host->get("_saved_revision");
    NBNativeEffectRevision::RevisionScope saved_graph_scope(revision.ptr());
    const bool profile=bool(host->get_meta("angara_fx_profile_renderer",false));
    Dictionary phases;
    auto phase_start=profile ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point();
    auto mark=[&](const char *name) {
        if(!profile) return;
        auto now=std::chrono::steady_clock::now();
        phases[name]=std::chrono::duration<double,std::micro>(now-phase_start).count();phase_start=now;
    };
    host->set("_instance_parameters", NBNativeEffectRevision::parameters(host, compiler));
    for (const char *method :
         {"_refresh_curve_parameters", "_refresh_mesh_resource_bindings", "_refresh_host_dynamic_masks",
          "_refresh_sprite_coverage", "_check_native_bounds_inputs", "_sync_primitive_material_bounds"}) {
        host->call(method);mark(method);
    }
    Camera3D *camera = host->get_viewport()->get_camera_3d();
    int64_t signature = host->call("_scoped_input_signature");
    if (signature != int64_t(host->get("_scoped_signature"))) {
        host->set("_scoped_signature", signature);
        host->call("_prepare_composite");
    }
    mark("composition_signature_prepare");
    Object *composite = host->get("_composite");
    if (composite) {
        composite->call("update_view", camera);
        host->call("_sync_composite", camera);
    }
    mark("composite_geometry_upload");
    Array emitters = host->get("_emitters");
    for (int index = 0; index < emitters.size(); ++index) {
        Dictionary emitter = emitters[index], plan = emitter["plan"];
        Array light_records;
        TypedArray<String> light_errors;
        Array configurations = Dictionary(emitter.get("light_configurations", Dictionary())).values();
        if(!configurations.is_empty()) NBNativeParticleView::materialize(emitter);
        Array particles = emitter["particles"];
        for (int n = 0; n < configurations.size(); ++n) {
            Dictionary config = configurations[n];
            Dictionary context = host->call("_emitter_context", emitter, 0., host->get("simulation_time"));
            Dictionary lights =
                NBNativeParticleLights::generate(config, particles, context, host->get_global_transform(),
                                                 plan["local_space"], host->get_instance_id());
            Array records = lights["lights"], errors = lights["errors"];
            if (String(config.get("backend", "")) == "native_empty" && !records.is_empty()) {
                errors.append("LightRenderer exceeded its verified empty-light policy.");
                records.clear();
            }
            light_errors.append_array(errors);
            if (errors.is_empty())
                light_records.append_array(records);
        }
        if (!light_errors.is_empty())
            light_records.clear();
        emitter["light_records"] = light_records;
        emitter["light_errors"] = light_errors;
        emitter["light_error_reported"] = !light_errors.is_empty();
        int64_t light_key = light_errors.is_empty() ? 0 : light_errors.hash();
        if (light_key != int64_t(emitter.get("light_error_key", 0))) {
            Ref<Resource> source = plan["source"];
            String name = source->get("display_name");
            Array previous = host->get("diagnostics");
            TypedArray<Dictionary> diagnostics;
            for (int n = 0; n < previous.size(); ++n) {
                Dictionary row = previous[n];
                if (String(row.get("code", "")) != "particle_light_data" ||
                    String(row.get("emitter", "")) != name)
                    diagnostics.append(row);
            }
            for (int n = 0; n < light_errors.size(); ++n) {
                Dictionary row;
                row["severity"] = "error";
                row["code"] = "particle_light_data";
                row["emitter"] = name;
                row["message"] = light_errors[n];
                diagnostics.append(row);
            }
            host->set("diagnostics", diagnostics);
            emitter["light_error_key"] = light_key;
        }
        Transform3D space = bool(plan["local_space"]) ? host->get_global_transform() : Transform3D();
        Array renderers = emitter["renderers"];
        for (int n = 0; n < renderers.size(); ++n) {
            Dictionary renderer = renderers[n];
            Node3D *instance = Object::cast_to<Node3D>(static_cast<Object *>(renderer["instance"]));
            if (composite && bool(renderer.get("scoped_hidden", false))) {
                instance->set_visible(false);
                continue;
            }
            Ref<Resource> source = renderer["source"];
            String type=source->get("renderer_type");
            const char *sync_method=type=="MeshRenderer" ? "_sync_mesh_renderer" : "_sync_sprite_renderer";
            if(type!="RibbonRenderer" && NBNativeParticleView::stock_player_method(host,"_renderer_particle_view") &&
               NBNativeParticleView::stock_player_method(host,sync_method)) {
                Dictionary layouts=plan.get("renderer_attributes",Dictionary()),layout=layouts.get(source,Dictionary());
                if(!bool(layout.get("emitter_source",false)) && NBNativeParticleView::for_emitter(emitter).is_valid()) {
                    bool invalid=renderer.get("binding_invalid",false);
                    instance->set_visible(bool(source->get("enabled")) && bool(host->call("_render_emitter_visible",emitter)) &&
                                          !invalid && !bool(emitter.get("curve_parameters_invalid",false)));
                    Array materials=renderer["materials"];
                    for(int64_t slot=0;slot<materials.size();++slot) {
                        Ref<ShaderMaterial> material=materials[slot];if(material.is_valid()) material->set_shader_parameter("simulation_time",host->get("simulation_time"));
                    }
                    if(invalid) continue;
                    Dictionary submitted=synchronize_resident(renderer,emitter,camera,host->get_global_transform(),host->call("_context_frame"),
                                                               builtins,host->get("_instance_parameters"),
                                                               compiler->call("renderer_camera_offset_reference",source),type=="MeshRenderer");
                    renderer_view_errors(host,emitter,source,submitted["errors"]);
                    if(bool(submitted["supported"])) continue;
                }
            }
            Dictionary view = host->call("_renderer_particle_view", emitter, source);
            Array draw_particles = view["particles"];
            if (String(source->get("renderer_type")) == "RibbonRenderer") {
                host->call("_sync_ribbon_renderer", emitter, renderer, draw_particles, camera, space);
                continue;
            }
            auto *multi_instance = Object::cast_to<MultiMeshInstance3D>(instance);
            Ref<MultiMesh> multimesh = multi_instance->get_multimesh();
            Array materials = renderer["materials"];
            for (int slot = 0; slot < materials.size(); ++slot) {
                Ref<ShaderMaterial> material = materials[slot];
                if (material.is_valid())
                    material->set_shader_parameter("simulation_time", host->get("simulation_time"));
            }
            if (draw_particles.size() > int64_t(renderer["capacity"])) {
                const int64_t limit = NBNativeParticleView::emitter_capacity(emitter["plan"], host->get("max_particles_per_emitter"));
                int64_t capacity = std::max<int64_t>(draw_particles.size(), std::min(limit, std::max<int64_t>(16, draw_particles.size() * 2)));
                renderer["capacity"] = capacity;
                multimesh->set_instance_count(capacity);
                int64_t width = std::min(capacity * 5, int64_t(1024));
                int64_t height = (capacity * 5 + width - 1) / width;
                Ref<Image> image = Image::create_empty(width, height, false, Image::FORMAT_RGBAF);
                renderer["attributes_image"] = image;
                Ref<ImageTexture> texture = renderer["attributes_texture"];
                texture->set_image(image);
            }
            multimesh->set_visible_instance_count(draw_particles.size());
            instance->set_visible(bool(source->get("enabled")) &&
                                  bool(host->call("_render_emitter_visible", emitter)) &&
                                  !bool(renderer.get("binding_invalid", false)) &&
                                  !bool(emitter.get("curve_parameters_invalid", false)));
            if (bool(renderer.get("binding_invalid", false)))
                continue;
            const char *method = String(source->get("renderer_type")) == "MeshRenderer"
                                     ? "_sync_mesh_renderer"
                                     : "_sync_sprite_renderer";
            host->call(method, emitter, renderer, draw_particles, camera, space);
        }
    }
    mark("individual_renderers_lights");
    host->call("_sync_refraction", camera);mark("refraction");
    host->call("_sync_particle_lights", camera);mark("particle_light_pass");
    host->call("_sync_native_surface", camera);mark("surface");
    if(profile) host->set_meta("angara_fx_renderer_timings_usec",phases);
}
} // namespace godot
