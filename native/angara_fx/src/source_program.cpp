#include "source_program.h"
#include "effect_compiler.h"
#include "compiler_frontend.h"
#include "vm_datasets.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace godot {
void NBNativeSourceProgram::_bind_methods() {
    ClassDB::bind_static_method("NBNativeSourceProgram", D_METHOD("system_fingerprint", "effect"),
                                &NBNativeSourceProgram::system_fingerprint);
    ClassDB::bind_static_method("NBNativeSourceProgram", D_METHOD("fingerprint", "emitter"),
                                &NBNativeSourceProgram::fingerprint);
    ClassDB::bind_static_method("NBNativeSourceProgram", D_METHOD("build", "effect", "plan", "parameters"),
                                &NBNativeSourceProgram::build, DEFVAL(Dictionary()));
    ClassDB::bind_static_method("NBNativeSourceProgram", D_METHOD("invoke", "method", "arguments"),
                                &NBNativeSourceProgram::invoke);
}
namespace {
void fingerprint_stages(Array records, const Ref<Resource> &r) {
    Array stages = r->get("stages");
    for (int s = 0; s < stages.size(); ++s) {
        Ref<Resource> stage = stages[s];
        Array operations = stage->get("operations");
        for (int o = 0; o < operations.size(); ++o) {
            Ref<Resource> op = operations[o];
            Array inputs, source_inputs = op->get("inputs");
            for (int i = 0; i < source_inputs.size(); ++i) {
                Ref<Resource> input = source_inputs[i];
                inputs.append(Array::make(input->get("input_name"), input->get("value")));
            }
            records.append(Array::make(stage->get("stage_name"), op->get("stable_id"), op->get("module_name"),
                                       op->get("alias_name"), op->get("enabled"), op->get("operation_kind"),
                                       op->get("target_type"), inputs));
        }
    }
}
String system_digest(const Ref<Resource> &effect, bool imported_defaults) {
    Array records, settings = effect->get("settings"), parameters = effect->get("user_parameters");
    Array keys;
    Dictionary entities;
    if (imported_defaults) {
        Dictionary baseline = effect->get("source_baseline");
        entities = baseline.get("entities", Dictionary());
        Dictionary root = entities.get(baseline.get("root", ""), Dictionary());
        Dictionary children = root.get("children", Dictionary());
        keys = children.get("user_parameters", Array());
        if (keys.size() != parameters.size() || keys.is_empty())
            return String();
    }
    for (int i = 0; i < settings.size(); ++i) {
        Ref<Resource> r = settings[i];
        records.append(Array::make(r->get("input_name"), r->get("value")));
    }
    for (int i = 0; i < parameters.size(); ++i) {
        Ref<Resource> r = parameters[i];
        Variant name = r->get("input_name"), type = r->get("target_type"), has_default = r->get("has_default");
        Variant value = r->get("value");
        if (imported_defaults) {
            Dictionary entity = entities.get(keys[i], Dictionary());
            Dictionary fields = entity.get("fields", Dictionary());
            if (fields.get("input_name", Variant()) != name || fields.get("target_type", Variant()) != type ||
                fields.get("has_default", Variant()) != has_default || !fields.has("value"))
                return String();
            value = fields["value"];
        }
        records.append(Array::make(name, type, has_default, value));
    }
    fingerprint_stages(records, effect);
    return UtilityFunctions::var_to_str(records).sha256_text();
}
} // namespace
String NBNativeSourceProgram::system_fingerprint(const Ref<Resource> &effect) {
    return system_digest(effect, false);
}
String NBNativeSourceProgram::fingerprint(const Ref<Resource> &emitter) {
    Array records, settings, source_settings = emitter->get("settings");
    for (int i = 0; i < source_settings.size(); ++i) {
        Ref<Resource> r = source_settings[i];
        settings.append(Array::make(r->get("input_name"), r->get("value")));
    }
    records.append(Array::make(emitter->get("stable_id"), emitter->get("display_name"),
                               emitter->get("enabled"), emitter->get("parent_emitter_source"),
                               emitter->get("source_lineage"), settings));
    fingerprint_stages(records, emitter);
    return UtilityFunctions::var_to_str(records).sha256_text();
}
Dictionary NBNativeSourceProgram::build(const Ref<Resource> &effect, const Dictionary &plan,
                                        const Dictionary &parameters) {
    using nbcompiler::equal;
    using nbcompiler::named;
    Dictionary result, stages, curves, math;
    Array errors;
    result["supported"] = false;
    result["errors"] = errors;
    result["stages"] = stages;
    result["curves"] = curves;
    result["cpu_math"] = math;
    result["seed"] = 0;
    result["seed_mode"] = "";
    result["system_update_on_spawn"] = 1;
    Ref<Resource> emitter = plan["source"];
    Dictionary packet = emitter->get("source_rng_contracts");
    if (bool(packet.get("comparison_excluded", false))) {
        errors.append("Emitter was explicitly excluded from this source comparison; no compiled execution "
                      "contract is attached.");
        return result;
    }
    if (packet.is_empty()) {
        errors.append("Source execution requires a paired compiled source contract.");
        return result;
    }
    if (!equal(packet.get("source_hash", ""), effect->get("source_hash")) ||
        !equal(packet.get("document_fingerprint", ""), fingerprint(emitter)))
        errors.append(
            "Compiled RNG contract does not match this effect source/document; re-extract after edits.");
    if (!named(packet.get("seed_mode", ""),
               {"source_deterministic", "source_nondeterministic", "transient_deterministic"}))
        errors.append("Compiled RNG seed provenance is absent.");
    int64_t seed_max = packet.get("instance_seed_max", 0);
    result["instance_seed_max"] = seed_max;
    result["uses_instance_seed"] = false;
    result["uses_unseeded_gpu"] = false;
    result["gpu_spawn_unseeded_calls"] = 0;
    if (packet.has("system_fingerprint") && !equal(packet["system_fingerprint"], system_fingerprint(effect))) {
        // User defaults go through the same compiled parameter bindings as a
        // player override. Verify the exported ABI and every authored stage
        // against the original digest before accepting edited default values.
        String original = system_digest(effect, true);
        if (original.is_empty() || !equal(packet["system_fingerprint"], original))
            errors.append(
                "Source system settings/operations changed after execution export; re-extract its programs.");
    }
    if (packet.get("emitter_seed", Variant()).get_type() != Variant::INT)
        errors.append("Compiled RNG emitter seed must be an explicit integer.");
    else
        result["seed"] = packet["emitter_seed"];
    result["seed_mode"] = packet.get("seed_mode", "").stringify();
    Variant update = packet.get("system_update_on_spawn", Variant());
    if (update.get_type() != Variant::INT ||
        (int64_t(update) != 0 && int64_t(update) != 1 && int64_t(update) != 2))
        errors.append("Source capture must record fx.Niagara.SystemSimulation.UpdateOnSpawn (0/1/2).");
    else
        result["system_update_on_spawn"] = update;
    if (!named(plan["backend"], {"cpu", "gpu_compute"}))
        errors.append("Compiled source RNG requires the native CPU or GPU backend.");
    if (!bool(plan.get("native_system", false)))
        nbsource::bind_lifecycle(effect, plan, packet, result);
    if (bool(plan.get("native_vm_requested", false))) {
        Dictionary vm_stages;
        result["vm_stages"] = vm_stages;
        Array stage_names = Array::make("ParticleSpawn", "ParticleUpdate"), keys = packet.keys();
        for (int i = 0; i < keys.size(); ++i)
            if (String(keys[i]).begins_with("EventHandler:"))
                stage_names.append(keys[i]);
        for (int s = 0; s < stage_names.size(); ++s) {
            String name = stage_names[s];
            Dictionary contract = packet.get(name, Dictionary()),
                       program = contract.get("vm_program", Dictionary());
            Ref<NBNativeVMParticles> adapter;
            adapter.instantiate();
            TypedArray<String> invalid = adapter->configure(
                program, contract.get("cached_default_data_interfaces", Array()), parameters, contract);
            for (int i = 0; i < invalid.size(); ++i)
                errors.append(name + ": " + String(invalid[i]));
            if (!invalid.is_empty())
                continue;
            Dictionary stage;
            stage["program"] = program;
            stage["interfaces"] = contract.get("cached_default_data_interfaces", Array());
            stage["external_parameter_defaults"] = contract.get("external_parameter_defaults", Array());
            stage["external_parameter_defaults_producer"] =
                contract.get("external_parameter_defaults_producer", "");
            if (name.begins_with("EventHandler:")) {
                Dictionary handler = contract.get("event_handler", Dictionary());
                if (!named(handler.get("execution_mode", Variant()), {"SpawnedParticles", "EveryParticle"}) ||
                    handler.get("source_emitter", Variant()).get_type() != Variant::STRING ||
                    handler.get("source_event", Variant()).get_type() != Variant::STRING)
                    errors.append("Unsupported/missing native event handler configuration: " + name);
                for (const char *field :
                     {"spawn_number", "min_spawn_number", "max_events_per_frame", "index"})
                    if (!NBNativeProgramSchema::unsigned_integer(handler.get(field, Variant())))
                        errors.append(String("Invalid native event handler field: ") + field);
                if (handler.get("random_spawn_number", Variant()).get_type() != Variant::BOOL)
                    errors.append("Missing native event random spawn policy.");
                stage["event_handler"] = handler.duplicate(true);
            }
            vm_stages[name] = stage;
            stages[name] = Dictionary();
            curves[name] = Dictionary();
            math[name] = Dictionary();
            Array constants = program["constants"];
            for (int i = 0; i < constants.size(); ++i)
                if (equal(Dictionary(constants[i])["parameter"], String("Engine.Emitter.InstanceSeed")))
                    result["uses_instance_seed"] = true;
        }
        if (bool(result["uses_instance_seed"]) && (seed_max < 1 || seed_max > 2147483647))
            errors.append("InstanceSeed requires the original platform RAND_MAX export.");
        result["supported"] = errors.is_empty();
        return result;
    }
    return nbsource::module_programs(plan, packet, result);
}
Variant NBNativeSourceProgram::invoke(const String &m, const Array &a) {
    using namespace nbsource;
    if (m == "_signed32")
        return signed32(a[0]);
    if (m == "_instance_int_parameter")
        return instance_int_parameter(a[0], a[1]);
    if (m == "_random_count")
        return random_count(a[0]);
    if (m == "_random_leaves")
        return random_leaves(a[0], a[1]);
    if (m == "_curve_leaves")
        return curve_leaves(a[0], a[1], a[2], a[3]);
    if (m == "_curve_identifier")
        return curve_identifier(a[0]);
    if (m == "_curve_sample_owner")
        return curve_owner(a[0]);
    if (m == "_curve_sample_for_path")
        return curve_sample(a[0], a[1], a[2], a[3]);
    if (m == "_compiled_curve")
        return compiled_curve(a[0], a[1], a[2], a[3]);
    if (m == "_bind_curves")
        return bind_curves(a[0], a[1], a[2], a[3]);
    if (m == "_build_module_programs")
        return module_programs(a[0], a[1], a[2]);
    if (m == "_bind_lifecycle") {
        bind_lifecycle(a[0], a[1], a[2], a[3]);
        return Variant();
    }
    if (m == "_bind_emitter_spawn") {
        bind_emitter_spawn(a[0], a[1], a[2]);
        return Variant();
    }
    if (m == "_bind") {
        bind(a[0], a[1], a[2], a[3], a[4], a[5], a[6]);
        return Variant();
    }
    if (m == "_bind_intrinsic") {
        bind_intrinsic(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8]);
        return Variant();
    }
    if (m == "_bind_curl_offset") {
        bind_curl(a[0], a[1], a[2], a[3], a[4], a[5], a[6]);
        return Variant();
    }
    if (m == "_bind_orientation") {
        bind_orientation(a[0], a[1], a[2], a[3], a[4], a[5]);
        return Variant();
    }
    if (m == "_bind_color_adjustment") {
        bind_color(a[0], a[1], a[2], a[3], a[4], a[5], a[6]);
        return Variant();
    }
    ERR_FAIL_V_MSG(Variant(), "Unknown native source program method: " + m);
}
} // namespace godot
