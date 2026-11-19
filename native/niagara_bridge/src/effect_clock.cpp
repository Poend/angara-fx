#include "effect_controller.h"
#include "effect_revision.h"
#include "collision_world.h"
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/script.hpp>
#include <algorithm>
#include <cmath>
#include <chrono>
namespace godot {
void NBNativeEffectController::advance(Node3D *player, double seconds, const Ref<Script> &compiler) {
    if (!player || seconds <= 0. || !std::isfinite(seconds))
        return;
    const bool profile=bool(player->get_meta("niagara_bridge_profile_renderer",false));
    Dictionary phases;
    auto phase_start=profile ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point();
    auto mark=[&](const char *name) {
        if(!profile) return;
        auto now=std::chrono::steady_clock::now();
        phases[name]=double(phases.get(name,0.))+std::chrono::duration<double,std::micro>(now-phase_start).count();
        phase_start=now;
    };
    player->call("_refresh_runtime_operations");
    player->call("_check_native_bounds_inputs");
    player->set("_instance_parameters", NBNativeEffectRevision::parameters(player, compiler));
    player->call("_refresh_curve_parameters");
    bool variable = player->get("_source_variable_tick");
    double step_delta = variable ? double(float(seconds)) : double(player->get("_run_step"));
    Object *clock = player->get("_source_clock");
    int64_t steps;
    if (clock)
        steps = clock->call("take_steps", seconds);
    else if (variable)
        steps = 1;
    else {
        double accumulator = double(player->get("_accumulator")) + seconds;
        player->set("_accumulator", accumulator);
        double pending = std::floor((accumulator + 1e-8) / step_delta);
        steps = pending >= 3600. ? 3600 : int64_t(pending);
    }
    mark("preparation");
    Transform3D target = player->is_inside_tree() ? player->get_global_transform() : player->get_transform(),
                source = player->get("_source_transform");
    for (int64_t index = 0; index < steps; ++index) {
        player->set("_step_source_start", source.interpolate_with(target, double(index) / double(steps)));
        player->set("_step_source_end", source.interpolate_with(target, double(index + 1) / double(steps)));
        if (!bool(player->get("_simulation_started"))) {
            Array emitters = player->get("_emitters");
            for (int n = 0; n < emitters.size(); ++n) {
                Dictionary emitter = emitters[n];
                player->call("_schedule_spawn", emitter, 0., true);
                if (emitter.has("gpu"))
                    player->call("_run_gpu", emitter, 0., 0.);
            }
            player->set("_simulation_started", true);
        }
        mark("clock_before_step");
        player->call("_step", step_delta);mark("simulation");
        player->call("_advance_native_bounds", seconds);mark("bounds");
        int64_t tick = int64_t(player->get("_tick")) + 1;
        player->set("_tick", tick);
        Object *current_clock = player->get("_source_clock");
        if (current_clock) {
            current_clock->call("finish_step");
            player->set("simulation_time", current_clock->get("simulation_age"));
        } else if (bool(player->get("_source_variable_tick")))
            player->set("simulation_time",
                        double(float(double(player->get("simulation_time")) + step_delta)));
        else {
            player->set("simulation_time", double(tick) * step_delta);
            player->set("_accumulator", double(player->get("_accumulator")) - step_delta);
        }
    }
    if (steps > 0)
        player->set("_source_transform", target);
    mark("clock_after_step");
    player->call("_sync_renderers");mark("renderers");
    if(profile) player->set_meta("niagara_bridge_advance_timings_usec",phases);
}
void NBNativeEffectController::seek(Node *player, double time) {
    if (!player || !std::isfinite(time))
        return;
    player->call("restart");
    double limit = std::min(60., double(player->get("_run_step")) * 3600.),
           target = std::clamp(time, 0., limit);
    if (time > limit) {
        Array diagnostics = player->get("diagnostics");
        Dictionary d;
        d["severity"] = "warning";
        d["message"] = "Synchronous seek is limited to 3600 steps / 60 seconds.";
        diagnostics.append(d);
    }
    Object *clock = player->get("_source_clock");
    if (clock || bool(player->get("_source_variable_tick"))) {
        double frame_delta = std::max(double(player->get("fixed_step")), .001);
        int64_t frames = std::min(int64_t(std::floor(target / frame_delta + 1e-8)), int64_t(3600));
        for (int64_t index = 0; index < frames; ++index)
            player->call("advance", frame_delta);
        double remainder = target - double(frames) * frame_delta;
        if (remainder > 1e-8)
            player->call("advance", remainder);
    } else
        player->call("advance", target);
}
void NBNativeEffectController::step(Node3D *player, double delta) {
    player->set("_context_epoch", int64_t(player->get("_context_epoch")) + 1);
    player->set("_context_batch_active", true);
    bool collision = player->get("_uses_collision");
    Ref<NBNativeCollisionWorld> world = player->get("_collision_world");
    if (collision && world.is_null()) {
        player->call("_collision_failure", Array::make("Native collision world is unavailable."));
        player->set("_context_batch_active", false);
        return;
    }
    if (collision && world.is_valid() && world->get_world() != player->get_world_3d()) {
        Array errors = world->configure(player->get_world_3d(), player->get("collision_settings"));
        if (!errors.is_empty()) {
            player->call("_collision_failure", errors);
            player->set("_context_batch_active", false);
            return;
        }
    }
    if (collision && world.is_valid() && !bool(world->get_statistics().get("frame_open", false))) {
        Array errors = world->begin_frame();
        if (!errors.is_empty()) {
            player->call("_collision_failure", errors);
            player->set("_context_batch_active", false);
            return;
        }
    }
    player->call("_simulate_step", delta);
    if (bool(player->get("_uses_collision"))) {
        Ref<NBNativeCollisionWorld> current = player->get("_collision_world");
        if (current.is_valid()) {
            Array errors = current->end_frame();
            if (!errors.is_empty())
                player->call("_collision_failure", errors);
        }
    }
    player->set("_context_batch_active", false);
}
} // namespace godot
