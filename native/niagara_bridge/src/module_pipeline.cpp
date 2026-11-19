#include "module_execution.h"
#include "attribute_values.h"
#include "particle_context.h"
#include "value_evaluator.h"
#include <godot_cpp/classes/random_number_generator.hpp>
#include <algorithm>
namespace godot {
namespace {
Dictionary particle_defaults(const Dictionary &plan) {
    Dictionary particle;
    particle["id"] = 0;
    particle["birth_time"] = 0.;
    for (const char *key : {"age", "normalized_age", "drag", "rotation", "rotational_drag", "subimage_index",
                            "material_random", "camera_offset"})
        particle[key] = 0.;
    particle["lifetime"] = 1.;
    particle["mass"] = 1.;
    particle["ribbon_width"] = 1.;
    for (const char *key : {"alive", "first_frame", "newborn", "drag_ignore_mass"})
        particle[key] = true;
    particle["spawn_execution_index"] = 0;
    particle["spawn_execution_count"] = 1;
    particle["update_execution_index"] = 0;
    particle["update_execution_count"] = 1;
    particle["position"] = Vector3();
    for (const char *key : {"velocity", "previous_velocity", "initial_position", "force",
                            "advection_velocity", "presolve_position", "presolve_velocity", "presolve_force"})
        particle[key] = Vector3();
    particle["alignment"] = Vector3(0, 1, 0);
    for (const char *key : {"size", "initial_size", "size_scale_factor"})
        particle[key] = Vector2(1, 1);
    particle["mesh_scale"] = plan["mesh_scale_default"];
    particle["initial_mesh_scale"] = plan["mesh_scale_default"];
    particle["mesh_scale_factor"] = Vector3(1, 1, 1);
    for (const char *key : {"mesh_orientation", "initial_mesh_orientation", "initial_rotation"})
        particle[key] = Quaternion();
    particle["mesh_index"] = 0;
    for (const char *key : {"color", "initial_color", "dynamic1"})
        particle[key] = Color(1, 1, 1, 1);
    for (const char *key : {"dynamic", "dynamic2", "dynamic3"})
        particle[key] = Color(0, 0, 0, 0);
    particle["uv_flip"] = 1.;
    particle["uv_flip_y"] = 1.;
    particle["cache"] = Dictionary();
    particle["rng"] = Variant();
    Dictionary layout = plan.get("attribute_layout", Dictionary()), attributes;
    Array fields = layout.get("particle", Array());
    for (int n = 0; n < fields.size(); ++n) {
        Dictionary field = fields[n];
        attributes[field["name"]] = NBNativeAttributeValues::zero(field["type"]);
    }
    particle["attributes"] = attributes;
    return particle;
}
} // namespace
void NBNativeParticleModules::spawn(Dictionary emitter, double birth_time, const Transform3D &birth_transform,
                                    int64_t execution_index, int64_t execution_count, const Dictionary &frame,
                                    const Dictionary &schema) {
    spawn_initialized(emitter, birth_time, birth_transform, execution_index, execution_count, frame, schema,
                      particle_defaults(emitter["plan"]));
}
void NBNativeParticleModules::spawn_with_defaults(Dictionary emitter, double birth_time, const Transform3D &birth_transform,
                                    int64_t execution_index, int64_t execution_count, const Dictionary &frame,
                                    const Dictionary &schema, Dictionary &batch_defaults) {
    if (batch_defaults.is_empty())
        batch_defaults = particle_defaults(emitter["plan"]);
    spawn_initialized(emitter, birth_time, birth_transform, execution_index, execution_count, frame, schema,
                      batch_defaults.duplicate(true));
}
void NBNativeParticleModules::spawn_initialized(Dictionary emitter, double birth_time, const Transform3D &birth_transform,
                                    int64_t execution_index, int64_t execution_count, const Dictionary &frame,
                                    const Dictionary &schema, Dictionary particle) {
    Dictionary plan = emitter["plan"];
    Ref<RandomNumberGenerator> rng, stream = emitter["rng"];
    rng.instantiate();
    rng->set_seed(stream->randi());
    particle["id"] = emitter["emitted"];
    particle["spawn_group"] = frame.get("spawn_group", 0);
    particle["birth_time"] = birth_time;
    particle["spawn_execution_index"] = execution_index;
    particle["spawn_execution_count"] = execution_count;
    particle["position"] = bool(plan["local_space"]) ? Vector3() : NBNativeValues::godot_to_unreal(birth_transform.origin);
    particle["rng"] = rng;
    if (emitter.has("native_vm"))
        particle["vm_needs_spawn"] = true;
    else if (emitter.has("gpu")) {
        particle["gpu_needs_spawn"] = true;
        particle["gpu_seed"] = rng->get_seed();
        particle["gpu_birth_transform"] = birth_transform;
    } else {
        Dictionary layout = plan.get("attribute_layout", Dictionary()), attributes = particle["attributes"];
        Dictionary spawn_frame = frame.duplicate();
        spawn_frame["spawning"] = true;
        Dictionary ctx =
            NBNativeParticleContext::for_particle(particle, emitter, 0., birth_time, birth_transform,
                                                  spawn_frame, schema["builtins"], schema["extra"]);
        if (plan.has("source_rng")) {
            Dictionary source = plan["source_rng"], stages = source["stages"], curves = source["curves"],
                       math = source.get("cpu_math", Dictionary());
            ctx["source_rng"] = stages["ParticleSpawn"];
            ctx["source_curves"] = curves["ParticleSpawn"];
            ctx["source_cpu_math"] = math.get("ParticleSpawn", Dictionary());
        }
        Dictionary stages = plan["stages"];
        Array operations = stages.get("ParticleSpawn", Array());
        for (int n = 0; n < operations.size(); ++n) {
            Ref<Resource> operation = operations[n];
            if (operation.is_valid() && bool(operation->get("enabled"))) {
                execute(operation, particle, ctx, 0., schema["builtins"]);
                NBNativeParticleContext::refresh_operation(operation, ctx, particle, schema["writes"],
                                                           schema["builtins"], schema["extra"]);
            }
        }
        for (const char *key : {"mesh_scale", "mesh_orientation", "position", "color", "size"})
            particle[String("initial_") + key] = particle[key];
        Dictionary initial = layout.get("initial", Dictionary());
        Array keys = initial.keys();
        for (int n = 0; n < keys.size(); ++n)
            attributes[keys[n]] = attributes[initial[keys[n]]];
    }
    Array particles = emitter["particles"];
    particles.append(particle);
    emitter["emitted"] = int64_t(emitter["emitted"]) + 1;
}
void NBNativeParticleModules::update(Dictionary emitter, double delta, double time, const Dictionary &frame,
                                     const Dictionary &schema) {
    Dictionary plan = emitter["plan"], stages = plan["stages"];
    Array operations = stages.get("ParticleUpdate", Array()), particles = emitter["particles"];
    TypedArray<Dictionary> survivors;
    Transform3D transform = frame.get("source_transform", Transform3D());
    for (int n = 0; n < particles.size(); ++n) {
        Dictionary particle = particles[n];
        bool first = particle.get("newborn", false);
        if (first) {
            particle["newborn"] = false;
            if (String(plan.get("spawn_update_mode", "RunUpdateScript")) == "NoInterpolation") {
                if (bool(particle["alive"]))
                    survivors.append(particle);
                continue;
            }
        }
        double effective = std::clamp(time - double(particle["birth_time"]), 0., delta);
        if (plan.has("source_rng"))
            effective = double(float(effective));
        particle["force"] = Vector3();
        particle["drag"] = 0.;
        particle["rotational_drag"] = 0.;
        particle["drag_ignore_mass"] = true;
        particle["advection_velocity"] = Vector3();
        particle["size_scale_factor"] = Vector2(1, 1);
        particle["mesh_scale_factor"] = Vector3(1, 1, 1);
        Dictionary update_frame = frame.duplicate();
        update_frame["spawning"] = false;
        update_frame["spawn_execution"] = first;
        Dictionary ctx = NBNativeParticleContext::for_particle(
            particle, emitter, effective, time, transform, update_frame, schema["builtins"], schema["extra"]);
        if (first && plan.has("source_rng")) {
            Dictionary source = plan["source_rng"], math = source.get("cpu_math", Dictionary());
            ctx["source_cpu_math"] = math.get("InterpolatedUpdate", ctx.get("source_cpu_math", Dictionary()));
        }
        for (int index = 0; index < operations.size(); ++index) {
            Ref<Resource> operation = operations[index];
            if (operation.is_valid() && bool(operation->get("enabled"))) {
                execute(operation, particle, ctx, effective, schema["builtins"]);
                NBNativeParticleContext::refresh_operation(operation, ctx, particle, schema["writes"],
                                                           schema["builtins"], schema["extra"]);
            }
        }
        if (bool(particle["alive"]))
            survivors.append(particle);
    }
    emitter["particles"] = survivors;
}
} // namespace godot
