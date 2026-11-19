#include "attribute_values.h"
#include "value_evaluator.h"
#include "renderer_filter.h"
#include "particle_context.h"
#include <godot_cpp/core/class_db.hpp>
#include <cmath>
#include <cstring>
namespace godot {
namespace {
Vector4 parts(const Variant &v) {
    if (v.get_type() == Variant::VECTOR4)
        return v;
    if (v.get_type() == Variant::QUATERNION) {
        Quaternion q = v;
        return Vector4(q.x, q.y, q.z, q.w);
    }
    if (v.get_type() == Variant::COLOR) {
        Color c = v;
        return Vector4(c.r, c.g, c.b, c.a);
    }
    if (v.get_type() == Variant::ARRAY && Array(v).size() == 4) {
        Array a = v;
        return Vector4(a[0], a[1], a[2], a[3]);
    }
    return Vector4();
}
bool number(const Variant &v) {
    return v.get_type() == Variant::FLOAT || v.get_type() == Variant::INT;
}
} // namespace
void NBNativeAttributeValues::_bind_methods() {
    ClassDB::bind_static_method("NBNativeAttributeValues", D_METHOD("type_name", "value"),
                                &NBNativeAttributeValues::type_name);
    ClassDB::bind_static_method("NBNativeAttributeValues", D_METHOD("shader_type", "type"),
                                &NBNativeAttributeValues::shader_type);
    ClassDB::bind_static_method("NBNativeAttributeValues", D_METHOD("compatible", "a", "b"),
                                &NBNativeAttributeValues::compatible);
    ClassDB::bind_static_method("NBNativeAttributeValues", D_METHOD("zero", "type"),
                                &NBNativeAttributeValues::zero);
    ClassDB::bind_static_method("NBNativeAttributeValues", D_METHOD("coerce", "value", "type"),
                                &NBNativeAttributeValues::coerce);
    ClassDB::bind_static_method("NBNativeAttributeValues",
                                D_METHOD("write_particle", "particle", "name", "type", "value", "builtins"),
                                &NBNativeAttributeValues::write_particle);
    ClassDB::bind_static_method("NBNativeAttributeValues", D_METHOD("component_array", "value", "type"),
                                &NBNativeAttributeValues::component_array);
}
String NBNativeAttributeValues::type_name(const String &s) {
    if (s == "float" || s == "int" || s == "bool")
        return s;
    if (s == "int32")
        return "int";
    if (s == "Vector2D" || s == "Vector2")
        return "vec2";
    if (s == "Vector" || s == "Vector3")
        return "vec3";
    if (s == "Position")
        return "position";
    if (s == "Color" || s == "LinearColor")
        return "color";
    if (s == "Quat" || s == "Quaternion")
        return "quat";
    if (s == "Vector4")
        return "vec4";
    return String();
}
String NBNativeAttributeValues::shader_type(const String &s) {
    return s == "position" ? String("vec3") : s == "color" || s == "quat" ? String("vec4") : s;
}
bool NBNativeAttributeValues::compatible(const String &a, const String &b) {
    return a == b || ((a == "position" || a == "vec3") && (b == "position" || b == "vec3")) ||
           ((a == "color" || a == "vec4") && (b == "color" || b == "vec4"));
}
Variant NBNativeAttributeValues::zero(const String &s) {
    if (s == "bool")
        return false;
    if (s == "int")
        return 0;
    if (s == "float")
        return 0.;
    if (s == "vec2")
        return Vector2();
    if (s == "vec3" || s == "position")
        return Vector3();
    if (s == "color")
        return Color(0, 0, 0, 0);
    if (s == "quat")
        return Quaternion(0, 0, 0, 0);
    if (s == "vec4")
        return Vector4();
    return Variant();
}
Variant NBNativeAttributeValues::coerce(const Variant &v, const String &s) {
    if (s == "bool")
        return bool(v);
    if (s == "int") {
        uint32_t bits = uint32_t(int64_t(v));
        int32_t result;
        std::memcpy(&result, &bits, 4);
        return result;
    }
    if (s == "float")
        return double(float(double(v)));
    if (s == "vec2")
        return NBNativeValues::vector2(v, Vector2());
    if (s == "vec3" || s == "position")
        return NBNativeValues::vector3(v);
    if (s == "color") {
        if (v.get_type() == Variant::VECTOR4 || v.get_type() == Variant::QUATERNION) {
            Vector4 p = parts(v);
            return Color(p.x, p.y, p.z, p.w);
        }
        return NBNativeValues::color(v, Color(0, 0, 0, 0));
    }
    if (s == "quat" || s == "vec4") {
        Vector4 p = parts(v);
        if (s == "quat")
            return Quaternion(p.x, p.y, p.z, p.w);
        return p;
    }
    return Variant();
}
void NBNativeAttributeValues::write_particle(Dictionary particle, const String &name, const String &type,
                                             const Variant &value, const Dictionary &builtins) {
    Variant typed = coerce(value, type);
    if (name == "Particles.UVScale") {
        Vector2 uv = typed;
        particle["uv_flip"] = uv.x;
        particle["uv_flip_y"] = uv.y;
    } else if (builtins.has(name)) {
        if (name == "Particles.SpriteRotation")
            typed = Math::deg_to_rad(double(typed));
        if (name.begins_with("Particles.DynamicMaterialParameter"))
            typed = coerce(typed, "color");
        Array fields = builtins[name];
        if (fields.size() > 1)
            particle[fields[1]] = typed;
    } else {
        Dictionary attributes = particle.get("attributes", Dictionary());
        attributes[name] = typed;
        particle["attributes"] = attributes;
    }
}
Array NBNativeAttributeValues::component_array(const Variant &v, const String &s) {
    Array a;
    a.resize(4);
    a.fill(0);
    String type = shader_type(s);
    if (type == "float" || type == "int" || type == "bool")
        a[0] = v;
    else if (v.get_type() == Variant::VECTOR2) {
        Vector2 p = v;
        a[0] = p.x;
        a[1] = p.y;
    } else if (v.get_type() == Variant::VECTOR3) {
        Vector3 p = v;
        a[0] = p.x;
        a[1] = p.y;
        a[2] = p.z;
    } else if (v.get_type() == Variant::COLOR || v.get_type() == Variant::QUATERNION ||
               v.get_type() == Variant::VECTOR4) {
        Vector4 p = parts(v);
        for (int n = 0; n < 4; ++n)
            a[n] = p[n];
    }
    return a;
}
void NBNativeRendererViews::_bind_methods() {
    ClassDB::bind_static_method("NBNativeRendererViews",D_METHOD("resident_projection","emitter","renderer","parameters","global_position","frame","builtins","camera","space"),&NBNativeRendererViews::resident_projection);
    ClassDB::bind_static_method(
        "NBNativeRendererViews",
        D_METHOD("view", "emitter", "renderer", "parameters", "global_position", "frame", "builtins"),
        &NBNativeRendererViews::view);
    ClassDB::bind_static_method("NBNativeRendererViews",
                                D_METHOD("configuration", "renderer", "types", "builtins", "fields"),
                                &NBNativeRendererViews::configuration);
    ClassDB::bind_static_method("NBNativeRendererViews", D_METHOD("fallback", "key", "position"),
                                &NBNativeRendererViews::fallback);
    ClassDB::bind_static_method("NBNativeRendererViews",
                                D_METHOD("read_particle", "particle", "name", "builtins"),
                                &NBNativeRendererViews::read_particle);
    ClassDB::bind_static_method("NBNativeRendererViews", D_METHOD("valid", "value", "type"),
                                &NBNativeRendererViews::valid);
    ClassDB::bind_static_method(
        "NBNativeRendererViews",
        D_METHOD("project", "config", "particles", "parameters", "default_position", "builtins"),
        &NBNativeRendererViews::project);
    ClassDB::bind_static_method("NBNativeRendererViews", D_METHOD("emitter_particle", "position"),
                                &NBNativeRendererViews::emitter_particle);
}
Dictionary NBNativeRendererViews::configuration(const Ref<Resource> &renderer, const Dictionary &types,
                                                const Dictionary &builtins, const Dictionary &specs) {
    Dictionary result;
    TypedArray<String> errors;
    Array fields;
    result["errors"] = errors;
    result["fields"] = fields;
    result["host_bound"] = false;
    result["emitter_source"] = false;
    if (renderer.is_null()) {
        errors.append("Renderer resource is required.");
        return result;
    }
    String kind = renderer->get("renderer_type");
    if (kind != "SpriteRenderer" && kind != "MeshRenderer")
        return result;
    String mode =
        NBNativeValues::enum_name(NBNativeRendererFilter::input(renderer, "SourceMode", "Particles"));
    bool emitter = mode == "Emitter";
    result["emitter_source"] = emitter;
    if (mode != "Particles" && mode != "Emitter")
        errors.append("Unsupported renderer SourceMode: " + mode);
    Array keys = specs.keys();
    for (int n = 0; n < keys.size(); ++n) {
        String key = keys[n];
        bool sprite = key == "SpriteSize" || key == "SpriteRotation" || key == "UVScale" ||
                      key == "PivotOffset" || key == "SpriteFacing" || key == "SpriteAlignment";
        bool mesh = key == "Scale" || key == "MeshOrientation" || key == "MeshIndex";
        if ((sprite && kind != "SpriteRenderer") || (mesh && kind != "MeshRenderer"))
            continue;
        Array spec = specs[key];
        String canonical = spec[0], expected = spec[1], property = spec[2],
               name = NBNativeRendererFilter::binding_reference(renderer, key, property, canonical);
        if (name == canonical && !emitter)
            continue;
        Dictionary row;
        row["key"] = key;
        row["canonical"] = canonical;
        row["type"] = expected;
        row["name"] = name;
        row["kind"] = "default";
        if (key == "PivotOffset")
            row["fallback"] = NBNativeValues::vector2(
                NBNativeRendererFilter::input(renderer, "PivotInUVSpace", Vector2(.5, .5)));
        if (key == "MeshIndex" && emitter)
            row["fallback"] = -1;
        if (!name.is_empty() && name != "None") {
            String type;
            if (types.has(name))
                type = types[name];
            else if (builtins.has(name)) {
                Array builtin = builtins[name];
                if (!builtin.is_empty())
                    type = builtin[0];
            }
            if (!type.is_empty() && !NBNativeAttributeValues::compatible(type, expected)) {
                errors.append(key + " renderer binding type differs: " + name);
                continue;
            }
            if (name.begins_with("Particles.") && !emitter) {
                Variant raw = NBNativeRendererFilter::input(renderer, property);
                bool exists = false;
                if (raw.get_type() == Variant::DICTIONARY) {
                    Dictionary d = raw;
                    exists = d.get("bBindingExistsOnSource", Variant()) == Variant(true) &&
                             Dictionary(d.get("paramMapVariable", Dictionary())).get("name", Variant()) ==
                                 Variant(name);
                }
                row["kind"] = !type.is_empty() || exists ? "particle" : "default";
            } else if (name.begins_with("Particles.")) {
            } else if (NBNativeRendererFilter::host_binding(renderer, key, property, name)) {
                row["kind"] = "host";
                result["host_bound"] = true;
            } else {
                errors.append("Unsupported renderer attribute namespace: " + name);
                continue;
            }
        }
        fields.append(row);
    }
    return result;
}
Variant NBNativeRendererViews::fallback(const String &key, const Vector3 &p) {
    if (key == "Position")
        return p;
    if (key == "Velocity")
        return Vector3();
    if (key == "Color" || key == "DynamicMaterial" || key == "DynamicMaterial1" ||
        key == "DynamicMaterial2" || key == "DynamicMaterial3")
        return Color(1, 1, 1, 1);
    if (key == "Scale")
        return Vector3(1, 1, 1);
    if (key == "MeshOrientation")
        return Quaternion();
    if (key == "MeshIndex")
        return 0;
    if (key == "SpriteSize")
        return Vector2(50, 50);
    if (key == "UVScale")
        return Vector2(1, 1);
    if (key == "PivotOffset")
        return Vector2(.5, .5);
    if (key == "SpriteFacing" || key == "SpriteAlignment")
        return Vector3(1, 0, 0);
    return 0.;
}
Variant NBNativeRendererViews::read_particle(Dictionary p, const String &name, const Dictionary &builtins) {
    p = p.get("_renderer_source", p);
    if (name == "Particles.SpriteRotation")
        return Math::rad_to_deg(double(p.get("rotation", 0.)));
    if (name == "Particles.UVScale")
        return Vector2(p.get("uv_flip", 1.), p.get("uv_flip_y", 1.));
    if (builtins.has(name)) {
        Array fields = builtins[name];
        return fields.size() > 1 ? p.get(fields[1], Variant()) : Variant();
    }
    return Dictionary(p.get("attributes", Dictionary())).get(name, Variant());
}
bool NBNativeRendererViews::valid(const Variant &v, const String &s) {
    if (s == "float")
        return number(v) && std::isfinite(double(v));
    if (s == "int")
        return number(v) && std::isfinite(double(v)) && double(v) == std::floor(double(v)) &&
               double(v) >= -2147483648. && double(v) <= 2147483647.;
    int width = s == "vec2"                                  ? 2
                : s == "vec3" || s == "position"             ? 3
                : s == "color" || s == "quat" || s == "vec4" ? 4
                                                             : 0;
    if (v.get_type() == Variant::ARRAY) {
        Array a = v;
        if (a.size() != width)
            return false;
        for (int n = 0; n < a.size(); ++n)
            if (!number(a[n]) || !std::isfinite(double(a[n])))
                return false;
        return true;
    }
    if (s == "vec2")
        return v.get_type() == Variant::VECTOR2 && Vector2(v).is_finite();
    if (s == "vec3" || s == "position")
        return v.get_type() == Variant::VECTOR3 && Vector3(v).is_finite();
    if ((s == "color" || s == "vec4") && v.get_type() == Variant::COLOR) {
        Color c = v;
        return std::isfinite(c.r) && std::isfinite(c.g) && std::isfinite(c.b) && std::isfinite(c.a);
    }
    if ((s == "color" || s == "quat" || s == "vec4") && v.get_type() == Variant::VECTOR4)
        return Vector4(v).is_finite();
    return s == "quat" && v.get_type() == Variant::QUATERNION && Quaternion(v).is_finite();
}
Dictionary NBNativeRendererViews::project(const Dictionary &config, const Array &particles,
                                          const Dictionary &parameters, const Vector3 &position,
                                          const Dictionary &builtins) {
    return project_view(config, particles, parameters, position, builtins, Ref<NBNativeParticleView>(), Array());
}
Dictionary NBNativeRendererViews::project_view(const Dictionary &config, const Array &particles,
                                               const Dictionary &parameters, const Vector3 &position,
                                               const Dictionary &builtins,
                                               const Ref<NBNativeParticleView> &view, const Array &indices) {
    Dictionary result;
    TypedArray<String> errors;
    Array output;
    result["errors"] = errors;
    result["particles"] = output;
    errors.append_array(config.get("errors", Array()));
    if (!errors.is_empty())
        return result;
    Array fields = config.get("fields", Array());
    if (fields.is_empty()) {
        result["particles"] = particles;
        return result;
    }
    std::vector<int64_t> handles(fields.size(), -1);
    if (view.is_valid()) {
        errors.append_array(view->get_errors());
        if (indices.size() != particles.size()) errors.append("Resident renderer row mapping differs.");
        if (!errors.is_empty()) return result;
        for (int i = 0; i < fields.size(); ++i) {
            Dictionary field = fields[i];
            if (field.get("kind", "") == Variant("particle"))
                handles[size_t(i)] = view->binding_handle(field["name"], builtins);
        }
    }
    for (int n = 0; n < particles.size(); ++n) {
        Dictionary original = particles[n], p = original.duplicate();
        p["_renderer_source"] = original;
        p["attributes"] = Dictionary(original.get("attributes", Dictionary())).duplicate();
        for (int i = 0; i < fields.size(); ++i) {
            Dictionary field = fields[i];
            String key = field["key"], name = field["name"], type = field["type"];
            Variant v = field.get("fallback", fallback(key, position));
            if (field.get("kind", "") == Variant("particle"))
                v = view.is_valid() ? view->read(handles[size_t(i)], int64_t(indices[n])) : read_particle(original, name, builtins);
            else if (field.get("kind", "") == Variant("host"))
                v = parameters.get(name, v);
            if (!valid(v, type)) {
                errors.append("Missing/invalid renderer " + key + " value: " + name);
                break;
            }
            NBNativeAttributeValues::write_particle(p, field["canonical"], type, v, builtins);
            if (key == "Velocity")
                p["_renderer_velocity"] = p["velocity"];
            if (key == "SpriteFacing" || key == "SpriteAlignment")
                p[String("_renderer_") + key] = NBNativeAttributeValues::coerce(v, "vec3");
        }
        if (!errors.is_empty()) {
            output.clear();
            return result;
        }
        output.append(p);
    }
    return result;
}
Dictionary NBNativeRendererViews::emitter_particle(const Vector3 &position) {
    Dictionary p;
    p["_emitter_source"] = true;
    p["id"] = 0;
    p["birth_time"] = 0.;
    p["age"] = 0.;
    p["lifetime"] = 1.;
    p["normalized_age"] = 0.;
    p["position"] = position;
    p["velocity"] = Vector3();
    p["color"] = Color(1, 1, 1, 1);
    p["size"] = Vector2(50, 50);
    p["rotation"] = 0.;
    p["mesh_scale"] = Vector3(1, 1, 1);
    p["mesh_orientation"] = Quaternion();
    p["mesh_index"] = -1;
    p["camera_offset"] = 0.;
    for (const char *field : {"dynamic", "dynamic1", "dynamic2", "dynamic3"})
        p[field] = Color(1, 1, 1, 1);
    p["subimage_index"] = 0.;
    p["material_random"] = 0.;
    p["uv_flip"] = 1.;
    p["uv_flip_y"] = 1.;
    p["attributes"] = Dictionary();
    return p;
}
Dictionary NBNativeRendererViews::view(Dictionary emitter, const Ref<Resource> &renderer,
                                       const Dictionary &parameters, const Vector3 &global_position,
                                       const Dictionary &frame, const Dictionary &builtins) {
    TypedArray<String> projection_errors=NBNativeParticleView::materialize(emitter);
    if(!projection_errors.is_empty()) {
        Dictionary result;result["errors"]=projection_errors;result["particles"]=Array();
        result["visible_indices"]=Dictionary();result["all_visible"]=false;return result;
    }
    Dictionary plan = emitter["plan"], filters = plan.get("renderer_filters", Dictionary()),
               layouts = plan.get("renderer_attributes", Dictionary()),
               config = filters.get(renderer, Dictionary()), attributes = layouts.get(renderer, Dictionary());
    Vector3 position =
        bool(plan["local_space"]) ? Vector3() : NBNativeValues::godot_to_unreal(global_position);
    bool emitter_source = attributes.get("emitter_source", false);
    Array particles;
    if (emitter_source)
        particles.append(emitter_particle(position));
    else
        particles = emitter["particles"];
    Dictionary values = parameters.duplicate();
    String enabled = config.get("enabled_name", "None");
    if ((!enabled.is_empty() && enabled != "None") || bool(config.get("host_tag", false)) ||
        bool(attributes.get("host_bound", false)))
        values.merge(
            NBNativeParticleContext::for_emitter(emitter, 0., frame.get("simulation_time", 0.), frame), true);
    Dictionary result = NBNativeRendererFilter::apply(config, particles, values);
    Array fields = attributes.get("fields", Array()), errors = result["errors"];
    if (errors.is_empty() && !fields.is_empty()) {
        Array indices;
        Ref<NBNativeParticleView> resident;
        if (!emitter_source) {
            resident = NBNativeParticleView::for_emitter(emitter);
            if (bool(result["all_visible"])) {
                for (int i = 0; i < particles.size(); ++i) indices.append(i);
            } else indices = Dictionary(result["visible_indices"]).keys();
        }
        Dictionary projected = project_view(attributes, result["particles"], values, position, builtins, resident, indices);
        Array projected_errors = projected["errors"];
        errors.append_array(projected_errors);
        if (projected_errors.is_empty()) {
            Array rows = projected["particles"];
            Dictionary mapped;
            result["projected"] = mapped;
            for (int i = 0; i < indices.size(); ++i)
                mapped[indices[i]] = rows[i];
            result["particles"] = rows;
        } else {
            result["particles"] = Array();
            result["visible_indices"] = Dictionary();
            result["all_visible"] = false;
        }
    }
    if (emitter_source) {
        result["visible_indices"] = Dictionary();
        result["all_visible"] = false;
    }
    return result;
}
} // namespace godot
