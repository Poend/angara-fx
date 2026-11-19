#include "source_reader.h"
#include "gpu_particle_reader.h"
#include "compiler_frontend.h"
#include "effect_compiler.h"
#include "value_evaluator.h"
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/reg_ex.hpp>
#include <godot_cpp/classes/reg_ex_match.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <cmath>

namespace godot {
namespace {
bool table(const Variant &value) {
    if (value.get_type() != Variant::ARRAY)
        return false;
    Array rows = value;
    for (int i = 0; i < rows.size(); ++i)
        if (rows[i].get_type() != Variant::DICTIONARY)
            return false;
    return true;
}
Dictionary extract(const Ref<Script> &parser, const String &text, const String &entry) {
    Ref<RefCounted> instance = parser->call("new");
    return instance->call("extract", text, entry);
}
} // namespace
void NBNativeSourceReader::_bind_methods() {
    ClassDB::bind_static_method("NBNativeSourceReader", D_METHOD("hash_matches", "path", "expected"),
                                &NBNativeSourceReader::hash_matches);
    ClassDB::bind_static_method("NBNativeSourceReader",
                                D_METHOD("relative_file", "directory", "relative", "errors"),
                                &NBNativeSourceReader::relative_file);
    ClassDB::bind_static_method(
        "NBNativeSourceReader",
        D_METHOD("vm_program", "script", "hashes", "directory", "errors", "analysis_errors"),
        &NBNativeSourceReader::vm_program);
    ClassDB::bind_static_method("NBNativeSourceReader",
                                D_METHOD("system_stages", "manifest", "hashes", "directory", "errors"),
                                &NBNativeSourceReader::system_stages);
    ClassDB::bind_static_method("NBNativeSourceReader",
                                D_METHOD("stages", "emitter", "manifest", "hashes", "directory", "errors",
                                         "analysis_errors", "parser"),
                                &NBNativeSourceReader::stages);
    ClassDB::bind_static_method(
        "NBNativeSourceReader",
        D_METHOD("system_spawn", "manifest", "hashes", "directory", "errors", "parser"),
        &NBNativeSourceReader::system_spawn);
}
bool NBNativeSourceReader::hash_matches(const String &path, const String &expected) {
    return expected.length() == 64 && FileAccess::file_exists(path) &&
           FileAccess::get_sha256(path) == expected.to_lower();
}
String NBNativeSourceReader::relative_file(const String &directory, const String &relative, Array errors) {
    String normalized = relative.replace("\\", "/");
    if (normalized.is_empty() || normalized.is_absolute_path() || normalized.contains(":") ||
        normalized.split("/").has("..")) {
        errors.append("Execution artifact path must stay inside its capture directory: " + relative);
        return "";
    }
    return directory.path_join(normalized);
}
Dictionary NBNativeSourceReader::vm_program(const Dictionary &script, const Dictionary &hashes,
                                            const String &directory, Array errors, Array analysis_errors) {
    String relative = script.get("vm_program", ""), path = relative_file(directory, relative, errors);
    if (path.is_empty() || !hash_matches(path, hashes.get(relative, ""))) {
        errors.append("Optimized VM program hash mismatch: " + relative);
        return Dictionary();
    }
    Variant raw = JSON::parse_string(FileAccess::get_file_as_string(path));
    TypedArray<String> invalid = NBNativeProgramSchema::validate(raw);
    if (!invalid.is_empty()) {
        for (int i = 0; i < invalid.size(); ++i)
            errors.append(relative + ": " + String(invalid[i]));
        return Dictionary();
    }
    Dictionary result = raw;
    // Godot's text resource writer prints -0.0 as 0.0. VectorVM can observe its sign.
    Array constants = result.get("constants", Array());
    for (int i = 0; i < constants.size(); ++i) {
        Dictionary row = constants[i];
        Variant value = row.get("value", Variant());
        if (row.get("type", "") == Variant("float32") && value.get_type() == Variant::FLOAT &&
            double(value) == 0.0 && std::signbit(double(value)))
            row["value"] = "-0";
    }
    if (bool(result["available"]) && !bool(result["complete"]))
        analysis_errors.append("Native optimized program is incomplete: " + relative + ": " +
                               result["errors"].stringify());
    return result;
}
Dictionary NBNativeSourceReader::system_stages(const Dictionary &manifest, const Dictionary &hashes,
                                               const String &directory, Array errors) {
    Dictionary result;
    Array scripts = manifest["compiled_scripts"];
    for (const char *name : {"SystemSpawn", "SystemUpdate"}) {
        Array matches;
        for (int i = 0; i < scripts.size(); ++i)
            if (scripts[i].get_type() == Variant::DICTIONARY) {
                Dictionary d = scripts[i];
                if (nbcompiler::equal(d.get("usage", Variant()), String(name) + "Script") &&
                    nbcompiler::equal(d.get("emitter", ""), String("")))
                    matches.append(d);
            }
        if (matches.size() != 1 || !Dictionary(matches[0]).has("external_parameter_defaults_producer"))
            return Dictionary();
        Dictionary script = matches[0];
        Array unsupported;
        Dictionary stage;
        stage["program"] = vm_program(script, hashes, directory, errors, unsupported);
        stage["script_path"] = script.get("script_path", "");
        stage["interfaces"] = Array(script.get("cached_default_data_interfaces", Array())).duplicate(true);
        for (const char *field :
             {"external_parameter_defaults_producer", "external_parameter_defaults", "instance_input_layout",
              "instance_parameter_defaults", "instance_input_initialization_producer"})
            stage[field] = script.get(field, Variant());
        result[name] = stage;
    }
    return result;
}
Dictionary NBNativeSourceReader::stages(const Ref<Resource> &emitter, const Dictionary &manifest,
                                        const Dictionary &hashes, const String &directory, Array errors,
                                        Array analysis_errors, const Ref<Script> &parser) {
    using nbcompiler::equal;
    Dictionary packet;
    String name = emitter->get("display_name");
    bool gpu = nbcompiler::named(NBNativeValues::enum_name(emitter->call("get_input", "SimTarget", "CPU")),
                                 {"GPU", "GPUComputeSim"});
    String spawn_mode = NBNativeValues::enum_name(
        emitter->call("get_input", "InterpolatedSpawnMode",
                      emitter->call("get_input", "InterpolatedSpawning", "RunUpdateScript")));
    Array scripts = manifest["compiled_scripts"];
    for (const char *stage_key : {"ParticleSpawn", "ParticleUpdate"}) {
        String stage_name = stage_key,
               usage = gpu ? "ParticleGPUComputeScript"
                       : stage_name == "ParticleSpawn"
                           ? (spawn_mode == "NoInterpolation" ? "ParticleSpawnScript"
                                                              : "ParticleSpawnScriptInterpolated")
                           : "ParticleUpdateScript";
        Array matches;
        for (int i = 0; i < scripts.size(); ++i)
            if (scripts[i].get_type() == Variant::DICTIONARY) {
                Dictionary d = scripts[i];
                if (equal(d.get("emitter", "").stringify(), name) &&
                    equal(d.get("usage", "").stringify(), usage))
                    matches.append(d);
            }
        if (matches.size() != 1 ||
            !bool(Dictionary(matches[0]).get(gpu ? "gpu_hlsl_available" : "hlsl_available", false))) {
            errors.append("Exactly one available emitted native script is required for " + name + "/" +
                          stage_name);
            continue;
        }
        Dictionary script = matches[0];
        String relative = script.get(gpu ? "gpu_hlsl" : "hlsl", ""),
               path = relative_file(directory, relative, errors);
        if (path.is_empty())
            continue;
        if (!hash_matches(path, hashes.get(relative, ""))) {
            errors.append("Emitted script hash mismatch: " + relative);
            continue;
        }
        String entry = stage_name == "ParticleSpawn" && (gpu || spawn_mode != "NoInterpolation")
                           ? "SimulateMapSpawn"
                       : gpu ? "SimulateMapUpdate"
                             : "Simulate";
        Dictionary contract = extract(parser, FileAccess::get_file_as_string(path), entry);
        contract["source_backend"] = gpu ? "gpu_compute" : "cpu";
        if (!gpu && bool(script.get("vm_program_available", false)))
            contract["vm_program"] = vm_program(script, hashes, directory, errors, analysis_errors);
        if (!gpu && bool(script.get("vm_assembly_available", false))) {
            String vm_relative = script.get("vm_assembly", ""),
                   vm_path = relative_file(directory, vm_relative, errors);
            if (vm_path.is_empty() || !hash_matches(vm_path, hashes.get(vm_relative, "")))
                errors.append("Native VM assembly hash mismatch: " + vm_relative);
            else {
                Dictionary math = NBNativeProgramSchema::extract_math(
                    FileAccess::get_file_as_string(path), FileAccess::get_file_as_string(vm_path));
                contract["vm_math"] = math;
                if (!bool(math["complete"]))
                    analysis_errors.append_array(math["errors"]);
            }
        }
        if (!bool(contract["complete"])) {
            Array diagnostics = contract["diagnostics"];
            for (int i = 0; i < diagnostics.size(); ++i)
                analysis_errors.append(name + "/" + stage_name + ": " +
                                       Dictionary(diagnostics[i])["message"].stringify());
        }
        contract["compiled_script_path"] = relative;
        for (const char *field :
             {"cached_default_data_interfaces", "vm_external_functions", "external_parameter_defaults"}) {
            Variant rows = script.get(field, Array());
            if (!table(rows)) {
                errors.append("Malformed compiled script metadata: " + relative + "/" + field);
                contract[field] = Array();
            } else
                contract[field] = Array(rows).duplicate(true);
        }
        contract["external_parameter_defaults_producer"] =
            script.get("external_parameter_defaults_producer", "");
        if (gpu && contract.has("indexed_reader_modules")) {
            contract["gpu_readers"] = nbreader::bind(contract["indexed_reader_modules"],
                emitter->call("get_stage",stage_name),contract["cached_default_data_interfaces"]);
            contract.erase("indexed_reader_modules");
        }
        packet[stage_name] = contract;
    }
    Array source_stages = emitter->get("stages");
    for (int s = 0; s < source_stages.size(); ++s) {
        Ref<Resource> stage = source_stages[s];
        String stage_name = stage->get("stage_name");
        if (!stage_name.begins_with("EventHandler:"))
            continue;
        int64_t index = stage_name.trim_prefix("EventHandler:").to_int();
        Array matches;
        for (int i = 0; i < scripts.size(); ++i)
            if (scripts[i].get_type() == Variant::DICTIONARY) {
                Dictionary d = scripts[i];
                if (equal(d.get("emitter", Variant()), name) &&
                    equal(d.get("usage", Variant()), String("ParticleEventScript")) &&
                    int64_t(Dictionary(d.get("event_handler", Dictionary())).get("index", -1)) == index)
                    matches.append(d);
            }
        if (gpu || matches.size() != 1) {
            errors.append("Exactly one native CPU event script with handler metadata is required for " +
                          name + "/" + stage_name);
            continue;
        }
        Dictionary script = matches[0], contract;
        contract["vm_program"] = vm_program(script, hashes, directory, errors, analysis_errors);
        contract["event_handler"] = Dictionary(script["event_handler"]).duplicate(true);
        contract["cached_default_data_interfaces"] =
            Array(script.get("cached_default_data_interfaces", Array())).duplicate(true);
        contract["script_path"] = script.get("script_path", "");
        contract["usage_id"] = script.get("usage_id", "");
        contract["external_parameter_defaults"] = script.get("external_parameter_defaults", Array());
        contract["external_parameter_defaults_producer"] =
            script.get("external_parameter_defaults_producer", "");
        packet[stage_name] = contract;
    }
    Ref<Resource> spawn = emitter->call("get_stage", "EmitterSpawn");
    if (spawn.is_valid()) {
        Array operations = spawn->get("operations");
        bool assignments = false;
        for (int i = 0; i < operations.size(); ++i) {
            Ref<Resource> op = operations[i];
            if (bool(op->get("enabled")) && equal(op->get("operation_kind"), String("assignment")))
                assignments = true;
        }
        if (assignments)
            packet["EmitterSpawn"] =
                system_spawn(manifest, hashes, directory, errors, parser).duplicate(true);
    }
    return packet;
}
Dictionary NBNativeSourceReader::system_spawn(const Dictionary &manifest, const Dictionary &hashes,
                                              const String &directory, Array errors,
                                              const Ref<Script> &parser) {
    Array scripts = manifest["compiled_scripts"], matches;
    for (int i = 0; i < scripts.size(); ++i)
        if (scripts[i].get_type() == Variant::DICTIONARY) {
            Dictionary d = scripts[i];
            if (nbcompiler::equal(d.get("usage", Variant()), String("SystemSpawnScript")) &&
                d.get("emitter", "").stringify().is_empty())
                matches.append(d);
        }
    if (matches.size() != 1 || !bool(Dictionary(matches[0]).get("hlsl_available", false))) {
        errors.append("Emitter-spawn comparison requires one emitted SystemSpawnScript.");
        return Dictionary();
    }
    Dictionary script = matches[0];
    String relative = script.get("hlsl", ""), path = relative_file(directory, relative, errors);
    if (path.is_empty() || !hash_matches(path, hashes.get(relative, ""))) {
        errors.append("System-spawn script hash mismatch: " + relative);
        return Dictionary();
    }
    String hlsl = FileAccess::get_file_as_string(path);
    Dictionary contract = extract(parser, hlsl, "Simulate");
    contract["compiled_script_path"] = relative;
    contract["source_backend"] = "cpu";
    if (bool(script.get("vm_program_available", false)))
        contract["vm_program"] = vm_program(script, hashes, directory, errors, errors);
    contract["instance_input_initialization_producer"] =
        script.get("instance_input_initialization_producer", "");
    for (const char *field : {"instance_input_layout", "instance_parameter_defaults"}) {
        Variant rows = script.get(field, Array());
        if (!table(rows)) {
            errors.append(String("Malformed system-spawn input metadata: ") + field);
            return Dictionary();
        }
        contract[field] = Array(rows).duplicate(true);
    }
    Array bindings;
    contract["instance_int_bindings"] = bindings;
    Ref<RegEx> pattern = RegEx::create_from_string("(?m)^[ \\t]*(Context\\.Map\\.[A-Za-z_][A-Za-z0-9_.]*)[ "
                                                   "\\t]*=[ \\t]*InputDataInt\\(1,[ \\t]*([0-9]+)\\);");
    TypedArray<RegExMatch> found = pattern->search_all(hlsl);
    for (int i = 0; i < found.size(); ++i) {
        Ref<RegExMatch> match = found[i];
        Dictionary binding;
        binding["expression"] = match->get_string(1);
        binding["int32_start"] = match->get_string(2).to_int();
        bindings.append(binding);
    }
    return contract;
}
} // namespace godot
