#include "editor_operations.h"
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <cmath>
namespace godot {
using nbdocument::dict;
namespace {
Dictionary failure(const String &message) {
    return dict({{"error", message}});
}
Array array(std::initializer_list<Variant> values) {
    Array result;
    for (const Variant &value : values)
        result.append(value);
    return result;
}
String text(const Dictionary &values, const String &key, const String &fallback = "") {
    return Variant(values.get(key, fallback)).stringify();
}
} // namespace
String NBNativeEditorOperations::encode(const Ref<Resource> &module, const String &stage) {
    Array inputs, source = module->get("inputs");
    for (int64_t i = 0; i < source.size(); ++i) {
        Ref<Resource> input = source[i];
        inputs.append(dict({{"name", input->get("input_name")},
                            {"target_type", input->get("target_type")},
                            {"has_default", input->get("has_default")},
                            {"value", pack(input->get("value"))},
                            {"line", input->get("line")},
                            {"provenance", input->get("provenance")}}));
    }
    return JSON::stringify(dict({{"format", "niagara_bridge.module/1"},
                                 {"stage", stage},
                                 {"name", module->get("module_name")},
                                 {"alias", module->get("alias_name")},
                                 {"enabled", module->get("enabled")},
                                 {"line", module->get("line")},
                                 {"kind", module->get("operation_kind")},
                                 {"target_type", module->get("target_type")},
                                 {"inputs", inputs}}),
                           "\t", false, true);
}
Dictionary NBNativeEditorOperations::decode(const String &source, const Ref<Script> &module_schema,
                                            const Ref<Script> &input_schema) {
    if (source.length() > 2000000)
        return failure("Clipboard module exceeds the 2 MB limit.");
    Variant raw = JSON::parse_string(source);
    Dictionary parsed = raw.get_type() == Variant::DICTIONARY ? Dictionary(raw) : Dictionary();
    if (raw.get_type() != Variant::DICTIONARY ||
        parsed.get("format", Variant()) != Variant("niagara_bridge.module/1"))
        return failure("The clipboard does not contain a Angara FX module.");
    Variant inputs = parsed.get("inputs", Array());
    String kind = text(parsed, "kind", "call");
    if (inputs.get_type() != Variant::ARRAY || (kind != "call" && kind != "assignment"))
        return failure("Clipboard module has invalid fields.");
    Ref<Resource> module = module_schema->call("new");
    String name = text(parsed, "name");
    module->set("module_name", name);
    if (name.is_empty())
        return failure("Clipboard module has no name.");
    module->set("alias_name", text(parsed, "alias"));
    module->set("enabled", bool(parsed.get("enabled", true)));
    module->set("line", int64_t(parsed.get("line", 0)));
    module->set("operation_kind", kind);
    module->set("target_type", text(parsed, "target_type"));
    Array records = inputs, output = module->get("inputs");
    for (int64_t i = 0; i < records.size(); ++i) {
        if (records[i].get_type() != Variant::DICTIONARY)
            return failure("Clipboard input is invalid.");
        Dictionary item = records[i], decoded = unpack(item.get("value", Variant()), 0);
        if (decoded.has("error"))
            return decoded;
        Ref<Resource> input = input_schema->call("new");
        input->set("input_name", text(item, "name"));
        input->set("target_type", text(item, "target_type"));
        input->set("has_default", bool(item.get("has_default", true)));
        input->set("value", decoded["value"]);
        input->set("line", int64_t(item.get("line", 0)));
        input->set("provenance", text(item, "provenance", "explicit"));
        output.append(input);
    }
    return dict({{"module", module}, {"stage", text(parsed, "stage")}});
}
Dictionary NBNativeEditorOperations::pack(const Variant &value) {
    if (value.get_type() == Variant::INT)
        return dict({{"type", "int"}, {"value", String::num_int64(value)}});
    if (value.get_type() == Variant::DICTIONARY) {
        Dictionary source = value;
        Array pairs, keys = source.keys();
        for (int64_t i = 0; i < keys.size(); ++i)
            pairs.append(array({pack(keys[i]), pack(source[keys[i]])}));
        return dict({{"type", "dictionary"}, {"value", pairs}});
    }
    if (value.get_type() == Variant::ARRAY) {
        Array source = value, elements;
        for (int64_t i = 0; i < source.size(); ++i)
            elements.append(pack(source[i]));
        return dict({{"type", "array"}, {"value", elements}});
    }
    if (value.get_type() == Variant::VECTOR2) {
        Vector2 v = value;
        return dict({{"type", "vector2"}, {"value", array({v.x, v.y})}});
    }
    if (value.get_type() == Variant::VECTOR3) {
        Vector3 v = value;
        return dict({{"type", "vector3"}, {"value", array({v.x, v.y, v.z})}});
    }
    if (value.get_type() == Variant::COLOR) {
        Color v = value;
        return dict({{"type", "color"}, {"value", array({v.r, v.g, v.b, v.a})}});
    }
    return dict({{"type", Variant::get_type_name(value.get_type()).to_lower()}, {"value", value}});
}
Dictionary NBNativeEditorOperations::unpack(const Variant &encoded, int64_t depth) {
    if (depth > 32 || encoded.get_type() != Variant::DICTIONARY)
        return failure("Clipboard value nesting is invalid.");
    Dictionary data = encoded;
    String kind = text(data, "type");
    Variant value = data.get("value", Variant());
    if (kind == "nil")
        return dict({{"value", Variant()}});
    if (kind == "bool") {
        if (value.get_type() == Variant::BOOL)
            return dict({{"value", value}});
    } else if (kind == "int" || kind == "float") {
        if (kind == "int" && value.get_type() == Variant::STRING && String(value).is_valid_int())
            return dict({{"value", String(value).to_int()}});
        if (nbdocument::number(value)) {
            if (!std::isfinite(double(value)))
                return failure("Clipboard number must be finite.");
            return dict({{"value", kind == "int" ? Variant(int64_t(value)) : Variant(double(value))}});
        }
    } else if (kind == "string" || kind == "stringname") {
        if (value.get_type() == Variant::STRING)
            return dict({{"value", value}});
    } else if (kind == "array") {
        if (value.get_type() == Variant::ARRAY) {
            Array source = value, result;
            for (int64_t i = 0; i < source.size(); ++i) {
                Dictionary item = unpack(source[i], depth + 1);
                if (item.has("error"))
                    return item;
                result.append(item["value"]);
            }
            return dict({{"value", result}});
        }
    } else if (kind == "dictionary") {
        if (value.get_type() == Variant::ARRAY) {
            Array pairs = value;
            Dictionary result;
            for (int64_t i = 0; i < pairs.size(); ++i) {
                Variant item = pairs[i];
                if (item.get_type() != Variant::ARRAY || Array(item).size() != 2)
                    return failure("Clipboard dictionary entry is invalid.");
                Array pair = item;
                Dictionary key = unpack(pair[0], depth + 1), decoded = unpack(pair[1], depth + 1);
                if (key.has("error") || decoded.has("error"))
                    return failure("Clipboard dictionary value is invalid.");
                Variant index = key["value"];
                if (index.get_type() != Variant::STRING && index.get_type() != Variant::INT)
                    return failure("Clipboard dictionary keys must be strings or integers.");
                result[index] = decoded["value"];
            }
            return dict({{"value", result}});
        }
    } else if (kind == "vector2" || kind == "vector3" || kind == "color") {
        int count = kind == "vector2" ? 2 : (kind == "vector3" ? 3 : 4);
        if (value.get_type() == Variant::ARRAY && Array(value).size() == count) {
            Array components = value;
            for (int i = 0; i < count; ++i)
                if (!nbdocument::number(components[i]) || !std::isfinite(double(components[i])))
                    return failure("Clipboard vector component is invalid.");
            if (kind == "vector2")
                return dict({{"value", Vector2(double(components[0]), double(components[1]))}});
            if (kind == "vector3")
                return dict({{"value",
                              Vector3(double(components[0]), double(components[1]), double(components[2]))}});
            return dict({{"value", Color(double(components[0]), double(components[1]), double(components[2]),
                                         double(components[3]))}});
        }
    }
    return failure("Clipboard contains an unsupported value type: " + kind);
}
void NBNativeEditorOperations::_bind_methods() {
    ClassDB::bind_static_method("NBNativeEditorOperations", D_METHOD("encode", "module", "stage"),
                                &NBNativeEditorOperations::encode);
    ClassDB::bind_static_method("NBNativeEditorOperations",
                                D_METHOD("decode", "text", "module_schema", "input_schema"),
                                &NBNativeEditorOperations::decode);
    ClassDB::bind_static_method("NBNativeEditorOperations", D_METHOD("pack", "value"),
                                &NBNativeEditorOperations::pack);
    ClassDB::bind_static_method("NBNativeEditorOperations", D_METHOD("unpack", "encoded", "depth"),
                                &NBNativeEditorOperations::unpack);
    ClassDB::bind_static_method("NBNativeEditorOperations",
                                D_METHOD("timeline", "effect", "dependencies", "contracts"),
                                &NBNativeEditorOperations::timeline);
    ClassDB::bind_static_method("NBNativeEditorOperations", D_METHOD("snapshot_bounds", "snapshot"),
                                &NBNativeEditorOperations::snapshot_bounds);
    ClassDB::bind_static_method("NBNativeEditorOperations",
                                D_METHOD("frame", "camera", "bounds", "viewport_size"),
                                &NBNativeEditorOperations::frame);
}
} // namespace godot
