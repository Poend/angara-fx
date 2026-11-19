#include "renderer_geometry.h"
#include "attribute_values.h"
#include "raster_buffers.h"
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/script.hpp>
namespace godot {
Dictionary NBNativeRendererGeometry::mesh_resource_parameters(Node *host, const Ref<Resource> &source) {
    Dictionary values, parameters = host->get("_instance_parameters");
    if (String(source->get("renderer_type")) != "MeshRenderer")
        return values;
    Array entries = source->get("mesh_entries");
    for (int n = 0; n < entries.size(); ++n) {
        Ref<Resource> entry = entries[n];
        String name = entry->get("user_parameter");
        if (!name.is_empty())
            values[name] = parameters.get(name, Variant());
    }
    Array names = source->get("material_parameter_names");
    for (int n = 0; n < names.size(); ++n)
        if (!String(names[n]).is_empty())
            values[names[n]] = parameters.get(names[n], Variant());
    return values;
}
void NBNativeRendererGeometry::refresh_mesh_resources(Node *host, const Ref<Script> &compiler) {
    Array emitters = host->get("_emitters"), diagnostics = host->get("diagnostics");
    for (int n = 0; n < emitters.size(); ++n) {
        Dictionary emitter = emitters[n], plan = emitter["plan"];
        Array renderers = emitter["renderers"];
        for (int index = 0; index < renderers.size(); ++index) {
            Dictionary renderer = renderers[index];
            Ref<Resource> source = renderer["source"];
            if (String(source->get("renderer_type")) != "MeshRenderer")
                continue;
            Dictionary current = host->call("_mesh_resource_parameters", source);
            if (current == Dictionary(renderer["resource_parameter_values"]))
                continue;
            Dictionary result;
            result["diagnostics"] = Array();
            result["fully_supported"] = true;
            Dictionary binding = compiler->call("_compile_mesh_renderer", source,
                                                host->get("_instance_parameters"), result, plan["source"]);
            diagnostics.append_array(result["diagnostics"]);
            if (!bool(binding["valid"])) {
                renderer["binding_invalid"] = true;
                renderer["resource_parameter_values"] = current;
                continue;
            }
            int64_t source_index = Dictionary(renderer["mesh_binding"]).get("source_index", 0);
            Array entries = binding["entries"];
            if (source_index >= entries.size()) {
                renderer["binding_invalid"] = true;
                renderer["resource_parameter_values"] = current;
                continue;
            }
            Node *instance = Object::cast_to<Node>(renderer["instance"].get_validated_object());
            host->remove_child(instance);
            instance->queue_free();
            Dictionary bindings = plan["mesh_bindings"];
            bindings[source] = binding;
            renderers[index] = host->call("_create_renderer", source, index, entries[source_index],
                                          compiler->call("renderer_dynamic_parameter_mask", plan, source));
            if (renderer.has("grid_volume_targets"))
                bind_renderer_volumes(renderers[index], renderer["grid_volume_targets"]);
        }
    }
}
void NBNativeRendererGeometry::refresh_dynamic_masks(Node *host) {
    Array emitters = host->get("_emitters");
    for (int n = 0; n < emitters.size(); ++n) {
        Dictionary emitter = emitters[n], plan = emitter["plan"], context;
        Array renderers = emitter["renderers"];
        Dictionary attributes = plan.get("renderer_attributes", Dictionary());
        for (int slot = 0; slot < renderers.size(); ++slot) {
            Dictionary renderer = renderers[slot];
            int64_t mask = renderer["dynamic_mask"];
            Array fields =
                Dictionary(attributes.get(renderer["source"], Dictionary())).get("fields", Array());
            for (int index = 0; index < fields.size(); ++index) {
                Dictionary field = fields[index];
                String key = field["key"];
                if (String(field["kind"]) != "host" || !key.begins_with("DynamicMaterial"))
                    continue;
                if (context.is_empty())
                    context = host->call("_emitter_context", emitter, 0., host->get("simulation_time"));
                String suffix = key.trim_prefix("DynamicMaterial");
                int64_t bits = int64_t(15) << (suffix.is_empty() ? 0 : suffix.to_int() * 4);
                mask &= ~bits;
                if (context.has(field["name"]))
                    mask |= bits;
            }
            if (mask == int64_t(renderer["dynamic_mask"]))
                continue;
            renderer["dynamic_mask"] = mask;
            Array materials = renderer["materials"];
            for (int index = 0; index < materials.size(); ++index) {
                Ref<ShaderMaterial> material = materials[index];
                if (material.is_valid())
                    material->set_shader_parameter("nb_dynamic_parameter_valid_mask", mask);
            }
            host->set("_scoped_signature", 0);
        }
    }
}
Dictionary NBNativeRendererGeometry::renderer_view(Node3D *host, Dictionary emitter,
                                                   const Ref<Resource> &source, const Dictionary &builtins) {
    Dictionary view =
        NBNativeRendererViews::view(emitter, source, host->get("_instance_parameters"),
                                    host->get_global_position(), host->call("_context_frame"), builtins);
    renderer_view_errors(host,emitter,source,view["errors"]);
    return view;
}
void NBNativeRendererGeometry::renderer_view_errors(Node3D *host,Dictionary emitter,
                                                     const Ref<Resource> &source,const Array &errors) {
    if (!emitter.has("renderer_filter_keys"))
        emitter["renderer_filter_keys"] = Dictionary();
    Dictionary keys = emitter["renderer_filter_keys"];
    int64_t key = errors.is_empty() ? 0 : errors.hash();
    if (key != int64_t(keys.get(source, 0))) {
        int64_t identity = source->get_instance_id();
        Array previous = host->get("diagnostics");
        TypedArray<Dictionary> diagnostics;
        for (int n = 0; n < previous.size(); ++n) {
            Dictionary row = previous[n];
            if (String(row.get("code", "")) != "renderer_filter_data" ||
                int64_t(row.get("renderer_instance_id", 0)) != identity)
                diagnostics.append(row);
        }
        Ref<Resource> emitter_source = Dictionary(emitter["plan"])["source"];
        for (int n = 0; n < errors.size(); ++n) {
            Dictionary row;
            row["severity"] = "error";
            row["code"] = "renderer_filter_data";
            row["renderer_instance_id"] = identity;
            row["emitter"] = emitter_source->get("display_name");
            row["message"] = errors[n];
            diagnostics.append(row);
        }
        host->set("diagnostics", diagnostics);
        keys[source] = key;
    }
}
void NBNativeRendererGeometry::refresh_coverage(Node *host, const Ref<Script> &policy) {
    Array emitters = host->get("_emitters"), diagnostics = host->get("diagnostics");
    Ref<Resource> effect = host->get("effect");
    NBMaterialSignatureMemo material_memo;
    auto stock=[](Object *object,const char *path) {
        if(!object) return false;
        Ref<Script> script=object->get_script();return script.is_valid() && script->get_path()==path;
    };
    const bool plain_effect=stock(effect.ptr(),"res://addons/angara_fx/document/nb_effect.gd");
    for (int n = 0; n < emitters.size(); ++n) {
        Dictionary emitter = emitters[n], plan = emitter["plan"];
        Array renderers = emitter["renderers"];
        for (int slot = 0; slot < renderers.size(); ++slot) {
            Dictionary renderer = renderers[slot];
            Ref<Resource> source = renderer["source"];
            bool plain=plain_effect && stock(source.ptr(),"res://addons/angara_fx/document/nb_renderer.gd");
            // No extra reads of custom document properties. Exact stock input
            // records are plain data; callbacks retain the original fresh path.
            if(plain && !NBNativeEffectRevision::current_revision) {
                Array inputs=source->get("properties");
                for(int64_t i=0;i<inputs.size();++i) {
                    Ref<Resource> input=inputs[i];
                    if(!stock(input.ptr(),"res://addons/angara_fx/document/nb_input.gd")) {plain=false;break;}
                }
            }
            if(!plain) material_memo.clear();
            if (String(source->get("renderer_type")) != "SpriteRenderer")
                continue;
            Dictionary order = effect->get("source_renderer_order");
            int64_t signature =
                Array::make(order.get("verified", Variant()),
                            order.get("sprite_pixel_coverage_policy", Variant()),
                            source->call("get_input", "PixelCoverageMode", "Automatic"),
                            source->call("get_input", "PixelCoverageBlend", 1.),
                            NBNativeRasterBuffers::material_signature_memoized(source->get("material"),material_memo))
                    .hash();
            if(!plain) material_memo.clear();
            Variant previous = renderer.get("pixel_coverage_signature", Variant());
            if (previous.get_type() == Variant::INT && int64_t(previous) == signature)
                continue;
            renderer["pixel_coverage_signature"] = signature;
            Dictionary config = policy->call("configuration", source, order);
            // Configuration is an external callback and may mutate another graph.
            material_memo.clear();
            renderer["pixel_coverage"] = config;
            if (!bool(config["verified"])) {
                Ref<Resource> emitter_source = plan["source"];
                Dictionary row;
                row["severity"] = "warning";
                row["code"] = "sprite_pixel_coverage_unverified";
                row["emitter"] = emitter_source->get("display_name");
                row["message"] = config["diagnostic"];
                diagnostics.append(row);
            }
        }
    }
}
bool NBNativeRendererGeometry::validate_temporal(Node3D *host) {
    if (!bool(host->get_viewport()->get("use_taa")))
        return true;
    Array emitters = host->get("_emitters");
    for (int n = 0; n < emitters.size(); ++n) {
        Array renderers = Dictionary(emitters[n])["renderers"];
        for (int slot = 0; slot < renderers.size(); ++slot) {
            Array materials = Dictionary(renderers[slot])["materials"];
            for (int index = 0; index < materials.size(); ++index) {
                Ref<Material> material = materials[index];
                if (material.is_null())
                    continue;
                Dictionary conversion = material->get_meta("angara_fx_conversion", Dictionary());
                Array requirements = conversion.get("requirements", Array());
                if (!requirements.has("temporal_view_requires_TAA_disabled_in_source_and_target"))
                    continue;
                if (!bool(host->get("_temporal_view_reported"))) {
                    Dictionary row;
                    row["severity"] = "error";
                    row["code"] = "temporal_view";
                    row["message"] =
                        "Material temporal view properties currently require TAA disabled in both source and "
                        "target; matching jitter sequence transport is not implemented.";
                    Array diagnostics = host->get("diagnostics");
                    diagnostics.append(row);
                    host->set("_temporal_view_reported", true);
                }
                for (int e = 0; e < emitters.size(); ++e) {
                    Array draws = Dictionary(emitters[e])["renderers"];
                    for (int d = 0; d < draws.size(); ++d) {
                        Node3D *node =
                            Object::cast_to<Node3D>(Dictionary(draws[d])["instance"].get_validated_object());
                        if (node)
                            node->set_visible(false);
                    }
                }
                host->call("_dispose_composite");
                host->set("_scoped_signature", 0);
                Ref<RefCounted> weak = host->get("_refraction_pass");
                if (weak.is_valid()) {
                    Ref<RefCounted> pass = weak->call("get_ref");
                    if (pass.is_valid())
                        pass->call("remove_player", host);
                }
                host->set("_refraction_pass", Variant());
                return false;
            }
        }
    }
    return true;
}
Ref<Material> NBNativeRendererGeometry::diagnostic_material() {
    Ref<StandardMaterial3D> material;
    material.instantiate();
    material->set("albedo_color", Color(1., .2, .8, .5));
    material->set("vertex_color_use_as_albedo", true);
    material->set("transparency", BaseMaterial3D::TRANSPARENCY_ALPHA);
    material->set("shading_mode", BaseMaterial3D::SHADING_MODE_UNSHADED);
    material->set("cull_mode", BaseMaterial3D::CULL_DISABLED);
    return material;
}
} // namespace godot
