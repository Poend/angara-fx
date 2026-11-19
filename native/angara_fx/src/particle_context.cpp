#include "particle_context.h"
#include "attribute_values.h"
#include "runtime_control.h"
#include "value_evaluator.h"
#include <cmath>
#include <godot_cpp/core/class_db.hpp>
namespace godot {
namespace {
void copy_field(Dictionary context, const Dictionary &particle, const String &name,
                const Dictionary &builtins, const Dictionary &extra, bool custom) {
    if (name == "Particles.SpriteRotation")
        context[name] = Math::rad_to_deg(double(particle.get("rotation", 0.)));
    else if (name == "Particles.UVScale")
        context[name] = Vector2(particle.get("uv_flip", 1.), particle.get("uv_flip_y", 1.));
    else if (extra.has(name))
        context[name] = particle.get(extra[name], Variant());
    else if (builtins.has(name)) {
        Array fields = builtins[name];
        if (fields.size() > 1)
            context[name] = particle.get(fields[1], Variant());
    } else if (custom) {
        Dictionary attributes = particle.get("attributes", Dictionary());
        if (attributes.has(name))
            context[name] = attributes[name];
    }
}
} // namespace
void NBNativeParticleContext::_bind_methods() {
    ClassDB::bind_static_method("NBNativeParticleContext",
                                D_METHOD("for_particle", "particle", "emitter", "delta", "time",
                                         "source_transform", "frame", "builtins", "extra"),
                                &NBNativeParticleContext::for_particle);
    ClassDB::bind_static_method("NBNativeParticleContext",
                                D_METHOD("for_emitter", "emitter", "delta", "time", "frame"),
                                &NBNativeParticleContext::for_emitter);
    ClassDB::bind_static_method(
        "NBNativeParticleContext",
        D_METHOD("assign_emitter", "operation", "emitter", "delta", "time", "stage", "frame"),
        &NBNativeParticleContext::assign_emitter);
    ClassDB::bind_static_method("NBNativeParticleContext",
                                D_METHOD("refresh", "context", "particle", "builtins", "extra"),
                                &NBNativeParticleContext::refresh);
    ClassDB::bind_static_method(
        "NBNativeParticleContext",
        D_METHOD("refresh_operation", "operation", "context", "particle", "writes", "builtins", "extra"),
        &NBNativeParticleContext::refresh_operation);
}
Dictionary NBNativeParticleContext::for_particle(Dictionary particle, Dictionary emitter, double delta,
                                                 double time, const Transform3D &source_transform,
                                                 const Dictionary &frame, const Dictionary &builtins,
                                                 const Dictionary &extra) {
    bool spawning = frame.get("spawning", false), spawn_execution = frame.get("spawn_execution", false),
         batch = frame.get("batch", false);
    int64_t epoch = frame.get("epoch", 0);
    if (batch && !spawning && int64_t(emitter.get("context_epoch", -1)) == epoch) {
        Dictionary cached = Dictionary(emitter["context_template"]).duplicate();
        cached["rng"] = particle["rng"];
        cached["cache"] = particle["cache"];
        cached["Engine.DeltaTime"] = delta;
        String prefix = spawn_execution ? "spawn" : "update";
        cached["_execution_index"] = particle.get(prefix + "_execution_index", 0);
        cached["_execution_count"] = particle.get(prefix + "_execution_count", 1);
        if (cached.has("source_unique_id"))
            cached["source_unique_id"] = particle["id"];
        refresh(cached, particle, builtins, extra);
        return cached;
    }
    Dictionary plan = emitter["plan"], result;
    double duration = plan["duration"];
    result["rng"] = particle["rng"];
    result["cache"] = particle["cache"];
    result["local_space"] = plan["local_space"];
    result["source_transform"] = source_transform;
    result["Engine.DeltaTime"] = delta;
    result["Emitter.LoopedAge"] = std::fmod(time, duration);
    result["Emitter.Age"] = time;
    result["System.Age"] = time;
    Ref<Resource> source = plan.get("source", Variant());
    result["Emitter.Determinism"] = source.is_valid() && bool(NBNativeValues::input_value(source->get("settings"), "Determinism", false));
    result["Emitter.RandomSeed"] =
        source.is_valid() ? int64_t(NBNativeValues::input_value(source->get("settings"), "RandomSeed", 0))
                          : 0;
    if (!emitter.has("curl_phases"))
        emitter["curl_phases"] = Dictionary();
    result["_curl_phases"] = emitter["curl_phases"];
    result["Engine.System.TickCount"] = frame.get("tick", 0);
    if (emitter.has("source_instance_seed"))
        result["Engine.Emitter.InstanceSeed"] = emitter["source_instance_seed"];
    String prefix = spawning || spawn_execution ? "spawn" : "update";
    result["_execution_index"] = particle.get(prefix + "_execution_index", 0);
    result["_execution_count"] = particle.get(prefix + "_execution_count", 1);
    if (plan.has("lifecycle")) {
        result.merge(NBNativeLifecycle::context(emitter["lifecycle"], "Emitter"), true);
        result.merge(NBNativeLifecycle::context(frame.get("system", Dictionary()), "System"), true);
        result["Emitter.Age"] = double(emitter.get("source_particle_age", 0.));
    }
    if (plan.has("source_rng")) {
        Dictionary source_rng = plan["source_rng"], stages = source_rng["stages"],
                   curves = source_rng["curves"], math = source_rng.get("cpu_math", Dictionary());
        result["source_rng"] = stages["ParticleUpdate"];
        result["source_curves"] = curves["ParticleUpdate"];
        result["source_seed"] = source_rng["seed"];
        result["source_unique_id"] = particle["id"];
        result["source_cpu_math"] = math.get("ParticleUpdate", Dictionary());
        result["Emitter.LoopedAge"] = double(emitter.get("source_looped_age", 0.));
        result["Emitter.Age"] = double(emitter.get("source_particle_age", 0.));
    }
    if (spawning && bool(plan.get("interpolated_spawn", false)) &&
        double(emitter.get("source_particle_delta", 0.)) > 0.) {
        double previous = emitter.get("source_previous_particle_age", 0.),
               interpolation = double(float(double(float(time - double(frame.get("simulation_time", 0.)))) /
                                            double(emitter["source_particle_delta"])));
        result["Emitter.Age"] = double(
            float(previous +
                  double(float(interpolation * double(float(double(result["Emitter.Age"]) - previous))))));
    }
    if (!plan.has("lifecycle"))
        result["Emitter.NormalizedLoopAge"] = double(result["Emitter.LoopedAge"]) / duration;
    result.merge(frame.get("parameters", Dictionary()), false);
    result.merge(emitter.get("variables", Dictionary()), false);
    Vector3 scale = source_transform.basis.get_scale();
    result["Engine.Owner.Scale"] = Vector3(scale.z, scale.x, scale.y);
    result["Engine.Emitter.SimulationPosition"] =
        bool(plan["local_space"]) ? Vector3() : NBNativeValues::godot_to_unreal(source_transform.origin);
    if (batch && !spawning) {
        if (plan.has("context_reads"))
            result["_context_reads"] = plan["context_reads"];
        emitter["context_epoch"] = epoch;
        emitter["context_template"] = result.duplicate();
    }
    refresh(result, particle, builtins, extra);
    return result;
}
Dictionary NBNativeParticleContext::for_emitter(Dictionary emitter, double delta, double time,
                                                const Dictionary &frame) {
    Dictionary plan = emitter["plan"], result;
    double duration = plan["duration"];
    Transform3D source = frame.get("source_transform", Transform3D());
    result["rng"] = emitter["rng"];
    result["cache"] = emitter.get("variable_cache", Dictionary());
    result["local_space"] = plan["local_space"];
    result["source_transform"] = source;
    result["Engine.DeltaTime"] = delta;
    result["Emitter.LoopedAge"] = double(emitter.get("source_looped_age", std::fmod(time, duration)));
    result["Emitter.Age"] = double(emitter.get("source_emitter_age", time));
    result["System.Age"] = time;
    result["Emitter.NormalizedLoopAge"] = double(result["Emitter.LoopedAge"]) / duration;
    result["Engine.System.TickCount"] = frame.get("tick", 0);
    if (emitter.has("source_instance_seed"))
        result["Engine.Emitter.InstanceSeed"] = emitter["source_instance_seed"];
    if (plan.has("lifecycle")) {
        result.merge(NBNativeLifecycle::context(emitter["lifecycle"], "Emitter"), true);
        result.merge(NBNativeLifecycle::context(frame.get("system", Dictionary()), "System"), true);
    }
    Vector3 scale = source.basis.get_scale();
    result["Engine.Owner.Scale"] = Vector3(scale.z, scale.x, scale.y);
    result["Engine.Emitter.SimulationPosition"] =
        bool(plan["local_space"]) ? Vector3() : NBNativeValues::godot_to_unreal(source.origin);
    result.merge(frame.get("parameters", Dictionary()), false);
    result.merge(emitter.get("variables", Dictionary()), false);
    return result;
}
void NBNativeParticleContext::assign_emitter(const Ref<Resource> &operation, Dictionary emitter, double delta,
                                             double time, const String &stage, const Dictionary &frame) {
    if (operation.is_null())
        return;
    Array inputs = operation->get("inputs");
    if (inputs.size() != 1)
        return;
    Dictionary ctx = for_emitter(emitter, delta, time, frame), plan = emitter["plan"];
    if (plan.has("source_rng")) {
        Dictionary source = plan["source_rng"], stages = source["stages"];
        if (stages.has(stage)) {
            Dictionary curves = source["curves"];
            Dictionary extra;
            extra["source_rng"] = stages[stage];
            extra["source_curves"] = curves.get(stage, Dictionary());
            extra["source_seed"] = source["seed"];
            extra["source_unique_id"] = 0;
            ctx.merge(extra);
        }
    }
    Ref<Resource> input = inputs[0];
    Variant resolved = NBNativeValues::evaluate(input->get("value"), ctx,
                                                String(operation->get("stable_id")) + "/assignment");
    Dictionary variables = emitter.get("variables", Dictionary());
    variables[operation->get("module_name")] = NBNativeAttributeValues::coerce(
        resolved, NBNativeAttributeValues::type_name(operation->get("target_type")));
    emitter["variables"] = variables;
}
void NBNativeParticleContext::refresh(Dictionary context, const Dictionary &particle,
                                      const Dictionary &builtins, const Dictionary &extra) {
    context.merge(particle.get("attributes", Dictionary()), true);
    if (context.has("_context_reads")) {
        Dictionary reads = context["_context_reads"];
        Array names = reads.keys();
        for (int n = 0; n < names.size(); ++n)
            copy_field(context, particle, names[n], builtins, extra, false);
        return;
    }
    Array names = builtins.keys();
    for (int n = 0; n < names.size(); ++n) {
        Array fields = builtins[names[n]];
        if (fields.size() < 2)
            continue;
        String field = fields[1];
        if (!field.is_empty() && particle.has(field))
            context[names[n]] = particle[field];
    }
    names = extra.keys();
    for (int n = 0; n < names.size(); ++n)
        context[names[n]] = particle.get(extra[names[n]], Variant());
    copy_field(context, particle, "Particles.SpriteRotation", builtins, extra, false);
    copy_field(context, particle, "Particles.UVScale", builtins, extra, false);
}
void NBNativeParticleContext::refresh_operation(const Ref<Resource> &operation, Dictionary context,
                                                const Dictionary &particle, const Dictionary &writes,
                                                const Dictionary &builtins, const Dictionary &extra) {
    if (operation.is_null())
        return;
    String name = operation->get("module_name");
    Array names;
    if (String(operation->get("operation_kind")) == "assignment")
        names.append(name);
    else
        names = writes.get(name, Array());
    if (names.is_empty()) {
        refresh(context, particle, builtins, extra);
        return;
    }
    Dictionary reads = context.get("_context_reads", Dictionary());
    for (int n = 0; n < names.size(); ++n) {
        if (context.has("_context_reads") && !reads.has(names[n]))
            continue;
        copy_field(context, particle, names[n], builtins, extra, true);
    }
}
} // namespace godot
