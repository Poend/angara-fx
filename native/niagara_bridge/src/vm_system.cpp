#include "vm_datasets.h"
#include <godot_cpp/core/class_db.hpp>
#include <algorithm>
using namespace godot;
namespace {
int iv(const Dictionary &d, const String &key, int fallback = 0) {
    return int(int64_t(d.get(key, fallback)));
}
void append(TypedArray<String> &out, const Variant &values) {
    Array a = values;
    for (int n = 0; n < a.size(); n++)
        out.append(a[n]);
}
bool rows(const Dictionary &d, const char *name) {
    if (d.get(name, Variant()).get_type() != Variant::ARRAY)
        return false;
    Array a = d[name];
    for (int n = 0; n < a.size(); n++)
        if (a[n].get_type() != Variant::DICTIONARY)
            return false;
    return true;
}
} // namespace
void NBNativeVMSystem::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_interface_state"), &NBNativeVMSystem::get_interface_state);
    ClassDB::bind_method(D_METHOD("configure", "contracts", "parameters"), &NBNativeVMSystem::configure,
                         DEFVAL(Dictionary()));
    ClassDB::bind_method(D_METHOD("run", "stage", "parameters", "instance_inputs"), &NBNativeVMSystem::run);
    ClassDB::bind_method(D_METHOD("flat_values"), &NBNativeVMSystem::flat_values);
    ClassDB::bind_method(D_METHOD("spawn_events", "names"), &NBNativeVMSystem::spawn_events);
    ClassDB::bind_method(D_METHOD("get_stages"), &NBNativeVMSystem::get_stages);
    ClassDB::bind_method(D_METHOD("get_state"), &NBNativeVMSystem::get_state);
    ClassDB::bind_method(D_METHOD("get_ready"), &NBNativeVMSystem::get_ready);
    ClassDB::bind_method(D_METHOD("set_capture_execution", "value"),
                         &NBNativeVMSystem::set_capture_execution);
    ClassDB::bind_method(D_METHOD("get_capture_execution"), &NBNativeVMSystem::get_capture_execution);
    ClassDB::bind_method(D_METHOD("get_last_execution"), &NBNativeVMSystem::get_last_execution);
    ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "stages", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "",
                 "get_stages");
    ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "state", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "",
                 "get_state");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "ready", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "",
                 "get_ready");
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "capture_execution", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE),
        "set_capture_execution", "get_capture_execution");
    ADD_PROPERTY(
        PropertyInfo(Variant::DICTIONARY, "last_execution", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "",
        "get_last_execution");
}
TypedArray<String> NBNativeVMSystem::configure(const Dictionary &contracts, const Dictionary &parameters) {
    stages = Dictionary();
    interface_state = Dictionary();
    state = Dictionary();
    packed_state = Dictionary();
    previous_parameters = Dictionary();
    ready = false;
    TypedArray<String> errors;
    for (String name : {String("SystemSpawn"), String("SystemUpdate")}) {
        Dictionary contract = contracts.get(name, Dictionary()),
                   program = contract.get("program", Dictionary());
        Ref<NBNativeVMExecutor> executor;
        executor.instantiate();
        executor->set_system_interfaces(interface_state);
        Ref<NBNativeVMDataset> layout, input;
        layout.instantiate();
        input.instantiate();
        append(errors, executor->configure(program, contract.get("interfaces", Array()), parameters));
        if (!errors.is_empty())
            continue;
        if (iv(program, "input_dataset_count") > 2 || iv(program, "output_dataset_count") != 1) {
            errors.append("Native system scripts require one output and at most two input datasets.");
            continue;
        }
        append(errors, layout->configure(program.get("system_dataset", Variant())));
        append(errors, input->configure(program.get("instance_input_dataset", Variant())));
        if (contract.get("external_parameter_defaults_producer", "") !=
                Variant("FNiagaraScriptRuntimeData.GetExecutionReadyParameterStore") ||
            !rows(contract, "external_parameter_defaults")) {
            errors.append("Native system script has no authoritative external defaults.");
            continue;
        }
        if (contract.get("instance_input_initialization_producer", "") !=
            Variant("FNiagaraDataSet::Allocate; compiled ParameterStore and constant-buffer bindings "
                    "inspected")) {
            errors.append("Native system input initialization provenance is missing.");
            continue;
        }
        if (!rows(contract, "instance_input_layout") || !rows(contract, "instance_parameter_defaults")) {
            errors.append("Malformed native system input metadata.");
            continue;
        }
        Array rows = contract["external_parameter_defaults"];
        Dictionary defaults;
        for (int n = 0; n < rows.size(); n++) {
            Dictionary row = rows[n];
            if (row.get("name", Variant()).get_type() != Variant::STRING ||
                row.get("float32", Variant()).get_type() != Variant::ARRAY ||
                row.get("int32", Variant()).get_type() != Variant::ARRAY) {
                errors.append("Invalid native system external default.");
                continue;
            }
            Dictionary raw;
            raw["float32"] = Array(row["float32"]).duplicate();
            raw["int32"] = Array(row["int32"]).duplicate();
            defaults[row["name"]] = raw;
        }
        Dictionary stage;
        stage["executor"] = executor;
        stage["layout"] = layout;
        stage["input"] = input;
        stage["contract"] = contract.duplicate(true);
        stage["defaults"] = defaults;
        stages[name] = stage;
    }
    if (errors.is_empty() && stages.size() == 2) {
        Ref<NBNativeVMDataset> spawn = Dictionary(stages["SystemSpawn"])["layout"],
                               update = Dictionary(stages["SystemUpdate"])["layout"];
        if (spawn->get_variables() != update->get_variables())
            errors.append("System spawn/update native layouts differ.");
    }
    ready = errors.is_empty() && stages.size() == 2;
    return errors;
}
Dictionary NBNativeVMSystem::run(const String &name, const Dictionary &parameters,
                                 const Dictionary &instance_inputs) {
    return run_for_step(name, parameters, instance_inputs, true);
}
Dictionary NBNativeVMSystem::run_for_step(const String &name, const Dictionary &parameters,
                                          const Dictionary &instance_inputs, bool copy_state) {
    Dictionary result, values;
    TypedArray<String> errors;
    result["errors"] = errors;
    result["values"] = values;
    if (!ready || !stages.has(name)) {
        errors.append("Configure valid original system programs first.");
        return result;
    }
    Dictionary stage = stages[name], contract = stage["contract"],
               current = Dictionary(stage["defaults"]).duplicate(),
               inputs = instance_inputs.duplicate();
    Ref<NBNativeVMExecutor> executor = stage["executor"];
    const Dictionary &program = executor->source_program();
    current.merge(parameters, true);
    Array defaults = contract["instance_parameter_defaults"];
    for (int n = 0; n < defaults.size(); n++) {
        Dictionary row = defaults[n];
        if (row.get("name", Variant()).get_type() == Variant::STRING && row.has("value") &&
            !inputs.has(row["name"]))
            inputs[row["name"]] = row["value"];
    }
    Array input_layout = contract["instance_input_layout"];
    for (int n = 0; n < input_layout.size(); n++) {
        Dictionary row = input_layout[n];
        if (inputs.has(row.get("name", "")))
            continue;
        if (row.get("parameter_store_bound", Variant()) == Variant(false) &&
            row.get("constant_buffer_bound", Variant()) == Variant(false) &&
            iv(row, "int32_components") == 1 && iv(row, "float32_components") == 0 &&
            row.has("fresh_allocation_int32"))
            inputs[row["name"]] = int64_t(row["fresh_allocation_int32"]);
    }
    Array sets;
    Ref<NBNativeVMDataset> layout = stage["layout"], input = stage["input"];
    for (int n = 0; n < std::max(iv(program, "input_dataset_count"), 1); n++) {
        // Both stages share the validated source layout. The previous VM
        // output already has the component columns the next update consumes.
        if (n == 0 && name == "SystemUpdate" && !packed_state.is_empty() && !capture_execution && !copy_state) {
            sets.append(packed_state);
            continue;
        }
        Dictionary packed = (n == 0 ? layout : input)->pack(n == 0 ? state : inputs, program["inputs"], n);
        append(errors, packed["errors"]);
        sets.append(packed["dataset"]);
    }
    if (!errors.is_empty())
        return result;
    append(errors, executor->update_user_curves(parameters));
    if (!errors.is_empty())
        return result;
    Dictionary frame;
    frame["current"] = current;
    frame["previous"] = previous_parameters;
    Dictionary next_interfaces = interface_state;
    if (executor->has_system_interfaces()) {
        next_interfaces = interface_state.duplicate(true);
        next_interfaces.erase("@debug_boxes");
        executor->set_system_interfaces(next_interfaces);
    }
    Dictionary actual = executor->execute(frame, sets, 1);
    if (capture_execution) {
        last_execution = Dictionary();
        last_execution["parameters"] = frame.duplicate(true);
        last_execution["datasets"] = sets.duplicate(true);
        last_execution["result"] = actual.duplicate(true);
        Ref<NBNativeVMReaders> readers = executor->get_particle_context();
        if (readers.is_valid())
            last_execution["reader_sources"] =
                readers->capture(executor->get_curves(), executor->get_reader_scope());
    }
    append(errors, actual["errors"]);
    if (!errors.is_empty())
        return result;
    Array counts = actual["dataset_counts"], datasets = actual["datasets"];
    if (counts.is_empty() || int64_t(counts[0]) != 1) {
        errors.append("Native system script removed its instance; scheduling unavailable.");
        return result;
    }
    Dictionary decoded = layout->unpack(datasets[0]);
    append(errors, decoded["errors"]);
    if (errors.is_empty()) {
        state = decoded["values"];
        packed_state = datasets[0];
        interface_state = next_interfaces;
        previous_parameters = current.duplicate(true);
        if (copy_state)
            result["values"] = state.duplicate(true);
    }
    return result;
}
Dictionary NBNativeVMSystem::flat_values() const {
    Dictionary result;
    Array names = state.keys();
    for (int n = 0; n < names.size(); n++)
        result[names[n]] = NBNativeVMDataset::value(state[names[n]]);
    return result;
}
Dictionary NBNativeVMSystem::spawn_events(const Array &names) const {
    Dictionary result;
    TypedArray<String> errors;
    Array events;
    result["errors"] = errors;
    result["events"] = events;
    if (!ready) {
        errors.append("Configure a valid native system before reading SpawnInfo.");
        return result;
    }
    Ref<NBNativeVMDataset> layout = Dictionary(stages["SystemUpdate"])["layout"];
    for (int n = 0; n < names.size(); n++) {
        if (names[n].get_type() != Variant::STRING || !state.has(names[n])) {
            errors.append("Missing native SpawnInfo output.");
            continue;
        }
        Dictionary raw = state[names[n]];
        Array floats = raw.get("float32", Array()), ints = raw.get("int32", Array());
        Dictionary variable = layout->get_variables().get(names[n], Dictionary());
        if (floats.size() != 2 || ints.size() != 2 ||
            variable.get("type", "") != Variant("NiagaraSpawnInfo")) {
            errors.append("Invalid native SpawnInfo layout.");
            continue;
        }
        Dictionary event;
        event["count"] = std::max(int(int64_t(ints[0])), 0);
        event["start_dt"] = floats[0];
        event["interval"] = floats[1];
        event["group"] = ints[1];
        event["order"] = events.size();
        events.append(event);
    }
    if (!errors.is_empty())
        events.clear();
    return result;
}

