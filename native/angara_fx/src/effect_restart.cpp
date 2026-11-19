#include "effect_controller.h"
#include "effect_revision.h"
#include "runtime_control.h"
#include "gpu_backend.h"
#include "renderer_geometry.h"
#include "vm_datasets.h"
#include "vm_events.h"
#include "collision_world.h"
#include "ribbon_geometry.h"
#include "particle_view.h"
#include <godot_cpp/classes/random_number_generator.hpp>
#include <godot_cpp/core/object.hpp>
#include <algorithm>

namespace godot {
namespace {
Dictionary system_instance_seed_inputs(const Dictionary &stages) {
    Dictionary inputs;
    for (const char *name : {"SystemSpawn", "SystemUpdate"}) {
        Dictionary stage = stages.get(name, Dictionary());
        Array layout = stage.get("instance_input_layout", Array());
        for (int i = 0; i < layout.size(); ++i) {
            Dictionary row = layout[i];
            String field = row.get("name", "");
            if (field.ends_with(".InstanceSeed") && bool(row.get("constant_buffer_bound", false)))
                inputs[field] = true;
        }
    }
    return inputs;
}

TypedArray<String> bind_grid_volumes(const Array &emitters, const Dictionary &parameters) {
    TypedArray<String> errors;
    Dictionary targets;
    for (int i = 0; i < emitters.size(); ++i) {
        Dictionary state = emitters[i];
        Ref<NBNativeGPUBackend> gpu = state.get("gpu", Variant());
        if (gpu.is_null())
            continue;
        Dictionary volumes = gpu->get_volume_textures();
        Array keys = volumes.keys();
        for (int j = 0; j < keys.size(); ++j) {
            String key = keys[j];
            if (!key.begins_with("User."))
                continue;
            Variant object = parameters.get(key, Variant());
            if (object.get_type() == Variant::NIL ||
                (object.get_type() == Variant::STRING &&
                 (String(object).is_empty() || String(object) == "None")))
                continue;
            if (targets.has(key)) {
                errors.append("Multiple Grid3D fields publish to " + key);
                targets[key] = Variant();
            } else
                targets[key] = volumes[key];
        }
    }
    if (targets.is_empty())
        return errors;
    for (int i = 0; i < emitters.size(); ++i) {
        Array renderers = Dictionary(emitters[i])["renderers"];
        for (int j = 0; j < renderers.size(); ++j) {
            Dictionary renderer = renderers[j];
            NBNativeRendererGeometry::bind_renderer_volumes(renderer, targets);
        }
    }
    return errors;
}
} // namespace
void NBNativeEffectController::release(Node *player) {
    if (!player)
        return;
    player->call("_release_particle_lights");
    if (Node *debug = Object::cast_to<Node>(player->get("_native_debug_boxes"))) {
        debug->queue_free();
        player->set("_native_debug_boxes", Variant());
    }
    Ref<RefCounted> weak_pass = player->get("_refraction_pass");
    if (weak_pass.is_valid()) {
        Ref<RefCounted> pass = weak_pass->call("get_ref");
        if (pass.is_valid())
            pass->call("remove_player", player);
    }
    player->call("_dispose_composite");
    Array emitters = player->get("_emitters");
    for (int n = 0; n < emitters.size(); ++n) {
        Ref<RefCounted> gpu = Dictionary(emitters[n]).get("gpu", Variant());
        if (gpu.is_valid())
            gpu->call("dispose");
    }
}
TypedArray<String> NBNativeEffectController::bind_world(Node3D *player,
                                                        const Ref<NBNativeVMExecutor> &executor,
                                                        const String &scope) {
    TypedArray<String> errors;
    if (!player || executor.is_null()) {
        errors.append("Native world binding requires a player and VM executor.");
        return errors;
    }
    executor->bind_player_readers(player->get("_particle_readers"));
    executor->set_reader_scope(scope);
    Array initialization = player->get("_collision_initialization_errors");
    if (bool(executor->get_statistics().get("requires_collision", false))) {
        Ref<NBNativeCollisionWorld> world = player->get("_collision_world");
        if (world.is_null()) {
            world.instantiate();
            player->set("_collision_world", world);
            initialization.assign(
                world->configure(player->get_world_3d(), player->get("collision_settings")));
        }
        executor->set_collision_context(world);
        player->set("_uses_collision", true);
    }
    errors.assign(initialization);
    return errors;
}
void NBNativeEffectController::collision_failure(Node *player, const Array &errors) {
    if (!player)
        return;
    player->set("_lifecycle_failed", true);
    Array diagnostics = player->get("diagnostics");
    for (int n = 0; n < errors.size(); ++n) {
        Dictionary row;
        row["severity"] = "error";
        row["message"] = "Native collision: " + String(errors[n]);
        diagnostics.append(row);
    }
}
void NBNativeEffectController::ribbon_history(Dictionary emitter) {
    Array renderers = emitter["renderers"];
    for (int n = 0; n < renderers.size(); ++n) {
        Dictionary renderer = renderers[n];
        Ref<Resource> source = renderer["source"];
        if (String(source->get("renderer_type")) == "RibbonRenderer") {
            auto view=NBNativeParticleView::for_emitter(emitter);
            bool unique=source->call("get_input","bLinkOrderUseUniqueID",true);double tension=source->call("get_input","CurveTension",0.);
            Dictionary topology;
            if(view.is_valid() && view->get_errors().is_empty()) topology=NBNativeRibbonGeometry::resident_history(view,unique,tension);
            else {
                NBNativeParticleView::materialize(emitter);
                topology=NBNativeRibbonGeometry::topology(emitter["particles"],unique,tension);
            }
            NBNativeRibbonGeometry::update_history(topology, renderer["history"]);
        }
    }
}
void NBNativeEffectController::restart(Node3D *player, const Ref<Script> &compiler,
                                       const Ref<Script> &lifecycle_module, const Dictionary &prepared) {
    if (!player || compiler.is_null() || lifecycle_module.is_null())
        return;
    player->call("_release_particle_lights");
    player->set("_native_system", Variant());
    player->set("_collision_world", Variant());
    Array collision_errors = player->get("_collision_initialization_errors");
    collision_errors.clear();
    player->set("_uses_collision", false);
    Ref<NBNativeVMEvents> events;
    events.instantiate();
    player->set("_native_events", events);
    Ref<NBNativeVMReaders> readers;
    readers.instantiate();
    player->set("_particle_readers", readers);
    player->set("_temporal_view_reported", false);
    Ref<RefCounted> weak_pass = player->get("_refraction_pass");
    if (weak_pass.is_valid()) {
        Ref<RefCounted> pass = weak_pass->call("get_ref");
        if (pass.is_valid())
            pass->call("remove_player", player);
    }
    player->set("_refraction_pass", Variant());
    player->call("_dispose_composite");
    player->set("_scoped_signature", 0);
    player->set("_scoped_status", Dictionary());
    player->set("_native_bounds", Variant());
    player->set("_native_bounds_result", Dictionary());
    Array emitters = player->get("_emitters");
    for (int n = 0; n < emitters.size(); ++n) {
        Dictionary emitter = emitters[n];
        Ref<RefCounted> gpu = emitter.get("gpu", Variant());
        if (gpu.is_valid())
            gpu->call("dispose");
        Array renderers = emitter.get("renderers", Array());
        for (int slot = 0; slot < renderers.size(); ++slot) {
            Dictionary renderer = renderers[slot];
            auto *instance = Object::cast_to<Node>(renderer["instance"].get_validated_object());
            if (instance) {
                player->remove_child(instance);
                instance->queue_free();
            }
        }
    }
    emitters.clear();
    player->set("simulation_time", 0.);
    player->set("_accumulator", 0.);
    player->set("_tick", 0);
    player->set("_capacity_reported", false);
    player->set("_run_step", std::max(double(player->get("fixed_step")), .001));
    player->set("_source_clock", Variant());
    player->set("_source_variable_tick", false);
    player->set("_simulation_started", false);
    Transform3D transform =
        player->is_inside_tree() ? player->get_global_transform() : player->get_transform();
    player->set("_source_transform", transform);
    player->set("_step_source_start", transform);
    player->set("_step_source_end", transform);
    Ref<Resource> effect = player->get("effect");
    Dictionary parameters = NBNativeEffectRevision::parameters(player, compiler);
    player->set("_instance_parameters", parameters);
    Dictionary options;
    options["allow_gpu_cpu_fallback"] = player->get("allow_gpu_cpu_fallback");
    options["enabled_emitters_only"] = player->get("_saved_revision").get_validated_object() != nullptr;
    options["use_compiled_source_rng"] = player->get("use_compiled_source_rng");
    Dictionary compiled = prepared;
    if (compiled.is_empty())
        compiled = compiler->call("compile_effect", effect, player->get("user_parameters"), options);
    player->set("_event_execution_order", compiled.get("event_execution_order", Array()));
    player->set("_system_state", NBNativeLifecycle::initial());
    player->set("_system_updated", false);
    int64_t system_seed = effect.is_valid() ? int64_t(effect->call("get_input", "RandomSeed", 0)) : 0;
    Dictionary native_system = compiled.get("native_system", Dictionary());
    Dictionary source_execution =
        effect.is_valid() ? Dictionary(effect->get("source_execution")) : Dictionary();
    if (!native_system.is_empty() && !bool(source_execution.get("source_system_deterministic", false))) {
        int64_t limit = source_execution.get("source_platform_rand_max", 0);
        if (limit > 0) {
            Ref<RandomNumberGenerator> entropy;
            entropy.instantiate();
            entropy->randomize();
            system_seed = entropy->randi_range(0, limit);
        }
    }
    player->set("_native_system_seed", system_seed);
    player->set("_system_update_on_spawn", 1);
    player->set("_owner_execution_state", 0);
    player->set("_lifecycle_failed", false);
    Dictionary lifecycle = compiled.get("system_lifecycle", Dictionary());
    if (lifecycle.is_empty()) {
        Ref<Resource> operation = lifecycle_module->call("new");
        lifecycle = NBNativeLifecycle::configuration(operation, true);
    }
    player->set("_system_lifecycle", lifecycle);
    Array diagnostics = player->get("diagnostics");
    diagnostics.assign(compiled["diagnostics"]);
    auto diagnostic = [&](const String &message, const Ref<Resource> &source = Ref<Resource>()) {
        Dictionary row;
        row["severity"] = "error";
        row["message"] = message;
        if (source.is_valid())
            row["emitter"] = source->get("display_name");
        diagnostics.append(row);
    };
    Dictionary clock_settings = compiled.get("source_clock", Dictionary());
    bool clock_ok = Array(clock_settings.get("errors", Array())).is_empty();
    bool fixed_source = bool(clock_settings.get("enabled", false)) && clock_ok;
    player->set("_source_variable_tick", effect.is_valid() && !source_execution.is_empty() &&
                                             !bool(clock_settings.get("enabled", false)) && clock_ok);
    if (fixed_source) {
        Ref<NBNativeSourceClock> clock;
        clock.instantiate();
        clock->configure(clock_settings);
        player->set("_source_clock", clock);
        player->set("_run_step", double(clock_settings["delta"]));
    }
    int64_t emitter_index = 0;
    Dictionary seed_inputs = system_instance_seed_inputs(native_system);
    Array plans = compiled["emitters"];
    for (int n = 0; n < plans.size(); ++n) {
        Dictionary plan = plans[n];
        if (!bool(plan["runnable"]))
            continue;
        Ref<Resource> source = plan["source"];
        Ref<RandomNumberGenerator> rng;
        rng.instantiate();
        rng->set_seed(int64_t(player->get("seed")) + String(source->get("stable_id")).hash());
        Dictionary state;
        state["plan"] = plan;
        state["particles"] = Array();
        state["rng"] = rng;
        state["emitted"] = 0;
        state["rate_carry"] = Dictionary();
        Array renderers;
        state["renderers"] = renderers;
        state["light_configurations"] = plan.get("light_configurations", Dictionary());
        state["light_records"] = Array();
        state["lifecycle"] = NBNativeLifecycle::initial();
        Dictionary source_rng = plan.get("source_rng", Dictionary());
        Dictionary lineage = source->get("source_lineage");
        String unique_name = lineage.get("native_unique_name", source->get("display_name"));
        bool needs_instance_seed = bool(source_rng.get("uses_instance_seed", false)) ||
                                   seed_inputs.has("Engine." + unique_name + ".InstanceSeed");
        if (needs_instance_seed) {
            int64_t limit = source_rng.get("instance_seed_max", source_execution.get("source_platform_rand_max", 0));
            if (limit <= 0 || limit > INT32_MAX) {
                diagnostic("Emitter InstanceSeed requires the exported platform RAND_MAX.", source);
                player->set("_lifecycle_failed", true);
                continue;
            }
            Ref<RandomNumberGenerator> instance_rng;
            instance_rng.instantiate();
            instance_rng->randomize();
            state["source_instance_seed"] = instance_rng->randi_range(0, limit);
        }
        if (plan.has("source_rng"))
            player->set("_system_update_on_spawn", source_rng["system_update_on_spawn"]);
        state["variables"] = Dictionary();
        state["variable_cache"] = Dictionary();
        Array assignments = plan.get("emitter_assignments", Array());
        if (!bool(plan.get("native_system", false)))
            for (int slot = 0; slot < assignments.size(); ++slot)
                player->call("_execute_emitter_assignment", assignments[slot], state, 0., 0., "EmitterSpawn");
        state["curve_parameters"] = player->call("_curve_parameter_values", plan);
        if (bool(plan.get("native_vm_requested", false))) {
            Dictionary adapters;
            state["native_vm"] = adapters;
            state["vm_spawn_batches"] = Array();
            Ref<NBNativeVMIDs> ids;
            ids.instantiate();
            state["vm_ids"] = ids;
            TypedArray<String> invalid;
            Dictionary stages = source_rng["vm_stages"];
            Array names = stages.keys();
            for (int slot = 0; slot < names.size(); ++slot) {
                String name = names[slot];
                Dictionary stage = stages[name];
                Ref<NBNativeVMParticles> adapter;
                adapter.instantiate();
                invalid.append_array(
                    adapter->configure(stage["program"], stage["interfaces"], parameters, stage));
                invalid.append_array(
                    player->call("_bind_native_world", adapter->get_executor(), source->get("display_name")));
                adapters[name] = adapter;
            }
            if (!invalid.is_empty()) {
                for (int slot = 0; slot < invalid.size(); ++slot)
                    diagnostic(invalid[slot], source);
                continue;
            }
            state["vm_previous_parameters"] = player->call("_native_vm_frame", state, 0., 0.);
        }
        if (String(plan["backend"]) == "gpu_compute") {
            Ref<NBNativeGPUBackend> gpu;
            gpu.instantiate();
            if (!gpu->initialize(plan["gpu_program"])) {
                diagnostic(gpu->get_error_message(), source);
                continue;
            }
            state["gpu"] = gpu;
        }
        Array sources = plan["renderers"];
        for (int slot = 0; slot < sources.size(); ++slot) {
            Ref<Resource> renderer = sources[slot];
            String kind = renderer->get("renderer_type");
            if (kind == "LightRenderer")
                continue;
            Array entries = Array::make(Dictionary());
            if (kind == "MeshRenderer")
                entries = Dictionary(Dictionary(plan["mesh_bindings"])[renderer])["entries"];
            for (int entry = 0; entry < entries.size(); ++entry) {
                Dictionary created =
                    player->call("_create_renderer", renderer, emitter_index, entries[entry],
                                 compiler->call("renderer_dynamic_parameter_mask", plan, renderer));
                created["sprite_modes"] =
                    Dictionary(plan.get("sprite_modes", Dictionary())).get(renderer, Dictionary());
                renderers.append(created);
            }
        }
        emitters.append(state);
        if (!fixed_source) {
            player->call("_schedule_spawn", state, 0., true);
            if (state.has("gpu"))
                player->call("_run_gpu", state, 0., 0.);
        }
        ++emitter_index;
    }
    TypedArray<String> volume_errors = bind_grid_volumes(emitters, parameters);
    for (int i = 0; i < volume_errors.size(); ++i) diagnostic(volume_errors[i]);
    player->set("_simulation_started", !fixed_source);
    if (!native_system.is_empty()) {
        Ref<NBNativeVMSystem> system;
        system.instantiate();
        player->set("_native_system", system);
        TypedArray<String> errors = system->configure(native_system, parameters);
        if (errors.is_empty()) {
            Array stages = system->get_stages().values();
            for (int slot = 0; slot < stages.size(); ++slot)
                errors.append_array(
                    player->call("_bind_native_world", Dictionary(stages[slot])["executor"], "System"));
        }
        if (errors.is_empty())
            player->call("_run_native_system", "SystemSpawn", 0.);
        else {
            player->set("_lifecycle_failed", true);
            for (int n = 0; n < errors.size(); ++n)
                diagnostic(errors[n]);
        }
    }
    player->call("_prepare_native_bounds");
    player->call("_sync_renderers");
}
} // namespace godot
