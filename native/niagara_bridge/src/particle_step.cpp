#include "particle_step.h"
#include "vm_datasets.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <chrono>

namespace godot {
void NBNativeVMParticleStep::_bind_methods() {
    ClassDB::bind_static_method(
        "NBNativeVMParticleStep",
        D_METHOD("run", "emitter", "current", "previous", "handlers", "readers", "events", "tick"),
        &NBNativeVMParticleStep::run);
}
Dictionary NBNativeVMParticleStep::run(const Dictionary &emitter, const Dictionary &frame,
                                       const Dictionary &previous, const Array &handlers,
                                       const Ref<NBNativeVMReaders> &readers,
                                       const Ref<NBNativeVMEvents> &events, int64_t tick) {
    Dictionary result;
    TypedArray<String> errors;
    Array particles;
    result["errors"] = errors;
    result["particles"] = particles;
    Dictionary timings;
    result["timings_usec"] = timings;
    const auto start_time = std::chrono::steady_clock::now();
    auto phase_start = start_time;
    auto mark = [&](const char *phase) {
        const auto now = std::chrono::steady_clock::now();
        timings[phase] = std::chrono::duration<double, std::micro>(now - phase_start).count();
        phase_start = now;
    };
    auto fail = [&](const String &message) {
        errors.append(message);
        return result;
    };
    auto failed = [&](const Dictionary &execution) {
        Array problems = execution.get("errors", Array());
        for (int n = 0; n < problems.size(); ++n)
            errors.append(problems[n]);
        return !problems.is_empty();
    };
    if (readers.is_null() || events.is_null())
        return fail("Native particle step requires frame readers and events.");
    Dictionary adapters = emitter.get("native_vm", Dictionary()), plan = emitter.get("plan", Dictionary());
    Ref<Resource> source = plan.get("source", Variant());
    Ref<NBNativeVMIDs> ids = emitter.get("vm_ids", Variant());
    if (source.is_null() || ids.is_null())
        return fail("Invalid native emitter state.");
    String name = source->get("display_name");
    Ref<NBNativeVMParticles> update = adapters.get("ParticleUpdate", Variant()),
                             spawn = adapters.get("ParticleSpawn", Variant());
    if (update.is_null() || spawn.is_null())
        return fail("Native particle step lacks spawn/update executors.");
    Array input = emitter.get("particles", Array()),
          existing = NBNativeVMParticles::existing_particles(input);
    Ref<NBNativeParticleBuffer> buffer = emitter.get("vm_buffer", Variant());
    if (buffer.is_null())
        buffer.instantiate();
    if (existing.is_empty())
        buffer.instantiate();
    else if (update->get_capture_execution()) {
        // Diagnostic traces explicitly accept edited raw input dictionaries.
        // Production playback keeps typed columns authoritative between ticks.
        Dictionary imported = update->import_buffer(existing);
        if (failed(imported))
            return result;
        buffer = imported["buffer"];
    } else if (buffer->get_count() != existing.size())
        return fail("Resident particle state differs from its snapshot; reinitialize the emitter.");
    if (!existing.is_empty() && !update->get_capture_execution())
        buffer->prepare_tick_metadata();
    ids->allocate(int(input.size()), int(tick + 1));
    mark("prepare_state");
    Dictionary current = frame.duplicate();
    Ref<NBNativeParticleBuffer> survivors;
    survivors.instantiate();
    Array pending;
    int64_t spawned_count = 0;
    auto publish = [&](const Dictionary &execution) {
        TypedArray<String> problems = events->publish(name, execution.get("events", Dictionary()));
        for (int n = 0; n < problems.size(); ++n)
            errors.append(problems[n]);
        return problems.is_empty();
    };
    auto append = [&](const Ref<NBNativeParticleBuffer> &target, const Ref<NBNativeParticleBuffer> &other) {
        TypedArray<String> problems = target->append(other);
        for (int n = 0; n < problems.size(); ++n)
            errors.append(problems[n]);
        return problems.is_empty();
    };
    if (!existing.is_empty()) {
        current["Engine.ExecutionCount"] = existing.size();
        Dictionary executed =
            update->run_buffer(buffer, current, previous, Ref<NBNativeVMRandom>(), Dictionary(), ids);
        if (failed(executed) || !publish(executed))
            return result;
        survivors = executed["buffer"];
    }
    int64_t offset = survivors->get_count();
    mark("update");
    Array batches = emitter.get("vm_spawn_batches", Array());
    for (int n = 0; n < batches.size(); ++n) {
        Dictionary batch = batches[n];
        Array templates = batch.get("particles", Array());
        Dictionary parameters = NBNativeVMParameters::spawn(current, name, batch.get("total", 0),
                                                            templates.size(), batch.get("interval", 0.),
                                                            batch.get("start_dt", 0.), batch.get("group", 0));
        Dictionary imported = spawn->import_buffer(templates);
        if (failed(imported))
            return result;
        Dictionary executed = spawn->run_buffer(imported["buffer"], parameters, previous,
                                                Ref<NBNativeVMRandom>(), Dictionary(), ids, int(offset));
        if (failed(executed) || !publish(executed))
            return result;
        Ref<NBNativeParticleBuffer> born = executed["buffer"];
        offset += born->get_count();
        spawned_count += born->get_count();
        if (batch.has("handler")) {
            Dictionary item;
            item["batch"] = batch;
            item["buffer"] = born;
            pending.append(item);
        } else if (!append(survivors, born))
            return result;
    }
    // Readers need a projection only at actual cross-stage visibility boundaries.
    mark("spawn");
    bool include_raw_fields = readers->needs_raw_fields();
    Array stages = adapters.values();
    for (int n = 0; n < stages.size(); ++n) {
        Ref<NBNativeVMParticles> adapter = stages[n];
        include_raw_fields = include_raw_fields || (adapter.is_valid() && adapter->get_capture_execution());
    }
    auto projection = [&](const Ref<NBNativeParticleBuffer> &state) -> Array {
        if (!state->get_count())
            return Array();
        Dictionary projected = state->snapshot(include_raw_fields);
        failed(projected);
        return projected["particles"];
    };
    if (!pending.is_empty() || !handlers.is_empty()) {
        Ref<NBNativeParticleBuffer> before = survivors->duplicate_buffer();
        for (int n = 0; n < pending.size(); ++n) {
            Dictionary item = pending[n];
            if (!append(before, item["buffer"]))
                return result;
        }
        readers->publish(name, projection(before), ids, int(spawned_count));
        if (!errors.is_empty())
            return result;
    }
    readers->set_phase("event");
    for (int n = 0; n < pending.size(); ++n) {
        Dictionary item = pending[n], batch = item["batch"],
                   data = Dictionary(batch["event_input"]).duplicate();
        data["instance_offset"] = batch["event_index"];
        Ref<NBNativeParticleBuffer> born = item["buffer"];
        Ref<NBNativeVMParticles> handler = adapters.get(batch["handler"], Variant());
        if (handler.is_null())
            return fail("Missing native spawned-particle event executor.");
        current["Engine.ExecutionCount"] = born->get_count();
        Dictionary event_input;
        event_input[1] = data;
        Dictionary executed = handler->run_buffer(born, current, previous, Ref<NBNativeVMRandom>(),
                                                  event_input, ids, int(survivors->get_count()));
        if (failed(executed) || !append(survivors, executed["buffer"]))
            return result;
    }
    bool published = false;
    for (int n = 0; n < handlers.size(); ++n) {
        Dictionary handler = handlers[n], settings = handler["settings"];
        if (settings.get("execution_mode", "") != Variant("EveryParticle"))
            continue;
        if (!published) {
            readers->publish(name, projection(survivors), ids, int(spawned_count));
            published = true;
        }
        if (!errors.is_empty())
            return result;
        Dictionary payload = events->read(name, settings);
        for (int i = 0; i < int64_t(payload.get("count", 0)); ++i) {
            if (!survivors->get_count())
                break;
            Dictionary data = Dictionary(payload["data"]).duplicate();
            data["instance_offset"] = i;
            ids->allocate(int(survivors->get_count()), int(tick + 1));
            current["Engine.ExecutionCount"] = survivors->get_count();
            Ref<NBNativeVMParticles> adapter = adapters.get(handler["stage"], Variant());
            if (adapter.is_null())
                return fail("Missing native every-particle event executor.");
            Dictionary event_input;
            event_input[1] = data;
            Dictionary executed =
                adapter->run_buffer(survivors, current, previous, Ref<NBNativeVMRandom>(), event_input, ids);
            if (failed(executed))
                return result;
            survivors = executed["buffer"];
            readers->publish(name, projection(survivors), ids, int(spawned_count));
            if (!errors.is_empty())
                return result;
        }
    }
    mark("handlers");
    bool deferred=bool(emitter.get("defer_particle_projection",false)) && !include_raw_fields;
    particles = deferred ? survivors->metadata_rows() : projection(survivors);
    if (!errors.is_empty())
        return result;
    NBNativeVMParticles::clear_spawn_flags(particles);
    if(deferred) readers->publish_buffer(name,survivors,ids,int(spawned_count));
    else readers->publish(name, particles, ids, int(spawned_count));
    readers->set_phase("particles");
    result["particles"] = particles;
    result["buffer"] = survivors;
    result["particle_projection_pending"]=deferred;
    result["previous_parameters"] = current;
    result["spawned_count"] = spawned_count;
    mark("projection_and_readers");
    timings["total"] =
        std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start_time).count();
    return result;
}
} // namespace godot
