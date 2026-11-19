#include "gpu_particle_reader.h"
#include "particle_view.h"
#include <godot_cpp/classes/reg_ex.hpp>
#include <godot_cpp/classes/reg_ex_match.hpp>
#include <cstring>

namespace godot::nbreader {
namespace {
Ref<RegEx> pattern(const char *expression) {
    return RegEx::create_from_string(expression);
}
String as_glsl(const String &type) {
    return type == "float3" ? String("vec3") : type;
}
} // namespace
Dictionary lower(const String &body, const String &map, const String &alias) {
    const auto declaration = pattern("^(int|bool|float3|float) (\\w+)(?: = (.+))?$");
    const auto assignment = pattern("^([\\w.]+) = (.+)$");
    const auto fetch =
        pattern("^GetVectorByIndex_(\\w+)_Attribute(Position|Velocity)\\(([\\w.]+), (\\w+), (\\w+)\\)$");
    const auto token =
        pattern("[A-Za-z_]\\w*(?:\\.[A-Za-z_]\\w*)*|(?:[0-9]*\\.)?[0-9]+(?:[eE][+-]?[0-9]+)?|[,+*/()-]");
    Dictionary locals, types, pins, row, integer_literals;
    String code, reader;
    bool writes = false;
    auto resolve = [&](const String &name, const String &type, bool target) -> String {
        String base = name.get_slice(".", 0);
        if (locals.has(base)) {
            String tail = name.substr(base.length());
            if (!tail.is_empty() && tail != ".x" && tail != ".y" && tail != ".z")
                return "";
            return String(locals[base]) + tail;
        }
        const String context = "Context." + map + ".";
        if (name == context + String("Particles.Position"))
            return "p.position";
        if (name == context + String("Particles.Velocity"))
            return "p.velocity";
        const String prefix = context + alias + ".";
        if (!target && name.begins_with(prefix)) {
            String pin = name.trim_prefix(prefix);
            if (!pin.is_valid_ascii_identifier() || (pins.has(pin) && pins[pin] != Variant(type)))
                return "";
            pins[pin] = type;
            return "NB_INPUT_" + pin;
        }
        return "";
    };
    PackedStringArray lines = body.strip_edges().split(";");
    for (int i = 0; i < lines.size(); ++i) {
        String line = lines[i].strip_edges();
        if (line.is_empty())
            continue;
        auto call = fetch->search(line);
        if (call.is_valid()) {
            if (!reader.is_empty())
                return {};
            String index = call->get_string(3), valid = call->get_string(4), value = call->get_string(5);
            String index_expression = resolve(index, "int", false);
            if (index_expression.is_empty() || (locals.has(index) && types[index] != Variant("int")) ||
                types.get(valid, "") != Variant("bool") || types.get(value, "") != Variant("vec3"))
                return {};
            reader = call->get_string(1);
            row["attribute"] = call->get_string(2);
            if (integer_literals.has(index))
                row["fixed_index"] = integer_literals[index];
            code += "nb_read_vector(NB_COLUMN, " + index_expression + " , " + String(locals[valid]) + ", " +
                    String(locals[value]) + ");\n";
            continue;
        }
        auto decl = declaration->search(line);
        auto store = decl.is_valid() ? Ref<RegExMatch>() : assignment->search(line);
        if (decl.is_null() && store.is_null())
            return {};
        String target, type, expression, key;
        if (decl.is_valid()) {
            key = decl->get_string(2);
            if (locals.has(key))
                return {};
            target = "nb_v" + String::num_int64(locals.size());
            type = as_glsl(decl->get_string(1));
            locals[key] = target;
            types[key] = type;
            code += type + " " + target;
            expression = decl->get_string(3);
            if (expression.is_empty()) {
                code += ";\n";
                continue;
            }
        } else {
            key = store->get_string(1);
            target = resolve(key, "", true);
            if (target.is_empty())
                return {};
            type = types.get(key.get_slice(".", 0), "vec3");
            if (target.ends_with(".x") || target.ends_with(".y") || target.ends_with(".z"))
                type = "float";
            writes |= target == "p.position" || target == "p.velocity";
            code += target;
            expression = store->get_string(2);
        }
        integer_literals.erase(key);
        if (type == "int" && expression.is_valid_int() && expression.to_int() >= INT32_MIN &&
            expression.to_int() <= INT32_MAX)
            integer_literals[key] = expression.to_int();
        code += " = ";
        int end = 0;
        for (const Ref<RegExMatch> &part : token->search_all(expression)) {
            if (!expression.substr(end, part->get_start() - end).strip_edges().is_empty())
                return {};
            String word = part->get_string();
            if ((word[0] >= 'A' && word[0] <= 'Z') || (word[0] >= 'a' && word[0] <= 'z') || word[0] == '_') {
                if (word == "float3")
                    word = "vec3";
                else
                    word = resolve(word, type, false);
                if (word.is_empty())
                    return {};
            }
            code += word + " ";
            end = part->get_end();
        }
        if (!expression.substr(end).strip_edges().is_empty())
            return {};
        code += ";\n";
    }
    if (!writes || reader.is_empty())
        return {};
    row["di_symbol"] = reader;
    row["pins"] = pins;
    row["code"] = code;
    row["alias"] = alias;
    return row;
}

Dictionary bind(const Dictionary &extracted, const Ref<Resource> &stage, const Array &interfaces) {
    Dictionary result;
    if (stage.is_null())
        return result;
    Array ops = stage->get("operations"), aliases = extracted.keys();
    for (int a = 0; a < aliases.size(); ++a) {
        String alias = aliases[a];
        Dictionary row = extracted[alias];
        Ref<Resource> match;
        for (int n = 0; n < ops.size(); ++n) {
            Ref<Resource> op = ops[n];
            String name = String(op->get("module_name")).get_file(), label = op->get("alias_name");
            if (!label.is_empty() ? label != alias : (name != alias && !name.ends_with("_" + alias)))
                continue;
            if (match.is_valid()) {
                match.unref();
                break;
            }
            match = op;
        }
        if (match.is_null())
            continue;
        String source;
        bool has_attribute = false;
        for (int i = 0; i < interfaces.size(); ++i) {
            Dictionary info = interfaces[i], binding = info.get("particle_reader", Dictionary());
            String symbol = String(info.get("compile_name", "")).replace(".", "_").replace(" ", "");
            if (symbol != String(row["di_symbol"]) || !bool(binding.get("resolved", false)))
                continue;
            if (!source.is_empty()) {
                source = "";
                break;
            }
            source = binding.get("source_emitter", "");
            Array attributes = Dictionary(binding.get("layout", Dictionary())).get("variables", Array());
            for (int v = 0; v < attributes.size(); ++v) {
                Dictionary item = attributes[v];
                String type = item.get("type", "");
                if (item.get("name", "") == row["attribute"] &&
                    (type == "NiagaraPosition" || type == "Vector3f"))
                    has_attribute = true;
            }
        }
        if (source.is_empty())
            continue;
        row = row.duplicate();
        row["source"] = source;
        row["has_attribute"] = has_attribute;
        result[match->get("stable_id")] = row;
    }
    return result;
}

String pack(const Array &columns, const Array &emitters, PackedByteArray &packet) {
    if (columns.size() > INT32_MAX / 16 || packet.resize(columns.size() * 16) != OK)
        return "Cannot allocate indexed particle reader headers.";
    Dictionary views;
    for (int i = 0; i < columns.size(); ++i) {
        Dictionary column = columns[i];
        String source = column["source"], attr = column["attribute"];
        if (!views.has(source)) {
            for (int n = 0; n < emitters.size(); ++n) {
                Dictionary emitter = emitters[n];
                Ref<Resource> asset = Dictionary(emitter["plan"])["source"];
                if (asset->get("display_name") == Variant(source)) {
                    views[source] = NBNativeParticleView::for_emitter(emitter);
                    break;
                }
            }
        }
        Ref<NBNativeParticleView> view = views.get(source, Variant());
        if (view.is_null() || !view->get_errors().is_empty())
            return "Missing particle reader source: " + source;
        int64_t handle = bool(column["has_attribute"]) ? view->field_handle(attr.to_lower()) : -1;
        int64_t count = handle < 0 ? 0 : view->get_count(), offset = packet.size() / 16, first = 0;
        if (column.has("fixed_index")) {
            int64_t index = column["fixed_index"];
            count = index >= 0 && index < count ? 1 : 0;
            first = std::max<int64_t>(0, index);
        }
        if (count > INT32_MAX / 16 - offset || packet.resize((offset + count) * 16) != OK)
            return "Particle reader buffer exceeds its capacity.";
        int32_t header[4] = {int32_t(offset), int32_t(count), int32_t(first), 0};
        std::memcpy(packet.ptrw() + i * 16, header, sizeof(header));
        auto *data = packet.ptrw() + offset * 16;
        for (int64_t j = 0; j < count; ++j) {
            Vector3 v = view->read_native<Vector3>(handle, first + j);
            float value[4] = {v.x, v.y, v.z, 0.f};
            std::memcpy(data + j * 16, value, sizeof(value));
        }
    }
    return "";
}
} // namespace godot::nbreader