void NBNativeVMParameters::_bind_methods() {
    ClassDB::bind_static_method("NBNativeVMParameters", D_METHOD("matrix", "value"),
                                &NBNativeVMParameters::matrix);
    ClassDB::bind_static_method(
        "NBNativeVMParameters",
        D_METHOD("frame", "context", "owner", "previous_owner", "delta", "total_spawned"),
        &NBNativeVMParameters::frame);
    ClassDB::bind_static_method(
        "NBNativeVMParameters",
        D_METHOD("spawn", "frame", "emitter_name", "total", "count", "interval", "start", "group"),
        &NBNativeVMParameters::spawn);
}
Array NBNativeVMParameters::matrix(const Transform3D &v) {
    Vector3 x = v.basis.get_column(0), y = v.basis.get_column(1), z = v.basis.get_column(2);
    return Array::make(x.x, x.y, x.z, 0., y.x, y.y, y.z, 0., z.x, z.y, z.z, 0., v.origin.x, v.origin.y,
                       v.origin.z, 1.);
}
Dictionary NBNativeVMParameters::frame(const Dictionary &context, const Transform3D &owner,
                                       const Transform3D &previous_owner, double delta, int total_spawned) {
    Dictionary result = context.duplicate();
    Basis conversion(Vector3(0, 0, -1), Vector3(1, 0, 0), Vector3(0, 1, 0));
    auto to_unreal = [](Vector3 p) { return Vector3(-p.z, p.x, p.y) * 100.f; };
    Transform3D transform(conversion.inverse() * owner.basis * conversion, to_unreal(owner.origin)),
        unscaled(conversion.inverse() * Basis(owner.basis.get_rotation_quaternion()) * conversion,
                 transform.origin);
    result["Engine.DeltaTime"] = float(delta);
    result["Engine.InverseDeltaTime"] = delta > 0 ? float(1. / float(delta)) : 0.f;
    result["Engine.Emitter.TotalSpawnedParticles"] = total_spawned;
    result["Engine.Owner.Position"] = transform.origin;
    result["Engine.Owner.LWCTile"] = Array::make(0., 0., 0., 0.);
    result["Engine.Owner.Velocity"] =
        delta > 0 ? to_unreal(owner.origin - previous_owner.origin) / float(delta) : Vector3();
    result["Engine.Owner.Rotation"] = unscaled.basis.get_rotation_quaternion();
    Vector3 scale = owner.basis.get_scale();
    result["Engine.Owner.Scale"] = Vector3(scale.z, scale.x, scale.y);
    result["Engine.Owner.SystemXAxis"] = unscaled.basis.get_column(0);
    result["Engine.Owner.SystemYAxis"] = unscaled.basis.get_column(1);
    result["Engine.Owner.SystemZAxis"] = unscaled.basis.get_column(2);
    result["Engine.Owner.SystemLocalToWorld"] = matrix(transform);
    result["Engine.Owner.SystemWorldToLocal"] = matrix(transform.affine_inverse());
    result["Engine.Owner.SystemLocalToWorldNoScale"] = matrix(unscaled);
    result["Engine.Owner.SystemWorldToLocalNoScale"] = matrix(unscaled.affine_inverse());
    return result;
}
Dictionary NBNativeVMParameters::spawn(const Dictionary &frame, const String &name, int total, int count,
                                       double interval, double start, int group) {
    Dictionary result = frame.duplicate();
    result["Engine.Emitter.TotalSpawnedParticles"] = total;
    result["Engine.ExecutionCount"] = count;
    result[name + String(".SpawnInterval")] = interval;
    result[name + String(".InterpSpawnStartDt")] = start;
    result[name + String(".SpawnGroup")] = group;
    return result;
}
