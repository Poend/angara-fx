#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <chrono>
#include "effect_controller.h"
#include "system_debug_boxes.h"
#include "gpu_backend.h"
#include "gpu_particle_reader.h"
#include "gpu_buffers.h"
#include "world_clock.h"
#include "value_evaluator.h"
#include "vm_datasets.h"
#include "particle_step.h"
#include "collision_world.h"
#include <godot_cpp/classes/random_number_generator.hpp>
#include <algorithm>
#include <vector>
namespace godot {
namespace {
double lod_distance_cm(Node *player) {
    auto *owner = Object::cast_to<Node3D>(player);
    auto *viewport = owner && owner->is_inside_tree() ? owner->get_viewport() : nullptr;
    auto *camera = viewport ? viewport->get_camera_3d() : nullptr;
    return camera ? owner->get_global_position().distance_to(camera->get_global_position()) * 100. : 0.;
}
String emitter_name(const Dictionary &emitter) {
    Dictionary plan = emitter["plan"];
    Ref<Resource> source = plan["source"];
    return source->get("display_name");
}
String native_name(const Ref<Resource> &source) {
    Dictionary lineage = source->get("source_lineage");
    return lineage.get("native_unique_name", source->get("display_name"));
}
const char *LIFECYCLE_FIELDS[] = {"Age",
                                  "LoopedAge",
                                  "NormalizedLoopAge",
                                  "LoopCount",
                                  "CurrentLoopDelay",
                                  "CurrentLoopDuration",
                                  "ExecutionState",
                                  "ExecutionStateSource"};
} // namespace
Dictionary NBNativeEffectController::frame(Node *player, Dictionary emitter, double delta, double time) {
    Dictionary values = player->call("_emitter_context", emitter, delta, time), plan = emitter["plan"];
    values.merge(NBNativeWorldClock::values(player), false);
    values["Engine.QualityLevel"] = player->get("source_quality_level");
    values["Engine.WorldDeltaTime"] = delta;
    values["Emitter.Age"] = double(emitter.get("source_particle_age", 0.));
    values["Engine.Owner.LODDistance"] = lod_distance_cm(player);
    Ref<Resource> source = plan["source"];
    values["Emitter.RandomSeed"] =
        int64_t(NBNativeValues::input_value(source->get("settings"), "RandomSeed", 0));
    // A paired transient capture can use a different seed from the saved emitter.
    // VectorVM reads this parameter directly; GPU kernels use the same verified seed.
    Dictionary rng = plan.get("source_rng", Dictionary());
    if (rng.get("seed_mode", "") == Variant("transient_deterministic") && rng.has("seed"))
        values["Emitter.RandomSeed"] = rng["seed"];
    Array keys = values.keys();
    String prefix = String(source->get("display_name")) + ".";
    for (int n = 0; n < keys.size(); ++n) {
        String key = keys[n];
        if (key.begins_with("Emitter."))
            values[prefix + key.trim_prefix("Emitter.")] = values[key];
    }
    return NBNativeVMParameters::frame(values, player->get("_step_source_end"),
                                       player->get("_step_source_start"), delta, emitter["emitted"]);
}
bool NBNativeEffectController::run_system(Node *player, const String &stage, double delta) {
    Ref<NBNativeVMSystem> system = player->get("_native_system");
    Ref<Resource> effect = player->get("effect");
    if (system.is_null() || effect.is_null())
        return false;
    if (bool(player->get("_uses_collision"))) {
        Dictionary stages = system->get_stages(), current_stage = stages.get(stage, Dictionary());
        Ref<NBNativeVMExecutor> executor = current_stage.get("executor", Variant());
        if (executor.is_valid() && bool(executor->get_statistics().get("requires_collision", false))) {
            Ref<NBNativeCollisionWorld> world = player->get("_collision_world");
            if (world.is_null()) {
                player->call("_collision_failure", Array::make("Native collision world is unavailable."));
                return false;
            }
            if (!bool(world->get_statistics().get("frame_open", false))) {
                Array errors = world->begin_frame();
                if (!errors.is_empty()) {
                    player->call("_collision_failure", errors);
                    return false;
                }
            }
        }
    }
    Dictionary values = Dictionary(player->get("_instance_parameters")).duplicate();
    values.merge(NBNativeWorldClock::values(player), false);
    values["Engine.QualityLevel"] = player->get("source_quality_level");
    values["Engine.WorldDeltaTime"] = delta;
    values["Engine.System.TickCount"] = player->get("_tick");
    values["Engine.Owner.ExecutionState"] = player->get("_owner_execution_state");
    values["Engine.Owner.LODDistance"] = lod_distance_cm(player);
    values["Engine.Owner.LODDistanceFraction"] = player->get("source_lod_distance_fraction");
    values["Engine.System.RandomSeed"] = player->get("_native_system_seed");
    Array sources = effect->get("emitters"), emitters = player->get("_emitters");
    Dictionary active;
    for (int i = 0; i < emitters.size(); ++i) {
        Dictionary emitter = emitters[i], plan = emitter["plan"];
        Variant source = plan["source"];
        if (active.has(source))
            active[source] = Variant();
        else
            active[source] = emitter;
    }
    for (int n = 0; n < sources.size(); ++n) {
        Ref<Resource> source = sources[n];
        String prefix = "Engine." + native_name(source) + ".";
        int64_t count = 0;
        Variant instance = active.get(source, Variant());
        if (instance.get_type() == Variant::DICTIONARY) {
            Dictionary emitter = instance;
            count = NBNativeParticleView::particle_count(emitter);
            if (emitter.has("source_instance_seed"))
                values[prefix + "InstanceSeed"] = emitter["source_instance_seed"];
        }
        values[prefix + "NumParticles"] = count;
        values[prefix + "SpawnCountScale"] = player->get("source_spawn_count_scale");
    }
    values = NBNativeVMParameters::frame(values, player->get("_step_source_end"),
                                         player->get("_step_source_start"), delta, 0);
    values["Engine.DeltaTime"] = stage == "SystemSpawn" ? Variant(0.) : player->get("_lifecycle_delta");
    Dictionary parameters = NBNativeVMParticles::parameter_values(values);
    Dictionary execution = system->run_for_step(stage, parameters, parameters);
    Array errors = execution["errors"];
    if (!errors.is_empty()) {
        player->set("_lifecycle_failed", true);
        Array diagnostics = player->get("diagnostics");
        for (int n = 0; n < errors.size(); ++n) {
            Dictionary d;
            d["severity"] = "error";
            d["message"] = "Native system playback: " + String(errors[n]);
            diagnostics.append(d);
        }
        return false;
    }
    if (stage == "SystemUpdate") draw_system_boxes(player,system->interface_value("@debug_boxes"));
    Dictionary flat = system->flat_values(), state = player->get("_system_state");
    for (const char *field : LIFECYCLE_FIELDS)
        if (flat.has(String("System.") + field))
            state[field] = flat[String("System.") + field];
    for (int n = 0; n < emitters.size(); ++n) {
        Dictionary emitter = emitters[n], plan = emitter["plan"], lifecycle = emitter["lifecycle"],
                   variables = emitter["variables"];
        Ref<Resource> source = plan["source"];
        String prefix = native_name(source) + ".";
        for (const char *field : LIFECYCLE_FIELDS)
            if (flat.has(prefix + field))
                lifecycle[field] = flat[prefix + field];
        emitter["source_emitter_age"] = lifecycle["Age"];
        emitter["source_looped_age"] = lifecycle["LoopedAge"];
        variables.merge(flat, true);
        // SystemSpawn and SystemUpdate are validated against the same dataset layout.
        if (!emitter.has("system_variable_aliases")) {
            Array aliases, names = flat.keys();
            for (int i = 0; i < names.size(); ++i) {
                String key = names[i];
                if (key.begins_with(prefix)) {
                    aliases.append(key);
                    aliases.append("Emitter." + key.trim_prefix(prefix));
                }
            }
            emitter["system_variable_aliases"] = aliases;
        }
        Array aliases = emitter["system_variable_aliases"];
        for (int i = 0; i < aliases.size(); i += 2)
            variables[aliases[i + 1]] = flat[aliases[i]];
    }
    return true;
}
void NBNativeEffectController::vm_failure(Node *player, Dictionary emitter, const Array &errors) {
    emitter["vm_failed"] = true;
    Array particles = emitter["particles"];
    NBNativeParticleView::clear_particles(emitter);
    String name = emitter_name(emitter);
    Object *readers = player->get("_particle_readers"), *events = player->get("_native_events");
    if (readers) {
        Dictionary current = readers->get("current");
        current.erase(name);
    }
    if (events) {
        Dictionary current = events->get("current");
        current.erase(name);
    }
    Array diagnostics = player->get("diagnostics");
    for (int n = 0; n < errors.size(); ++n) {
        Dictionary d;
        d["severity"] = "error";
        d["emitter"] = name;
        d["message"] = "Native CPU playback: " + String(errors[n]);
        diagnostics.append(d);
    }
}
bool NBNativeEffectController::publish_events(Node *player, Dictionary emitter, const Dictionary &published) {
    Ref<NBNativeVMEvents> events = player->get("_native_events");
    Array errors = events->publish(emitter_name(emitter), published);
    if (!errors.is_empty()) {
        player->call("_native_vm_failure", emitter, errors);
        return false;
    }
    return true;
}
Array NBNativeEffectController::handlers(const Dictionary &plan) {
    Dictionary source = plan.get("source_rng", Dictionary()), stages = source.get("vm_stages", Dictionary());
    Array names = stages.keys();
    std::vector<Dictionary> rows;
    for (int n = 0; n < names.size(); ++n) {
        Dictionary contract = stages[names[n]];
        if (contract.has("event_handler")) {
            Dictionary row;
            row["stage"] = names[n];
            row["settings"] = contract["event_handler"];
            rows.push_back(row);
        }
    }
    std::stable_sort(rows.begin(), rows.end(), [](const Dictionary &a, const Dictionary &b) {
        return int64_t(Dictionary(a["settings"])["index"]) < int64_t(Dictionary(b["settings"])["index"]);
    });
    Array result;
    for (auto &row : rows)
        result.append(row);
    return result;
}
void NBNativeEffectController::run_vm(Node *player, Dictionary emitter, double delta, double time) {
    Array event_handlers = handlers(emitter["plan"]);
    Ref<NBNativeVMEvents> events = player->get("_native_events");
    for (int n = 0; n < event_handlers.size(); ++n) {
        Dictionary handler = event_handlers[n], settings = handler["settings"];
        if (String(settings["execution_mode"]) != "SpawnedParticles")
            continue;
        Dictionary payload = events->read(emitter_name(emitter), settings);
        int64_t count = payload["count"], maximum = settings["max_events_per_frame"];
        if (maximum > 0)
            count = std::min(count, maximum);
        for (int64_t index = 0; index < count; ++index) {
            int64_t number = settings["spawn_number"], minimum = settings["min_spawn_number"];
            if (bool(settings["random_spawn_number"]) && minimum < number) {
                Ref<RandomNumberGenerator> rng = emitter["rng"];
                number = rng->randi_range(minimum, number);
            }
            Dictionary event;
            event["count"] = number;
            event["interval"] = 0.;
            event["start_dt"] = delta * .5;
            event["group"] = 0;
            event["handler"] = handler["stage"];
            event["event_input"] = payload["data"];
            event["event_index"] = index;
            player->call("_spawn_events", emitter, Array::make(event), delta);
        }
    }
    Dictionary current = player->call("_native_vm_frame", emitter, delta, time),
               previous = emitter["vm_previous_parameters"];
    Ref<NBNativeVMReaders> readers = player->get("_particle_readers");
    emitter["defer_particle_projection"]=NBNativeParticleView::stock_player_method(player,"_renderer_particle_view") &&
                                          NBNativeParticleView::stock_player_method(player,"_run_native_vm");
    Dictionary executed = NBNativeVMParticleStep::run(emitter, current, previous, event_handlers, readers,
                                                      events, player->get("_tick"));
    Array errors = executed["errors"];
    if (!errors.is_empty()) {
        player->call("_native_vm_failure", emitter, errors);
        return;
    }
    emitter["particles"] = executed["particles"];
    emitter["vm_buffer"] = executed["buffer"];
    emitter["particle_projection_pending"]=executed.get("particle_projection_pending",false);
    emitter["native_step_timings_usec"] = executed["timings_usec"];
    Array batches = emitter["vm_spawn_batches"];
    batches.clear();
    emitter["vm_previous_parameters"] = executed["previous_parameters"];
}
void NBNativeEffectController::run_gpu(Node *player, Dictionary emitter, double delta, double time) {
    const bool profile=bool(player->get_meta("angara_fx_profile_renderer",false));
    Dictionary phases;
    auto phase_start=profile ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point();
    auto mark=[&](const char *name) {
        if(!profile) return;
        auto now=std::chrono::steady_clock::now();
        phases[name]=std::chrono::duration<double,std::micro>(now-phase_start).count();phase_start=now;
    };
    Ref<Script> host_script=player->get_script();
    const bool stock_host=host_script.is_valid() && host_script->get_path()=="res://addons/angara_fx/runtime/effect_player.gd";
    // Crossing to a custom host restores its ordinary observable particle array
    // before invoking any of its context callbacks.
    Object *initial_gpu=emitter["gpu"];
    if(bool(emitter.get("gpu_births_only",false)) && (!stock_host || !Object::cast_to<NBNativeGPUBackend>(initial_gpu))) {
        auto errors=NBNativeParticleView::materialize(emitter);
        if(!errors.is_empty()) {
            Array diagnostics=player->get("diagnostics");Dictionary error;
            error["severity"]="error";error["emitter"]=emitter_name(emitter);error["message"]=errors[0];diagnostics.append(error);
            initial_gpu->call("dispose");emitter["gpu_failed"]=true;
            emitter.erase("gpu_buffer");emitter.erase("particle_projection_pending");
            NBNativeParticleView::clear_particles(emitter);return;
        }
    }
    Dictionary parameters = player->call("_emitter_context", emitter, delta, time), plan = emitter["plan"];
    if (plan.has("lifecycle")) {
        parameters["Emitter.Age"] = double(emitter.get("source_particle_age", 0.));
        parameters["_NB.PreviousEmitterAge"] = double(emitter.get("source_previous_particle_age", 0.));
    }
    Object *gpu = emitter["gpu"];
    auto *native = Object::cast_to<NBNativeGPUBackend>(gpu);
    bool succeeded = false;
    bool resident_only=native && NBNativeParticleView::stock_player_method(player,"_renderer_particle_view") &&
                                  NBNativeParticleView::stock_player_method(player,"_run_gpu");
    mark("context_and_guards");
    if (native) {
        Array input = emitter["particles"];
        Ref<NBNativeGPUParticleBuffer> buffer = emitter.get("gpu_buffer", Variant());
        TypedArray<String> errors;
        bool births_only=bool(emitter.get("gpu_births_only",false));
        if(births_only && (buffer.is_null() || !buffer->matches_layout(native->get_program()))) {
            errors=NBNativeParticleView::materialize(emitter);
            input=emitter["particles"];births_only=false;
        }
        if(errors.is_empty() && (buffer.is_null() || !buffer->matches_layout(native->get_program()) || (!births_only && buffer->get_count() > input.size()))) {
            buffer.instantiate();
            errors = buffer->import_particles(native->get_program(), input);
        } else if(errors.is_empty()) {
            // Existing numerical state stays in ABI words. Only births are
            // packed; CPU metadata/index/count preparation keeps its old order.
            buffer->prepare_tick_metadata();
            if(births_only) {
                if(!input.is_empty()) errors=buffer->append_particles(input);
            } else if (input.size() > buffer->get_count())
                errors = buffer->append_particles(input.slice(buffer->get_count()));
        }
        mark("buffer_preparation");
        if (errors.is_empty() && !native->get_particle_reader_columns().is_empty() && buffer->get_count() > 0) {
            PackedByteArray packet;
            String error = nbreader::pack(native->get_particle_reader_columns(),player->get("_emitters"),packet);
            if (!error.is_empty()) errors.append(error);
            else parameters["_NB.ReaderColumns"] = packet;
        }
        if (errors.is_empty()) {
            succeeded = native->run_buffer(buffer, delta, time, plan["duration"], plan["local_space"],
                                            player->get("_step_source_end"), parameters, resident_only);
            mark("backend_run");
            // Stock retained rendering needs the compact numerical buffer only.
            // Custom/public projection keeps its original pre-compaction order.
            if (succeeded && !resident_only) {
                errors = buffer->project_into(input);
                if (errors.is_empty())
                    errors = buffer->commit_output(buffer->get_bytes(), true);
                succeeded = errors.is_empty();
            }
        }
        mark("custom_projection");
        if (!errors.is_empty())
            native->set_error_message(errors[0]);
        if (succeeded) {
            emitter["gpu_buffer"] = buffer;
            if(resident_only) {
                if(stock_host) {
                    emitter["particles"]=Array();emitter["gpu_births_only"]=true;
                } else emitter["particles"]=buffer->metadata_rows();
                emitter["particle_projection_pending"]=true;
            }
        }
    } else {
        // A stock context can also replace the backend during a callback.
        auto errors=NBNativeParticleView::materialize(emitter);
        if(errors.is_empty())
            succeeded = bool(gpu->call("run", emitter["particles"], delta, time, plan["duration"], plan["local_space"],
                                       player->get("_step_source_end"), parameters));
        else gpu->set("error_message",errors[0]);
    }
    mark("metadata_publication");
    if (succeeded && native && delta > 0. && plan.has("grid_stages"))
        succeeded = native->step_grids(parameters,player->get("_emitters"));
    if(profile) emitter["gpu_controller_timings_usec"]=phases;
    if (!succeeded) {
        Array diagnostics = player->get("diagnostics");
        Dictionary d;
        d["severity"] = "error";
        d["emitter"] = emitter_name(emitter);
        d["message"] = gpu->get("error_message");
        diagnostics.append(d);
        gpu->call("dispose");
        emitter["gpu_failed"] = true;
        emitter.erase("gpu_buffer");
        emitter.erase("particle_projection_pending");
        Array particles = emitter["particles"];
        NBNativeParticleView::clear_particles(emitter);
        return;
    }
    if(resident_only) return;
    Array particles = emitter["particles"];
    std::vector<Dictionary> rows;
    for (int n = 0; n < particles.size(); ++n) {
        Dictionary p = particles[n];
        if (bool(p["alive"]))
            rows.push_back(p);
    }
    std::stable_sort(rows.begin(), rows.end(), [](const Dictionary &a, const Dictionary &b) {
        return int64_t(a["gpu_output_index"]) < int64_t(b["gpu_output_index"]);
    });
    TypedArray<Dictionary> survivors;
    for (auto &row : rows)
        survivors.append(row);
    emitter["particles"] = survivors;
}
} // namespace godot
