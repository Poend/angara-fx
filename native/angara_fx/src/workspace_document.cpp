#include "workspace_document.h"
#include "text_saver.h"
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
namespace godot {
using nbdocument::dict;
namespace {
void assign_save_ids(const Ref<Resource> &resource, String identity,
                                              const String &output) {
    TypedArray<Dictionary> properties = resource->get_property_list();
    for (int64_t i = 0; i < properties.size(); ++i) {
        Dictionary p = properties[i];
        if (String(p["name"]) == "stable_id" && !resource->get("stable_id").stringify().is_empty())
            identity = resource->get("stable_id").stringify();
    }
    resource->set("resource_scene_unique_id", "nb_" + identity.sha256_text().substr(0, 24));
    Ref<Script> script = resource->get_script();
    if (script.is_valid())
        script->set_id_for_path(output, script->get_path().get_file().get_basename());
    for (int64_t i = 0; i < properties.size(); ++i) {
        Dictionary p = properties[i];
        String name = p["name"];
        if (!(int64_t(p["usage"]) & PROPERTY_USAGE_STORAGE) || name == "script")
            continue;
        Variant raw = resource->get(name);
        if (raw.get_type() != Variant::ARRAY)
            continue;
        Array children = raw;
        Dictionary occurrences;
        for (int64_t n = 0; n < children.size(); ++n) {
            if (children[n].get_type() != Variant::OBJECT)
                continue;
            Ref<Resource> child = children[n];
            if (child.is_null())
                continue;
            String path = child->get_path().get_slice("::", 0);
            if (!path.is_empty() && path != output)
                continue;
            String key = String::num_int64(n);
            Variant input_name = child->get("input_name");
            if (input_name.get_type() != Variant::NIL)
                key = input_name.stringify();
            int64_t count = occurrences.get(key, 0);
            occurrences[key] = count + 1;
            assign_save_ids(child, identity + "/" + name + "/" + key + "/" + String::num_int64(count),
                            output);
        }
    }
}
Ref<Resource> make(const Dictionary &schemas, const String &kind) {
    Ref<Script> script = schemas[kind];
    return script->call("new");
}
void stage(const Ref<Resource> &owner, const String &name, const Dictionary &schemas) {
    Ref<Resource> value = make(schemas, "stage");
    value->set("stable_id", NBNativeWorkspaceDocument::new_id());
    value->set("stage_name", name);
    Array stages = owner->get("stages");
    stages.append(value);
}
} // namespace
String NBNativeWorkspaceDocument::new_id() {
    return (Variant(Time::get_singleton()->get_unix_time_from_system()).stringify() + String(":") +
            Variant(Time::get_singleton()->get_ticks_usec()).stringify() + String(":") +
            Variant(UtilityFunctions::randi()).stringify())
        .sha256_text()
        .substr(0, 20);
}
Ref<Resource> NBNativeWorkspaceDocument::new_effect(const Dictionary &schemas) {
    Ref<Resource> effect = make(schemas, "effect");
    effect->set("stable_id", new_id());
    effect->set("display_name", "New effect");
    stage(effect, "SystemSpawn", schemas);
    stage(effect, "SystemUpdate", schemas);
    return effect;
}
Ref<Resource> NBNativeWorkspaceDocument::new_emitter(int64_t index, const Dictionary &schemas) {
    Ref<Resource> emitter = make(schemas, "emitter");
    emitter->set("stable_id", new_id());
    emitter->set("display_name", String("Emitter ") + String::num_int64(index + 1));
    Ref<Resource> local_space = make(schemas, "input");
    local_space->set("input_name", "LocalSpace");
    local_space->set("value", false);
    local_space->set("provenance", "authored");
    Array settings = emitter->get("settings");
    settings.append(local_space);
    for (const char *name : {"EmitterSpawn", "EmitterUpdate", "ParticleSpawn", "ParticleUpdate"})
        stage(emitter, name, schemas);
    return emitter;
}
Ref<Resource> NBNativeWorkspaceDocument::new_renderer(const String &type, const Dictionary &schemas) {
    Ref<Resource> renderer = make(schemas, "renderer");
    renderer->set("stable_id", new_id());
    renderer->set("renderer_type", type);
    if (type == "MeshRenderer") {
        Array entries = renderer->get("mesh_entries");
        entries.append(make(schemas, "mesh_entry"));
    } else {
        Ref<StandardMaterial3D> material;
        material.instantiate();
        material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
        material->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
        renderer->set("material", material);
    }
    return renderer;
}
Ref<Resource> NBNativeWorkspaceDocument::load_effect(const String &path, const Ref<Script> &schema) {
    Ref<Resource> resource =
        ResourceLoader::get_singleton()->load(path, "", ResourceLoader::CACHE_MODE_REPLACE);
    if (resource.is_null())
        return Ref<Resource>();
    Ref<Script> script = resource->get_script();
    return script == schema ? resource : Ref<Resource>();
}
Ref<Resource> NBNativeWorkspaceDocument::load_asset(const String &path) {
    String local = ProjectSettings::get_singleton()->localize_path(path);
    if (local.get_extension().to_lower() != "tres" || !local.begins_with("res://"))
        return Ref<Resource>();
    return ResourceLoader::get_singleton()->load(local, "", ResourceLoader::CACHE_MODE_REUSE);
}
Dictionary NBNativeWorkspaceDocument::save_effect(const Ref<Resource> &effect, String path) {
    if (!path.ends_with(".tres"))
        path += ".tres";
    Error error = DirAccess::make_dir_recursive_absolute(ProjectSettings::get_singleton()->globalize_path(path.get_base_dir()));
    if (error == OK) {
        assign_save_ids(effect, "effect", path);
        error = Error(NBNativeTextSaver::save(effect, path));
    }
    return dict({{"path", path}, {"error", error}});
}
void NBNativeWorkspaceDocument::_bind_methods() {
    ClassDB::bind_static_method("NBNativeWorkspaceDocument", D_METHOD("new_id"),
                                &NBNativeWorkspaceDocument::new_id);
    ClassDB::bind_static_method("NBNativeWorkspaceDocument", D_METHOD("new_effect", "schemas"),
                                &NBNativeWorkspaceDocument::new_effect);
    ClassDB::bind_static_method("NBNativeWorkspaceDocument", D_METHOD("new_emitter", "index", "schemas"),
                                &NBNativeWorkspaceDocument::new_emitter);
    ClassDB::bind_static_method("NBNativeWorkspaceDocument", D_METHOD("new_renderer", "type", "schemas"),
                                &NBNativeWorkspaceDocument::new_renderer);
    ClassDB::bind_static_method("NBNativeWorkspaceDocument", D_METHOD("load_effect", "path", "schema"),
                                &NBNativeWorkspaceDocument::load_effect);
    ClassDB::bind_static_method("NBNativeWorkspaceDocument", D_METHOD("load_asset", "path"),
                                &NBNativeWorkspaceDocument::load_asset);
    ClassDB::bind_static_method("NBNativeWorkspaceDocument", D_METHOD("save_effect", "effect", "path"),
                                &NBNativeWorkspaceDocument::save_effect);
    ClassDB::bind_static_method("NBNativeWorkspaceDocument",
                                D_METHOD("preserve_session", "effect", "state", "layout", "session_path"),
                                &NBNativeWorkspaceDocument::preserve_session);
    ClassDB::bind_static_method(
        "NBNativeWorkspaceDocument",
        D_METHOD("restore_session", "session_path", "fallback_recovery", "schema", "memory"),
        &NBNativeWorkspaceDocument::restore_session);
}
} // namespace godot
