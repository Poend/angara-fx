#include "composite_renderer.h"
#include "renderer_geometry.h"
#include "renderer_filter.h"
#include "raster_buffers.h"
#include "value_evaluator.h"
#include <godot_cpp/classes/multi_mesh_instance3d.hpp>
#include <godot_cpp/classes/multi_mesh.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/templates/hashfuncs.hpp>
#include <cmath>
#include <unordered_map>

namespace godot {
namespace {
bool equal(const Variant &a, const Variant &b) {
    Variant result;
    bool valid = false;
    Variant::evaluate(Variant::OP_EQUAL, a, b, result, valid);
    return valid && bool(result);
}
String identity(const Dictionary &record) {
    return UtilityFunctions::str(record.get("emitter_stable_id", "")) + "/" +
           UtilityFunctions::str(record.get("renderer_stable_id", ""));
}
String identity(const Ref<Resource> &emitter, const Ref<Resource> &renderer) {
    return String(emitter->get("stable_id")) + "/" + String(renderer->get("stable_id"));
}
Node3D *instance(const Dictionary &renderer) {
    return Object::cast_to<Node3D>(renderer["instance"].get_validated_object());
}
void warning(Node *host, const String &code, const String &message) {
    Array diagnostics = host->get("diagnostics");
    Dictionary row;
    row["severity"] = "warning";
    row["code"] = code;
    row["message"] = message;
    diagnostics.append(row);
}
} // namespace
void NBNativeRendererGeometry::dispose_composite(Node *host) {
    host->call("_release_native_surface");
    Ref<RefCounted> composite = host->get("_composite");
    if (composite.is_valid())
        composite->call("dispose");
    host->set("_composite", Variant());
    Array contexts = host->get("_composite_contexts");
    contexts.clear();
    Array emitters = host->get("_emitters");
    for (int n = 0; n < emitters.size(); ++n) {
        Array renderers = Dictionary(emitters[n])["renderers"];
        for (int slot = 0; slot < renderers.size(); ++slot) {
            Dictionary renderer = renderers[slot];
            renderer["scoped_hidden"] = false;
            Ref<Resource> source = renderer["source"];
            auto *node = instance(renderer);
            if (String(source->get("renderer_type")) == "MeshRenderer" && renderer.has("full_mesh") && node) {
                auto *multi = Object::cast_to<MultiMeshInstance3D>(node);
                multi->get_multimesh()->set_mesh(renderer["full_mesh"]);
            }
        }
    }
}
Ref<Material> NBNativeRendererGeometry::composite_ribbon_material(Node *host, const Ref<Resource> &source) {
    Variant raw = NBNativeRendererFilter::input(source, "MaterialUserParamBinding", Dictionary());
    if (raw.get_type() == Variant::STRING)
        raw = JSON::parse_string(raw);
    Dictionary binding = raw.get_type() == Variant::DICTIONARY ? Dictionary(raw) : Dictionary();
    String name = UtilityFunctions::str(Dictionary(binding.get("parameter", Dictionary())).get("name", ""));
    Variant value = Dictionary(host->get("_instance_parameters")).get(name, Variant());
    if (value.get_type() == Variant::NIL || value.get_type() == Variant::STRING)
        return source->get("material");
    return value.get_type() == Variant::OBJECT
               ? Ref<Material>(Object::cast_to<Material>(value.get_validated_object()))
               : Ref<Material>();
}
int64_t NBNativeRendererGeometry::composite_signature(Node3D *host, const Ref<Script> &exposure) {
    Ref<Resource> effect = host->get("effect");
    if (effect.is_null())
        return Array::make(host->get("use_scoped_sprite_rendering"), Variant()).hash();
    // One read-only signature evaluation may reference a shared material many
    // times. Keep this memo local: every new evaluation reads live graph,
    // shader, metadata and uniform values again, including silent edits.
    NBMaterialSignatureMemo graph_memo;
    // Custom document getters may mutate shared graphs during traversal. Only
    // stock data resources permit reuse across distinct material identities.
    auto stock=[](const Ref<Resource> &resource,const char *path) {
        Ref<Script> script=resource.is_valid() ? Ref<Script>(resource->get_script()) : Ref<Script>();
        return script.is_valid() && script->get_path()==path;
    };
    Ref<Script> host_script=host->get_script();
    bool plain=host_script.is_valid() && host_script->get_path()=="res://addons/niagara_bridge/runtime/effect_player.gd" &&
               stock(effect,"res://addons/niagara_bridge/document/nb_effect.gd");
    auto *revision = NBNativeEffectRevision::current_revision;
    Array source_emitters=plain && !revision ? Array(effect->get("emitters")) : Array();
    for(int64_t i=0;i<source_emitters.size() && plain;++i) {
        Ref<Resource> emitter=source_emitters[i];plain=stock(emitter,"res://addons/niagara_bridge/document/nb_emitter.gd");
        if(!plain) break;
        Array renderers=emitter->get("renderers");
        for(int64_t n=0;n<renderers.size() && plain;++n) {
            Ref<Resource> renderer=renderers[n];plain=stock(renderer,"res://addons/niagara_bridge/document/nb_renderer.gd");
            if(!plain) break;
            Array inputs=renderer->get("properties");
            for(int64_t slot=0;slot<inputs.size() && plain;++slot) plain=stock(inputs[slot],"res://addons/niagara_bridge/document/nb_input.gd");
        }
    }
    Array runtime_emitters=plain && !revision ? Array(host->get("_emitters")) : Array();
    for(int64_t i=0;i<runtime_emitters.size() && plain;++i) {
        Array renderers=Dictionary(runtime_emitters[i])["renderers"];
        for(int64_t n=0;n<renderers.size() && plain;++n) {
            Ref<Resource> source=Dictionary(renderers[n])["source"];
            plain=stock(source,"res://addons/niagara_bridge/document/nb_renderer.gd");
            if(!plain) break;
            Array inputs=source->get("properties");
            for(int64_t slot=0;slot<inputs.size() && plain;++slot) plain=stock(inputs[slot],"res://addons/niagara_bridge/document/nb_input.gd");
        }
    }
    std::unordered_map<uint64_t, Variant> material_signatures;
    auto signature = [&](const Ref<Material> &material) -> Variant {
        uint64_t id = material.is_valid() ? uint64_t(material->get_instance_id()) : 0;
        auto found = material_signatures.find(id);
        if (found != material_signatures.end())
            return found->second;
        if(!plain) graph_memo.clear();
        Variant value = NBNativeRasterBuffers::material_signature_memoized(material,graph_memo);
        material_signatures.emplace(id, value);
        return value;
    };
    Dictionary view = exposure->call("for_camera", host->get_viewport()->get_camera_3d());
    Array parts = Array::make(host->get("use_scoped_sprite_rendering"), effect->get("source_hash"),
                              effect->get("source_renderer_order"), host->get("_render_filter_enabled"),
                              host->get("_render_emitter_filter"), view["supported"]);
    Array sources = effect->get("emitters");
    for (int n = 0; n < sources.size(); ++n) {
        Ref<Resource> emitter = sources[n];
        parts.append(Array::make(emitter->get("stable_id"), emitter->get("enabled")));
        Array renderers = emitter->get("renderers");
        for (int slot = 0; slot < renderers.size(); ++slot) {
            Ref<Resource> renderer = renderers[slot];
            parts.append(Array::make(
                renderer->get("stable_id"), renderer->get("enabled"), renderer->get("renderer_type"),
                renderer->call("get_input", "SortOrderHint", 0),
                renderer->call("get_input", "SubImageSize", Array::make(1, 1)),
                renderer->call("get_input", "bSubImageBlend", false), signature(renderer->get("material"))));
        }
    }
    Array emitters = host->get("_emitters");
    for (int n = 0; n < emitters.size(); ++n) {
        Array renderers = Dictionary(emitters[n])["renderers"];
        for (int slot = 0; slot < renderers.size(); ++slot) {
            Dictionary renderer = renderers[slot];
            Ref<Resource> source = renderer["source"];
            String kind = source->get("renderer_type");
            parts.append(renderer.get("dynamic_mask", 0));
            if (kind == "MeshRenderer") {
                Dictionary binding = renderer["mesh_binding"];
                parts.append(Array::make(binding["mesh"], renderer["resource_parameter_values"],
                                         renderer.get("binding_invalid", false)));
                Array materials = binding["materials"];
                for (int index = 0; index < materials.size(); ++index)
                    parts.append(signature(materials[index]));
            } else if (kind == "RibbonRenderer")
                parts.append(signature(composite_ribbon_material(host, source)));
        }
    }
    if (!revision)
        return parts.hash();
    // Keep the public Array hash exactly, including the live material uniforms.
    // Slot 2 contains the entire imported system ordering and bounds document.
    if (!revision->saved_order_hash)
        revision->saved_order_hash = parts[2].recursive_hash(1);
    uint32_t hash = hash_murmur3_one_32(Variant::ARRAY);
    for (int64_t i = 0; i < parts.size(); ++i)
        hash = hash_murmur3_one_32(i == 2 ? *revision->saved_order_hash : parts[i].recursive_hash(1), hash);
    return int64_t(hash_fmix32(hash));
}
String NBNativeRendererGeometry::material_pass(const Ref<Material> &material, const Ref<Script> &program) {
    if (material.is_valid() && material->is_class("BaseMaterial3D"))
        return int64_t(material->get("transparency")) == 0 ? "opaque" : "unsealed";
    Ref<ShaderMaterial> shader = material;
    if (shader.is_null() || !Array(program->call("validate", shader)).is_empty())
        return "unsealed";
    Dictionary metadata = shader->get_meta("niagara_bridge_material_program");
    Ref<Resource> graph = metadata["graph"];
    String blend =
        UtilityFunctions::str(Dictionary(graph->get("material_properties")).get("BlendMode", "BLEND_Opaque"));
    if (blend == "BLEND_Opaque" || blend == "BLEND_Masked")
        return "opaque";
    return blend == "BLEND_Additive" || blend == "BLEND_Translucent" || blend == "BLEND_AlphaComposite" ? "transparent" : "unsupported";
}
void NBNativeRendererGeometry::composite_unavailable(Node *host, const String &message) {
    Array contexts = host->get("_composite_contexts");
    contexts.clear();
    Dictionary status = host->get("_scoped_status");
    Array errors = status["errors"];
    errors.append(message);
    warning(host, "scoped_transparency_unavailable", message);
}
void NBNativeRendererGeometry::prepare_composite(Node3D *host, const Ref<Script> &composer,
                                                 const Ref<Script> &program,
                                                 const Ref<Script> &composite_type) {
    host->call("_dispose_composite");
    Dictionary status;
    status["mode"] = "separate_renderers";
    status["source_order_verified"] = false;
    status["shared_depth_verified"] = false;
    status["errors"] = Array();
    status["limitations"] = Array();
    host->set("_scoped_status", status);
    Ref<Resource> effect = host->get("effect");
    if (effect.is_null())
        return;
    auto fail = [&](const String &message) { host->call("_scoped_unavailable", message); };
    if (!bool(host->get("use_scoped_sprite_rendering"))) {
        fail("Scoped sprite rendering is disabled; separate transparent renderers do not reproduce native "
             "component-scoped ordering.");
        return;
    }
    Dictionary order = effect->get("source_renderer_order");
    String source_hash = effect->get("source_hash");
    if (!equal(order.get("schema_version", Variant()), 1) || !equal(order.get("verified", Variant()), true) ||
        !equal(order.get("source_dfs_sha256", ""), source_hash) || source_hash.is_empty()) {
        fail("Missing or stale verified native renderer order; separate transparent renderers use Godot "
             "bounds ordering.");
        return;
    }
    Variant priority = order.get("component_priority", Variant());
    if ((priority.get_type() != Variant::INT && priority.get_type() != Variant::FLOAT) ||
        !std::isfinite(double(priority)) || double(priority) != std::floor(double(priority)) ||
        double(priority) < -128. || double(priority) > 127.) {
        fail("Native component priority cannot be represented by Godot's [-128,127] material priority; no "
             "clamping is applied.");
        return;
    }
    Variant offset = order.get("component_distance_offset_cm", Variant());
    if ((offset.get_type() != Variant::INT && offset.get_type() != Variant::FLOAT) ||
        !std::isfinite(double(offset))) {
        fail("Native component sort distance offset must be finite.");
        return;
    }
    Variant raw_records = order.get("draw_order", Variant());
    if (raw_records.get_type() != Variant::ARRAY) {
        fail("Native renderer order is not an array.");
        return;
    }
    Array records = raw_records;
    Dictionary cached;
    for (int rank = 0; rank < records.size(); ++rank) {
        if (records[rank].get_type() != Variant::DICTIONARY ||
            !equal(Dictionary(records[rank]).get("draw_rank", Variant()), rank)) {
            fail("Native renderer order has a missing or noncontiguous rank.");
            return;
        }
        Dictionary record = records[rank];
        String key = identity(record);
        if (UtilityFunctions::str(record.get("emitter_stable_id", "")).is_empty() ||
            UtilityFunctions::str(record.get("renderer_stable_id", "")).is_empty() || cached.has(key)) {
            fail("Native renderer order has an empty or duplicate stable identity.");
            return;
        }
        cached[key] = record;
    }
    Dictionary active;
    PackedStringArray filter = host->get("_render_emitter_filter");
    Array sources = effect->get("emitters");
    for (int n = 0; n < sources.size(); ++n) {
        Ref<Resource> emitter = sources[n];
        if (!bool(emitter->get("enabled")))
            continue;
        Array renderers = emitter->get("renderers");
        for (int slot = 0; slot < renderers.size(); ++slot) {
            Ref<Resource> renderer = renderers[slot];
            if (!bool(renderer->get("enabled")))
                continue;
            String key = identity(emitter, renderer);
            if (active.has(key) || !cached.has(key)) {
                fail("An enabled renderer is new or has ambiguous identity outside the verified native "
                     "order.");
                return;
            }
            Dictionary record = cached[key];
            if (!equal(renderer->call("get_input", "SortOrderHint", 0),
                       record.get("sort_order_hint", Variant()))) {
                fail("An authored SortOrderHint differs from the cached source order; regenerate source "
                     "ordering evidence.");
                return;
            }
            if (bool(host->get("_render_filter_enabled")) &&
                !filter.has(String(emitter->get("display_name"))))
                continue;
            String kind = renderer->get("renderer_type");
            String expected = kind == "SpriteRenderer"   ? "NiagaraSpriteRendererProperties"
                              : kind == "MeshRenderer"   ? "NiagaraMeshRendererProperties"
                              : kind == "RibbonRenderer" ? "NiagaraRibbonRendererProperties"
                                                         : "";
            if (expected.is_empty() || !equal(record.get("renderer_class", Variant()), expected)) {
                fail("The visible composite group requires verified sprite, mesh or ribbon renderer "
                     "geometry.");
                return;
            }
            Dictionary state;
            state["source"] = renderer;
            active[key] = state;
        }
    }
    Array emitters = host->get("_emitters"), composite_contexts = host->get("_composite_contexts");
    for (int n = 0; n < emitters.size(); ++n) {
        Dictionary emitter = emitters[n], plan = emitter["plan"];
        Array renderers = emitter["renderers"];
        for (int slot = 0; slot < renderers.size(); ++slot) {
            Dictionary renderer = renderers[slot];
            String key = identity(plan["source"], renderer["source"]);
            if (!active.has(key))
                continue;
            Dictionary state = active[key];
            if (!state.has("draws"))
                state["draws"] = Array();
            Array draws = state["draws"];
            Dictionary draw;
            draw["emitter"] = emitter;
            draw["renderer"] = renderer;
            draws.append(draw);
        }
    }
    TypedArray<Dictionary> contexts, partitions;
    for (int n = 0; n < records.size(); ++n) {
        Dictionary record = records[n];
        String key = identity(record);
        if (!active.has(key))
            continue;
        Dictionary source_state = active[key];
        if (!source_state.has("draws")) {
            fail("An enabled source renderer has a blocked simulation plan; the full composite group is "
                 "unavailable.");
            return;
        }
        Array draws = source_state["draws"];
        for (int slot = 0; slot < draws.size(); ++slot) {
            Dictionary draw = draws[slot], renderer = draw["renderer"];
            if (bool(renderer.get("binding_invalid", false))) {
                fail("An enabled mesh renderer has an invalid resource parameter binding; stale geometry is "
                     "not composed.");
                return;
            }
            Ref<Resource> source = renderer["source"];
            String type = source->get("renderer_type");
            int kind = type == "MeshRenderer" ? 3 : type == "RibbonRenderer" ? 2 : 1;
            Array selected = kind == 3
                                 ? Array(Dictionary(renderer["mesh_binding"])["materials"])
                                 : Array::make(kind == 2 ? Variant(composite_ribbon_material(host, source))
                                                         : source->get("material"));
            if (renderer.has("grid_volume_parameters")) {
                selected = selected.duplicate();
                for (int surface = 0; surface < selected.size(); ++surface)
                    selected[surface] = volume_material(selected[surface], renderer["grid_volume_parameters"], true);
            }
            TypedArray<int64_t> opaque, transparent;
            for (int surface = 0; surface < selected.size(); ++surface) {
                String pass = material_pass(selected[surface], program);
                if (pass == "opaque") {
                    opaque.append(surface);
                    continue;
                }
                if (pass != "transparent") {
                    fail(
                        "Composite geometry requires compatible compiled ShaderMaterials for every surface.");
                    return;
                }
                transparent.append(surface);
                Array arrays;
                if (kind == 3) {
                    Ref<Mesh> mesh = Dictionary(renderer["mesh_binding"])["mesh"];
                    Dictionary metadata =
                        RenderingServer::get_singleton()->mesh_get_surface(mesh->get_rid(), surface);
                    if (int64_t(metadata.get("primitive", -1)) != Mesh::PRIMITIVE_TRIANGLES) {
                        fail("Composite mesh geometry requires triangle surfaces.");
                        return;
                    }
                    arrays = mesh->surface_get_arrays(surface);
                }
                Dictionary context;
                context["renderer"] = source;
                context["material"] = selected[surface];
                context["renderer_kind"] = kind;
                context["subimage_size"] =
                    NBNativeValues::vector2(source->call("get_input", "SubImageSize", Array::make(1, 1)));
                context["subimage_blend"] = bool(source->call("get_input", "bSubImageBlend", false));
                context["dynamic_mask"] = renderer["dynamic_mask"];
                contexts.append(context);
                Dictionary submitted;
                submitted["emitter"] = draw["emitter"];
                submitted["renderer"] = renderer;
                submitted["source_rank"] = record["draw_rank"];
                submitted["context_slot"] = contexts.size() - 1;
                submitted["kind"] = kind;
                submitted["arrays"] = arrays;
                submitted["surface"] = surface;
                composite_contexts.append(submitted);
            }
            Dictionary partition;
            partition["renderer"] = renderer;
            partition["kind"] = kind;
            partition["opaque"] = opaque;
            partition["transparent"] = transparent;
            partitions.append(partition);
        }
    }
    if (contexts.is_empty()) {
        status["mode"] = partitions.is_empty() ? "no_enabled_renderers" : "opaque_renderers";
        return;
    }
    Dictionary composed = composer->call("build", contexts);
    Ref<ShaderMaterial> material;
    Variant candidate = composed.get("material", Variant());
    if (candidate.get_type() == Variant::OBJECT)
        material = candidate;
    if (!bool(composed.get("supported", false)) || !equal(composed.get("protocol_version", Variant()), 1) ||
        material.is_null()) {
        Array errors = composed.get("errors", Array::make("unsupported protocol/material"));
        PackedStringArray messages;
        for (int n = 0; n < errors.size(); ++n)
            messages.append(UtilityFunctions::str(errors[n]));
        fail("Composite material rejected: " + String("; ").join(messages));
        return;
    }
    Ref<RefCounted> composite = composite_type->call("new");
    host->set("_composite", composite);
    composite->call("configure", host, material, int64_t(priority), -double(offset) * .01);
    Dictionary view = composite->call("update_view", host->get_viewport()->get_camera_3d());
    if (!bool(view["supported"])) {
        host->call("_dispose_composite");
        fail(view["error"]);
        return;
    }
    host->call("_sync_renderer_primitive_bounds", composite->get("bounds_context"));
    for (int n = 0; n < emitters.size(); ++n) {
        Array renderers = Dictionary(emitters[n])["renderers"];
        for (int slot = 0; slot < renderers.size(); ++slot)
            instance(renderers[slot])->set_visible(false);
    }
    bool non_sprite = false;
    for (int n = 0; n < composite_contexts.size(); ++n) {
        Dictionary state = composite_contexts[n], renderer = state["renderer"];
        instance(renderer)->set_visible(false);
        renderer["attributes_supported"] = true;
        non_sprite |= int64_t(state["kind"]) != 1;
    }
    int64_t opaque_count = 0;
    for (int n = 0; n < partitions.size(); ++n) {
        Dictionary partition = partitions[n], renderer = partition["renderer"];
        Array opaque = partition["opaque"], transparent = partition["transparent"];
        renderer["scoped_hidden"] = opaque.is_empty();
        opaque_count += opaque.size();
        if (int64_t(partition["kind"]) == 3 && !opaque.is_empty() && !transparent.is_empty())
            Object::cast_to<MultiMeshInstance3D>(instance(renderer))
                ->get_multimesh()
                ->set_mesh(opaque_sections(renderer, opaque));
    }
    Array limitations =
        Array::make("One transparent surface preserves renderer ranks within this component. Exact shared "
                    "depth still requires verified native bounds history and camera sort policy; "
                    "geometry-union fallback bounds are not native Niagara bounds.",
                    "Separate components at exactly equal sort depth retain engine-specific tie behavior.",
                    "CPU world-space geometry rebuild and upload cover every submitted sprite, mesh vertex "
                    "and ribbon vertex; no GPU-only batching performance claim.");
    status["mode"] = non_sprite ? "scoped_particle_surface" : "scoped_sprite_surface";
    status["source_order_verified"] = true;
    status["renderers"] = contexts.size();
    status["component_priority"] = int64_t(priority);
    status["component_distance_offset_cm"] = double(offset);
    status["bounds_mode"] = "current_geometry_union";
    status["limitations"] = limitations;
    status["opaque_surfaces"] = opaque_count;
    status["mixed_transparency_blending"] = composed.get("mixed_blending", false);
    status["mixed_lighting"] = composed.get("mixed_lighting", false);
    TypedArray<Dictionary> ranks;
    for (int n = 0; n < composite_contexts.size(); ++n) {
        Dictionary state = composite_contexts[n], emitter = state["emitter"], plan = emitter["plan"],
                   renderer = state["renderer"];
        Ref<Resource> emitter_source = plan["source"], renderer_source = renderer["source"];
        Dictionary row;
        row["emitter"] = emitter_source->get("display_name");
        row["emitter_stable_id"] = emitter_source->get("stable_id");
        row["renderer_stable_id"] = renderer_source->get("stable_id");
        row["source_rank"] = state["source_rank"];
        row["context_slot"] = state["context_slot"];
        ranks.append(row);
    }
    status["draw_order"] = ranks;
    warning(host, "scoped_transparency_bounds", limitations[0]);
}
void NBNativeRendererGeometry::synchronize_composite(Node3D *host, Camera3D *camera,
                                                     const Ref<Script> &compiler,
                                                     const Dictionary &builtins) {
    Array packets, contexts = host->get("_composite_contexts");
    // Only bypass the stock script adapter. Derived user implementations remain
    // observable even when a compatible resident particle buffer is available.
    bool stock_view=NBNativeParticleView::stock_player_method(host,"_renderer_particle_view");
    for (int n = 0; n < contexts.size(); ++n) {
        Dictionary state = contexts[n], emitter = state["emitter"], renderer = state["renderer"];
        if (bool(emitter.get("curve_parameters_invalid", false)))
            continue;
        Dictionary packet;
        Dictionary layouts=Dictionary(emitter["plan"]).get("renderer_attributes",Dictionary());
        Dictionary layout=layouts.get(renderer["source"],Dictionary());
        bool resident=stock_view && !bool(layout.get("emitter_source",false)) &&
                         NBNativeParticleView::for_emitter(emitter).is_valid();
        Dictionary view;
        if(resident) {
            view["particles"]=Array();packet["resident_rows"]=true;
            packet["parameters"]=host->get("_instance_parameters");packet["host"]=host;
        } else view=host->call("_renderer_particle_view",emitter,renderer["source"]);
        packet["state"] = state;
        packet["particles"] = view["particles"];
        packet["visible"] = host->call("_render_emitter_visible", emitter);
        packet["offset_name"] = compiler->call("renderer_camera_offset_reference", renderer["source"]);
        packets.append(packet);
    }
    NBGeometryPacket submitted = composite_packet(packets, camera, host->get_global_transform(),
                                                   host->call("_context_frame"), builtins, host->get("_tick"));
    Array diagnostics = host->get("diagnostics");
    diagnostics.append_array(submitted.diagnostics);
    Variant bounds = submitted.bounds;
    Ref<RefCounted> native_bounds = host->get("_native_bounds");
    if (native_bounds.is_valid()) {
        host->set("_native_bounds_result",
                  native_bounds->call("step", Array(), host->get_global_transform(), 0., false, false));
        host->call("_mark_partial_bounds");
    }
    Dictionary result = host->get("_native_bounds_result"), status = host->get("_scoped_status");
    status["native_bounds"] = result.duplicate(true);
    bool verified = result.get("verified", false);
    status["bounds_history_verified"] = verified;
    status["bounds_mode"] = verified ? "native_component_history" : "current_geometry_union";
    if (verified)
        bounds = result["world_bounds_godot"];
    Ref<RefCounted> composite = host->get("_composite");
    Ref<NBNativeCompositeRenderer> native_composite = composite;
    Ref<Script> script = composite->get_script();
    bool script_update = script.is_valid() && script->has_script_method("update");
    if (native_composite.is_valid() && !script_update)
        native_composite->update_packet(submitted.records, host->get("simulation_time"), bounds);
    else
        composite->call("update", packet_records(submitted.records), host->get("simulation_time"), bounds);
}
} // namespace godot
