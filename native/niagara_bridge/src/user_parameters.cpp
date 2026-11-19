#include "user_parameters.h"
#include "attribute_values.h"
#include <godot_cpp/core/class_db.hpp>
#include <cmath>
#include <limits>

namespace godot {
void NBNativeUserParameters::_bind_methods() {
    ClassDB::bind_static_method("NBNativeUserParameters", D_METHOD("qualified_name", "name"), &NBNativeUserParameters::qualified_name);
    ClassDB::bind_static_method("NBNativeUserParameters", D_METHOD("declaration", "effect", "name"), &NBNativeUserParameters::declaration);
    ClassDB::bind_static_method("NBNativeUserParameters", D_METHOD("typed_value", "declared_type", "value"), &NBNativeUserParameters::typed_value);
}

String NBNativeUserParameters::qualified_name(const String &name) {
    String local = name.strip_edges().trim_prefix("User.");
    return local.is_valid_identifier() ? String("User.") + local : String();
}

Ref<Resource> NBNativeUserParameters::declaration(const Ref<Resource> &effect, const String &name) {
    if (effect.is_null() || name.is_empty())
        return {};
    // Imported declarations can already be qualified and can contain nested namespaces.
    String key = name.begins_with("User.") ? name : "User." + name;
    Array entries = effect->get("user_parameters");
    for (int64_t i = 0; i < entries.size(); ++i) {
        Ref<Resource> entry = entries[i];
        if (entry.is_valid()) {
            String stored = entry->get("input_name");
            if ((stored.begins_with("User.") ? stored : "User." + stored) == key)
                return entry;
        }
    }
    return {};
}

Variant NBNativeUserParameters::typed_value(const String &declared_type, const Variant &value) {
    String type = NBNativeAttributeValues::type_name(declared_type);
    if (type.is_empty())
        return Variant();
    if (type == "bool")
        return value.get_type() == Variant::BOOL ? value : Variant();
    if (!NBNativeRendererViews::valid(value, type))
        return Variant();
    if (type == "float" && std::abs(double(value)) > std::numeric_limits<float>::max())
        return Variant();
    Variant converted = NBNativeAttributeValues::coerce(value, type);
    return NBNativeRendererViews::valid(converted, type) ? converted : Variant();
}
}
