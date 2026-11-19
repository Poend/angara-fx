#include "effect_controller.h"
#include "component_bounds.h"
#include "vm_datasets.h"
#include <godot_cpp/variant/utility_functions.hpp>
namespace godot {
namespace {
Dictionary unverified(const String &message) {
    Dictionary result;
    result["verified"] = false;
    result["diagnostics"] = Array::make(message);
    return result;
}
} // namespace
int64_t NBNativeEffectController::bounds_signature(Node *player) {
    Ref<Resource> effect = player->get("effect");
    if (effect.is_null())
        return 0;
    Array values = Array::make(effect->get("source_hash"), effect->get("source_renderer_order"));
    auto settings = [&](const Array &inputs) {
        for (int n = 0; n < inputs.size(); ++n) {
            Ref<Resource> input = inputs[n];
            values.append(Array::make(input->get("input_name"), input->get("value")));
        }
    };
    settings(effect->get("settings"));
    Array emitters = effect->get("emitters");
    for (int n = 0; n < emitters.size(); ++n) {
        Ref<Resource> emitter = emitters[n];
        values.append(Array::make(emitter->get("stable_id"), emitter->get("enabled")));
        settings(emitter->get("settings"));
        Array renderers = emitter->get("renderers");
        for (int slot = 0; slot < renderers.size(); ++slot) {
            Ref<Resource> renderer = renderers[slot];
            values.append(Array::make(renderer->get("stable_id"), renderer->get("enabled"),
                                      renderer->get("renderer_type"), renderer->get("bindings")));
            Array entries = renderer->get("mesh_entries");
            for (int index = 0; index < entries.size(); ++index) {
                Ref<Resource> entry = entries[index];
                values.append(Array::make(entry->get("source_path"), entry->get("user_parameter"),
                                          entry->get("local_transform"), entry->get("pivot_offset"),
                                          entry->get("pivot_space"), entry->get("lod_index")));
            }
        }
    }
    return values.hash();
}
void NBNativeEffectController::prepare_bounds(Node3D *player) {
    Array omitted = player->get("_native_bounds_omitted");
    omitted.clear();
    player->set("_native_bounds_signature", player->call("_bounds_input_signature"));
    player->set("_native_bounds_result", unverified("No paired native component bounds policy."));
    Ref<Resource> effect = player->get("effect");
    if (effect.is_null())
        return;
    Dictionary order = effect->get("source_renderer_order");
    if (!order.has("bounds_policy") || !order.has("emitter_bindings"))
        return;
    if (order.get("verified", Variant()) != Variant(true) ||
        order.get("source_dfs_sha256", "") != effect->get("source_hash")) {
        player->set("_native_bounds_result", unverified("Bounds source pairing is stale."));
        return;
    }
    if (!order.has("bounds_authored_state") ||
        Dictionary(order["bounds_authored_state"]) != NBNativeComponentBounds::authored_state(effect)) {
        player->set("_native_bounds_result",
                    unverified("Bounds-affecting authored fields differ from the verified native policy."));
        return;
    }
    Dictionary scoped = order.duplicate(true);
    Array running, emitters = player->get("_emitters");
    for (int n = 0; n < emitters.size(); ++n) {
        Ref<Resource> source = Dictionary(Dictionary(emitters[n])["plan"])["source"];
        running.append(source->get("stable_id"));
    }
    Array bindings = scoped["emitter_bindings"],
          policy_emitters = Dictionary(scoped["bounds_policy"])["emitters"];
    for (int n = 0; n < bindings.size(); ++n) {
        Dictionary binding = bindings[n];
        if (running.has(binding["emitter_stable_id"]))
            continue;
        String guid = UtilityFunctions::str(binding["emitter_handle_guid"]).replace("-", "").to_upper();
        for (int slot = 0; slot < policy_emitters.size(); ++slot) {
            Dictionary item = policy_emitters[slot];
            if (UtilityFunctions::str(item["emitter_handle_guid"]).replace("-", "").to_upper() == guid &&
                bool(item["enabled"])) {
                omitted.append(item["emitter_name"]);
                item["enabled"] = false;
            }
        }
    }
    Ref<NBNativeComponentBounds> helper;
    helper.instantiate();
    Dictionary configured = helper->configure(scoped);
    if (!bool(configured.get("verified", false))) {
        player->set("_native_bounds_result", configured);
        return;
    }
    player->set("_native_bounds", helper);
    player->set("_native_bounds_result",
                helper->step(Array(), player->get_global_transform(), 0., false, false));
    player->call("_mark_partial_bounds");
}
void NBNativeEffectController::check_bounds(Node *player) {
    Ref<RefCounted> helper = player->get("_native_bounds");
    if (helper.is_null() ||
        int64_t(player->call("_bounds_input_signature")) == int64_t(player->get("_native_bounds_signature")))
        return;
    player->set("_native_bounds", Variant());
    player->set("_native_bounds_result",
                unverified("Bounds-affecting document settings changed during playback; source policy and "
                           "complete history must be revalidated on restart."));
}
void NBNativeEffectController::advance_bounds(Node *player, double delta) {
    Ref<RefCounted> helper = player->get("_native_bounds");
    if (helper.is_null())
        return;
    Ref<NBNativeComponentBounds> native = helper;
    Ref<NBNativeVMSystem> system = player->get("_native_system");
    if (native.is_valid() && system.is_valid()) {
        Variant bounds = system->interface_value("@fixed_bounds");
        if (bounds.get_type() == Variant::DICTIONARY) native->set_runtime_fixed_bounds(bounds);
    }
    Array emitters = player->get("_emitters");
    Dictionary collected = native.is_valid() && native->get_script().get_validated_object() == nullptr
                              ? native->collect_for_step(emitters)
                              : NBNativeComponentBounds::collect_snapshots(emitters);
    Dictionary result = helper->call("step", collected["snapshots"], player->get("_step_source_end"), delta,
                                     collected["any_post_tick"], collected["any_post_tick"]);
    player->set("_native_bounds_result", result);
    if (!bool(result.get("verified", false)))
        player->set("_native_bounds", Variant());
    player->call("_mark_partial_bounds");
}
void NBNativeEffectController::mark_partial_bounds(Node *player) {
    Array omitted = player->get("_native_bounds_omitted");
    Dictionary result = player->get("_native_bounds_result");
    if (omitted.is_empty() || !bool(result.get("verified", false)))
        return;
    result["verified"] = false;
    result["partial_simulation_bounds"] = true;
    result["omitted_emitters"] = omitted.duplicate();
    Array diagnostics = result["diagnostics"];
    diagnostics.append(
        "Bounds use only executable emitters; blocked simulations may change native component bounds.");
}
void NBNativeEffectController::sync_primitive_bounds(Node *player) {
    Array emitters = player->get("_emitters");
    for (int n = 0; n < emitters.size(); ++n) {
        Array renderers = Dictionary(emitters[n])["renderers"];
        for (int slot = 0; slot < renderers.size(); ++slot)
            player->call("_sync_renderer_primitive_bounds", renderers[slot]);
    }
    Ref<RefCounted> composite = player->get("_composite");
    if (composite.is_valid())
        player->call("_sync_renderer_primitive_bounds", composite->get("bounds_context"));
}
} // namespace godot
