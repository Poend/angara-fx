#include "material_programs.h"
#include "material_graph_hash.h"
#include "raster_buffers.h"
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <mutex>
#include <unordered_map>
namespace godot {
namespace {
using namespace nbmaterial;
constexpr const char *program_meta = "niagara_bridge_material_program",
                     *system_meta = "niagara_bridge_system_program";
PackedStringArray fields(const Ref<Resource> &graph) {
    PackedStringArray names = String("schema_version,source_material,root_material,source_manifest,source_hashes,material_"
                  "properties,nodes,outputs,functions,parameters,graph_dependencies,texture_dependencies,"
                  "diagnostics")
        .split(",");
    Dictionary custom = graph->get("custom_shader_functions");
    if (!custom.is_empty()) names.append("custom_shader_functions");
    return names;
}
struct Entry {
    int64_t stamp = 0;
    TypedArray<String> errors;
};
std::mutex program_mutex, system_mutex;
std::unordered_map<uint64_t, Entry> program_cache, system_cache;
struct ShaderCodeEntry {
    // An exact COW string snapshot, not a retained Shader or a hash-only stamp.
    String code, sha256;
    int64_t hash = 0;
    bool hash_ready = false, sha256_ready = false;
};
std::mutex shader_code_mutex;
std::unordered_map<uint64_t, ShaderCodeEntry> shader_code_cache;
uint64_t shader_hash_computations = 0, shader_sha256_computations = 0, shader_cache_hits = 0;
ShaderCodeEntry &shader_entry(const Ref<Shader> &shader, const String &code) {
    uint64_t id = shader->get_instance_id();
    auto found = shader_code_cache.find(id);
    if (found == shader_code_cache.end()) {
        if (shader_code_cache.size() >= 512)
            shader_code_cache.clear();
        found = shader_code_cache.emplace(id, ShaderCodeEntry()).first;
    }
    if (found->second.code != code) {
        found->second = ShaderCodeEntry();
        found->second.code = code;
    }
    return found->second;
}
using GraphStamps = std::unordered_map<uint64_t, int64_t>;
int64_t graph_validation_stamp(const Ref<Resource> &graph, GraphStamps &memo) {
    uint64_t id = graph->get_instance_id();
    Variant script_value = graph->get_script();
    Ref<Script> script = script_value;
    const bool plain = script.is_valid() && script->get_path() == "res://addons/niagara_bridge/materials/conversion/material_graph.gd";
    auto *revision = NBNativeEffectRevision::current_revision;
    auto &stamps = plain && revision ? revision->saved_validation_stamps : memo;
    auto found = stamps.find(id);
    if (found != stamps.end())
        return found->second;
    Array values = Array::make(script_value);
    PackedStringArray names = fields(graph);
    int64_t functions_index = -1;
    bool large_nodes = false;
    for (int64_t i = 0; i < names.size(); ++i) {
        const String name = names[i];
        Variant value = graph->get(name);
        if (name == "nodes" && value.get_type() == Variant::ARRAY) large_nodes = Array(value).size() >= 16;
        if (name == "functions" && value.get_type() == Variant::DICTIONARY && !Dictionary(value).is_empty())
            functions_index = values.size();
        values.append(value);
    }
    int64_t stamp = graph_array_hash(values, plain && large_nodes ? functions_index : -1);
    stamps.emplace(id, stamp);
    return stamp;
}
// Private cache stamps may group hashes; public fingerprints and sealed SHA256
// remain unchanged. Authoring and public validation read the current graph.
int64_t validation_stamp(const Ref<ShaderMaterial> &material, bool system, GraphStamps &memo) {
    if (material.is_null())
        return Variant().hash();
    if (material->get_shader().is_null())
        return Variant(material).hash();
    Variant raw = material->get_meta(system ? system_meta : program_meta, Dictionary());
    Array values = Array::make(material->get_shader(),
                               NBNativeMaterialPrograms::shader_code_hash(material->get_shader()),raw.get_type());
    if (raw.get_type() == Variant::DICTIONARY) {
        Dictionary data = raw;
        values.append(data.get("schema_version",Variant()));
        values.append(data.get("shader_sha256",Variant()));
        if (system) {
            values.append(data.get("contexts",Variant()));
            values.append(data.get("aliases",Variant()));
            values.append(data.get("defaults",Variant()).get_type());
            values.append(data.get("context_sha256",Variant()));
            Variant items = data.get("contexts", Variant());
            if (items.get_type() == Variant::ARRAY) {
                Array contexts = items;
                for (int64_t i = 0; i < contexts.size(); ++i) {
                    Variant item = contexts[i];
                    if (item.get_type() != Variant::DICTIONARY)
                        continue;
                    Ref<ShaderMaterial> source = Dictionary(item).get("material", Variant());
                    if (source.is_valid())
                        values.append(validation_stamp(source, false, memo));
                }
            }
        } else {
            values.append(data.get("compiler_abi",Variant()));
            values.append(data.get("graph_sha256",Variant()));
            values.append(data.get("graph",Variant()));
            for(const char *field:{"defaults","native_texture_formats","bindings"}) values.append(data.get(field,Variant()).get_type());
            Ref<Resource> graph = data.get("graph", Variant());
            if (graph.is_valid())
                values.append(graph_validation_stamp(graph, memo));
        }
    }
    return values.hash();
}
Dictionary live_values(const Ref<ShaderMaterial> &material, const StringName &metadata) {
    Dictionary data = material->get_meta(metadata, Dictionary()),
               defaults = data.get("defaults", Dictionary()), values;
    Array keys = defaults.keys();
    for (int64_t i = 0; i < keys.size(); ++i) {
        String key = keys[i];
        Variant value = material->get_shader_parameter(key);
        values[key] = value.get_type() == Variant::NIL ? defaults[key] : value;
    }
    return values;
}
} // namespace
int64_t NBNativeMaterialPrograms::shader_code_hash(const Ref<Shader> &shader) {
    if (shader.is_null())
        return String().hash();
    String code = shader->get_code();
    std::lock_guard<std::mutex> guard(shader_code_mutex);
    ShaderCodeEntry &entry = shader_entry(shader, code);
    if (!entry.hash_ready) {
        entry.hash = code.hash();
        entry.hash_ready = true;
        ++shader_hash_computations;
    } else
        ++shader_cache_hits;
    return entry.hash;
}
String NBNativeMaterialPrograms::shader_code_sha256(const Ref<Shader> &shader) {
    if (shader.is_null())
        return String().sha256_text();
    String code = shader->get_code();
    std::lock_guard<std::mutex> guard(shader_code_mutex);
    ShaderCodeEntry &entry = shader_entry(shader, code);
    if (!entry.sha256_ready) {
        entry.sha256 = code.sha256_text();
        entry.sha256_ready = true;
        ++shader_sha256_computations;
    } else
        ++shader_cache_hits;
    return entry.sha256;
}
Dictionary NBNativeMaterialPrograms::shader_cache_statistics() {
    std::lock_guard<std::mutex> guard(shader_code_mutex);
    return nbdocument::dict({{"entries", int64_t(shader_code_cache.size())},
                           {"hash_computations", int64_t(shader_hash_computations)},
                           {"sha256_computations", int64_t(shader_sha256_computations)},
                           {"hits", int64_t(shader_cache_hits)}});
}
Dictionary NBNativeMaterialPrograms::seal(const Dictionary &result) {
    Ref<Resource> graph = result["graph"];
    return nbdocument::dict({{"schema_version", 1},
                           {"compiler_abi", 1},
                           {"shader_sha256", nbmaterial::text(result, "shader_code").sha256_text()},
                           {"graph", graph},
                           {"graph_sha256", graph_hash(graph)},
                           {"defaults", Dictionary(result["uniforms"]).duplicate()},
                           {"bindings", Array(result["parameter_bindings"]).duplicate(true)},
                           {"native_texture_formats",
                            Dictionary(result.get("native_texture_formats", Dictionary())).duplicate(true)}});
}
String NBNativeMaterialPrograms::graph_hash(const Ref<Resource> &graph) {
    Dictionary values;
    PackedStringArray names = fields(graph);
    for (int64_t i = 0; i < names.size(); ++i)
        values[names[i]] = graph->get(names[i]);
    return UtilityFunctions::var_to_str(values).sha256_text();
}
TypedArray<String> NBNativeMaterialPrograms::validate(const Ref<ShaderMaterial> &material,
                                                      const Ref<Script> &schema) {
    TypedArray<String> errors;
    if (material.is_null() || material->get_shader().is_null()) {
        errors.append("Composition requires a ShaderMaterial with a shader.");
        return errors;
    }
    Variant raw = material->get_meta(program_meta, Dictionary());
    Dictionary program = raw.get_type() == Variant::DICTIONARY ? Dictionary(raw) : Dictionary();
    if (raw.get_type() != Variant::DICTIONARY ||
        !nbdocument::equal(program.get("schema_version", Variant()), 1) ||
        !nbdocument::equal(program.get("compiler_abi", Variant()), 1)) {
        errors.append("Material has no supported sealed graph program; regenerate it through the converter.");
        return errors;
    }
    if (program.get("shader_sha256", Variant()) != Variant(shader_code_sha256(material->get_shader())))
        errors.append("Shader code changed after graph sealing; composition cannot use stale graph data.");
    Ref<Resource> graph = program.get("graph", Variant());
    if (graph.is_null() || graph->get_script() != Variant(schema))
        errors.append("Sealed program has no supported MaterialGraph resource.");
    else if (Variant(graph_hash(graph)) != program.get("graph_sha256", Variant()))
        errors.append("Sealed graph content changed; regenerate the shader and program together.");
    for (const char *field : {"defaults", "native_texture_formats"})
        if (program.get(field, Variant()).get_type() != Variant::DICTIONARY)
            errors.append(String("Malformed sealed program field: ") + field);
    if (program.get("bindings", Variant()).get_type() != Variant::ARRAY)
        errors.append("Malformed sealed parameter bindings.");
    return errors;
}
int64_t NBNativeMaterialPrograms::fingerprint(const Ref<ShaderMaterial> &material) {
    if (material.is_null())
        return Variant().hash();
    if (material->get_shader().is_null())
        return Variant(material).hash();
    Variant program = material->get_meta(program_meta, Dictionary());
    Array values =
        nbmaterial::list({material->get_shader(), shader_code_hash(material->get_shader()), program});
    if (program.get_type() == Variant::DICTIONARY) {
        Ref<Resource> graph = Dictionary(program).get("graph", Variant());
        if (graph.is_valid()) {
            values.append(graph->get_script());
            PackedStringArray names = fields(graph);
            for (int64_t i = 0; i < names.size(); ++i)
                values.append(graph->get(names[i]));
        }
    }
    return values.hash();
}
TypedArray<String> NBNativeMaterialPrograms::cached_validate(const Ref<ShaderMaterial> &material,
                                                             const Ref<Script> &schema) {
    if (material.is_null())
        return validate(material, schema);
    uint64_t id = material->get_instance_id();
    GraphStamps graphs;
    int64_t stamp = Array::make(validation_stamp(material, false, graphs),schema).hash();
    std::lock_guard<std::mutex> lock(program_mutex);
    auto found = program_cache.find(id);
    if (found == program_cache.end() || found->second.stamp != stamp) {
        if (program_cache.size() >= 256)
            program_cache.clear();
        Entry entry;
        entry.stamp = stamp;
        entry.errors = validate(material, schema);
        program_cache[id] = entry;
        found = program_cache.find(id);
    }
    return found->second.errors.duplicate();
}
Dictionary NBNativeMaterialPrograms::current_values(const Ref<ShaderMaterial> &material) {
    return live_values(material, program_meta);
}
Dictionary NBNativeMaterialPrograms::system_values(const Ref<ShaderMaterial> &material) {
    return live_values(material, system_meta);
}
Dictionary NBNativeMaterialPrograms::seal_system(const String &shader, const TypedArray<Dictionary> &contexts,
                                                 const TypedArray<Dictionary> &aliases,
                                                 const Dictionary &defaults) {
    TypedArray<Dictionary> sources;
    for (int64_t i = 0; i < contexts.size(); ++i) {
        Dictionary context = contexts[i];
        Ref<ShaderMaterial> original = context["material"], snapshot;
        snapshot.instantiate();
        snapshot->set_shader(original->get_shader());
        snapshot->set_meta(program_meta, Dictionary(original->get_meta(program_meta)).duplicate(true));
        Dictionary values = current_values(original);
        Array names = values.keys();
        for (int64_t j = 0; j < names.size(); ++j)
            snapshot->set_shader_parameter(names[j], values[names[j]]);
        sources.append(nbdocument::dict({{"material", snapshot},
                                       {"renderer_kind", int64_t(context["renderer_kind"])},
                                       {"subimage_size", context["subimage_size"]},
                                       {"dynamic_mask", int64_t(context.get("dynamic_mask", 0))},
                                       {"subimage_blend", bool(context.get("subimage_blend", false))}}));
    }
    return nbdocument::dict({{"schema_version", 1},
                           {"shader_sha256", shader.sha256_text()},
                           {"contexts", sources},
                           {"aliases", aliases.duplicate(true)},
                           {"context_sha256", context_hash(sources, aliases)},
                           {"defaults", defaults.duplicate()}});
}
String NBNativeMaterialPrograms::context_hash(const Array &contexts, const Array &aliases) {
    Array descriptors;
    for (int64_t i = 0; i < contexts.size(); ++i) {
        Dictionary context = contexts[i];
        Ref<ShaderMaterial> material = context["material"];
        Dictionary program = material->get_meta(program_meta);
        descriptors.append(
            nbmaterial::list({program.get("shader_sha256", Variant()), program.get("graph_sha256", Variant()),
                              context["renderer_kind"], context["subimage_size"], context["dynamic_mask"],
                              context["subimage_blend"]}));
    }
    return UtilityFunctions::var_to_str(nbmaterial::list({descriptors, aliases})).sha256_text();
}
TypedArray<String> NBNativeMaterialPrograms::validate_system(const Ref<ShaderMaterial> &material,
                                                             const Ref<Script> &schema) {
    TypedArray<String> errors;
    if (material.is_null() || material->get_shader().is_null()) {
        errors.append("System program requires a shader.");
        return errors;
    }
    Variant raw = material->get_meta(system_meta, Dictionary());
    Dictionary data = raw.get_type() == Variant::DICTIONARY ? Dictionary(raw) : Dictionary();
    if (raw.get_type() != Variant::DICTIONARY || !nbdocument::equal(data.get("schema_version", Variant()), 1) ||
        data.get("contexts", Variant()).get_type() != Variant::ARRAY ||
        data.get("aliases", Variant()).get_type() != Variant::ARRAY ||
        data.get("defaults", Variant()).get_type() != Variant::DICTIONARY) {
        errors.append("Malformed sealed system program.");
        return errors;
    }
    if (data.get("shader_sha256", Variant()) != Variant(shader_code_sha256(material->get_shader())))
        errors.append("Composed shader changed after sealing.");
    Array contexts = data["contexts"], aliases = data["aliases"];
    if (contexts.is_empty() || contexts.size() != aliases.size()) {
        errors.append("System program context count mismatch.");
        return errors;
    }
    for (int64_t i = 0; i < contexts.size(); ++i) {
        Variant item = contexts[i];
        Dictionary context = item.get_type() == Variant::DICTIONARY ? Dictionary(item) : Dictionary();
        Ref<ShaderMaterial> source = context.get("material", Variant());
        if (item.get_type() != Variant::DICTIONARY || source.is_null() ||
            aliases[i].get_type() != Variant::DICTIONARY) {
            errors.append("Malformed system source context.");
            continue;
        }
        errors.append_array(cached_validate(source, schema));
        Variant kind = context.get("renderer_kind", Variant()),
                grid = context.get("subimage_size", Variant());
        bool valid_kind = nbdocument::equal(kind, 1) || nbdocument::equal(kind, 2) || nbdocument::equal(kind, 3);
        if (!valid_kind || grid.get_type() != Variant::VECTOR2 ||
            context.get("dynamic_mask", Variant()).get_type() != Variant::INT ||
            context.get("subimage_blend", Variant()).get_type() != Variant::BOOL)
            errors.append("Malformed system renderer context.");
    }
    if (errors.is_empty() &&
        data.get("context_sha256", Variant()) != Variant(context_hash(contexts, aliases)))
        errors.append("System source contexts changed after sealing.");
    return errors;
}
int64_t NBNativeMaterialPrograms::fingerprint_system(const Ref<ShaderMaterial> &material) {
    if (material.is_null())
        return Variant().hash();
    if (material->get_shader().is_null())
        return Variant(material).hash();
    Variant raw = material->get_meta(system_meta, Dictionary());
    Array values = nbmaterial::list({material->get_shader(), shader_code_hash(material->get_shader()), raw});
    if (raw.get_type() == Variant::DICTIONARY) {
        Variant items = Dictionary(raw).get("contexts", Variant());
        if (items.get_type() == Variant::ARRAY) {
            Array contexts = items;
            for (int64_t i = 0; i < contexts.size(); ++i) {
                Variant item = contexts[i];
                if (item.get_type() != Variant::DICTIONARY)
                    continue;
                Ref<ShaderMaterial> source = Dictionary(item).get("material", Variant());
                if (source.is_valid())
                    values.append(fingerprint(source));
            }
        }
    }
    return values.hash();
}
TypedArray<String> NBNativeMaterialPrograms::cached_validate_system(const Ref<ShaderMaterial> &material,
                                                                    const Ref<Script> &schema) {
    if (material.is_null())
        return validate_system(material, schema);
    uint64_t id = material->get_instance_id();
    GraphStamps graphs;
    int64_t stamp = Array::make(validation_stamp(material, true, graphs),schema).hash();
    std::lock_guard<std::mutex> lock(system_mutex);
    auto found = system_cache.find(id);
    if (found == system_cache.end() || found->second.stamp != stamp) {
        if (system_cache.size() >= 256)
            system_cache.clear();
        Entry entry;
        entry.stamp = stamp;
        entry.errors = validate_system(material, schema);
        system_cache[id] = entry;
        found = system_cache.find(id);
    }
    return found->second.errors.duplicate();
}
TypedArray<Dictionary> NBNativeMaterialPrograms::source_contexts(const Ref<ShaderMaterial> &material) {
    Dictionary data = material->get_meta(system_meta), live = system_values(material);
    Array contexts = data["contexts"], aliases = data["aliases"];
    TypedArray<Dictionary> result;
    for (int64_t i = 0; i < contexts.size(); ++i) {
        Dictionary context = Dictionary(contexts[i]).duplicate();
        Ref<ShaderMaterial> source = context["material"], copy;
        copy.instantiate();
        copy->set_shader(source->get_shader());
        copy->set_meta(program_meta, source->get_meta(program_meta));
        Dictionary values = current_values(source);
        Array keys = values.keys();
        for (int64_t j = 0; j < keys.size(); ++j)
            copy->set_shader_parameter(keys[j], values[keys[j]]);
        Dictionary mapping = aliases[i];
        keys = mapping.keys();
        for (int64_t j = 0; j < keys.size(); ++j)
            copy->set_shader_parameter(mapping[keys[j]], live[keys[j]]);
        for (const char *name :
             {"simulation_time", "nb_source_exposure", "nb_mesh_bounds_min", "nb_mesh_bounds_size","nb_object_world_center"})
            if (live.has(name))
                copy->set_shader_parameter(name, live[name]);
        context["material"] = copy;
        result.append(context);
    }
    return result;
}
void NBNativeMaterialPrograms::clear_cache() {
    std::scoped_lock lock(program_mutex, system_mutex, shader_code_mutex);
    program_cache.clear();
    system_cache.clear();
    shader_code_cache.clear();
    shader_hash_computations = 0;
    shader_sha256_computations = 0;
    shader_cache_hits = 0;
}
void NBNativeMaterialPrograms::_bind_methods() {
    ClassDB::bind_static_method("NBNativeMaterialPrograms", D_METHOD("shader_code_hash", "shader"),
                                &NBNativeMaterialPrograms::shader_code_hash);
    ClassDB::bind_static_method("NBNativeMaterialPrograms", D_METHOD("shader_code_sha256", "shader"),
                                &NBNativeMaterialPrograms::shader_code_sha256);
    ClassDB::bind_static_method("NBNativeMaterialPrograms", D_METHOD("shader_cache_statistics"),
                                &NBNativeMaterialPrograms::shader_cache_statistics);
    ClassDB::bind_static_method("NBNativeMaterialPrograms", D_METHOD("seal", "result"),
                                &NBNativeMaterialPrograms::seal);
    ClassDB::bind_static_method("NBNativeMaterialPrograms", D_METHOD("validate", "material", "schema"),
                                &NBNativeMaterialPrograms::validate);
    ClassDB::bind_static_method("NBNativeMaterialPrograms", D_METHOD("graph_hash", "graph"),
                                &NBNativeMaterialPrograms::graph_hash);
    ClassDB::bind_static_method("NBNativeMaterialPrograms", D_METHOD("fingerprint", "material"),
                                &NBNativeMaterialPrograms::fingerprint);
    ClassDB::bind_static_method("NBNativeMaterialPrograms", D_METHOD("cached_validate", "material", "schema"),
                                &NBNativeMaterialPrograms::cached_validate);
    ClassDB::bind_static_method("NBNativeMaterialPrograms", D_METHOD("current_values", "material"),
                                &NBNativeMaterialPrograms::current_values);
    ClassDB::bind_static_method("NBNativeMaterialPrograms",
                                D_METHOD("seal_system", "shader", "contexts", "aliases", "defaults"),
                                &NBNativeMaterialPrograms::seal_system);
    ClassDB::bind_static_method("NBNativeMaterialPrograms", D_METHOD("validate_system", "material", "schema"),
                                &NBNativeMaterialPrograms::validate_system);
    ClassDB::bind_static_method("NBNativeMaterialPrograms", D_METHOD("fingerprint_system", "material"),
                                &NBNativeMaterialPrograms::fingerprint_system);
    ClassDB::bind_static_method("NBNativeMaterialPrograms",
                                D_METHOD("cached_validate_system", "material", "schema"),
                                &NBNativeMaterialPrograms::cached_validate_system);
    ClassDB::bind_static_method("NBNativeMaterialPrograms", D_METHOD("context_hash", "contexts", "aliases"),
                                &NBNativeMaterialPrograms::context_hash);
    ClassDB::bind_static_method("NBNativeMaterialPrograms", D_METHOD("system_values", "material"),
                                &NBNativeMaterialPrograms::system_values);
    ClassDB::bind_static_method("NBNativeMaterialPrograms", D_METHOD("source_contexts", "material"),
                                &NBNativeMaterialPrograms::source_contexts);
    ClassDB::bind_static_method("NBNativeMaterialPrograms", D_METHOD("clear_cache"),
                                &NBNativeMaterialPrograms::clear_cache);
}
} // namespace godot
