#include "gpu_program.h"
#include "effect_compiler.h"
#include "compiler_frontend.h"
#include "attribute_values.h"
#include "value_evaluator.h"
#include <godot_cpp/core/class_db.hpp>
#include <cmath>

namespace godot {
void NBNativeGPUProgram::_bind_methods() {
    ClassDB::bind_static_method("NBNativeGPUProgram", D_METHOD("build", "plan", "parameters", "contracts"),
                                &NBNativeGPUProgram::build);
}
Dictionary NBNativeGPUProgram::build(const Dictionary &plan, const Dictionary &parameters,
                                     const Dictionary &contracts) {
    return nbgpu::Builder(contracts).build(plan, parameters);
}
} // namespace godot
namespace godot::nbgpu {
using nbcompiler::equal;
using nbcompiler::named;
using nbcompiler::reference;
String cat(std::initializer_list<String> parts) {
    String result;
    for (const String &part : parts)
        result += part;
    return result;
}
String integer(int64_t value) {
    return String::num_int64(value);
}
String joined(const Array &values, const String &delimiter) {
    String result;
    for (int i = 0; i < values.size(); ++i) {
        if (i)
            result += delimiter;
        result += String(values[i]);
    }
    return result;
}
Builder::Builder(const Dictionary &c)
    : contracts(c), builtins(c["builtins"]), specs(c["dynamic_specs"]), fields(c["lifecycle_fields"]),
      references(c["references"]), cache_limit(c["cache_slots"]), stride(c["stride"]) {}
Dictionary Builder::build(const Dictionary &plan, const Dictionary &p) {
    user_parameters = p;
    reader_modules = plan.get("gpu_readers",Dictionary());
    attribute_layout = plan.get("attribute_layout", Dictionary());
    attribute_types = plan.get("attribute_types", Dictionary());
    interpolated_spawn = bool(plan.get("interpolated_spawn", false));
    Ref<Resource> emitter = plan.get("source", Variant());
    if (emitter.is_valid()) {
        emitter_deterministic = bool(NBNativeValues::input_value(emitter->get("settings"), "Determinism", false));
        emitter_seed = int64_t(NBNativeValues::input_value(emitter->get("settings"), "RandomSeed", 0));
    }
    Array rows = attribute_layout.get("particle", Array());
    for (int i = 0; i < rows.size(); ++i) {
        Dictionary row = rows[i];
        attribute_rows[row["name"]] = row;
    }
    Dictionary rng = plan.get("source_rng", Dictionary());
    if (bool(rng.get("supported", false)))
        source_rng = rng;
    Dictionary stages = plan["stages"];
    stage_name = "ParticleSpawn";
    Dictionary defaults = plan.get("source_output_defaults", Dictionary());
    literal_outputs = defaults.get(stage_name, Dictionary());
    String spawn = stage(stages.get("ParticleSpawn", Array()));
    for (int i = 0; i < rows.size(); ++i) {
        Dictionary row = rows[i];
        if (row.has("spawn_default"))
            spawn = "p.attributes[" + integer(row["slot"]) + "]=" + literal(row["spawn_default"], "vec4") + ";\n" + spawn;
    }
    stage_name = "ParticleUpdate";
    literal_outputs = defaults.get(stage_name, Dictionary());
    String update = stage(stages.get("ParticleUpdate", Array()));
    String source = String(contracts["kernel"])
                        .replace("/*NB_HELPERS*/", joined(helpers, "\n"))
                        .replace("/*NB_SPAWN*/", spawn)
                        .replace("/*NB_UPDATE*/", update);
    source = source.replace("/*NB_BIRTH_UPDATE*/",
                            equal(plan.get("spawn_update_mode", "RunUpdateScript"), String("NoInterpolation"))
                                ? "&& !newborn"
                                : "");
    int count = attribute_rows.size();
    String loads, stores, initial;
    Array values = attribute_rows.values();
    for (int i = 0; i < values.size(); ++i) {
        Dictionary row = values[i];
        String slot = row["slot"].stringify(), base = integer(stride + int64_t(row["slot"]));
        loads += cat({"p.attributes[", slot, "]=particles.data[base+", base, "u];\n"});
        stores += cat({"particles.data[base+", base, "u]=p.attributes[", slot, "];\n"});
    }
    Dictionary initial_names = attribute_layout.get("initial", Dictionary());
    Array keys = initial_names.keys();
    for (int i = 0; i < keys.size(); ++i)
        initial +=
            cat({"p.attributes[", Dictionary(attribute_rows[keys[i]])["slot"].stringify(), "]=p.attributes[",
                 Dictionary(attribute_rows[initial_names[keys[i]]])["slot"].stringify(), "];\n"});
    source = source.replace("/*NB_STRIDE*/", integer(stride + count) + "u")
                 .replace("/*NB_ATTRIBUTE_FIELDS*/",
                          count > 0 ? cat({"vec4 attributes[", integer(count), "];"}) : String())
                 .replace("/*NB_ATTRIBUTE_LOAD*/", loads)
                 .replace("/*NB_ATTRIBUTE_STORE*/", stores)
                 .replace("/*NB_ATTRIBUTE_INITIAL*/", initial);
    Dictionary result;
    if (!reader_columns.is_empty()) {
        source = source.replace("NB_READER_BASE", integer(8 + parameters.size()));
        result["reader_columns"] = reader_columns;
    }
    result["supported"] = errors.is_empty();
    result["errors"] = errors;
    result["cache_slots"] = cache_slots;
    result["parameters"] = parameters;
    result["static_parameters"] = static_parameters;
    result["source"] = source;
    result["attribute_layout"] = attribute_layout.get("particle", Array());
    result["stride_vec4"] = stride + count;
    result["vector_fields"] = vector_fields;
    return result;
}
String Builder::stage(const Array &operations) {
    Array code;
    module_outputs.clear();
    module_output_types.clear();
    for (int i = 0; i < operations.size(); ++i) {
        Ref<Resource> op = operations[i];
        if (!bool(op->get("enabled")))
            continue;
        Dictionary names = NBNativeCompilerAttributes::module_outputs(op, contracts["module_schemas"]);
        Array keys = names.keys();
        for (int n = 0; n < keys.size(); ++n) {
            if (module_outputs.has(keys[n]))
                continue;
            String identifier = "nb_module_output_" + integer(module_outputs.size());
            module_outputs[keys[n]] = identifier;
            String type = names[keys[n]];
            module_output_types[keys[n]] = type;
            code.append(type + " " + identifier + (type == "bool" ? "=false;" : "=vec3(0.0);"));
        }
    }
    for (int i = 0; i < operations.size(); ++i) {
        Ref<Resource> op = operations[i];
        if (bool(op->get("enabled"))) {
            operation = op;
            String text = module();
            code.append(cat({"{ // ", op->get("module_name"), "\n", text, "\n}"}));
        }
    }
    return joined(code, "\n");
}
String Builder::input(const String &name, const Variant &fallback, const String &type, int occurrence) {
    return expression(operation->call("get_input", name, fallback, occurrence), type,
                      cat({operation->get("stable_id"), "/", name, "/", integer(occurrence)}));
}
Variant Builder::source_curve(const String &path, const Variant &fallback) {
    return Dictionary(Dictionary(source_rng.get("curves", Dictionary())).get(stage_name, Dictionary()))
        .get(path, fallback);
}
String Builder::typed_read(const String &expr, const String &actual, const String &expected) {
    if (actual == expected)
        return expr;
    if (named(actual, {"float", "int"}) && named(expected, {"float", "int"}))
        return cat({expected, "(", expr, ")"});
    errors.append(cat({"GPU reference type mismatch: ", actual, " -> ", expected}));
    return cat({expected, "(0)"});
}
String Builder::assignment() {
    Array inputs = operation->get("inputs");
    if (inputs.size() != 1)
        return unsupported("typed assignment value");
    String name = operation->get("module_name"),
           type = NBNativeAttributeValues::shader_type(
               NBNativeAttributeValues::type_name(operation->get("target_type")));
    if (type.is_empty())
        return unsupported("assignment type");
    Ref<Resource> value = inputs[0];
    String expr = expression(value->get("value"), type, cat({operation->get("stable_id"), "/assignment"}));
    if (name == "Particles.UVScale")
        return cat({"vec2 assigned_uv=", expr, "; p.uv_flip=assigned_uv.x; p.uv_flip_y=assigned_uv.y;"});
    if (builtins.has(name)) {
        String target = Array(builtins[name])[2];
        if (target.is_empty())
            return unsupported("built-in attribute GPU storage: " + name);
        if (name == "Particles.Alive")
            expr = cat({"(", expr, "?1.0:0.0)"});
        else if (name == "Particles.MeshIndex")
            expr = cat({"float(", expr, ")"});
        else if (name == "Particles.SpriteRotation")
            expr = cat({"radians(", expr, ")"});
        return cat({target, "=", expr, ";"});
    }
    if (!attribute_rows.has(name))
        return unsupported("undeclared GPU attribute");
    String packed = type == "bool"    ? cat({"vec4(", expr, "?1.0:0.0,0,0,0)"})
                    : type == "int"   ? cat({"vec4(intBitsToFloat(", expr, "),0,0,0)"})
                    : type == "float" ? cat({"vec4(", expr, ",0,0,0)"})
                    : type == "vec2"  ? cat({"vec4(", expr, ",0,0)"})
                    : type == "vec3"  ? cat({"vec4(", expr, ",0)"})
                                      : expr;
    return cat({"p.attributes[", Dictionary(attribute_rows[name])["slot"].stringify(), "]=", packed, ";"});
}
String Builder::literal(const Variant &v, const String &type) {
    if (type == "bool")
        return bool(v) ? "true" : "false";
    if (type == "int")
        return integer(int64_t(v));
    if (type == "float")
        return number(v);
    Array parts;
    if (v.get_type() == Variant::ARRAY)
        parts = Array(v).duplicate();
    else if (v.get_type() == Variant::VECTOR2) {
        Vector2 a = v;
        parts = Array::make(a.x, a.y);
    } else if (v.get_type() == Variant::VECTOR3) {
        Vector3 a = v;
        parts = Array::make(a.x, a.y, a.z);
    } else if (v.get_type() == Variant::COLOR) {
        Color a = v;
        parts = Array::make(a.r, a.g, a.b, a.a);
    } else if (v.get_type() == Variant::QUATERNION) {
        Quaternion a = v;
        parts = Array::make(a.x, a.y, a.z, a.w);
    } else if (v.get_type() == Variant::VECTOR4) {
        Vector4 a = v;
        parts = Array::make(a.x, a.y, a.z, a.w);
    } else if (nbcompiler::number(v))
        return cat({type, "(", number(v), ")"});
    else {
        errors.append(cat({"Invalid GPU ", type, " literal."}));
        return cat({type, "(0.0)"});
    }
    int count = type.right(1).to_int();
    while (parts.size() < count)
        parts.append(parts.size() == 3 ? 1.0 : 0.0);
    Array strings;
    for (int i = 0; i < count; ++i)
        strings.append(number(parts[i]));
    return cat({type, "(", joined(strings, ","), ")"});
}
String Builder::number(const Variant &v) {
    if ((!nbcompiler::number(v) && v.get_type() != Variant::BOOL) || !std::isfinite(double(v))) {
        errors.append("GPU numeric input must be a finite number.");
        return "0.0";
    }
    String result = Variant(double(v)).stringify();
    return result.contains(".") || result.contains("e") ? result : result + ".0";
}
String Builder::mode(const String &key, const String &fallback) {
    return NBNativeValues::enum_name(operation->call("get_input", key, fallback));
}
bool Builder::flag(const String &key, bool fallback) {
    Variant value = operation->call("get_input", key, fallback);
    if (value.get_type() == Variant::BOOL)
        return value;
    errors.append(cat({"GPU control input requires an authored boolean constant: ", key}));
    return fallback;
}
String Builder::space(const String &key, const String &fallback) {
    String value = mode(key, fallback);
    if (!named(value, {"Local", "World", "Simulation", "SimulationSpace"}))
        errors.append("Unsupported GPU coordinate space: " + value);
    return value == "Local" ? "1" : value == "World" ? "2" : "0";
}
String Builder::orientation_space() {
    return mode("RotationCoordinateSpace", "Mesh") == "Mesh" ? String("0")
                                                             : space("RotationCoordinateSpace", "Mesh");
}
String Builder::unsupported(const String &branch) {
    errors.append(cat({"GPU ", branch, " is unsupported in ", operation->get("module_name"), "."}));
    return "";
}
} // namespace godot::nbgpu
