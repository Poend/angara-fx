#include "editor_models.h"
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/templates/sort_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <vector>
namespace godot {
namespace {
using nbdocument::dict;
String text(const Dictionary &data, const String &key) {
    return Variant(data.get(key, "")).stringify();
}
Variant json(const String &path) {
    return FileAccess::file_exists(path) ? JSON::parse_string(FileAccess::get_file_as_string(path))
                                         : Variant();
}
struct KeyOrder {
    bool operator()(const Variant &a, const Variant &b) const {
        return UtilityFunctions::var_to_str(a) < UtilityFunctions::var_to_str(b);
    }
};
} // namespace
String NBNativeEditorModels::compute(const Ref<Resource> &resource) {
    Ref<HashingContext> digest;
    digest.instantiate();
    digest->start(HashingContext::HASH_SHA256);
    feed(resource, digest, Dictionary());
    return digest->finish().hex_encode();
}
void NBNativeEditorModels::token(const Ref<HashingContext> &digest, const String &value) {
    PackedByteArray bytes = value.to_utf8_buffer();
    digest->update(UtilityFunctions::var_to_bytes(bytes.size()));
    if (!bytes.is_empty())
        digest->update(bytes);
}
bool NBNativeEditorModels::plain_container(const Variant &value) {
    if (value.get_type() == Variant::DICTIONARY) {
        Dictionary data = value;
        Array keys = data.keys();
        for (int64_t i = 0; i < keys.size(); ++i)
            if (!plain_container(keys[i]) || !plain_container(data[keys[i]]))
                return false;
    } else if (value.get_type() == Variant::ARRAY) {
        Array data = value;
        for (int64_t i = 0; i < data.size(); ++i)
            if (!plain_container(data[i]))
                return false;
    } else if (value.get_type() == Variant::OBJECT)
        return value == Variant();
    return true;
}
void NBNativeEditorModels::feed(const Variant &value, const Ref<HashingContext> &digest,
                                const Dictionary &ancestors) {
    if (value.get_type() == Variant::NIL ||
        (value.get_type() == Variant::OBJECT && ObjectDB::get_instance(value.operator ObjectID().operator uint64_t()) == nullptr)) {
        token(digest, "null");
        return;
    }
    Ref<Resource> resource = value.get_type() == Variant::OBJECT ? Ref<Resource>(value) : Ref<Resource>();
    if (resource.is_valid()) {
        Ref<Script> script = resource->get_script();
        String script_path = script.is_valid() ? script->get_path() : String(), path = resource->get_path();
        bool document = script_path.begins_with("res://addons/niagara_bridge/document/"),
             external = !path.is_empty() && !path.contains("::");
        token(digest, "resource:" + resource->get_class() + ":" + script_path);
        if (external && !document) {
            token(digest, path);
            return;
        }
        int64_t id = resource->get_instance_id();
        if (ancestors.has(id)) {
            token(digest, "ancestor:" + Variant(ancestors[id]).stringify());
            return;
        }
        Dictionary branch = ancestors.duplicate();
        branch[id] = ancestors.size();
        Ref<Image> image = resource;
        if (image.is_valid()) {
            token(digest, String::num_int64(image->get_width()) + ":" +
                              String::num_int64(image->get_height()) + ":" +
                              String::num_int64(image->get_format()) + ":" +
                              String::num_int64(image->get_mipmap_count()));
            PackedByteArray bytes = image->get_data();
            if (!bytes.is_empty())
                digest->update(bytes);
        }
        PackedStringArray properties;
        TypedArray<Dictionary> descriptors = resource->get_property_list();
        for (int64_t i = 0; i < descriptors.size(); ++i) {
            Dictionary property = descriptors[i];
            String name = Variant(property["name"]).stringify();
            int64_t usage = property["usage"];
            if ((usage & PROPERTY_USAGE_STORAGE) && name != "script" && name != "resource_path" &&
                name != "resource_scene_unique_id") {
                if (image.is_valid() && name == "data")
                    continue;
                properties.append(name);
            }
        }
        properties.sort();
        for (int64_t i = 0; i < properties.size(); ++i) {
            String name = properties[i];
            token(digest, name);
            if (document && (name == "source_snapshot" || name == "source_baseline" ||
                             name == "diagnostics" || name == "unknown_source"))
                token(digest, UtilityFunctions::var_to_str(resource->get(name)));
            else
                feed(resource->get(name), digest, branch);
        }
        token(digest, "end-resource");
    } else if (value.get_type() == Variant::DICTIONARY) {
        if (plain_container(value)) {
            token(digest, "dictionary-data");
            token(digest, UtilityFunctions::var_to_str(value));
            return;
        }
        token(digest, "dictionary");
        Dictionary data = value;
        Array keys = data.keys();
        std::vector<Variant> sorted;
        for (int64_t i = 0; i < keys.size(); ++i)
            sorted.push_back(keys[i]);
        SortArray<Variant, KeyOrder>().sort(sorted.data(), int64_t(sorted.size()));
        for (const Variant &key : sorted) {
            feed(key, digest, ancestors);
            feed(data[key], digest, ancestors);
        }
        token(digest, "end-dictionary");
    } else if (value.get_type() == Variant::ARRAY) {
        if (plain_container(value)) {
            token(digest, "array-data");
            token(digest, UtilityFunctions::var_to_str(value));
            return;
        }
        Array data = value;
        token(digest, "array:" + String::num_int64(data.size()));
        for (int64_t i = 0; i < data.size(); ++i)
            feed(data[i], digest, ancestors);
        token(digest, "end-array");
    } else {
        token(digest, "value:" + String::num_int64(value.get_type()));
        token(digest, UtilityFunctions::var_to_str(value));
    }
}
String NBNativeEditorModels::project_path(const Variant &raw) {
    if (raw.get_type() != Variant::STRING || String(raw).is_empty())
        return "";
    String path = String(raw).replace("\\", "/");
    if (!path.begins_with("res://")) {
        if (path.is_absolute_path())
            return "";
        path = "res://" + path;
    }
    if (path.contains("..") || path.trim_prefix("res://").contains(":"))
        return "";
    return path.simplify_path();
}
String NBNativeEditorModels::registry_path(const String &primary, const String &fallback) {
    return FileAccess::file_exists(primary) ? primary : fallback;
}
Dictionary NBNativeEditorModels::read_bundle(const String &path, const Ref<Script> &schema) {
    TypedArray<String> errors;
    TypedArray<Dictionary> plans;
    Dictionary result = dict({{"errors", errors}, {"plans", plans}, {"path", path}});
    Variant raw = json(path);
    Dictionary data = raw.get_type() == Variant::DICTIONARY ? Dictionary(raw) : Dictionary();
    if (raw.get_type() != Variant::DICTIONARY || !nbdocument::equal(data.get("schema_version", Variant()), 1) ||
        data.get("producer", Variant()) != Variant("NiagaraBridge.EffectBundle") ||
        data.get("complete", Variant()) != Variant(true) ||
        data.get("source_packages_saved", Variant()) != Variant(false)) {
        errors.append("Choose a completed Angara FX automatic export bundle.json.");
        return result;
    }
    Variant systems_v = data.get("systems", Variant()), plans_v = data.get("plans", Variant());
    if (systems_v.get_type() != Variant::ARRAY || plans_v.get_type() != Variant::ARRAY ||
        Array(plans_v).is_empty() || Array(plans_v).size() != Array(systems_v).size()) {
        errors.append("Bundle system/plan list is incomplete.");
        return result;
    }
    Array systems = systems_v, entries = plans_v;
    const char *input_fields[] = {"input", "source_manifest"},
               *hash_fields[] = {"input_sha256", "source_manifest_sha256"};
    for (int i = 0; i < 2; ++i) {
        String source = project_path(data.get(input_fields[i], Variant()));
        if (source.is_empty() || !FileAccess::file_exists(source) ||
            Variant(FileAccess::get_sha256(source)) != data.get(hash_fields[i], Variant()))
            errors.append(String("Bundle input changed or is missing: ") + input_fields[i]);
    }
    if (!errors.is_empty())
        return result;
    Variant selection_v = json(project_path(data["input"])),
            library_v = json(project_path(data["source_manifest"]));
    Dictionary selection =
                   selection_v.get_type() == Variant::DICTIONARY ? Dictionary(selection_v) : Dictionary(),
               library = library_v.get_type() == Variant::DICTIONARY ? Dictionary(library_v) : Dictionary();
    if (selection_v.get_type() != Variant::DICTIONARY ||
        selection.get("systems", Variant()) != data["systems"] ||
        library_v.get_type() != Variant::DICTIONARY ||
        library.get("assets", Variant()).get_type() != Variant::ARRAY) {
        errors.append("Bundle selection differs from its source export.");
        return result;
    }
    Dictionary sources;
    Array assets = library["assets"];
    for (int64_t i = 0; i < assets.size(); ++i) {
        Variant raw = assets[i];
        Dictionary asset = raw.get_type() == Variant::DICTIONARY ? Dictionary(raw) : Dictionary();
        Variant source = asset.get("source_path", Variant());
        if (raw.get_type() != Variant::DICTIONARY || source.get_type() != Variant::STRING ||
            sources.has(source)) {
            errors.append("Invalid source library identity.");
            continue;
        }
        sources[source] = project_path(asset.get("file", Variant()));
    }
    if (sources.size() != systems.size())
        errors.append("Source library does not cover the selected systems.");
    Dictionary identities;
    for (int64_t i = 0; i < entries.size(); ++i) {
        Variant raw = entries[i];
        Dictionary entry = raw.get_type() == Variant::DICTIONARY ? Dictionary(raw) : Dictionary();
        Variant source = entry.get("source", Variant());
        if (raw.get_type() != Variant::DICTIONARY || source.get_type() != Variant::STRING ||
            !systems.has(source) || identities.has(source)) {
            errors.append("Bundle has an invalid or duplicate system identity.");
            continue;
        }
        identities[source] = true;
        String plan_path = project_path(entry.get("plan", Variant()));
        if (plan_path.is_empty() || !FileAccess::file_exists(plan_path) ||
            Variant(FileAccess::get_sha256(plan_path)) != entry.get("plan_sha256", Variant())) {
            errors.append("Bundle plan changed or is missing: " + plan_path);
            continue;
        }
        Ref<Resource> plan =
            ResourceLoader::get_singleton()->load(plan_path, "", ResourceLoader::CACHE_MODE_IGNORE_DEEP);
        if (plan.is_null() || plan->get_script() != Variant(schema) ||
            plan->get("source_dfs") != sources.get(source, "") ||
            project_path(entry.get("effect", Variant())) !=
                String(plan->get("output_directory")).path_join(plan->get("effect_filename"))) {
            errors.append("Bundle plan type or output differs: " + plan_path);
            continue;
        }
        plans.append(dict({{"source", source}, {"plan", plan_path}}));
    }
    if (!errors.is_empty())
        plans.clear();
    return result;
}
Dictionary NBNativeEditorModels::read_inventory(const Dictionary &state, const String &project) {
    TypedArray<String> errors;
    Array assets;
    Dictionary result = dict({{"errors", errors}, {"assets", assets}});
    String path = project_path(state.get("inventory", Variant()));
    if (state.get("operation", Variant()) != Variant("inventory") ||
        state.get("status", Variant()) != Variant("ready") ||
        state.get("project", Variant()) != Variant(project)) {
        errors.append("The saved system catalog belongs to a different or unfinished source job.");
        return result;
    }
    if (path.is_empty() || !FileAccess::file_exists(path) ||
        Variant(FileAccess::get_sha256(path)) != state.get("inventory_sha256", "")) {
        errors.append("The saved system catalog is missing or changed. Refresh the source list.");
        return result;
    }
    Variant raw = json(path);
    Dictionary value = raw.get_type() == Variant::DICTIONARY ? Dictionary(raw) : Dictionary();
    if (raw.get_type() != Variant::DICTIONARY ||
        !nbdocument::equal(value.get("schema_version", Variant()), 1) ||
        value.get("producer", Variant()) != Variant("NiagaraBridge.SystemInventory") ||
        value.get("complete", Variant()) != Variant(true)) {
        errors.append("Unknown or incomplete source-system catalog.");
        return result;
    }
    if (value.get("project_file", Variant()) != state.get("project_file", Variant()) ||
        value.get("job_request_sha256", Variant()) != state.get("request_sha256", Variant())) {
        errors.append("The source-system catalog project or request identity changed.");
        return result;
    }
    if (value.get("saved_assets_only", Variant()) != Variant(true) ||
        value.get("asset_objects_loaded", Variant()) != Variant(false) ||
        value.get("source_packages_saved", Variant()) != Variant(false) ||
        value.get("assets", Variant()).get_type() != Variant::ARRAY) {
        errors.append("The source-system catalog is missing its saved-asset registry contract.");
        return result;
    }
    Dictionary paths;
    Array records = value["assets"];
    for (int64_t i = 0; i < records.size(); ++i) {
        if (records[i].get_type() != Variant::DICTIONARY) {
            errors.append("Malformed source-system record.");
            break;
        }
        Dictionary asset = records[i];
        for (const char *key : {"object_path", "package_name", "asset_name", "class_path"})
            if (asset.get(key, Variant()).get_type() != Variant::STRING)
                errors.append("Malformed source-system identity.");
        if (!errors.is_empty())
            break;
        String package = text(asset, "package_name"), object = text(asset, "object_path");
        if (asset["class_path"] != Variant("/Script/Niagara.NiagaraSystem") || !package.begins_with("/") ||
            object != package + "." + text(asset, "asset_name") || paths.has(object)) {
            errors.append("Invalid or duplicate source-system identity.");
            break;
        }
        paths[object] = true;
    }
    if (errors.is_empty())
        result["assets"] = records;
    return result;
}
void NBNativeEditorModels::_bind_methods() {
    ClassDB::bind_static_method("NBNativeEditorModels", D_METHOD("compute", "resource"),
                                &NBNativeEditorModels::compute);
    ClassDB::bind_static_method("NBNativeEditorModels", D_METHOD("token", "digest", "value"),
                                &NBNativeEditorModels::token);
    ClassDB::bind_static_method("NBNativeEditorModels", D_METHOD("feed", "value", "digest", "ancestors"),
                                &NBNativeEditorModels::feed);
    ClassDB::bind_static_method("NBNativeEditorModels", D_METHOD("plain_container", "value"),
                                &NBNativeEditorModels::plain_container);
    ClassDB::bind_static_method("NBNativeEditorModels", D_METHOD("project_path", "raw"),
                                &NBNativeEditorModels::project_path);
    ClassDB::bind_static_method("NBNativeEditorModels", D_METHOD("read_bundle", "path", "schema"),
                                &NBNativeEditorModels::read_bundle);
    ClassDB::bind_static_method("NBNativeEditorModels", D_METHOD("read_inventory", "state", "project"),
                                &NBNativeEditorModels::read_inventory);
    ClassDB::bind_static_method("NBNativeEditorModels", D_METHOD("registry_path", "primary", "fallback"),
                                &NBNativeEditorModels::registry_path);
}
} // namespace godot
