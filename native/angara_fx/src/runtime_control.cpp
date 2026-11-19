#include "runtime_control.h"
#include "value_evaluator.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
namespace godot {
void NBNativeSourceClock::_bind_methods() {
    ClassDB::bind_static_method("NBNativeSourceClock", D_METHOD("f32", "value"), &NBNativeSourceClock::f32);
    ClassDB::bind_static_method("NBNativeSourceClock",
                                D_METHOD("configuration", "effect", "native_comparison"),
                                &NBNativeSourceClock::configuration, DEFVAL(false));
    ClassDB::bind_method(D_METHOD("configure", "settings"), &NBNativeSourceClock::configure);
    ClassDB::bind_method(D_METHOD("take_steps", "engine_delta"), &NBNativeSourceClock::take_steps);
    ClassDB::bind_method(D_METHOD("finish_step"), &NBNativeSourceClock::finish_step);
#define NB_CLOCK_PROPERTY(TYPE, NAME)                                                                        \
    ClassDB::bind_method(D_METHOD("get_" #NAME), &NBNativeSourceClock::get_##NAME);                          \
    ClassDB::bind_method(D_METHOD("set_" #NAME, "value"), &NBNativeSourceClock::set_##NAME);                 \
    ADD_PROPERTY(PropertyInfo(TYPE, #NAME, PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "set_" #NAME,       \
                 "get_" #NAME)
    NB_CLOCK_PROPERTY(Variant::FLOAT, delta);
    NB_CLOCK_PROPERTY(Variant::FLOAT, engine_age);
    NB_CLOCK_PROPERTY(Variant::FLOAT, simulation_age);
    NB_CLOCK_PROPERTY(Variant::INT, max_substeps);
    NB_CLOCK_PROPERTY(Variant::INT, dropped_substeps);
#undef NB_CLOCK_PROPERTY
}
double NBNativeSourceClock::f32(double value) {
    return float(value);
}
Dictionary NBNativeSourceClock::configuration(const Ref<Resource> &effect, bool native_comparison) {
    Dictionary result;
    Array errors;
    result["enabled"] = false;
    result["delta"] = 0.;
    result["max_substeps"] = 100;
    result["errors"] = errors;
    if (effect.is_null())
        return result;
    Array inputs = effect->get("settings");
    Variant enabled = NBNativeDocument::input_value(inputs, "FixedTickDelta", false);
    if (enabled.get_type() != Variant::BOOL) {
        errors.append("FixedTickDelta must be an authored boolean.");
        return result;
    }
    result["enabled"] = enabled;
    if (bool(enabled)) {
        Variant authored = NBNativeDocument::input_value(inputs, "FixedTickDeltaTime");
        if ((authored.get_type() != Variant::INT && authored.get_type() != Variant::FLOAT) ||
            !std::isfinite(double(authored)) || f32(double(authored)) <= 0.) {
            errors.append("FixedTickDeltaTime must explicitly provide a finite positive source step.");
            return result;
        }
        result["delta"] = f32(double(authored));
    }
    if (native_comparison) {
        Array emitters = effect->get("emitters");
        std::vector<int64_t> caps;
        for (int n = 0; n < emitters.size(); ++n) {
            Ref<Resource> emitter = emitters[n];
            if (emitter.is_null() || !bool(emitter->get("enabled")))
                continue;
            Dictionary contracts = emitter->get("source_rng_contracts"),
                       clock = contracts.get("source_clock", Dictionary());
            Variant maximum = clock.get("max_substeps", Variant()), recorded = clock.get("delta", Variant());
            if (clock.get("enabled", Variant()) != enabled ||
                (recorded.get_type() != Variant::FLOAT && recorded.get_type() != Variant::INT) ||
                f32(double(recorded)) != double(result["delta"]) ||
                maximum.get_type() != Variant::INT || int64_t(maximum) < 1)
                errors.append(String("Native fixed-tick comparison requires matching source getters and a "
                                     "recorded MaxTickSubsteps CVar: ") +
                              String(emitter->get("display_name")));
            else if (std::find(caps.begin(), caps.end(), int64_t(maximum)) == caps.end())
                caps.push_back(maximum);
        }
        if (caps.size() > 1)
            errors.append("Emitter source captures disagree on the system fixed-tick cap.");
        if (caps.size() == 1)
            result["max_substeps"] = caps[0];
    }
    return result;
}
void NBNativeSourceClock::configure(const Dictionary &settings) {
    delta = settings.get("delta", 1.0 / 60.0);
    max_substeps = settings.get("max_substeps", 100);
    engine_age = 0;
    simulation_age = 0;
    dropped_substeps = 0;
}
int64_t NBNativeSourceClock::take_steps(double engine_delta) {
    if (!std::isfinite(delta) || delta <= 0 || max_substeps < 1 || !std::isfinite(engine_delta)) {
        UtilityFunctions::push_error(
            "Native source clock requires a finite positive fixed step and tick budget.");
        return 0;
    }
    double frame_delta = f32(engine_delta), budget = f32(f32(std::fmod(engine_age, delta)) + frame_delta);
    double quotient = std::floor(f32(budget / delta));
    if (!std::isfinite(quotient) || quotient >= double(std::numeric_limits<int64_t>::max())) {
        UtilityFunctions::push_error("Native source clock tick budget exceeds supported integer storage.");
        return 0;
    }
    int64_t requested = quotient > 0 ? int64_t(quotient) : 0, count = std::min(requested, max_substeps);
    dropped_substeps += requested - count;
    engine_age = f32(engine_age + frame_delta);
    return count;
}
void NBNativeSourceClock::finish_step() {
    simulation_age = f32(simulation_age + delta);
}
void NBNativeDocument::_bind_methods() {
    ClassDB::bind_static_method("NBNativeDocument",
                                D_METHOD("find", "records", "field", "name", "occurrence"),
                                &NBNativeDocument::find, DEFVAL(0));
    ClassDB::bind_static_method("NBNativeDocument",
                                D_METHOD("input_value", "records", "name", "fallback", "occurrence"),
                                &NBNativeDocument::input_value, DEFVAL(Variant()), DEFVAL(0));
}
Ref<Resource> NBNativeDocument::find(const Array &records, const StringName &field, const String &name,
                                     int64_t occurrence) {
    if (occurrence < 0)
        return Ref<Resource>();
    for (int n = 0; n < records.size(); ++n) {
        Ref<Resource> entry = records[n];
        if (entry.is_null())
            continue;
        if (entry->get(field) != Variant(name))
            continue;
        if (occurrence == 0)
            return entry;
        --occurrence;
    }
    return Ref<Resource>();
}
Variant NBNativeDocument::input_value(const Array &records, const String &name, const Variant &fallback,
                                      int64_t occurrence) {
    Ref<Resource> entry = find(records, "input_name", name, occurrence);
    return entry.is_valid() ? entry->get("value") : fallback;
}

namespace {
constexpr int ACTIVE = 0, INACTIVE = 1, CLEAR = 2, COMPLETE = 3, SCALABILITY = 0, INTERNAL = 1, OWNER = 2,
              COMPLETION = 3;
const char *LIFECYCLE_FIELDS[] = {"Age",
                                  "LoopedAge",
                                  "NormalizedLoopAge",
                                  "LoopCount",
                                  "CurrentLoopDelay",
                                  "CurrentLoopDuration",
                                  "ExecutionState",
                                  "ExecutionStateSource"};
String enum_value(const Variant &value) {
    if (value.get_type() == Variant::DICTIONARY) {
        Dictionary row = value;
        if (row.get("kind", "") == Variant("reference"))
            return row.get("name", "");
    }
    return String(value);
}
} // namespace
void NBNativeLifecycle::_bind_methods() {
    ClassDB::bind_static_method("NBNativeLifecycle", D_METHOD("initial"), &NBNativeLifecycle::initial);
    ClassDB::bind_static_method("NBNativeLifecycle", D_METHOD("context", "state", "prefix"),
                                &NBNativeLifecycle::context);
    ClassDB::bind_static_method("NBNativeLifecycle", D_METHOD("configuration", "operation", "system"),
                                &NBNativeLifecycle::configuration, DEFVAL(false));
    ClassDB::bind_static_method("NBNativeLifecycle",
                                D_METHOD("_evaluate", "config", "name", "fallback", "values"),
                                &NBNativeLifecycle::evaluate);
    ClassDB::bind_static_method("NBNativeLifecycle", D_METHOD("tick", "state", "config", "delta", "values"),
                                &NBNativeLifecycle::tick);
    ClassDB::bind_static_method(
        "NBNativeLifecycle",
        D_METHOD("emitter_tick", "state", "config", "system", "count", "delta", "values"),
        &NBNativeLifecycle::emitter_tick);
    ClassDB::bind_static_method("NBNativeLifecycle", D_METHOD("change", "state", "next", "source", "system"),
                                &NBNativeLifecycle::change, DEFVAL(false));
    ClassDB::bind_static_method(
        "NBNativeLifecycle",
        D_METHOD("tick_evaluated", "state", "config", "delta", "duration", "delay", "count"),
        &NBNativeLifecycle::tick_evaluated);
    ClassDB::bind_static_method("NBNativeLifecycle",
                                D_METHOD("system_response", "state", "config", "owner_state"),
                                &NBNativeLifecycle::system_response);
    ClassDB::bind_static_method("NBNativeLifecycle",
                                D_METHOD("emitter_response", "state", "config", "system", "count"),
                                &NBNativeLifecycle::emitter_response);
}
Dictionary NBNativeLifecycle::initial() {
    Dictionary result;
    for (const char *name : LIFECYCLE_FIELDS)
        result[name] = 0.;
    result["LoopCount"] = 0;
    result["ExecutionState"] = ACTIVE;
    result["ExecutionStateSource"] = SCALABILITY;
    result["bCompleteOnInactive"] = true;
    result["LoopCountIncreased"] = false;
    return result;
}
Dictionary NBNativeLifecycle::context(const Dictionary &state, const String &prefix) {
    Dictionary result;
    for (const char *name : LIFECYCLE_FIELDS)
        result[prefix + String(".") + String(name)] = state[name];
    return result;
}
Dictionary NBNativeLifecycle::configuration(const Ref<Resource> &operation, bool system) {
    Dictionary result;
    Array entries = operation.is_valid() ? Array(operation->get("inputs")) : Array();
    auto input = [&](const String &name, const Variant &fallback) {
        return NBNativeDocument::input_value(entries, name, fallback);
    };
    result["operation"] = operation;
    result["system"] = system;
    result["mode"] = system ? String("Self") : enum_value(input("LifeCycleMode", "System"));
    result["behavior"] = enum_value(input("LoopBehavior", "Infinite"));
    result["duration_mode"] = system ? String("Fixed") : enum_value(input("LoopDurationMode", "Fixed"));
    result["response"] = enum_value(input("InactiveResponse", "Complete"));
    result["scalability"] = enum_value(input("ScalabilityMode", "System"));
    result["delay"] = input("UseLoopDelay", false);
    result["first_delay_only"] = input("DelayFirstLoopOnly", false);
    result["recalculate"] = input("RecalculateDurationEachLoop", false);
    return result;
}
Variant NBNativeLifecycle::evaluate(const Dictionary &config, const String &name, const Variant &fallback,
                                    const Dictionary &values) {
    Ref<Resource> operation = config.get("operation", Variant());
    if (operation.is_null())
        return fallback;
    Array inputs = operation->get("inputs");
    return NBNativeValues::evaluate(NBNativeDocument::input_value(inputs, name, fallback), values,
                                    String(operation->get("stable_id")) + String("/") + name + String("/0"));
}
String NBNativeLifecycle::tick(Dictionary state, const Dictionary &config, double delta,
                               const Dictionary &values) {
    Ref<Resource> operation = config.get("operation", Variant());
    if (operation.is_null())
        return "Lifecycle configuration has no operation; simulation stopped.";
    Variant duration = evaluate(config, "LoopDuration", 5., values);
    Variant delay =
        bool(config.get("delay", false)) ? evaluate(config, "LoopDelay", 0., values) : Variant(0.);
    Variant count = config.get("behavior", "") == Variant("Multiple")
                        ? evaluate(config, "LoopCount", 1, values)
                        : Variant(1);
    return tick_evaluated(state, config, delta, duration, delay, count);
}
String NBNativeLifecycle::emitter_tick(Dictionary state, const Dictionary &config, const Dictionary &system,
                                       int64_t count, double delta, const Dictionary &values) {
    if (config.get("mode", "") != Variant("System")) {
        String error = tick(state, config, delta, values);
        if (!error.is_empty())
            return error;
    }
    return emitter_response(state, config, system, count);
}
void NBNativeLifecycle::change(Dictionary state, int64_t next, int64_t source, bool system) {
    int64_t current = state["ExecutionState"];
    if ((!system && current >= COMPLETE) || source < int64_t(state["ExecutionStateSource"]))
        return;
    if (!system && next == CLEAR && (current == INACTIVE || current == CLEAR))
        next = INACTIVE;
    state["ExecutionState"] = next;
    state["ExecutionStateSource"] = source;
}
String NBNativeLifecycle::tick_evaluated(Dictionary state, const Dictionary &config, double delta,
                                         const Variant &duration_value, const Variant &delay_value,
                                         const Variant &count_value) {
    for (const Variant &value : {duration_value, delay_value, count_value})
        if ((value.get_type() != Variant::FLOAT && value.get_type() != Variant::INT) ||
            !std::isfinite(double(value)))
            return "Lifecycle input must be a finite scalar; simulation stopped.";
    auto f32 = NBNativeSourceClock::f32;
    double duration = f32(double(duration_value)), delay = f32(double(delay_value));
    if (!std::isfinite(duration) || !std::isfinite(delay) || count_value.get_type() != Variant::INT ||
        int64_t(count_value) < INT32_MIN || int64_t(count_value) > INT32_MAX)
        return "Lifecycle input exceeds float32/int32 storage or LoopCount is not an integer; simulation "
               "stopped.";
    bool system = config["system"], use_delay = config["delay"];
    String behavior = config["behavior"];
    bool infinite = behavior == "Once" && config["duration_mode"] == Variant("Infinite");
    if (!system)
        duration = infinite ? 65500. : f32(std::max(duration, .016667));
    if (double(state["Age"]) == 0.) {
        state["CurrentLoopDuration"] = f32(std::max(duration, .016667));
        if (use_delay) {
            state["LoopedAge"] = f32(-delay);
            state["CurrentLoopDelay"] = delay;
        }
    }
    double dt = f32(delta);
    state["Age"] = f32(double(state["Age"]) + dt);
    double age = f32(double(state["LoopedAge"]) + dt), old_duration = state["CurrentLoopDuration"];
    double quotient = std::abs(old_duration) > 1e-8 ? f32(age / old_duration) : 0.;
    if (!std::isfinite(quotient) || quotient >= double(INT64_MAX) || quotient <= double(INT64_MIN))
        return "Lifecycle tick exceeds supported loop storage; simulation stopped.";
    int64_t loops = std::max(int64_t(quotient), int64_t(0));
    bool once_without_delay = behavior == "Once" && !use_delay;
    state["LoopCountIncreased"] = once_without_delay ? age >= old_duration : loops > 0;
    state["LoopCount"] = once_without_delay ? 0 : int64_t(state["LoopCount"]) + loops;
    state["LoopedAge"] = once_without_delay ? age : f32(age - f32(double(loops) * old_duration));
    if (infinite) {
        state["LoopCountIncreased"] = false;
        state["LoopCount"] = 0;
    }
    if (bool(state["LoopCountIncreased"])) {
        if (behavior == "Once") {
            state["CurrentLoopDuration"] = duration;
            state["LoopedAge"] = 0.;
        } else {
            if (bool(config["recalculate"]))
                state["CurrentLoopDuration"] = duration;
            if (use_delay) {
                state["CurrentLoopDelay"] = bool(config["first_delay_only"]) ? 0. : delay;
                state["LoopedAge"] = f32(double(state["LoopedAge"]) - double(state["CurrentLoopDelay"]));
            }
        }
    }
    double current_duration = state["CurrentLoopDuration"];
    state["NormalizedLoopAge"] = infinite || std::abs(current_duration) <= 1e-8
                                     ? 0.
                                     : f32(double(state["LoopedAge"]) / current_duration);
    if ((behavior == "Once" && bool(state["LoopCountIncreased"])) ||
        (behavior == "Multiple" && int64_t(state["LoopCount"]) >= int64_t(count_value)))
        change(state, INACTIVE, INTERNAL, system);
    return String();
}
void NBNativeLifecycle::system_response(Dictionary state, const Dictionary &config, int64_t owner_state) {
    state["bCompleteOnInactive"] = config["response"] == Variant("Complete");
    if (config["response"] == Variant("Kill") && int64_t(state["ExecutionState"]) != ACTIVE &&
        int64_t(state["ExecutionStateSource"]) != SCALABILITY)
        change(state, COMPLETE, COMPLETION, true);
    if (owner_state != ACTIVE)
        change(state, owner_state, OWNER, true);
}
String NBNativeLifecycle::emitter_response(Dictionary state, const Dictionary &config,
                                           const Dictionary &system, int64_t count) {
    bool system_mode = config["mode"] == Variant("System");
    if (system_mode)
        for (const char *name : LIFECYCLE_FIELDS)
            if (String(name) != "ExecutionState" && String(name) != "ExecutionStateSource")
                state[name] = system[name];
    int64_t system_state = system["ExecutionState"], source = system["ExecutionStateSource"];
    if ((config["scalability"] == Variant("System") && source == SCALABILITY) ||
        (config["scalability"] == Variant("Self") && source != SCALABILITY))
        change(state, system_state, source);
    if (system_mode) {
        if (system_state != ACTIVE && source != SCALABILITY)
            change(state, INACTIVE, INTERNAL);
        if (int64_t(state["ExecutionState"]) != ACTIVE &&
            (!bool(system["bCompleteOnInactive"]) ||
             (count == 0 && int64_t(state["ExecutionStateSource"]) != SCALABILITY)))
            change(state, COMPLETE, COMPLETION);
    } else {
        if (system_state != ACTIVE && source == OWNER)
            change(state, INACTIVE, INTERNAL);
        bool inactive = int64_t(state["ExecutionState"]) != ACTIVE &&
                        int64_t(state["ExecutionStateSource"]) != SCALABILITY;
        String response = config["response"];
        if ((response == "Complete" && inactive && count == 0) || (response == "Kill" && inactive) ||
            (response == "Continue" && int64_t(state["ExecutionState"]) != ACTIVE && source == OWNER))
            change(state, COMPLETE, COMPLETION);
    }
    return String();
}
} // namespace godot
