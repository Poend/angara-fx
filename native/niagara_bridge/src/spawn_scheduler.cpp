#include "particle_view.h"
#include "spawn_scheduler.h"
#include "runtime_control.h"
#include "value_evaluator.h"
#include <godot_cpp/core/class_db.hpp>
#include <algorithm>
#include <cmath>
#include <vector>
namespace godot {
namespace {
double f32(double v) {
    return double(float(v));
}
struct Birth {
    double time;
    int64_t order, index, count, group;
};
Variant input(const Ref<Resource> &operation, const String &name, const Variant &fallback) {
    return NBNativeDocument::input_value(operation->get("inputs"), name, fallback);
}
} // namespace
void NBNativeSpawnScheduler::_bind_methods() {
    ClassDB::bind_static_method(
        "NBNativeSpawnScheduler",
        D_METHOD("append_legacy", "emitter", "births", "delta", "frame", "schema", "start", "end"),
        &NBNativeSpawnScheduler::append_legacy);
    ClassDB::bind_static_method(
        "NBNativeSpawnScheduler",
        D_METHOD("append_events", "emitter", "events", "delta", "maximum", "frame", "schema", "start", "end"),
        &NBNativeSpawnScheduler::append_events);
    ClassDB::bind_static_method(
        "NBNativeSpawnScheduler",
        D_METHOD("source", "emitter", "system", "time", "delta", "lifecycle_delta", "context", "assignment"),
        &NBNativeSpawnScheduler::source);
    ClassDB::bind_static_method("NBNativeSpawnScheduler", D_METHOD("burst_10", "operation", "age", "delta"),
                                &NBNativeSpawnScheduler::burst_10);
    ClassDB::bind_static_method(
        "NBNativeSpawnScheduler",
        D_METHOD("legacy", "emitter", "time", "delta", "maximum", "include_initial", "context", "assignment"),
        &NBNativeSpawnScheduler::legacy);
}
Dictionary NBNativeSpawnScheduler::source(Dictionary emitter, const Dictionary &system, double time,
                                          double delta, double lifecycle_delta, const Callable &context,
                                          const Callable &assignment) {
    Dictionary result;
    Array events, errors;
    result["events"] = events;
    result["errors"] = errors;
    result["lifecycle_error"] = String();
    if (lifecycle_delta < 0.)
        return result;
    Dictionary plan = emitter["plan"], stages = plan.get("stages", Dictionary());
    Array operations = stages.get("EmitterUpdate", Array());
    std::vector<Dictionary> pending;
    for (int index = 0; index < operations.size(); ++index) {
        Ref<Resource> op = operations[index];
        if (op.is_null() || !bool(op->get("enabled")))
            continue;
        String name = op->get("module_name");
        Dictionary state = emitter["lifecycle"];
        Array particles = emitter["particles"];
        if (name == "EmitterState") {
            String error = NBNativeLifecycle::emitter_tick(
                state, plan["lifecycle"], system, NBNativeParticleView::particle_count(emitter), lifecycle_delta,
                context.call(emitter, lifecycle_delta, time + delta));
            if (!error.is_empty()) {
                result["lifecycle_error"] = error;
                return result;
            }
            emitter["source_emitter_age"] = state["Age"];
            emitter["source_looped_age"] = state["LoopedAge"];
            if (int64_t(state["ExecutionState"]) >= 3) {
                NBNativeParticleView::clear_particles(emitter);
                return result;
            }
            continue;
        }
        if (String(op->get("operation_kind")) == "assignment") {
            assignment.call(op, emitter, lifecycle_delta, time + delta);
            continue;
        }
        if (int64_t(state["ExecutionState"]) != 0)
            continue;
        double age = state["LoopedAge"], interval = 0., start = 0.;
        int64_t count = 0;
        if (name == "SpawnBurst_Instantaneous@1.0") {
            Dictionary event = burst_10(op, age, lifecycle_delta);
            count = event["count"];
            start = event["start_dt"];
        } else if (name == "SpawnBurst_Instantaneous") {
            double spawn = f32(input(op, "SpawnTime", 0.));
            start = f32(spawn - f32(age - lifecycle_delta));
            if (start >= 0. && f32(spawn - age) < 0.)
                count = std::max(int64_t(input(op, "SpawnCount", 1)), int64_t(0));
        } else if (name == "SpawnRate") {
            if (age < 0.)
                continue;
            Variant resolved = NBNativeValues::evaluate(input(op, "SpawnRate", 0.),
                                                        context.call(emitter, lifecycle_delta, time + delta));
            if ((resolved.get_type() != Variant::FLOAT && resolved.get_type() != Variant::INT) ||
                !std::isfinite(double(resolved)) || double(resolved) <= 0.)
                continue;
            double rate = f32(resolved);
            Dictionary carry = emitter["rate_carry"];
            String id = op->get("stable_id");
            double previous = carry.get(id, 0.), total = f32(f32(rate * lifecycle_delta) + previous);
            if (!std::isfinite(total) || total >= double(INT64_MAX)) {
                errors.append("SpawnRate exceeds the supported integer population range.");
                return result;
            }
            interval = f32(1. / rate);
            start = f32(interval * f32(1. - previous));
            count = int64_t(std::floor(total));
            carry[id] = f32(total - double(count));
        } else
            continue;
        Variant group = NBNativeValues::evaluate(input(op, "SpawnGroup", 0),
                                                 context.call(emitter, lifecycle_delta, time + delta));
        if ((group.get_type() != Variant::FLOAT && group.get_type() != Variant::INT) ||
            !std::isfinite(double(group)) || double(group) != std::floor(double(group))) {
            errors.append("SpawnGroup must resolve to an integer.");
            return result;
        }
        Dictionary order = plan.get("spawn_info_order", Dictionary()), event;
        event["count"] = count;
        event["interval"] = interval;
        event["start_dt"] = start;
        event["group"] = int64_t(group);
        event["order"] = order.get(op->get("stable_id"), int64_t(pending.size()));
        pending.push_back(event);
    }
    std::stable_sort(pending.begin(), pending.end(), [](const Dictionary &a, const Dictionary &b) {
        return int64_t(a["order"]) < int64_t(b["order"]);
    });
    for (auto &event : pending)
        events.append(event);
    return result;
}
Dictionary NBNativeSpawnScheduler::burst_10(const Ref<Resource> &operation, double age, double delta) {
    Dictionary result;
    result["count"] = 0;
    result["start_dt"] = 0.;
    if (operation.is_null())
        return result;
    double current = f32(age), spawn = f32(input(operation, "SpawnTime", 0.)),
           previous = f32(current - f32(delta)), start = f32(spawn - previous);
    result["start_dt"] = start;
    result["count"] =
        start >= 0 && f32(spawn - current) < 0 ? int64_t(input(operation, "SpawnCount", 0)) : int64_t(0);
    return result;
}
Dictionary NBNativeSpawnScheduler::legacy(Dictionary emitter, double time, double delta, int64_t maximum,
                                          bool initial, const Callable &context, const Callable &assignment) {
    Dictionary result;
    Array births, diagnostics;
    result["births"] = births;
    result["diagnostics"] = diagnostics;
    result["capacity_reached"] = false;
    Dictionary plan = emitter.get("plan", Dictionary());
    double duration = plan.get("duration", 0.);
    bool loop = plan.get("loop", false);
    if (!std::isfinite(time) || !std::isfinite(delta) || delta < 0 || !std::isfinite(duration) ||
        duration <= 0 || maximum < 0 || maximum > 1000000) {
        Dictionary error;
        error["severity"] = "error";
        error["message"] = "Invalid spawn scheduling time, duration or capacity.";
        diagnostics.append(error);
        return result;
    }
    if (!loop && time >= duration)
        return result;
    double finish = loop ? time + delta : std::min(time + delta, duration);
    Dictionary stages = plan.get("stages", Dictionary());
    Array operations = stages.get("EmitterUpdate", Array());
    std::vector<Birth> pending;
    int64_t group = 0;
    auto append = [&](double when, int64_t index, int64_t count) {
        if (int64_t(pending.size()) <= maximum)
            pending.push_back({when, int64_t(pending.size()), index, count, group});
    };
    for (int n = 0; n < operations.size(); ++n) {
        Ref<Resource> operation = operations[n];
        if (operation.is_null() || !bool(operation->get("enabled")))
            continue;
        if (String(operation->get("operation_kind")) == "assignment") {
            if (assignment.is_valid())
                assignment.call(operation, emitter, delta, finish);
            continue;
        }
        String name = operation->get("module_name");
        Variant group_input = input(operation, "SpawnGroup", 0);
        if (group_input.get_type() == Variant::DICTIONARY)
            group_input = NBNativeValues::evaluate(group_input, context.is_valid() ? Dictionary(context.call(emitter, delta, finish)) : Dictionary());
        group = int64_t(group_input);
        if (name == "SpawnBurst_Instantaneous@1.0") {
            Dictionary event = burst_10(operation, loop ? std::fmod(finish, duration) : finish, delta);
            int64_t count = event["count"];
            for (int64_t index = 0; index < std::min(count, maximum + 1); ++index)
                append(time + double(event["start_dt"]), index, count);
        } else if (name == "SpawnBurst_Instantaneous") {
            double spawn = input(operation, "SpawnTime", 0.);
            int64_t count = std::max(int64_t(0), int64_t(input(operation, "SpawnCount", 1)));
            if (!count)
                continue;
            double first_cycle = loop ? std::max(0., std::floor((time - spawn) / duration)) : 0.,
                   last_cycle = loop ? std::floor((finish - spawn + 1e-9) / duration) : 0.;
            if (!std::isfinite(first_cycle) || !std::isfinite(last_cycle) || std::abs(first_cycle) > 9e15 ||
                std::abs(last_cycle) > 9e15)
                continue;
            for (int64_t cycle = int64_t(first_cycle);
                 cycle <= int64_t(last_cycle) && int64_t(pending.size()) <= maximum; ++cycle) {
                double when = spawn + double(cycle) * duration;
                if ((when > time + 1e-9 || (initial && Math::is_zero_approx(when))) &&
                    when <= finish + 1e-9) {
                    for (int64_t index = 0; index < std::min(count, maximum + 1); ++index)
                        append(when, index, count);
                }
            }
        } else if (name == "SpawnRate") {
            Dictionary ctx =
                context.is_valid() ? Dictionary(context.call(emitter, delta, finish)) : Dictionary();
            Variant resolved = NBNativeValues::evaluate(input(operation, "SpawnRate", 0.), ctx);
            double rate = resolved.get_type() == Variant::FLOAT || resolved.get_type() == Variant::INT
                              ? double(resolved)
                              : -1.;
            Dictionary carry = emitter.get("rate_carry", Dictionary());
            String id = operation->get("stable_id");
            double previous = carry.get(id, 0.), total = previous + rate * (finish - time);
            if (!std::isfinite(rate) || rate < 0 || !std::isfinite(total) || total >= double(INT64_MAX)) {
                Dictionary invalid = emitter.get("invalid_spawn_rates", Dictionary());
                uint32_t signature = resolved.hash();
                if (!invalid.has(id) || int64_t(invalid[id]) != signature) {
                    Dictionary error;
                    error["severity"] = "error";
                    error["message"] = "SpawnRate requires a finite nonnegative scalar; invalid live rate "
                                       "does not spawn particles.";
                    Ref<Resource> source = plan.get("source", Variant());
                    if (source.is_valid())
                        error["emitter"] = source->get("display_name");
                    diagnostics.append(error);
                    invalid[id] = int64_t(signature);
                }
                emitter["invalid_spawn_rates"] = invalid;
                continue;
            }
            int64_t count = int64_t(std::floor(total + 1e-9));
            carry[id] = total - double(count);
            emitter["rate_carry"] = carry;
            if (rate > 0)
                for (int64_t index = 0; index < std::min(count, maximum + 1); ++index)
                    append(std::min(finish, time + (double(index + 1) - previous) / rate), index, count);
        }
    }
    std::stable_sort(pending.begin(), pending.end(), [](const Birth &a, const Birth &b) {
        return Math::is_equal_approx(a.time, b.time) ? a.order < b.order : a.time < b.time;
    });
    Array particles = emitter.get("particles", Array());
    int64_t available = std::max(int64_t(0), maximum - NBNativeParticleView::particle_count(emitter));
    result["capacity_reached"] = int64_t(pending.size()) > available;
    for (int64_t n = 0; n < std::min(int64_t(pending.size()), available); ++n) {
        auto b = pending[n];
        Dictionary row;
        row["time"] = b.time;
        row["order"] = b.order;
        row["group"] = b.group;
        row["execution_index"] = b.index;
        row["execution_count"] = b.count;
        births.append(row);
    }
    return result;
}
} // namespace godot
