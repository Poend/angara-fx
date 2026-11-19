#include "source_reader.h"
#include "source_program.h"
#include "runtime_control.h"
#include "compiler_frontend.h"
#include "effect_compiler.h"
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/core/class_db.hpp>

namespace godot {
namespace {
constexpr const char *PRODUCER = "NiagaraBridgeSourceLibrary.ExportExecutionSource";
constexpr const char *MAX_SUBSTEPS = "fx.Niagara.SystemSimulation.MaxTickSubsteps";
} // namespace
void NBNativeExecutionSource::_bind_methods() {
    ClassDB::bind_static_method("NBNativeExecutionSource", D_METHOD("discover", "effect"),
                                &NBNativeExecutionSource::discover);
    ClassDB::bind_static_method("NBNativeExecutionSource", D_METHOD("apply", "effect", "path", "parser"),
                                &NBNativeExecutionSource::apply);
    ClassDB::bind_static_method("NBNativeExecutionSource", D_METHOD("package_hash", "value"),
                                &NBNativeExecutionSource::package_hash);
}
String NBNativeExecutionSource::package_hash(const Dictionary &value) {
    using nbcompiler::equal;
    String hash = value.get("source_package_sha256", "").stringify();
    if (hash.length() != 64 || !hash.is_valid_hex_number(false) ||
        !equal(value.get("source_package_unchanged", Variant()), true) ||
        !equal(value.get("source_package_sha256_before", Variant()), hash) ||
        !equal(value.get("source_package_sha256_after", Variant()), hash))
        return "";
    return hash.to_lower();
}
Dictionary NBNativeExecutionSource::discover(const Ref<Resource> &effect) {
    Dictionary result, hashes;
    Array errors;
    result["path"] = "";
    result["errors"] = errors;
    result["input_hashes"] = hashes;
    Array records = Dictionary(effect->get("source_snapshot")).get("native_metadata", Array());
    for (int i = 0; i < records.size(); ++i) {
        Dictionary record = records[i], pairing = record.get("pairing", Dictionary()),
                   asset = pairing.get("asset", Dictionary());
        if (!asset.has("execution_source")) {
            if (bool(asset.get("execution_required", false)))
                errors.append("Original execution export has not completed for this source.");
            continue;
        }
        String relative = asset["execution_source"].stringify(), path = "res://" + relative;
        if (relative.is_empty() || relative.is_absolute_path() || relative.contains(":") ||
            relative.contains("\\") || relative.split("/").has("..")) {
            errors.append("Invalid automatically exported execution path.");
            continue;
        }
        if (!nbcompiler::equal(pairing.get("verified", Variant()), true) ||
            !NBNativeSourceReader::hash_matches(path, asset.get("execution_source_sha256", "").stringify())) {
            errors.append("Automatically exported execution manifest is missing, changed or unpaired.");
            continue;
        }
        String previous = result["path"];
        if (!previous.is_empty() && previous != path) {
            errors.append("Conflicting automatic execution manifests.");
            continue;
        }
        result["path"] = path;
        String manifest = pairing.get("manifest", "").stringify();
        hashes[manifest] = FileAccess::get_sha256(manifest);
    }
    return result;
}
Dictionary NBNativeExecutionSource::apply(const Ref<Resource> &effect, const String &path,
                                          const Ref<Script> &parser) {
    using nbcompiler::equal;
    Dictionary result, hashes;
    Array errors, unsupported;
    result["errors"] = errors;
    result["unsupported"] = unsupported;
    result["input_hashes"] = hashes;
    Variant raw_value = JSON::parse_string(FileAccess::get_file_as_string(path));
    auto original_header = [&](const Dictionary &d) {
        return equal(d.get("schema_version", Variant()), 1) &&
               equal(d.get("producer", Variant()), String(PRODUCER)) &&
               equal(d.get("source_packages_saved", Variant()), false) &&
               equal(d.get("source_settings_overridden", Variant()), false);
    };
    if (raw_value.get_type() != Variant::DICTIONARY || !original_header(raw_value) ||
        Dictionary(raw_value).get("systems", Variant()).get_type() != Variant::ARRAY) {
        errors.append(
            "Expected an original execution export, with no package writes or source setting overrides.");
        return result;
    }
    Dictionary raw = raw_value;
    Array matches, records = Dictionary(effect->get("source_snapshot")).get("native_metadata", Array()),
                   systems = raw["systems"];
    for (int i = 0; i < records.size(); ++i) {
        Dictionary record = records[i];
        String source = Dictionary(record.get("data", Dictionary())).get("system", "").stringify();
        for (int s = 0; s < systems.size(); ++s)
            if (systems[s].get_type() == Variant::DICTIONARY &&
                equal(Dictionary(systems[s]).get("path", Variant()), source)) {
                Dictionary match;
                match["record"] = record;
                match["item"] = systems[s];
                matches.append(match);
            }
    }
    if (matches.size() != 1) {
        errors.append("Execution export needs exactly one matching resolved DFS/native metadata pair.");
        return result;
    }
    Dictionary record = Dictionary(matches[0])["record"], item = Dictionary(matches[0])["item"],
               pairing = record.get("pairing", Dictionary()), asset = pairing.get("asset", Dictionary());
    String package = package_hash(item);
    if (package.is_empty() || package != package_hash(record["data"]) || package != package_hash(asset) ||
        !equal(asset.get("source_path", Variant()), item["path"]) ||
        !equal(asset.get("status", Variant()), String("exported")) ||
        !equal(item.get("status", Variant()), String("exported")) ||
        !equal(item.get("errors", Variant()), Array()))
        errors.append("Execution and DFS metadata do not identify the same unchanged source package.");
    String source_hash = effect->get("source_hash"), source_path = effect->get("source_path");
    if (!equal(pairing.get("verified", Variant()), true) ||
        !equal(pairing.get("resolved_child_graphs", Variant()), true) ||
        !equal(pairing.get("full_inputs", Variant()), true) ||
        !equal(pairing.get("file_sha256", Variant()), source_hash) ||
        !NBNativeSourceReader::hash_matches(source_path, source_hash) ||
        !equal(pairing.get("metadata_file_sha256", Variant()), record.get("sha256", Variant())) ||
        !NBNativeSourceReader::hash_matches(record.get("file", "").stringify(),
                                            record.get("sha256", "").stringify()))
        errors.append(
            "Execution requires current hashes for the complete resolved source and its native metadata.");
    Variant source_value = item.get("execution", Variant());
    bool valid_source = source_value.get_type() == Variant::DICTIONARY;
    Dictionary source;
    if (valid_source) {
        source = source_value;
        valid_source =
            original_header(source) && equal(source.get("status", Variant()), String("exported")) &&
            equal(source.get("source_system", Variant()), item["path"]) &&
            equal(source.get("seed_mode", Variant()), String("source")) &&
            equal(source.get("compilation_mode", Variant()), String("forced_source_flags_unchanged")) &&
            !source.has("seed_overrides") &&
            source.get("compiled_scripts", Variant()).get_type() == Variant::ARRAY &&
            source.get("source_emitter_seed_settings", Variant()).get_type() == Variant::ARRAY &&
            item.get("compiled_file_sha256", Variant()).get_type() == Variant::DICTIONARY;
    }
    if (!valid_source)
        errors.append(
            "Invalid original compiled execution metadata; diagnostic capture inputs are not accepted.");
    if (!errors.is_empty())
        return result;
    String directory = NBNativeSourceReader::relative_file(path.get_base_dir(),
                                                           item.get("directory", "").stringify(), errors);
    Dictionary compiled_hashes = item["compiled_file_sha256"];
    Array keys = compiled_hashes.keys();
    for (int i = 0; i < keys.size(); ++i) {
        String relative = keys[i], file = NBNativeSourceReader::relative_file(directory, relative, errors);
        if (file.is_empty() ||
            !NBNativeSourceReader::hash_matches(file, compiled_hashes[relative].stringify()))
            errors.append("Compiled source text changed: " + relative);
        else
            hashes[file] = compiled_hashes[relative];
    }
    Dictionary cvars = source.get("source_execution_cvars", Dictionary());
    String spawn_mode = cvars.get("fx.Niagara.SystemSimulation.UpdateOnSpawn", "").stringify();
    if (!nbcompiler::named(spawn_mode, {"0", "1", "2"}) ||
        !equal(cvars.get("fx.Niagara.SystemSimulation.SkipTickDeltaSeconds", "").stringify(), String("0")))
        errors.append("Source scheduling requires UpdateOnSpawn=0/1/2 and SkipTickDeltaSeconds=0.");
    Dictionary clock = NBNativeSourceClock::configuration(effect);
    errors.append_array(clock["errors"]);
    if (!equal(source.get("source_system_fixed_tick_enabled", Variant()), clock["enabled"]) ||
        (bool(clock["enabled"]) &&
         NBNativeSourceClock::f32(double(source.get("source_system_fixed_tick_delta_time_seconds", -1.0))) !=
             double(clock["delta"])))
        errors.append("Native source fixed-tick settings differ from the imported document.");
    String cap = cvars.get(MAX_SUBSTEPS, "").stringify();
    if (!cap.is_valid_int() || cap.to_int() < 1)
        errors.append("Source MaxTickSubsteps is missing or invalid.");
    else
        clock["max_substeps"] = cap.to_int();
    if (source.get("source_system_deterministic", Variant()).get_type() != Variant::BOOL ||
        !nbsource::signed32(source.get("source_system_seed", Variant())) ||
        !equal(source["source_system_deterministic"], effect->call("get_input", "Determinism", false)) ||
        int64_t(source["source_system_seed"]) != int64_t(effect->call("get_input", "RandomSeed", 0)))
        errors.append("Source system seed/determinism differs from the imported settings.");
    if (!errors.is_empty())
        return result;
    Dictionary packets, names;
    Array emitters = effect->get("emitters"), seed_settings = source["source_emitter_seed_settings"];
    for (int i = 0; i < emitters.size(); ++i) {
        Ref<Resource> emitter = emitters[i];
        if (!bool(emitter->get("enabled")))
            continue;
        String name = emitter->get("display_name");
        if (names.has(name)) {
            errors.append("Ambiguous emitter name: " + name);
            continue;
        }
        names[name] = true;
        Array seeds;
        for (int s = 0; s < seed_settings.size(); ++s)
            if (seed_settings[s].get_type() == Variant::DICTIONARY &&
                equal(Dictionary(seed_settings[s]).get("emitter", Variant()), name))
                seeds.append(seed_settings[s]);
        if (seeds.size() != 1 ||
            Dictionary(seeds[0]).get("deterministic", Variant()).get_type() != Variant::BOOL ||
            !nbsource::signed32(Dictionary(seeds[0]).get("random_seed", Variant())) ||
            !equal(Dictionary(seeds[0]).get("enabled", Variant()), true)) {
            errors.append("Missing original emitter settings: " + name);
            continue;
        }
        Dictionary setting = seeds[0];
        if (!equal(setting["deterministic"], emitter->call("get_input", "Determinism", false)) ||
            int64_t(setting["random_seed"]) != int64_t(emitter->call("get_input", "RandomSeed", 0))) {
            errors.append("Native emitter settings differ from imported values: " + name);
            continue;
        }
        Dictionary packet, source_clock;
        source_clock["enabled"] = clock["enabled"];
        source_clock["delta"] = clock["delta"];
        source_clock["max_substeps"] = clock["max_substeps"];
        packet["source_hash"] = source_hash;
        packet["document_fingerprint"] = NBNativeSourceProgram::fingerprint(emitter);
        packet["system_fingerprint"] = NBNativeSourceProgram::system_fingerprint(effect);
        packet["seed_mode"] =
            bool(setting["deterministic"]) ? "source_deterministic" : "source_nondeterministic";
        packet["emitter_seed"] = int64_t(setting["random_seed"]);
        if (source.has("grid3d_settings"))
            packet["grid3d_settings"] = source["grid3d_settings"];
        if (setting.has("max_instance_count")) {
            Variant count = setting["max_instance_count"];
            if (!NBNativeProgramSchema::unsigned_integer(count)) {
                errors.append("Invalid Niagara maximum instance count: " + name);
                continue;
            }
            packet["max_instance_count"] = int64_t(count);
        }
        packet["system_update_on_spawn"] = spawn_mode.to_int();
        packet["source_clock"] = source_clock;
        packet["source_package_sha256"] = package;
        packet["source_system"] = item["path"];
        packet["execution_manifest_sha256"] = FileAccess::get_sha256(path);
        if (nbsource::signed32(source.get("source_platform_rand_max", Variant())) &&
            int64_t(source["source_platform_rand_max"]) > 0)
            packet["instance_seed_max"] = int64_t(source["source_platform_rand_max"]);
        packet.merge(NBNativeSourceReader::stages(emitter, source, compiled_hashes, directory, errors,
                                                  unsupported, parser));
        packets[emitter->get("stable_id")] = packet;
    }
    if (!errors.is_empty())
        return result;
    for (int i = 0; i < emitters.size(); ++i) {
        Ref<Resource> emitter = emitters[i];
        emitter->set("source_rng_contracts", packets.get(emitter->get("stable_id"), Dictionary()));
    }
    hashes[path] = FileAccess::get_sha256(path);
    Dictionary programs = NBNativeSourceReader::system_stages(source, compiled_hashes, directory, errors);
    if (!errors.is_empty())
        return result;
    Dictionary execution;
    execution["schema_version"] = 1;
    execution["producer"] = PRODUCER;
    execution["source_system"] = item["path"];
    execution["manifest_path"] = path;
    execution["source_package_sha256"] = package;
    execution["source_system_deterministic"] = source["source_system_deterministic"];
    execution["source_system_seed"] = int64_t(source["source_system_seed"]);
    execution["source_settings_overridden"] = false;
    execution["input_hashes"] = hashes.duplicate();
    execution["unsupported"] = unsupported.duplicate();
    if (!programs.is_empty())
        execution["system_programs"] = programs;
    execution["source_platform_rand_max"] = int64_t(source.get("source_platform_rand_max", 0));
    effect->set("source_execution", execution);
    return result;
}
} // namespace godot
