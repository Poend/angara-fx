#include "renderer_filter.h"
#include "runtime_control.h"
#include "value_evaluator.h"
#include <godot_cpp/core/class_db.hpp>
#include <cmath>
namespace godot {
void NBNativeRendererFilter::_bind_methods() {
    ClassDB::bind_static_method("NBNativeRendererFilter", D_METHOD("int32", "value"),
                                &NBNativeRendererFilter::int32);
    ClassDB::bind_static_method("NBNativeRendererFilter",
                                D_METHOD("binding_reference", "renderer", "key", "property", "fallback"),
                                &NBNativeRendererFilter::binding_reference);
    ClassDB::bind_static_method("NBNativeRendererFilter",
                                D_METHOD("host_binding", "renderer", "key", "property", "name"),
                                &NBNativeRendererFilter::host_binding);
    ClassDB::bind_static_method("NBNativeRendererFilter", D_METHOD("configuration", "renderer", "types"),
                                &NBNativeRendererFilter::configuration);
    ClassDB::bind_static_method("NBNativeRendererFilter", D_METHOD("_host", "name"),
                                &NBNativeRendererFilter::host);
    ClassDB::bind_static_method("NBNativeRendererFilter",
                                D_METHOD("apply", "config", "particles", "parameters"),
                                &NBNativeRendererFilter::apply);
}
bool NBNativeRendererFilter::int32(const Variant &v) {
    return (v.get_type() == Variant::FLOAT || v.get_type() == Variant::INT) && std::isfinite(double(v)) &&
           double(v) == std::floor(double(v)) && double(v) >= -2147483648. && double(v) <= 2147483647.;
}
Variant NBNativeRendererFilter::input(const Ref<Resource> &r, const String &name, const Variant &fallback) {
    return r.is_valid() ? NBNativeDocument::input_value(r->get("properties"), name, fallback) : fallback;
}
String NBNativeRendererFilter::binding_reference(const Ref<Resource> &r, const String &key,
                                                 const String &property, const String &fallback) {
    if (r.is_null())
        return fallback;
    Array bindings = r->get("bindings");
    for (int n = 0; n < bindings.size(); ++n) {
        Dictionary b = bindings[n];
        if (b.get("name", Variant()) == Variant(key))
            return bool(b.get("enabled", true)) ? NBNativeValues::enum_name(b.get("value", "None"))
                                                : String("None");
    }
    Variant raw = input(r, property);
    if (raw.get_type() == Variant::DICTIONARY) {
        Dictionary d = raw;
        return Dictionary(d.get("paramMapVariable", Dictionary())).get("name", fallback);
    }
    return fallback;
}
bool NBNativeRendererFilter::host(const String &name) {
    return name.begins_with("User.") || name.begins_with("Emitter.") || name.begins_with("System.") ||
           name.begins_with("Engine.");
}
bool NBNativeRendererFilter::host_binding(const Ref<Resource> &r, const String &, const String &property,
                                          const String &name) {
    if (host(name))
        return true;
    Variant raw = input(r, property);
    if (raw.get_type() != Variant::DICTIONARY || name.begins_with("Particles."))
        return false;
    Dictionary d = raw;
    return d.get("bIsCachedParticleValue", Variant()) == Variant(false) &&
           Dictionary(d.get("paramMapVariable", Dictionary())).get("name", Variant()) == Variant(name);
}
Dictionary NBNativeRendererFilter::configuration(const Ref<Resource> &r, const Dictionary &types) {
    Dictionary c;
    TypedArray<String> errors;
    c["errors"] = errors;
    c["enabled_name"] = binding_reference(r, "RendererEnabled", "RendererEnabledBinding", "None");
    c["tag_name"] = "None";
    c["tag"] = 0;
    c["particle_tag"] = false;
    c["host_tag"] = false;
    if (r.is_null()) {
        errors.append("Renderer resource is required.");
        return c;
    }
    String enabled = c["enabled_name"];
    if (!enabled.is_empty() && enabled != "None") {
        if (!host_binding(r, "RendererEnabled", "RendererEnabledBinding", enabled))
            errors.append("RendererEnabled requires a host bool binding: " + enabled);
        else if (types.has(enabled) && types[enabled] != Variant("bool"))
            errors.append("RendererEnabled requires a bool variable: " + enabled);
    }
    String kind = r->get("renderer_type");
    if (kind != "SpriteRenderer" && kind != "MeshRenderer")
        return c;
    Variant tag = input(r, "RendererVisibility", 0);
    if (!int32(tag))
        errors.append("RendererVisibility must be a signed int32.");
    else
        c["tag"] = int64_t(tag);
    String name = binding_reference(r, "RendererVisibilityTag", "RendererVisibilityTagBinding",
                                    "Particles.VisibilityTag");
    c["tag_name"] = name;
    if (name.is_empty() || name == "None")
        return c;
    if (types.has(name) && types[name] != Variant("int")) {
        errors.append("Renderer visibility binding requires an int variable: " + name);
        return c;
    }
    if (name.begins_with("Particles.")) {
        if (NBNativeValues::enum_name(input(r, "SourceMode", "Particles")) == "Emitter")
            return c;
        Variant raw = input(r, "RendererVisibilityTagBinding");
        bool exists = false;
        if (raw.get_type() == Variant::DICTIONARY) {
            Dictionary d = raw;
            exists =
                d.get("bBindingExistsOnSource", Variant()) == Variant(true) &&
                Dictionary(d.get("paramMapVariable", Dictionary())).get("name", Variant()) == Variant(name);
        }
        c["particle_tag"] = types.get(name, Variant()) == Variant("int") || exists;
    } else if (host_binding(r, "RendererVisibilityTag", "RendererVisibilityTagBinding", name))
        c["host_tag"] = true;
    else
        errors.append("Unsupported renderer visibility namespace: " + name);
    return c;
}
Dictionary NBNativeRendererFilter::apply(const Dictionary &config, const Array &particles,
                                         const Dictionary &parameters) {
    Dictionary r, indices;
    TypedArray<String> errors;
    Array output;
    r["errors"] = errors;
    r["particles"] = output;
    r["visible_indices"] = indices;
    r["all_visible"] = false;
    errors.append_array(config.get("errors", Array()));
    if (!errors.is_empty())
        return r;
    String name = config.get("enabled_name", "None");
    if (!name.is_empty() && name != "None" && parameters.has(name)) {
        Variant v = parameters[name];
        if (v.get_type() != Variant::BOOL && !int32(v)) {
            errors.append("RendererEnabled value must be bool/int32: " + name);
            return r;
        }
        if (!bool(v))
            return r;
    }
    name = config.get("tag_name", "None");
    if (bool(config.get("host_tag", false)) && parameters.has(name)) {
        Variant v = parameters[name];
        if (!int32(v)) {
            errors.append("Renderer visibility host value must be int32: " + name);
            return r;
        }
        if (int64_t(v) != int64_t(config.get("tag", 0)))
            return r;
    }
    if (!bool(config.get("particle_tag", false))) {
        r["particles"] = particles;
        r["all_visible"] = true;
        return r;
    }
    for (int n = 0; n < particles.size(); ++n) {
        Dictionary p = particles[n];
        Variant v = Dictionary(p.get("attributes", Dictionary())).get(name, Variant());
        if (!int32(v)) {
            errors.append("Missing/invalid particle visibility value: " + name);
            break;
        }
        if (int64_t(v) != int64_t(config.get("tag", 0)))
            continue;
        output.append(p);
        indices[n] = true;
    }
    if (!errors.is_empty()) {
        output.clear();
        indices.clear();
    }
    return r;
}
} // namespace godot
