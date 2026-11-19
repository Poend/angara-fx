#include "workspace_document.h"
#include <filesystem>
#include <godot_cpp/classes/project_settings.hpp>
#include "text_saver.h"
#include <godot_cpp/classes/config_file.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
namespace godot {
using nbdocument::dict;
namespace {
bool inside(const String &path, const String &root) {
    std::error_code ec;
    auto canonical = [&](const String &p) {
        CharString u = ProjectSettings::get_singleton()->globalize_path(p).utf8();
        return std::filesystem::weakly_canonical(std::filesystem::u8path(u.get_data()), ec);
    };
    auto base = canonical(root);
    if (ec)
        return false;
    auto target = canonical(path);
    if (ec)
        return false;
    auto b = base.begin(), t = target.begin();
    for (; b != base.end(); ++b, ++t)
        if (t == target.end() || *b != *t)
            return false;
    return true;
}
Dictionary fail(Dictionary result, const String &message, int64_t error) {
    result["ok"] = false;
    result["error"] = message + UtilityFunctions::error_string(error);
    return result;
}
} // namespace
Dictionary NBNativeWorkspaceDocument::preserve_session(const Ref<Resource> &effect, const Dictionary &state,
                                                       const Dictionary &layout, const String &session_path) {
    Dictionary result = state.duplicate();
    result["ok"] = false;
    result["error"] = "";
    String directory = session_path.get_base_dir();
    if (!session_path.begins_with("res://") || !inside(directory, "res://")) {
        result["error"] = "Recovery manifest must remain inside the project.";
        return result;
    }
    Error directory_error = DirAccess::make_dir_recursive_absolute(directory);
    if (directory_error != OK)
        return fail(result, "Cannot create recovery directory: ", directory_error);
    Ref<ConfigFile> config;
    config.instantiate();
    config->set_value("document", "path", state["document_path"]);
    config->set_value("document", "dirty", state["dirty"]);
    config->set_value("import", "plan", state["last_import_plan"]);
    for (const char *key : {"columns", "work", "main"})
        config->set_value("layout", key, layout[key]);
    for (const char *key : {"zoom", "pan"})
        config->set_value("overview", key, layout[key]);
    String recovery_file = state["recovery_file"], fingerprint = state["current_fingerprint"];
    if (bool(state["dirty"]) &&
        (recovery_file.is_empty() || state["recovery_fingerprint"] != Variant(fingerprint))) {
        if (effect.is_null()) {
            result["error"] = "Cannot recover a missing effect.";
            return result;
        }
        recovery_file = directory.path_join(
            String("recovery_") + String(effect->get("stable_id")).validate_filename() + String("_") +
            String::num_uint64(Time::get_singleton()->get_ticks_usec()) + String(".tres"));
        int64_t saved = NBNativeTextSaver::save(effect, recovery_file);
        if (saved != OK)
            return fail(result, "Cannot save text recovery resource: ", saved);
        result["recovery_file"] = recovery_file;
        result["recovery_version"] = state["history_version"];
        result["recovery_fingerprint"] = fingerprint;
    }
    config->set_value("document", "recovery", recovery_file);
    String pending_path = session_path + String(".pending"),
           previous_path = session_path + String(".previous");
    if (!inside(session_path, directory) || !inside(pending_path, directory) ||
        !inside(previous_path, directory)) {
        result["error"] = "Recovery manifest paths must remain inside the recovery directory.";
        return result;
    }
    Error saved = config->save(pending_path);
    if (saved != OK)
        return fail(result, "Cannot save recovery manifest: ", saved);
    if (FileAccess::file_exists(session_path)) {
        if (FileAccess::file_exists(previous_path)) {
            Error removed = DirAccess::remove_absolute(previous_path);
            if (removed != OK)
                return fail(result, "Cannot rotate recovery manifest: ", removed);
        }
        Error backed_up = DirAccess::rename_absolute(session_path, previous_path);
        if (backed_up != OK)
            return fail(result, "Cannot preserve previous recovery manifest: ", backed_up);
    }
    Error committed = DirAccess::rename_absolute(pending_path, session_path);
    if (committed != OK) {
        if (FileAccess::file_exists(previous_path) && !FileAccess::file_exists(session_path))
            DirAccess::rename_absolute(previous_path, session_path);
        return fail(result, "Cannot commit recovery manifest: ", committed);
    }
    result["ok"] = true;
    return result;
}
Dictionary NBNativeWorkspaceDocument::restore_session(const String &session_path,
                                                      const String &fallback_recovery,
                                                      const Ref<Script> &schema, const Dictionary &memory) {
    Ref<Resource> memory_effect = memory.get("effect", Variant());
    if (memory_effect.is_valid())
        return dict({{"available", true},
                     {"kind", "memory"},
                     {"effect", memory_effect},
                     {"path", Variant(memory.get("path", "")).stringify()}});
    Ref<ConfigFile> config;
    config.instantiate();
    if (config->load(session_path) != OK && config->load(session_path + String(".previous")) != OK)
        return dict({{"available", false}});
    String path = config->get_value("document", "path", ""),
           recovery = config->get_value("document", "recovery", fallback_recovery);
    Dictionary layout = dict({{"columns", config->get_value("layout", "columns", 330)},
                              {"work", config->get_value("layout", "work", 500)},
                              {"main", config->get_value("layout", "main", 540)},
                              {"zoom", config->get_value("overview", "zoom", 1.0)},
                              {"pan", config->get_value("overview", "pan", Vector2(16, 20))}});
    Dictionary result =
        dict({{"available", true},
              {"kind", "empty"},
              {"effect", Variant()},
              {"path", path},
              {"layout", layout},
              {"last_import_plan", Variant(config->get_value("import", "plan", "")).stringify()},
              {"recovery_file", recovery}});
    ResourceLoader *loader = ResourceLoader::get_singleton();
    if (bool(config->get_value("document", "dirty", false)) && loader->exists(recovery)) {
        Ref<Resource> effect = loader->load(recovery, "", ResourceLoader::CACHE_MODE_REPLACE);
        if (effect.is_valid()) {
            result["kind"] = "recovery";
            result["effect"] = effect;
        }
    } else if (!path.is_empty() && loader->exists(path)) {
        Ref<Resource> effect = load_effect(path, schema);
        if (effect.is_valid()) {
            result["kind"] = "saved";
            result["effect"] = effect;
        }
    }
    return result;
}
} // namespace godot
