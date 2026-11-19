#include "effect_controller.h"
#include "effect_revision.h"
#include "curve_evaluator.h"
#include "gpu_backend.h"
#include "spawn_scheduler.h"
#include "particle_view.h"
#include "vm_program.h"
#include <godot_cpp/variant/utility_functions.hpp>
namespace godot {
Dictionary NBNativeEffectRevision::parameters(Node *player, const Ref<Script> &compiler) {
    Ref<Resource> effect = player->get("effect");
    Dictionary overrides = player->get("user_parameters");
    Ref<NBNativeEffectRevision> revision = player->get("_saved_revision");
    if (revision.is_null())
        return compiler->call("effective_parameters", effect, overrides);
    if (!revision->parameter_defaults) {
        revision->parameter_defaults = Dictionary(compiler->call("effective_parameters", effect, Dictionary()));
        Array declarations = effect->get("user_parameters");
        for (int64_t i = 0; i < declarations.size(); ++i) {
            Ref<Resource> declaration = declarations[i];
            String name = declaration->get("input_name");
            if (!name.begins_with("User."))
                name = "User." + name;
            Array types = revision->parameter_types.get(name, Array());
            types.append(declaration->get("target_type"));
            revision->parameter_types[name] = types;
        }
    }
    Dictionary values = revision->parameter_defaults->duplicate();
    Array keys = overrides.keys();
    for (int64_t i = 0; i < keys.size(); ++i) {
        const Variant &key = keys[i];
        Variant value = overrides[key];
        Array types = revision->parameter_types.get(key, Array());
        for (int64_t j = 0; j < types.size(); ++j)
            value = NBNativeCurves::prepare(value, types[j]);
        values[key] = value;
    }
    return values;
}
void NBNativeEffectController::collect_curve_parameters(Node *player, const Variant &value,
                                                        Dictionary result) {
    if (value.get_type() != Variant::DICTIONARY)
        return;
    Dictionary expression = value;
    String kind = expression.get("kind", "");
    if (kind == "reference") {
        String name = UtilityFunctions::str(expression.get("name", ""));
        Variant resolved = Dictionary(player->get("_instance_parameters")).get(name, Variant());
        if (resolved.get_type() != Variant::DICTIONARY)
            return;
        String type = Dictionary(resolved).get("kind", "");
        if (type == "curve_di" || type == "color_curve_di" || type == "vector2_curve_di" ||
            type == "vector3_curve_di")
            result[name] = resolved.hash();
    } else if (kind == "call") {
        Array arguments = expression.get("arguments", Array());
        for (int n = 0; n < arguments.size(); ++n)
            collect_curve_parameters(player, Dictionary(arguments[n]).get("value", Variant()), result);
    }
}
Dictionary NBNativeEffectController::curve_parameters(Node *player, const Dictionary &plan) {
    Dictionary result, parameters = player->get("_instance_parameters");
    if (bool(plan.get("native_vm_requested", false))) {
        constexpr const char *keys = "_saved_curve_parameter_names";
        if (plan.has(keys)) {
            Array names = plan[keys];
            for (int64_t i = 0; i < names.size(); ++i)
                result[names[i]] = parameters.get(names[i], Variant()).hash();
            return result;
        }
        Ref<Resource> source = plan["source"];
        Dictionary contracts = source->get("source_rng_contracts");
        for (const char *name : {"ParticleSpawn", "ParticleUpdate"}) {
            Dictionary contract = contracts.get(name, Dictionary());
            Array functions =
                Dictionary(contract.get("vm_program", Dictionary())).get("external_functions", Array());
            Array interfaces = contract.get("cached_default_data_interfaces", Array());
            for (int n = 0; n < functions.size(); ++n) {
                Dictionary function = functions[n];
                Variant owner = function.get("owner_name", Variant());
                for (int slot = 0; slot < interfaces.size(); ++slot) {
                    Dictionary interface = interfaces[slot];
                    Variant equal;
                    bool valid = false;
                    Variant::evaluate(Variant::OP_EQUAL, owner, interface.get("name", Variant()), equal,
                                      valid);
                    bool match = valid && bool(equal);
                    Variant::evaluate(Variant::OP_EQUAL, owner, interface.get("compile_name", Variant()),
                                      equal, valid);
                    if (!match && !(valid && bool(equal)))
                        continue;
                    TypedArray<String> names =
                        NBNativeVMExecutor::curve_parameter_names(interface, UtilityFunctions::str(owner));
                    for (int index = 0; index < names.size(); ++index) {
                        String parameter = names[index];
                        result[parameter] = parameters.get(parameter, Variant()).hash();
                    }
                }
            }
        }
        if (player->get("_saved_revision").get_validated_object()) {
            Dictionary stored_plan = plan;
            stored_plan[keys] = result.keys();
        }
        return result;
    }
    Array stages = Dictionary(plan["stages"]).values();
    for (int n = 0; n < stages.size(); ++n) {
        Array operations = stages[n];
        for (int slot = 0; slot < operations.size(); ++slot) {
            Ref<Resource> operation = operations[slot];
            if (!bool(operation->get("enabled")))
                continue;
            Array inputs = operation->get("inputs");
            for (int index = 0; index < inputs.size(); ++index) {
                Ref<Resource> input = inputs[index];
                collect_curve_parameters(player, input->get("value"), result);
            }
        }
    }
    return result;
}
void NBNativeEffectController::refresh_curves(Node *player, const Ref<Script> &gpu_program) {
    Array emitters = player->get("_emitters"), diagnostics = player->get("diagnostics");
    Dictionary parameters = player->get("_instance_parameters");
    for (int n = 0; n < emitters.size(); ++n) {
        Dictionary emitter = emitters[n], plan = emitter["plan"];
        Dictionary values = player->call("_curve_parameter_values", plan);
        if (values == Dictionary(emitter.get("curve_parameters", Dictionary())))
            continue;
        emitter["curve_parameters"] = values;
        TypedArray<String> errors;
        Array names = values.keys();
        for (int slot = 0; slot < names.size(); ++slot) {
            String name = names[slot];
            Variant value = parameters.get(name, Variant());
            if (value.get_type() != Variant::DICTIONARY)
                continue;
            Array reasons = Dictionary(value).get("errors", Array());
            for (int index = 0; index < reasons.size(); ++index)
                errors.append(name + ": " + String(reasons[index]));
        }
        if (emitter.has("native_vm")) {
            Array adapters = Dictionary(emitter["native_vm"]).values();
            for (int slot = 0; slot < adapters.size(); ++slot) {
                Ref<RefCounted> adapter = adapters[slot], executor = adapter->get("executor");
                Array reasons = executor->call("update_user_curves", parameters);
                for (int index = 0; index < reasons.size(); ++index)
                    if (!errors.has(reasons[index]))
                        errors.append(reasons[index]);
            }
        }
        if (emitter.has("gpu") && errors.is_empty()) {
            Dictionary program = gpu_program->call("build", plan, parameters);
            errors.append_array(program["errors"]);
            if (program["cache_slots"] != Dictionary(plan["gpu_program"])["cache_slots"])
                errors.append("Live curve edit changed GPU random-cache layout; restart is required.");
            if (errors.is_empty()) {
                Ref<NBNativeGPUBackend> replacement;
                replacement.instantiate();
                if (replacement->initialize(program)) {
                    Ref<RefCounted> previous = emitter["gpu"];
                    previous->call("dispose");
                    emitter["gpu"] = replacement;
                    plan["gpu_program"] = program;
                } else {
                    errors.append(replacement->get_error_message());
                    replacement->dispose();
                }
            }
        }
        emitter["curve_parameters_invalid"] = !errors.is_empty();
        Ref<Resource> source = plan["source"];
        for (int slot = 0; slot < errors.size(); ++slot) {
            Dictionary row;
            row["severity"] = "error";
            row["emitter"] = source->get("display_name");
            row["message"] = "Live curve update blocked: " + String(errors[slot]);
            diagnostics.append(row);
        }
    }
}
namespace {
void capacity_warning(Node *player, Dictionary emitter, const String &message) {
    if (bool(player->get("_capacity_reported")))
        return;
    Ref<Resource> source = Dictionary(emitter["plan"])["source"];
    Dictionary row;
    row["severity"] = "warning";
    row["message"] = message;
    row["emitter"] = source->get("display_name");
    Array diagnostics = player->get("diagnostics");
    diagnostics.append(row);
    player->set("_capacity_reported", true);
}
} // namespace
void NBNativeEffectController::schedule_spawn(Node *player, Dictionary emitter, double delta,
                                              bool include_initial) {
    Dictionary plan = emitter["plan"];
    if (plan.has("lifecycle")) {
        if (!include_initial)
            player->call("_schedule_source_spawn", emitter, delta);
        return;
    }
    Dictionary scheduled = NBNativeSpawnScheduler::legacy(
        emitter, player->get("simulation_time"), delta, NBNativeParticleView::emitter_capacity(plan, player->get("max_particles_per_emitter")),
        include_initial, Callable(player, "_emitter_context"),
        Callable(player, "_execute_emitter_assignment"));
    Array diagnostics = player->get("diagnostics");
    diagnostics.append_array(scheduled["diagnostics"]);
    if (bool(scheduled["capacity_reached"]))
        capacity_warning(player, emitter, "Particle capacity reached; additional births are dropped.");
    NBNativeSpawnScheduler::append_legacy(emitter, scheduled["births"], delta, player->call("_context_frame"),
                                          player->call("_particle_schema"), player->get("_step_source_start"),
                                          player->get("_step_source_end"));
}
void NBNativeEffectController::schedule_source(Node *player, Dictionary emitter, double delta) {
    if (emitter.has("native_vm")) {
        Array batches = emitter["vm_spawn_batches"];
        batches.clear();
    }
    if (double(player->get("_lifecycle_delta")) < 0.)
        return;
    Ref<RefCounted> system = player->get("_native_system");
    if (system.is_valid()) {
        Ref<Resource> source = Dictionary(emitter["plan"])["source"];
        Dictionary events = system->call(
            "spawn_events", Dictionary(source->get("source_lineage"))["compiled_spawn_attributes"]);
        if (!Array(events["errors"]).is_empty()) {
            player->call("_native_vm_failure", emitter, events["errors"]);
            return;
        }
        player->call("_spawn_events", emitter, events["events"], delta);
        return;
    }
    Dictionary scheduled = NBNativeSpawnScheduler::source(
        emitter, player->get("_system_state"), player->get("simulation_time"), delta,
        player->get("_lifecycle_delta"), Callable(player, "_emitter_context"),
        Callable(player, "_execute_emitter_assignment"));
    String lifecycle_error = scheduled["lifecycle_error"];
    if (!lifecycle_error.is_empty()) {
        player->set("_lifecycle_failed", true);
        Ref<Resource> source = Dictionary(emitter["plan"])["source"];
        Dictionary row;
        row["severity"] = "error";
        row["emitter"] = source->get("display_name");
        row["message"] = lifecycle_error;
        Array diagnostics = player->get("diagnostics");
        diagnostics.append(row);
        return;
    }
    if (!Array(scheduled["errors"]).is_empty()) {
        player->call("_native_vm_failure", emitter, scheduled["errors"]);
        return;
    }
    player->call("_spawn_events", emitter, scheduled["events"], delta);
}
void NBNativeEffectController::spawn_events(Node *player, Dictionary emitter, const Array &events,
                                            double delta) {
    Dictionary result = NBNativeSpawnScheduler::append_events(
        emitter, events, delta, NBNativeParticleView::emitter_capacity(emitter["plan"], player->get("max_particles_per_emitter")), player->call("_context_frame"),
        player->call("_particle_schema"), player->get("_step_source_start"), player->get("_step_source_end"));
    if (bool(result["capacity_reached"]))
        capacity_warning(player, emitter,
                         "Particle capacity limits source SpawnInfo count; source population is truncated.");
    if (!Array(result["errors"]).is_empty())
        player->call("_native_vm_failure", emitter, result["errors"]);
}
} // namespace godot
