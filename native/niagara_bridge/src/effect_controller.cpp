#include "particle_view.h"
#include "effect_controller.h"
#include "runtime_control.h"
#include "vm_program.h"
#include <godot_cpp/core/class_db.hpp>
#include <algorithm>
#include <vector>
#include <chrono>
namespace godot {
void NBNativeEffectController::_bind_methods() {
    ClassDB::bind_static_method("NBNativeEffectController", D_METHOD("collect_curve_parameters", "player", "value", "result"), &NBNativeEffectController::collect_curve_parameters);
    ClassDB::bind_static_method("NBNativeEffectController", D_METHOD("curve_parameters", "player", "plan"), &NBNativeEffectController::curve_parameters);
    ClassDB::bind_static_method("NBNativeEffectController", D_METHOD("refresh_curves", "player", "gpu_program"), &NBNativeEffectController::refresh_curves);
    ClassDB::bind_static_method("NBNativeEffectController", D_METHOD("schedule_spawn", "player", "emitter", "delta", "include_initial"), &NBNativeEffectController::schedule_spawn);
    ClassDB::bind_static_method("NBNativeEffectController", D_METHOD("schedule_source", "player", "emitter", "delta"), &NBNativeEffectController::schedule_source);
    ClassDB::bind_static_method("NBNativeEffectController", D_METHOD("spawn_events", "player", "emitter", "events", "delta"), &NBNativeEffectController::spawn_events);
    ClassDB::bind_static_method("NBNativeEffectController", D_METHOD("bounds_signature", "player"), &NBNativeEffectController::bounds_signature);
    ClassDB::bind_static_method("NBNativeEffectController", D_METHOD("prepare_bounds", "player"), &NBNativeEffectController::prepare_bounds);
    ClassDB::bind_static_method("NBNativeEffectController", D_METHOD("check_bounds", "player"), &NBNativeEffectController::check_bounds);
    ClassDB::bind_static_method("NBNativeEffectController", D_METHOD("advance_bounds", "player", "delta"), &NBNativeEffectController::advance_bounds);
    ClassDB::bind_static_method("NBNativeEffectController", D_METHOD("mark_partial_bounds", "player"), &NBNativeEffectController::mark_partial_bounds);
    ClassDB::bind_static_method("NBNativeEffectController", D_METHOD("sync_primitive_bounds", "player"), &NBNativeEffectController::sync_primitive_bounds);
    ClassDB::bind_static_method("NBNativeEffectController", D_METHOD("release", "player"), &NBNativeEffectController::release);
    ClassDB::bind_static_method("NBNativeEffectController", D_METHOD("bind_world", "player", "executor", "scope"), &NBNativeEffectController::bind_world);
    ClassDB::bind_static_method("NBNativeEffectController", D_METHOD("collision_failure", "player", "errors"), &NBNativeEffectController::collision_failure);
    ClassDB::bind_static_method("NBNativeEffectController", D_METHOD("ribbon_history", "emitter"), &NBNativeEffectController::ribbon_history);
    ClassDB::bind_static_method("NBNativeEffectController", D_METHOD("restart", "player", "compiler", "lifecycle_module", "prepared"),
                                &NBNativeEffectController::restart, DEFVAL(Dictionary()));
    ClassDB::bind_static_method("NBNativeEffectController",
                                D_METHOD("advance", "player", "seconds", "compiler"),
                                &NBNativeEffectController::advance);
    ClassDB::bind_static_method("NBNativeEffectController", D_METHOD("seek", "player", "time"),
                                &NBNativeEffectController::seek);
    ClassDB::bind_static_method("NBNativeEffectController", D_METHOD("step", "player", "delta"),
                                &NBNativeEffectController::step);
    ClassDB::bind_static_method("NBNativeEffectController",
                                D_METHOD("frame", "player", "emitter", "delta", "time"),
                                &NBNativeEffectController::frame);
    ClassDB::bind_static_method("NBNativeEffectController",
                                D_METHOD("run_system", "player", "stage", "delta"),
                                &NBNativeEffectController::run_system);
    ClassDB::bind_static_method("NBNativeEffectController",
                                D_METHOD("run_vm", "player", "emitter", "delta", "time"),
                                &NBNativeEffectController::run_vm);
    ClassDB::bind_static_method("NBNativeEffectController",
                                D_METHOD("run_gpu", "player", "emitter", "delta", "time"),
                                &NBNativeEffectController::run_gpu);
    ClassDB::bind_static_method("NBNativeEffectController",
                                D_METHOD("vm_failure", "player", "emitter", "errors"),
                                &NBNativeEffectController::vm_failure);
    ClassDB::bind_static_method("NBNativeEffectController",
                                D_METHOD("publish_events", "player", "emitter", "events"),
                                &NBNativeEffectController::publish_events);
    ClassDB::bind_static_method("NBNativeEffectController", D_METHOD("handlers", "plan"),
                                &NBNativeEffectController::handlers);
    ClassDB::bind_static_method("NBNativeEffectController", D_METHOD("simulate", "player", "delta"),
                                &NBNativeEffectController::simulate);
}
void NBNativeEffectController::simulate(Node *player, double delta) {
    if (!player)
        return;
    const bool profile=bool(player->get_meta("niagara_bridge_profile_renderer",false));
    Dictionary phases;
    auto phase_start=profile ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point();
    auto mark=[&](const char *name) {
        if(!profile) return;
        auto now=std::chrono::steady_clock::now();
        phases[name]=double(phases.get(name,0.))+std::chrono::duration<double,std::micro>(now-phase_start).count();phase_start=now;
    };
    constexpr int64_t ACTIVE = 0, COMPLETE = 3, COMPLETION = 3;
    Dictionary system = player->get("_system_state");
    if (bool(player->get("_lifecycle_failed")) || int64_t(system.get("ExecutionState", ACTIVE)) >= COMPLETE)
        return;
    Object *readers = player->get("_particle_readers");
    if (!readers)
        return;
    readers->call("begin_frame");
    bool first = !bool(player->get("_system_updated"));
    player->set("_system_updated", true);
    int64_t update_mode = player->get("_system_update_on_spawn");
    double lifecycle_delta = double(float(first && update_mode == 1 ? .0001 : delta));
    if (first && update_mode == 0)
        lifecycle_delta = -1.;
    player->set("_lifecycle_delta", lifecycle_delta);
    Object *native_system = player->get("_native_system");
    Array emitters = player->get("_emitters");
    if (lifecycle_delta >= 0. && native_system) {
        if (!bool(player->call("_run_native_system", "SystemUpdate", delta)))
            return;
        system = player->get("_system_state");
        if (int64_t(system["ExecutionState"]) >= COMPLETE) {
            for (int n = 0; n < emitters.size(); ++n) {
                Dictionary emitter = emitters[n];
                Array particles = emitter["particles"];
                NBNativeParticleView::clear_particles(emitter);
            }
            return;
        }
    } else if (lifecycle_delta >= 0.) {
        Dictionary values = Dictionary(player->get("_instance_parameters")).duplicate(),
                   config = player->get("_system_lifecycle");
        values.merge(NBNativeLifecycle::context(system, "System"), true);
        values["Engine.DeltaTime"] = lifecycle_delta;
        String error = NBNativeLifecycle::tick(system, config, lifecycle_delta, values);
        if (!error.is_empty()) {
            player->set("_lifecycle_failed", true);
            Array diagnostics = player->get("diagnostics");
            Dictionary diagnostic;
            diagnostic["severity"] = "error";
            diagnostic["message"] = error;
            diagnostics.append(diagnostic);
            return;
        }
        NBNativeLifecycle::system_response(system, config, player->get("_owner_execution_state"));
        if (int64_t(system["ExecutionState"]) >= COMPLETE) {
            for (int n = 0; n < emitters.size(); ++n) {
                Dictionary emitter = emitters[n];
                NBNativeLifecycle::change(emitter["lifecycle"], COMPLETE, COMPLETION);
                Array particles = emitter["particles"];
                NBNativeParticleView::clear_particles(emitter);
            }
            return;
        }
    }
    Object *events = player->get("_native_events");
    if (!events)
        return;
    events->call("begin_frame");
    readers->set("phase", "particles");
    Array order = player->get("_event_execution_order");
    std::vector<Dictionary> execution;
    for (int n = 0; n < emitters.size(); ++n)
        execution.push_back(emitters[n]);
    auto rank = [&](const Dictionary &emitter) {
        Dictionary plan = emitter["plan"];
        Ref<Resource> source = plan["source"];
        return order.find(source->get("display_name"));
    };
    std::stable_sort(execution.begin(), execution.end(),
                     [&](const Dictionary &a, const Dictionary &b) { return rank(a) < rank(b); });
    double time = player->get("simulation_time");
    mark("system_setup");
    for (auto &emitter : execution) {
        emitter["bounds_post_tick_completed"] = false;
        if (bool(emitter.get("gpu_failed", false)) || bool(emitter.get("vm_failed", false)) ||
            bool(emitter.get("curve_parameters_invalid", false)))
            continue;
        Object *current_native_system = player->get("_native_system");
        if (current_native_system &&
            int64_t(Dictionary(emitter["lifecycle"])["ExecutionState"]) >= COMPLETE) {
            Array particles = emitter["particles"];
            NBNativeParticleView::clear_particles(emitter);
            continue;
        }
        Dictionary plan = emitter["plan"];
        if (plan.has("lifecycle")) {
            double previous = emitter.get("source_particle_age", 0.);
            emitter["source_previous_particle_age"] = previous;
            emitter["source_particle_age"] = double(float(previous + double(float(delta))));
            emitter["source_particle_delta"] = double(float(delta));
        }
        Array particles = emitter["particles"];
        int64_t count = particles.size();
        for (int64_t index = 0; index < count && !bool(emitter.get("particle_projection_pending",false)); ++index) {
            Dictionary p = particles[index];
            p["update_execution_index"] = index;
            p["update_execution_count"] = count;
        }
        mark("emitter_setup");
        player->call("_schedule_spawn", emitter, delta);mark("spawn_schedule");
        if (int64_t(Dictionary(emitter["lifecycle"])["ExecutionState"]) >= COMPLETE) {
            Array current = emitter["particles"];
            NBNativeParticleView::clear_particles(emitter);
            continue;
        }
        if (emitter.has("gpu")) {
            player->call("_run_gpu", emitter, delta, time + delta);mark("gpu_controller");
            emitter["bounds_post_tick_completed"] = !bool(emitter.get("gpu_failed", false));
            continue;
        }
        if (emitter.has("native_vm")) {
            player->call("_run_native_vm", emitter, delta, time + delta);mark("vm_controller");
            emitter["bounds_post_tick_completed"] = !bool(emitter.get("vm_failed", false));
            player->call("_update_ribbon_history", emitter);
            continue;
        }
        player->call("_update_curated", emitter, delta);
        emitter["bounds_post_tick_completed"] = true;
        player->call("_update_ribbon_history", emitter);
    }
    mark("emitter_tail");
    bool complete = !emitters.is_empty();
    for (int n = 0; n < emitters.size(); ++n) {
        Dictionary emitter = emitters[n];
        complete = complete && int64_t(Dictionary(emitter["lifecycle"])["ExecutionState"]) >= COMPLETE;
    }
    if (complete)
        NBNativeLifecycle::change(player->get("_system_state"), COMPLETE, COMPLETION, true);
    if(profile) player->set_meta("niagara_bridge_simulation_timings_usec",phases);
}
} // namespace godot
