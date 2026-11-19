#include "particle_view.h"
#include "spawn_scheduler.h"
#include "module_execution.h"
#include "vm_datasets.h"
#include <algorithm>
namespace godot {
void NBNativeSpawnScheduler::append_legacy(Dictionary emitter, const Array &births, double delta,
                                           const Dictionary &frame, const Dictionary &schema,
                                           const Transform3D &start, const Transform3D &end) {
    Dictionary plan = emitter["plan"], gpu_defaults;
    double time = frame.get("simulation_time", 0.);
    Dictionary birth_frame = frame.duplicate();
    for (int n = 0; n < births.size(); ++n) {
        Dictionary birth = births[n];
        double when = birth["time"];
        birth_frame["spawn_group"] = birth.get("group", 0);
        double weight = delta > 0. ? std::clamp((when - time) / delta, 0., 1.) : 1.;
        Transform3D transform = bool(plan["interpolated_spawn"]) ? start.interpolate_with(end, weight) : end;
        if (emitter.has("gpu") && !emitter.has("native_vm"))
            NBNativeParticleModules::spawn_with_defaults(emitter, when, transform, birth["execution_index"],
                                                        birth["execution_count"], birth_frame, schema, gpu_defaults);
        else
            NBNativeParticleModules::spawn(emitter, when, transform, birth["execution_index"],
                                           birth["execution_count"], birth_frame, schema);
    }
}
Dictionary NBNativeSpawnScheduler::append_events(Dictionary emitter, const Array &events, double delta,
                                                 int64_t maximum, const Dictionary &frame,
                                                 const Dictionary &schema, const Transform3D &start,
                                                 const Transform3D &end) {
    Dictionary result;
    TypedArray<String> errors;
    result["errors"] = errors;
    result["capacity_reached"] = false;
    double time = frame.get("simulation_time", 0.);
    Dictionary plan = emitter["plan"], gpu_defaults;
    bool native = emitter.has("native_vm");
    for (int n = 0; n < events.size(); ++n) {
        Dictionary event = events[n];
        int64_t count = event["count"];
        double interval = event["interval"], start_dt = event["start_dt"];
        Array particles = emitter["particles"];
        int64_t available = std::max(int64_t(0), maximum - NBNativeParticleView::particle_count(emitter)),
                created_count = std::min(count, available);
        if (count > available)
            result["capacity_reached"] = true;
        Dictionary batch;
        batch["particles"] = Array();
        batch["total"] = emitter["emitted"];
        batch["interval"] = interval;
        batch["start_dt"] = start_dt;
        batch["group"] = event["group"];
        for (const char *field : {"handler", "event_input", "event_index"})
            if (event.has(field))
                batch[field] = event[field];
        if (native) {
            if (created_count < 0 || created_count > 1000000) {
                errors.append("Invalid native spawn template count.");
                return result;
            }
            Dictionary created = NBNativeVMParticles::append_spawn_templates(
                emitter, created_count, time, delta, start_dt, interval, start, end);
            Array failure = created["errors"];
            if (!failure.is_empty()) {
                errors.append_array(failure);
                return result;
            }
            Array rows = created["particles"];
            batch["particles"] = rows;
            if (!rows.is_empty()) {
                Array batches = emitter["vm_spawn_batches"];
                batches.append(batch);
            }
            continue;
        }
        Dictionary birth_frame = frame.duplicate();
        birth_frame["spawn_group"] = event["group"];
        auto f32 = [](double value) { return double(float(value)); };
        for (int64_t index = 0; index < created_count; ++index) {
            double offset = f32(start_dt + f32(interval * double(index))),
                   birth = bool(plan["interpolated_spawn"]) ? time + offset : time;
            if (String(plan.get("spawn_update_mode", "RunUpdateScript")) == "RunUpdateScript")
                birth = time + delta - f32(f32(delta) - f32(f32(delta) - start_dt));
            double weight = delta > 0. ? std::clamp(offset / delta, 0., 1.) : 1.;
            Transform3D transform =
                bool(plan["interpolated_spawn"]) ? start.interpolate_with(end, weight) : end;
            if (emitter.has("gpu"))
                NBNativeParticleModules::spawn_with_defaults(emitter, birth, transform, index, created_count,
                                                            birth_frame, schema, gpu_defaults);
            else
                NBNativeParticleModules::spawn(emitter, birth, transform, index, created_count, birth_frame, schema);
        }
    }
    return result;
}
} // namespace godot
