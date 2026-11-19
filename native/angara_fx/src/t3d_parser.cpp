#include "material_graph_loader.h"
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/reg_ex.hpp>
#include <godot_cpp/classes/reg_ex_match.hpp>
#include <godot_cpp/core/class_db.hpp>
namespace godot {
namespace {
using nbdocument::dict;
struct Reader {
    String value;
    int64_t cursor = 0, line = 0;
    TypedArray<Dictionary> errors;
    void error(const String &message) {
        errors.append(dict({{"severity", "error"}, {"line", line}, {"message", message}}));
    }
    void space() {
        while (cursor < value.length() && (value[cursor] == ' ' || value[cursor] == '\t' ||
                                           value[cursor] == '\n' || value[cursor] == '\r'))
            ++cursor;
    }
    Variant parse_value() {
        space();
        if (cursor >= value.length())
            return Variant();
        if (value[cursor] == '"') {
            int64_t start = cursor++;
            bool escaped = false;
            while (cursor < value.length()) {
                char32_t ch = value[cursor++];
                if (ch == '"' && !escaped) {
                    String quoted = value.substr(start, cursor - start);
                    Variant decoded = JSON::parse_string(quoted.replace("\\'", "'"));
                    if (decoded.get_type() == Variant::STRING)
                        return decoded;
                    error("Invalid quoted T3D string.");
                    return quoted.substr(1, quoted.length() - 2);
                }
                escaped = ch == '\\' && !escaped;
            }
            error("Unclosed quoted T3D string.");
            return String();
        }
        if (value[cursor] == '(') {
            ++cursor;
            space();
            Array sequence;
            Dictionary fields;
            while (cursor < value.length() && value[cursor] != ')') {
                space();
                int64_t start = cursor;
                while (cursor < value.length() && value[cursor] != '=' && value[cursor] != ',' &&
                       value[cursor] != ')' && value[cursor] != '(' && value[cursor] != '"')
                    ++cursor;
                if (cursor < value.length() && value[cursor] == '=') {
                    String key = value.substr(start, cursor - start).strip_edges();
                    ++cursor;
                    fields[key] = parse_value();
                } else {
                    cursor = start;
                    sequence.append(parse_value());
                }
                space();
                if (cursor < value.length() && value[cursor] == ',')
                    ++cursor;
                else if (cursor >= value.length() || value[cursor] != ')') {
                    error("Expected a compound-value separator.");
                    break;
                }
            }
            if (cursor < value.length() && value[cursor] == ')')
                ++cursor;
            else
                error("Unclosed compound T3D value.");
            if (!fields.is_empty() && !sequence.is_empty())
                error("Mixed named/positional compound T3D value.");
            return fields.is_empty() ? Variant(sequence) : Variant(fields);
        }
        int64_t start = cursor;
        while (cursor < value.length() && value[cursor] != ',' && value[cursor] != ')' &&
               value[cursor] != '(' && value[cursor] != '\n' && value[cursor] != '\r')
            ++cursor;
        String word = value.substr(start, cursor - start).strip_edges();
        if (cursor < value.length() && value[cursor] == '(')
            return dict({{"constructor", word}, {"arguments", parse_value()}});
        if (word == "True")
            return true;
        if (word == "False")
            return false;
        if (word.is_valid_int() && word.length() < 16)
            return word.to_int();
        if (word.is_valid_float() && (word.contains(".") || word.contains("e") || word.contains("E")))
            return word.to_float();
        return word;
    }
    Variant read(const String &raw) {
        value = raw;
        cursor = 0;
        Variant result = parse_value();
        space();
        if (cursor < value.length())
            error("Trailing T3D value data: " + value.substr(cursor, 80));
        return result;
    }
    bool unclosed(const String &raw) {
        int64_t depth = 0;
        bool quoted = false, escaped = false;
        for (int64_t i = 0; i < raw.length(); ++i) {
            char32_t ch = raw[i];
            if (ch == '"' && !escaped)
                quoted = !quoted;
            if (!quoted) {
                if (ch == '(')
                    ++depth;
                else if (ch == ')')
                    --depth;
            }
            escaped = ch == '\\' && !escaped;
        }
        return quoted || depth > 0;
    }
};
} // namespace
String NBNativeT3DParser::object_path(const String &reference) {
    return reference.contains("'") ? reference.get_slice("'", 1) : reference;
}
Dictionary NBNativeT3DParser::parse_file(const String &path) {
    if (!FileAccess::file_exists(path)) {
        Array errors;
        errors.append(dict({{"severity", "error"}, {"message", "T3D file not found: " + path}}));
        return dict({{"root", ""}, {"objects", Dictionary()}, {"diagnostics", errors}});
    }
    Dictionary result = parse(FileAccess::get_file_as_string(path));
    result["source_file"] = path;
    result["sha256"] = FileAccess::get_sha256(path);
    return result;
}
Dictionary NBNativeT3DParser::parse(const String &text) {
    Reader reader;
    Dictionary objects;
    Dictionary result = dict({{"root", ""}, {"objects", objects}, {"diagnostics", reader.errors}});
    PackedStringArray stack;
    Ref<RegEx> headers, indexed, remove;
    headers.instantiate();
    indexed.instantiate();
    remove.instantiate();
    headers->compile("(\\w+)=(\"(?:[^\"\\\\]|\\\\.)*\"|[^\\s]+)");
    indexed->compile("^([A-Za-z0-9_]+)\\(([0-9]+)\\)$");
    remove->compile("^([A-Za-z0-9_]+)\\.RemoveIndex\\(([0-9]+)\\)$");
    PackedStringArray lines = text.split("\n");
    int64_t index = 0;
    while (index < lines.size()) {
        reader.line = index + 1;
        String line = lines[index++].strip_edges();
        if (line.is_empty() || line.begins_with("//"))
            continue;
        if (line.begins_with("Begin Object ")) {
            Dictionary header;
            TypedArray<RegExMatch> matches = headers->search_all(line);
            for (int64_t i = 0; i < matches.size(); ++i) {
                Ref<RegExMatch> match = matches[i];
                header[match->get_string(1)] = reader.read(match->get_string(2));
            }
            String name = Variant(header.get("Name", "")).stringify(),
                   export_path = Variant(header.get("ExportPath", "")).stringify(),
                   path = object_path(export_path);
            if (path.is_empty())
                path = (stack.is_empty() ? String() : stack[stack.size() - 1] + ":") + name;
            if (name.is_empty())
                reader.error("Object has no Name.");
            if (!objects.has(path))
                objects[path] = dict({{"id", path},
                                      {"name", name},
                                      {"class", ""},
                                      {"parent", stack.is_empty() ? String() : stack[stack.size() - 1]},
                                      {"properties", Dictionary()},
                                      {"commands", Array()},
                                      {"line", reader.line}});
            Dictionary object = objects[path];
            if (header.has("Class")) {
                String cls = Variant(header["Class"]).stringify();
                object["class"] = cls.get_slice(".", cls.get_slice_count(".") - 1);
            }
            if (String(object["class"]).is_empty() && export_path.contains("'"))
                object["class"] = export_path.get_slice("'", 0).get_slice(".", 1);
            if (String(result["root"]).is_empty())
                result["root"] = path;
            stack.append(path);
        } else if (line == "End Object") {
            if (stack.is_empty())
                reader.error("Unexpected End Object.");
            else
                stack.resize(stack.size() - 1);
        } else if (stack.is_empty())
            reader.error("Property outside an object: " + line.left(80));
        else {
            Dictionary object = objects[stack[stack.size() - 1]], properties = object["properties"];
            Ref<RegExMatch> removal = remove->search(line);
            if (removal.is_valid()) {
                Array commands = object["commands"];
                commands.append(line);
                String key = removal->get_string(1);
                int64_t remove_index = removal->get_string(2).to_int();
                Variant data = properties.get(key, Variant());
                if (data.get_type() == Variant::ARRAY && remove_index < Array(data).size()) {
                    Array array = data;
                    array.remove_at(remove_index);
                }
                continue;
            }
            int64_t separator = line.find("=");
            if (separator < 1) {
                reader.error("Unsupported T3D object statement: " + line.left(100));
                continue;
            }
            String key = line.substr(0, separator).strip_edges(), raw = line.substr(separator + 1);
            while (reader.unclosed(raw) && index < lines.size())
                raw += "\n" + lines[index++];
            Variant value = reader.read(raw);
            Ref<RegExMatch> indexed_key = indexed->search(key);
            if (indexed_key.is_valid()) {
                key = indexed_key->get_string(1);
                int64_t element = indexed_key->get_string(2).to_int();
                if (!properties.has(key))
                    properties[key] = Array();
                if (Variant(properties[key]).get_type() != Variant::ARRAY) {
                    reader.error("Indexed property conflicts with scalar: " + key);
                    continue;
                }
                Array array = properties[key];
                if (array.size() <= element)
                    array.resize(element + 1);
                array[element] = value;
            } else
                properties[key] = value;
        }
    }
    if (!stack.is_empty())
        reader.error("Unclosed object block.");
    return result;
}
void NBNativeT3DParser::_bind_methods() {
    ClassDB::bind_static_method("NBNativeT3DParser", D_METHOD("parse", "text"), &NBNativeT3DParser::parse);
    ClassDB::bind_static_method("NBNativeT3DParser", D_METHOD("parse_file", "path"),
                                &NBNativeT3DParser::parse_file);
    ClassDB::bind_static_method("NBNativeT3DParser", D_METHOD("object_path", "reference"),
                                &NBNativeT3DParser::object_path);
}
} // namespace godot
