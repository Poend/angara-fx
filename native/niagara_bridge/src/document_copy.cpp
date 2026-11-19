#include "document_copy.h"
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
namespace godot {
void NBNativeDocumentCopy::_bind_methods() {
    ClassDB::bind_static_method("NBNativeDocumentCopy", D_METHOD("copy", "value", "renew_ids", "seen"),
                                &NBNativeDocumentCopy::copy);
}
Variant NBNativeDocumentCopy::copy(const Variant &value, bool renew_ids, Dictionary seen) {
    if (value.get_type() == Variant::OBJECT) {
        Ref<Resource> resource = value;
        if (resource.is_null())
            return value;
        Ref<Script> script = resource->get_script();
        if (script.is_null() || !script->get_path().begins_with("res://addons/niagara_bridge/document/"))
            return value;
        int64_t id = resource->get_instance_id();
        if (seen.has(id))
            return seen[id];
        Ref<Resource> clone = script->call("new");
        if (clone.is_null())
            return Variant();
        seen[id] = clone;
        clone->set("resource_name", resource->get("resource_name"));
        clone->set("resource_local_to_scene", resource->get("resource_local_to_scene"));
        TypedArray<Dictionary> properties = resource->get_property_list();
        for (int64_t i = 0; i < properties.size(); ++i) {
            Dictionary property = properties[i];
            int64_t usage = property["usage"];
            if (!(usage & PROPERTY_USAGE_STORAGE) || !(usage & PROPERTY_USAGE_SCRIPT_VARIABLE))
                continue;
            String name = property["name"];
            if (renew_ids && name == "stable_id") {
                String identity = Variant(Time::get_singleton()->get_unix_time_from_system()).stringify();
                identity += ":";
                identity += Variant(Time::get_singleton()->get_ticks_usec()).stringify();
                identity += ":";
                identity += Variant(UtilityFunctions::randi()).stringify();
                clone->set(name, identity.sha256_text().substr(0, 20));
            } else
                clone->set(name, copy(resource->get(name), renew_ids, seen));
        }
        return clone;
    }
    if (value.get_type() == Variant::ARRAY) {
        Array result = Array(value).duplicate();
        for (int64_t i = 0; i < result.size(); ++i)
            result[i] = copy(result[i], renew_ids, seen);
        return result;
    }
    if (value.get_type() == Variant::DICTIONARY) {
        Dictionary result = Dictionary(value).duplicate();
        Array keys = result.keys();
        for (int64_t i = 0; i < keys.size(); ++i)
            result[keys[i]] = copy(result[keys[i]], renew_ids, seen);
        return result;
    }
    return value;
}
} // namespace godot
