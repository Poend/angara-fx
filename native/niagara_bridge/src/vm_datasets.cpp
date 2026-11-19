#include "particle_buffer.h"
#include "vm_datasets.h"
#include "vm_scalar.h"
#include "vm_columns.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/quaternion.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
using namespace godot;
namespace {
int iv(const Dictionary &d, const String &key, int fallback = 0) {
    return int(int64_t(d.get(key, fallback)));
}
bool uint_value(const Variant &v) {
    if (v.get_type() != Variant::INT && v.get_type() != Variant::FLOAT)
        return false;
    double n = v;
    return std::isfinite(n) && n >= 0 && n <= 4294967295. && n == std::floor(n);
}
Dictionary column_map() {
    Dictionary d;
    d["float32"] = Dictionary();
    d["int32"] = Dictionary();
    return d;
}
Dictionary raw_components() {
    Dictionary d;
    d["float32"] = Array();
    d["int32"] = Array();
    return d;
}
Dictionary failure(const String &message, const char *field) {
    Dictionary d;
    TypedArray<String> e;
    e.append(message);
    d["errors"] = e;
    d[field] = Array();
    return d;
}
void append_errors(TypedArray<String> &target, const Variant &values) {
    Array a = values;
    for (int i = 0; i < a.size(); i++)
        target.append(a[i]);
}
struct Type {
    const char *source;
    const char *type;
    int width;
    bool integer;
};
constexpr Type TYPES[] = {{"NiagaraFloat", "float", 1, false}, {"NiagaraInt32", "int", 1, true},
                          {"NiagaraBool", "bool", 1, true},    {"Vector2f", "vec2", 2, false},
                          {"Vector3f", "vec3", 3, false},      {"NiagaraPosition", "position", 3, false},
                          {"Vector4f", "vec4", 4, false},      {"LinearColor", "color", 4, false},
                          {"Quat4f", "quat", 4, false}};
Type type_for(const String &source) {
    for (auto t : TYPES)
        if (source == t.source)
            return t;
    return {"", "", 0, false};
}
struct Field {
    const char *attribute, *type, *view;
};
constexpr Field FIELDS[] = {{"Position", "position", "position"},
                            {"Velocity", "vec3", "velocity"},
                            {"Age", "float", "age"},
                            {"NormalizedAge", "float", "normalized_age"},
                            {"Lifetime", "float", "lifetime"},
                            {"Alive", "bool", "alive"},
                            {"Color", "color", "color"},
                            {"Mass", "float", "mass"},
                            {"SpriteSize", "vec2", "size"},
                            {"SpriteAlignment", "vec3", "alignment"},
                            {"SpriteRotation", "float", "rotation"},
                            {"UVScale", "vec2", ""},
                            {"Scale", "vec3", "mesh_scale"},
                            {"MeshOrientation", "quat", "mesh_orientation"},
                            {"MeshIndex", "int", "mesh_index"},
                            {"SubImageIndex", "float", "subimage_index"},
                            {"MaterialRandom", "float", "material_random"},
                            {"CameraOffset", "float", "camera_offset"},
                            {"DynamicMaterialParameter", "vec4", "dynamic"},
                            {"DynamicMaterialParameter1", "vec4", "dynamic1"},
                            {"DynamicMaterialParameter2", "vec4", "dynamic2"},
                            {"DynamicMaterialParameter3", "vec4", "dynamic3"},
                            {"RibbonWidth", "float", "ribbon_width"},
                            {"RibbonFacing", "vec3", "ribbon_facing"}};
constexpr Field EXTRA[] = {{"UniqueID", "", "id"},
                           {"Initial.Color", "", "initial_color"},
                           {"Initial.SpriteSize", "", "initial_size"},
                           {"Initial.Scale", "", "initial_mesh_scale"},
                           {"Initial.MeshOrientation", "", "initial_mesh_orientation"},
                           {"Initial.Position", "", "initial_position"},
                           {"InitialRotation", "", "initial_rotation"},
                           {"Previous.Velocity", "", "previous_velocity"},
                           {"Previous.Position", "", "previous_position"},
                           {"Previous.Scale", "", "previous_mesh_scale"},
                           {"Previous.MeshOrientation", "", "previous_mesh_orientation"}};
bool compatible(const String &a, const String &b) {
    return a == b || ((a == "position" || a == "vec3") && (b == "position" || b == "vec3")) ||
           ((a == "color" || a == "vec4") && (b == "color" || b == "vec4"));
}
Variant coerce(const Dictionary &raw, const Type &type) {
    Array values = raw.get(type.integer ? "int32" : "float32", Array());
    if (type.width == 0)
        return raw;
    if (type.width == 1) {
        if (String(type.type) == "bool")
            return bool(int64_t(values[0]) != 0);
        if (type.integer)
            return nbvm::signed_word(uint32_t(int64_t(values[0])));
        return double(float(double(values[0])));
    }
    if (type.width == 2)
        return Vector2(float(double(values[0])), float(double(values[1])));
    if (type.width == 3)
        return Vector3(float(double(values[0])), float(double(values[1])), float(double(values[2])));
    if (String(type.type) == "color")
        return Color(float(double(values[0])), float(double(values[1])), float(double(values[2])),
                     float(double(values[3])));
    if (String(type.type) == "quat")
        return Quaternion(float(double(values[0])), float(double(values[1])), float(double(values[2])),
                          float(double(values[3])));
    return Vector4(float(double(values[0])), float(double(values[1])), float(double(values[2])),
                   float(double(values[3])));
}
Variant column_value(const Type &type, int kind, const std::vector<nbvm::ColumnView> &floats,
                     const std::vector<nbvm::ColumnView> &ints, int64_t out) {
                const auto &columns = type.integer ? ints : floats;
                auto component = [&](int n) { return float(columns[n].number_at(out)); };
                switch (type.width) {
                case 1:
                    return  kind == 1      ? Variant(columns[0].number_at(out) != 0.)
                            : type.integer ? Variant(int64_t(int32_t(columns[0].number_at(out))))
                                                 : Variant(double(component(0)));

                case 2:
                    return  Vector2(component(0), component(1));

                case 3:
                    return  Vector3(component(0), component(1), component(2));

                default:
                    if (kind == 2)
                        return  Color(component(0), component(1), component(2), component(3));
                    else if (kind == 3)
                        return  Quaternion(component(0), component(1), component(2), component(3));
                    else
                        return  Vector4(component(0), component(1), component(2), component(3));
                }
    return Variant();
}
} // namespace

void NBNativeVMDataset::_bind_methods() {
    ClassDB::bind_method(D_METHOD("configure", "layout"), &NBNativeVMDataset::configure);
    ClassDB::bind_method(D_METHOD("pack", "values", "bindings", "dataset_index"), &NBNativeVMDataset::pack);
    ClassDB::bind_method(D_METHOD("unpack", "dataset", "lane"), &NBNativeVMDataset::unpack, DEFVAL(0));
    ClassDB::bind_static_method("NBNativeVMDataset", D_METHOD("components", "value", "row"),
                                &NBNativeVMDataset::components);
    ClassDB::bind_static_method("NBNativeVMDataset", D_METHOD("value", "raw"), &NBNativeVMDataset::value);
    ClassDB::bind_method(D_METHOD("get_variables"), &NBNativeVMDataset::get_variables);
    ClassDB::bind_method(D_METHOD("get_columns"), &NBNativeVMDataset::get_columns);
    ClassDB::bind_method(D_METHOD("get_errors"), &NBNativeVMDataset::get_errors);
    ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "variables", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE),
                 "", "get_variables");
    ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "columns", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE),
                 "", "get_columns");
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "errors", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "",
                 "get_errors");
}
TypedArray<String> NBNativeVMDataset::configure(const Variant &layout) {
    variables = Dictionary();
    columns = column_map();
    errors.clear();
    if (layout.get_type() != Variant::DICTIONARY ||
        Dictionary(layout).get("variables", Variant()).get_type() != Variant::ARRAY) {
        errors.append("Native dataset has no variable layout.");
        return errors;
    }
    Array rows = Dictionary(layout)["variables"];
    for (int n = 0; n < rows.size(); n++) {
        if (rows[n].get_type() != Variant::DICTIONARY) {
            errors.append("Malformed native dataset variable.");
            continue;
        }
        Dictionary row = rows[n];
        String name = row.get("name", "");
        if (row.get("name", Variant()).get_type() != Variant::STRING || name.is_empty() ||
            variables.has(name) || row.get("type", Variant()).get_type() != Variant::STRING) {
            errors.append("Invalid/duplicate native dataset variable.");
            continue;
        }
        variables[name] = row.duplicate(true);
        if (!uint_value(row.get("float16_components", Variant())) || iv(row, "float16_components") != 0)
            errors.append("Half native datasets are not integrated.");
        for (String type : {String("float32"), String("int32")}) {
            String start = type + "_start", count = type + "_components";
            if (!uint_value(row.get(start, Variant())) || !uint_value(row.get(count, Variant())) ||
                int64_t(row[start]) + int64_t(row[count]) > 65536) {
                errors.append("Invalid native dataset component range.");
                continue;
            }
            Dictionary target = columns[type];
            for (int component = 0; component < iv(row, count); component++) {
                int index = iv(row, start) + component;
                if (target.has(index))
                    errors.append("Overlapping native dataset components.");
                Dictionary binding;
                binding["name"] = name;
                binding["component"] = component;
                target[index] = binding;
            }
        }
    }
    return errors;
}
Dictionary NBNativeVMDataset::components(const Variant &input, const Dictionary &row) {
    if (input.get_type() == Variant::DICTIONARY) {
        Dictionary d = input;
        if (d.get("float32", Variant()).get_type() == Variant::ARRAY &&
            d.get("int32", Variant()).get_type() == Variant::ARRAY)
            return d;
    }
    Array floats, ints;
    if (iv(row, "int32_components") > 0 && iv(row, "float32_components") == 0) {
        if (input.get_type() == Variant::BOOL)
            ints.append(bool(input) ? -1 : 0);
        else if (input.get_type() == Variant::INT || input.get_type() == Variant::FLOAT)
            ints.append(input);
        else if (input.get_type() == Variant::ARRAY)
            ints = Array(input).duplicate();
    } else if (iv(row, "float32_components") > 0 && iv(row, "int32_components") == 0) {
        switch (input.get_type()) {
        case Variant::INT:
        case Variant::FLOAT:
            floats.append(double(input));
            break;
        case Variant::VECTOR2: {
            Vector2 v = input;
            floats = Array::make(v.x, v.y);
            break;
        }
        case Variant::VECTOR3: {
            Vector3 v = input;
            floats = Array::make(v.x, v.y, v.z);
            break;
        }
        case Variant::VECTOR4: {
            Vector4 v = input;
            floats = Array::make(v.x, v.y, v.z, v.w);
            break;
        }
        case Variant::QUATERNION: {
            Quaternion v = input;
            floats = Array::make(v.x, v.y, v.z, v.w);
            break;
        }
        case Variant::COLOR: {
            Color v = input;
            floats = Array::make(v.r, v.g, v.b, v.a);
            break;
        }
        case Variant::ARRAY:
            floats = Array(input).duplicate();
            break;
        default:
            break;
        }
    }
    Dictionary result;
    result["float32"] = floats;
    result["int32"] = ints;
    return result;
}
Dictionary NBNativeVMDataset::pack(const Dictionary &values, const Array &bindings, int dataset_index) const {
    Dictionary result, dataset = raw_components();
    TypedArray<String> problems = errors.duplicate();
    result["errors"] = problems;
    result["dataset"] = dataset;
    for (int n = 0; n < bindings.size(); n++) {
        Dictionary binding = bindings[n];
        if (iv(binding, "dataset") != dataset_index)
            continue;
        String type = binding.get("type", "");
        int index = iv(binding, "component");
        Dictionary available = columns.get(type, Dictionary());
        if (!available.has(index)) {
            problems.append("VM input has no native dataset component.");
            continue;
        }
        Dictionary column = available[index];
        String name = column["name"];
        if (!values.has(name)) {
            problems.append("Missing native dataset input: " + name);
            continue;
        }
        Dictionary row = variables[name], raw = components(values[name], row);
        Array v = raw[type];
        int component = iv(column, "component");
        if (v.size() != iv(row, type + "_components") || component >= v.size()) {
            problems.append("Invalid native dataset input components: " + name);
            continue;
        }
        Array output = dataset[type];
        while (output.size() <= index)
            output.append(Array());
        output[index] = Array::make(v[component]);
    }
    return result;
}
Dictionary NBNativeVMDataset::unpack(const Dictionary &dataset, int lane) const {
    Dictionary result, values;
    TypedArray<String> problems = errors.duplicate();
    result["errors"] = problems;
    result["values"] = values;
    if (lane < 0) {
        problems.append("Negative native dataset lane.");
        return result;
    }
    const Array floats = dataset.get("float32", Array()), ints = dataset.get("int32", Array());
    Array names = variables.keys();
    for (int n = 0; n < names.size(); n++) {
        String name = names[n];
        Dictionary row = variables[name], raw = raw_components();
        auto read_columns = [&](const Array &input, Array target, int first, int count) {
            for (int c = 0; c < count; c++) {
                int index = first + c;
                nbvm::ColumnView column(index >= 0 && index < input.size() ? input[index] : Variant());
                if (lane >= column.size) {
                    problems.append("Missing native dataset output: " + name);
                    continue;
                }
                target.append(column.at(lane));
            }
        };
        read_columns(floats, raw["float32"], iv(row, "float32_start"), iv(row, "float32_components"));
        read_columns(ints, raw["int32"], iv(row, "int32_start"), iv(row, "int32_components"));
        values[name] = raw;
    }
    if (!problems.is_empty())
        values.clear();
    return result;
}
Variant NBNativeVMDataset::value(const Dictionary &raw) {
    Array floats = raw.get("float32", Array()), ints = raw.get("int32", Array());
    if (ints.is_empty())
        return floats.size() == 1 ? floats[0] : Variant(floats.duplicate());
    if (floats.is_empty())
        return ints.size() == 1 ? ints[0] : Variant(ints.duplicate());
    return raw.duplicate(true);
}

NBNativeVMParticles::NBNativeVMParticles() {
    executor.instantiate();
}
Dictionary NBNativeVMParticles::type_table() {
    Dictionary result;
    for (auto type : TYPES)
        result[type.source] = type.type;
    return result;
}
Array NBNativeVMParticles::existing_particles(const Array &particles) {
    Array result;
    for (int n = 0; n < particles.size(); n++) {
        Dictionary particle = particles[n];
        if (!bool(particle.get("vm_needs_spawn", false)))
            result.append(particle);
    }
    return result;
}
void NBNativeVMParticles::clear_spawn_flags(const Array &particles) {
    for (int n = 0; n < particles.size(); n++) {
        Dictionary particle = particles[n];
        particle.erase("vm_needs_spawn");
    }
}
void NBNativeVMParticles::_bind_methods() {
    ClassDB::bind_method(D_METHOD("import_buffer", "particles"), &NBNativeVMParticles::import_buffer);
    ClassDB::bind_method(D_METHOD("run_buffer", "buffer", "current", "previous", "random_stream",
                                  "event_inputs", "id_state", "particle_offset"),
                         &NBNativeVMParticles::run_buffer, DEFVAL(Variant()), DEFVAL(Dictionary()),
                         DEFVAL(Variant()), DEFVAL(0));

    ClassDB::bind_static_method("NBNativeVMParticles", D_METHOD("existing_particles", "particles"),
                                &NBNativeVMParticles::existing_particles);
    ClassDB::bind_static_method("NBNativeVMParticles", D_METHOD("clear_spawn_flags", "particles"),
                                &NBNativeVMParticles::clear_spawn_flags);
    ClassDB::bind_method(D_METHOD("configure", "program", "interfaces", "parameters", "contract"),
                         &NBNativeVMParticles::configure, DEFVAL(Array()), DEFVAL(Dictionary()),
                         DEFVAL(Dictionary()));
    ClassDB::bind_method(D_METHOD("pack", "particles", "packed_columns"), &NBNativeVMParticles::pack,
                         DEFVAL(false));
    ClassDB::bind_method(D_METHOD("unpack", "dataset", "templates", "kept", "include_raw_fields"),
                         &NBNativeVMParticles::unpack, DEFVAL(true));
    ClassDB::bind_method(D_METHOD("run", "particles", "current", "previous", "random_stream", "event_inputs",
                                  "id_state", "particle_offset"),
                         &NBNativeVMParticles::run, DEFVAL(Variant()), DEFVAL(Dictionary()),
                         DEFVAL(Variant()), DEFVAL(0));
    ClassDB::bind_static_method("NBNativeVMParticles", D_METHOD("parameter_values", "values"),
                                &NBNativeVMParticles::parameter_values);
    ClassDB::bind_static_method("NBNativeVMParticles", D_METHOD("type_table"),
                                &NBNativeVMParticles::type_table);
    ClassDB::bind_static_method("NBNativeVMParticles",
                                D_METHOD("append_spawn_templates", "emitter", "count", "time", "delta",
                                         "start_dt", "interval", "source_start", "source_end"),
                                &NBNativeVMParticles::append_spawn_templates);
    ClassDB::bind_static_method("NBNativeVMParticles", D_METHOD("project_field", "particle", "row", "raw"),
                                &NBNativeVMParticles::project_field);
    ClassDB::bind_method(D_METHOD("get_executor"), &NBNativeVMParticles::get_executor);
    ClassDB::bind_method(D_METHOD("get_variables"), &NBNativeVMParticles::get_variables);
    ClassDB::bind_method(D_METHOD("get_columns"), &NBNativeVMParticles::get_columns);
    ClassDB::bind_method(D_METHOD("get_event_layouts"), &NBNativeVMParticles::get_event_layouts);
    ClassDB::bind_method(D_METHOD("get_ready"), &NBNativeVMParticles::get_ready);
    ClassDB::bind_method(D_METHOD("set_capture_execution", "value"),
                         &NBNativeVMParticles::set_capture_execution);
    ClassDB::bind_method(D_METHOD("get_capture_execution"), &NBNativeVMParticles::get_capture_execution);
    ClassDB::bind_method(D_METHOD("get_last_execution"), &NBNativeVMParticles::get_last_execution);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "executor", PROPERTY_HINT_RESOURCE_TYPE, "NBNativeVMExecutor",
                              PROPERTY_USAGE_NONE),
                 "", "get_executor");
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "variables", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "",
                 "get_variables");
    ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "columns", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE),
                 "", "get_columns");
    ADD_PROPERTY(
        PropertyInfo(Variant::DICTIONARY, "event_layouts", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "",
        "get_event_layouts");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "ready", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "",
                 "get_ready");
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "capture_execution", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE),
        "set_capture_execution", "get_capture_execution");
    ADD_PROPERTY(
        PropertyInfo(Variant::DICTIONARY, "last_execution", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "",
        "get_last_execution");
}
TypedArray<String> NBNativeVMParticles::configure(const Dictionary &program, const Array &interfaces,
                                                  const Dictionary &parameters, const Dictionary &contract) {
    ++layout_revision;
    ready = false;
    external_defaults = Dictionary();
    variables = Array();
    columns = column_map();
    event_layouts = Dictionary();
    event_layouts["inputs"] = Dictionary();
    event_layouts["outputs"] = Dictionary();
    TypedArray<String> errors = executor->configure(program, interfaces, parameters);
    if (!errors.is_empty())
        return errors;
    if (contract.get("external_parameter_defaults", Array()).get_type() != Variant::ARRAY) {
        errors.append("Invalid native particle external defaults.");
        return errors;
    }
    Array defaults = contract.get("external_parameter_defaults", Array());
    if (!defaults.is_empty() && contract.get("external_parameter_defaults_producer", "") !=
                                    Variant("FNiagaraScriptRuntimeData.GetExecutionReadyParameterStore"))
        errors.append("Native particle external defaults lack provenance.");
    for (int n = 0; n < defaults.size(); n++) {
        if (defaults[n].get_type() != Variant::DICTIONARY) {
            errors.append("Invalid native particle external parameter.");
            continue;
        }
        Dictionary row = defaults[n];
        String name = row.get("name", "");
        if (row.get("name", Variant()).get_type() != Variant::STRING || external_defaults.has(name) ||
            row.get("float32", Variant()).get_type() != Variant::ARRAY ||
            row.get("int32", Variant()).get_type() != Variant::ARRAY) {
            errors.append("Invalid native particle external parameter.");
            continue;
        }
        external_defaults[name] = row.duplicate(true);
    }
    if (iv(program, "output_dataset_count") < 1)
        errors.append("Particle VM requires an output dataset.");
    for (String direction : {String("inputs"), String("outputs")}) {
        Variant value = program.get("event_" + direction, Array());
        if (value.get_type() != Variant::ARRAY) {
            errors.append("Malformed native event table.");
            continue;
        }
        Array rows = value;
        Dictionary targets = event_layouts[direction];
        for (int n = 0; n < rows.size(); n++) {
            if (rows[n].get_type() != Variant::DICTIONARY) {
                errors.append("Invalid native event dataset.");
                continue;
            }
            Dictionary row = rows[n];
            int index = iv(row, "dataset");
            String name = row.get("name", "");
            if (!uint_value(row.get("dataset", Variant())) || index < 1 || name.is_empty() ||
                targets.has(index)) {
                errors.append("Invalid/duplicate native event dataset.");
                continue;
            }
            Ref<NBNativeVMDataset> adapter;
            adapter.instantiate();
            append_errors(errors, adapter->configure(row.get("layout", Variant())));
            Dictionary item;
            item["name"] = name;
            item["adapter"] = adapter;
            targets[index] = item;
        }
    }
    if ((iv(program, "input_dataset_count") > 1 || iv(program, "output_dataset_count") > 1) &&
        program.get("event_dataset_producer", "") !=
            Variant("FNiagaraEmitterScriptProperties.EventGenerators; FNiagaraEmitterInstanceImpl.BindData"))
        errors.append("Native event dataset provenance is absent.");
    Variant layout = program.get("particle_dataset", Dictionary());
    Ref<NBNativeVMDataset> dataset;
    dataset.instantiate();
    append_errors(errors, dataset->configure(layout));
    if (!errors.is_empty())
        return errors;
    variables = Dictionary(layout)["variables"];
    columns = dataset->get_columns();
    if (variables.is_empty()) {
        errors.append("Particle VM has no attribute layout.");
        return errors;
    }
    for (int n = 0; n < variables.size(); n++) {
        Dictionary row = variables[n];
        String name = row["name"], source_type = row["type"];
        Type type = type_for(source_type);
        if (type.width && (iv(row, "int32_components") != (type.integer ? type.width : 0) ||
                           iv(row, "float32_components") != (type.integer ? 0 : type.width)))
            errors.append("Native particle type/component mismatch: " + name);
        for (auto field : FIELDS)
            if (name == field.attribute && !compatible(type.type, field.type))
                errors.append("Native particle type differs from renderer binding: " + name);
    }
    for (String direction : {String("inputs"), String("outputs")}) {
        Array mappings = program[direction];
        for (int n = 0; n < mappings.size(); n++) {
            Dictionary binding = mappings[n], layout_columns = columns;
            int index = iv(binding, "dataset");
            if (index > 0) {
                Dictionary direction_layouts = event_layouts[direction];
                if (!direction_layouts.has(index)) {
                    errors.append("VM event register has no dataset layout.");
                    continue;
                }
                Dictionary item = direction_layouts[index];
                Ref<NBNativeVMDataset> adapter = item["adapter"];
                layout_columns = adapter->get_columns();
            }
            Dictionary available = layout_columns.get(binding.get("type", ""), Dictionary());
            if (!available.has(iv(binding, "component")))
                errors.append("VM register has no native dataset attribute binding.");
        }
    }
    Array mappings = program["outputs"];
    for (String type : {String("float32"), String("int32")}) {
        Dictionary available = columns[type];
        Array keys = available.keys();
        for (int n = 0; n < keys.size(); n++) {
            bool found = false;
            for (int j = 0; j < mappings.size(); j++) {
                Dictionary row = mappings[j];
                if (iv(row, "dataset") == 0 && row.get("type", "") == Variant(type) &&
                    iv(row, "component") == int64_t(keys[n])) {
                    found = true;
                    break;
                }
            }
            if (!found)
                errors.append("Particle program does not write persisted component.");
        }
    }
    ready = errors.is_empty();
    return errors;
}
Dictionary NBNativeVMParticles::parameter_values(const Dictionary &values) {
    Dictionary result;
    Array names = values.keys();
    for (int n = 0; n < names.size(); n++) {
        Variant value = values[names[n]];
        if (value.get_type() == Variant::BOOL)
            result[names[n]] = bool(value) ? -1 : 0;
        else if (value.get_type() == Variant::VECTOR2 || value.get_type() == Variant::VECTOR3 ||
                 value.get_type() == Variant::VECTOR4 || value.get_type() == Variant::QUATERNION ||
                 value.get_type() == Variant::COLOR) {
            Dictionary row;
            row["float32_components"] = 1;
            row["int32_components"] = 0;
            Dictionary raw = NBNativeVMDataset::components(value, row);
            result[names[n]] = raw["float32"];
        } else
            result[names[n]] = value;
    }
    return result;
}
Dictionary NBNativeVMParticles::pack(const Array &particles, bool packed_columns) const {
    if (!ready)
        return failure("Configure a valid particle VM adapter first.", "datasets");
    Dictionary result;
    TypedArray<String> errors;
    Array datasets;
    Dictionary dataset = raw_components();
    datasets.append(dataset);
    result["errors"] = errors;
    result["datasets"] = datasets;
    Array bindings = executor->source_program()["inputs"];
    for (int n = 0; n < bindings.size(); n++) {
        Dictionary binding = bindings[n];
        if (iv(binding, "dataset") != 0)
            continue;
        String type = binding["type"];
        int index = iv(binding, "component");
        Dictionary layout = Dictionary(columns[type])[index];
        String name = layout["name"];
        int component = iv(layout, "component");
        Array column;
        PackedFloat32Array float_column;
        PackedInt32Array int_column;
        float *float_data = nullptr;
        int32_t *int_data = nullptr;
        if (!packed_columns)
            column.resize(particles.size());
        else if (type == "float32") {
            float_column.resize(particles.size());
            float_data = float_column.ptrw();
        } else {
            int_column.resize(particles.size());
            int_data = int_column.ptrw();
        }
        for (int lane = 0; lane < particles.size(); lane++) {
            Dictionary particle = particles[lane], fields = particle.get("_vm_fields", Dictionary()),
                       raw = fields.get(name, Dictionary());
            if (raw.get(type, Variant()).get_type() != Variant::ARRAY ||
                Array(raw[type]).size() <= component) {
                errors.append("Missing persisted particle attribute: " + name + "/" + type);
                return result;
            }
            Variant value = Array(raw[type])[component];
            if (!packed_columns)
                column[lane] = value;
            else {
                bool numeric = value.get_type() == Variant::INT || value.get_type() == Variant::FLOAT;
                if (type == "int32") {
                    if (!numeric || !std::isfinite(double(value)) ||
                        double(value) != std::floor(double(value)) || double(value) < -2147483648. ||
                        double(value) > 2147483647.) {
                        errors.append("Invalid persisted int32 particle attribute: " + name);
                        return result;
                    }
                    int_data[lane] = int32_t(int64_t(value));
                } else if (numeric)
                    float_data[lane] = float(double(value));
                else if (value == Variant("NaN"))
                    float_data[lane] = NAN;
                else if (value == Variant("Infinity"))
                    float_data[lane] = INFINITY;
                else if (value == Variant("-Infinity"))
                    float_data[lane] = -INFINITY;
                else {
                    errors.append("Invalid persisted float32 particle attribute: " + name);
                    return result;
                }
            }
        }
        Array output = dataset[type];
        while (output.size() <= index)
            output.append(Array());
        output[index] = !packed_columns     ? Variant(column)
                        : type == "float32" ? Variant(float_column)
                                            : Variant(int_column);
    }
    return result;
}
Dictionary NBNativeVMParticles::unpack(const Dictionary &dataset, const Array &templates, const Array &kept,
                                       bool include_raw_fields) const {
    return project_dataset(dataset, templates, kept, variables, include_raw_fields);
}
Dictionary NBNativeVMParticles::project_dataset(const Dictionary &dataset, const Array &templates,
                                                const Array &kept, const Array &layout,
                                                bool include_raw_fields, bool deep_metadata,
                                                const PackedInt32Array &update_indices,
                                                const PackedInt32Array &update_counts) {
    Dictionary result;
    TypedArray<String> errors;
    Array particles;
    result["errors"] = errors;
    result["particles"] = particles;
    if ((!update_indices.is_empty() || !update_counts.is_empty()) &&
        (update_indices.size() != templates.size() || update_counts.size() != templates.size())) {
        errors.append("Invalid resident tick metadata columns.");
        return result;
    }
    struct Field {
        Dictionary row;
        Type type;
        String name, attribute, view;
        int projection = 0, kind = 0;
        std::vector<nbvm::ColumnView> floats, ints;
    };
    std::vector<Field> fields;
    fields.reserve(layout.size());
    for (int n = 0; n < layout.size(); n++) {
        Field field;
        field.row = layout[n];
        String name = field.row["name"];
        field.name = name;
        field.attribute = String("Particles.") + name;
        field.type = type_for(field.row["type"]);
        field.kind = String(field.type.type) == "bool"    ? 1
                     : String(field.type.type) == "color" ? 2
                     : String(field.type.type) == "quat"  ? 3
                                                          : 0;
        for (auto item : EXTRA)
            if (name == item.attribute) {
                field.view = item.view;
                break;
            }
        for (auto item : FIELDS)
            if (name == item.attribute) {
                field.view = item.view;
                field.projection = name == "UVScale"                              ? 1
                                   : name == "SpriteRotation"                     ? 2
                                   : name.begins_with("DynamicMaterialParameter") ? 3
                                                                                  : 0;
                break;
            }
        for (String type : {String("float32"), String("int32")}) {
            Array input_columns = dataset.get(type, Array());
            auto &views = type == "float32" ? field.floats : field.ints;
            for (int c = 0; c < iv(field.row, type + "_components"); c++) {
                int index = iv(field.row, type + "_start") + c;
                nbvm::ColumnView column(index < input_columns.size() ? input_columns[index] : Variant());
                if (column.size < kept.size()) {
                    errors.append("Missing VM output attribute: " + String(field.row["name"]));
                    return result;
                }
                views.push_back(std::move(column));
            }
        }
        fields.push_back(std::move(field));
    }
    for (int out = 0; out < kept.size(); out++) {
        int input = int(int64_t(kept[out]));
        if (input < 0 || input >= templates.size()) {
            errors.append("Invalid particle compaction source index.");
            return result;
        }
        Dictionary particle = Dictionary(templates[input]).duplicate(deep_metadata);
        // Match the old preparation order without duplicating the template on
        // every tick. This dictionary is already isolated for the snapshot.
        if (!update_indices.is_empty() && update_indices[input] >= 0) {
            particle["update_execution_index"] = int64_t(update_indices[input]);
            particle["update_execution_count"] = int64_t(update_counts[input]);
        }
        Dictionary attributes = particle.get("attributes", Dictionary()), raw_fields;
        if (!deep_metadata)
            attributes = attributes.duplicate(true);
        particle["attributes"] = attributes;
        if (include_raw_fields)
            particle["_vm_fields"] = raw_fields;
        else
            particle.erase("_vm_fields");
        for (const auto &field : fields) {
            Dictionary raw;
            if (include_raw_fields || field.type.width == 0) {
                raw = raw_components();
                for (String type : {String("float32"), String("int32")}) {
                    Array values = raw[type];
                    const auto &views = type == "float32" ? field.floats : field.ints;
                    for (const auto &column : views)
                        values.append(column.at(out));
                }
                if (include_raw_fields)
                    raw_fields[field.name] = raw;
            }
            Variant value;
            if (field.type.width == 0)
                value = raw;
            else {
                value = column_value(field.type, field.kind, field.floats, field.ints, out);
            }
            attributes[field.attribute] = value;
            if (field.projection == 1) {
                Vector2 uv = value;
                particle["uv_flip"] = uv.x;
                particle["uv_flip_y"] = uv.y;
            } else if (field.projection == 2)
                particle[field.view] = double(value) * (3.14159265358979323846 / 180.);
            else if (field.projection == 3) {
                Vector4 vector = value;
                particle[field.view] = Color(vector.x, vector.y, vector.z, vector.w);
            } else if (!field.view.is_empty())
                particle[field.view] = value;
        }
        particle["alive"] = true;
        particle["newborn"] = false;
        particles.append(particle);
    }
    return result;
}
Ref<NBNativeParticleView> NBNativeVMParticles::create_view(const Dictionary &dataset, const Array &metadata,
                                                           const Array &layout, const PackedInt32Array &indices,
                                                           const PackedInt32Array &counts) {
    Ref<NBNativeParticleView> view;
    view.instantiate();
    view->initialize(metadata);
    if ((!indices.is_empty() || !counts.is_empty()) && (indices.size() != metadata.size() || counts.size() != metadata.size())) {
        view->add_error("Invalid resident tick metadata columns.");
        return view;
    }
    struct AttributeColumns {
        std::vector<nbvm::ColumnView> floats, ints;
    };
    const int64_t fields = layout.size(), lanes = metadata.size();
    auto storage = std::make_shared<std::vector<AttributeColumns>>(fields);
    Array float_inputs = dataset.get("float32", Array()), int_inputs = dataset.get("int32", Array());
    for (int64_t n = 0; n < fields; ++n) {
        Dictionary row = layout[n];
        String name = row["name"];
        Type type = type_for(row["type"]);
        int kind = String(type.type) == "bool" ? 1 : String(type.type) == "color" ? 2 : String(type.type) == "quat" ? 3 : 0;
        // Attribute, field and typed renderer accessors all retain the same
        // immutable columns, even if a caller keeps a view across frames.
        std::shared_ptr<AttributeColumns> binding(storage, &(*storage)[n]);
        auto bind = [&](const Array &input, std::vector<nbvm::ColumnView> &columns, int start, int count) {
            for (int c = 0; c < count; ++c) {
                int index = start + c;
                nbvm::ColumnView column(index >= 0 && index < input.size() ? input[index] : Variant());
                if (column.size < lanes) return false;
                columns.push_back(std::move(column));
            }
            return true;
        };
        if (!bind(float_inputs, binding->floats, iv(row, "float32_start"), iv(row, "float32_components")) ||
            !bind(int_inputs, binding->ints, iv(row, "int32_start"), iv(row, "int32_components"))) {
            view->add_error("Missing VM output attribute: " + name);
            return view;
        }
        if (type.width > int((type.integer ? binding->ints : binding->floats).size())) {
            view->add_error("Invalid VM output component width: " + name);
            return view;
        }
        std::function<Variant(int64_t)> read = [type, kind, binding](int64_t lane) -> Variant {
            if (type.width) return column_value(type, kind, binding->floats, binding->ints, lane);
            Dictionary raw;
            Array f, i;
            for (const auto &column : binding->floats) f.append(column.at(lane));
            for (const auto &column : binding->ints) i.append(column.at(lane));
            raw["float32"] = f; raw["int32"] = i;
            return raw;
        };
        view->add_attribute(String("Particles.") + name, read);
        String field;
        for (auto item : EXTRA) if (name == item.attribute) { field = item.view; break; }
        for (auto item : FIELDS) if (name == item.attribute) { field = item.view; break; }
        if (name == "UVScale") {
            view->add_field("uv_flip", [read](int64_t lane) { return double(Vector2(read(lane)).x); });
            view->add_field("uv_flip_y", [read](int64_t lane) { return double(Vector2(read(lane)).y); });
        } else if (name == "SpriteRotation")
            view->add_field(field, [read](int64_t lane) { return double(read(lane)) * (3.14159265358979323846 / 180.); });
        else if (name.begins_with("DynamicMaterialParameter"))
            view->add_field(field, [read](int64_t lane) { Vector4 v = read(lane); return Color(v.x, v.y, v.z, v.w); });
        else if (!field.is_empty()) view->add_field(field, read);
        if(!field.is_empty() && !type.integer && type.width>0) {
            auto component=[binding](int64_t lane,int n){return float(binding->floats[size_t(n)].number_at(lane));};
            if(name=="SpriteRotation")
                view->add_native_field<double>(field,[component](int64_t lane){return double(component(lane,0))*(3.14159265358979323846/180.);});
            else if(name.begins_with("DynamicMaterialParameter") || kind==2)
                view->add_native_field<Color>(field,[component](int64_t lane){return Color(component(lane,0),component(lane,1),component(lane,2),component(lane,3));});
            else if(kind==3)
                view->add_native_field<Quaternion>(field,[component](int64_t lane){return Quaternion(component(lane,0),component(lane,1),component(lane,2),component(lane,3));});
            else if(type.width==1)
                view->add_native_field<double>(field,[component](int64_t lane){return double(component(lane,0));});
            else if(type.width==2 && name!="UVScale")
                view->add_native_field<Vector2>(field,[component](int64_t lane){return Vector2(component(lane,0),component(lane,1));});
            else if(type.width==3)
                view->add_native_field<Vector3>(field,[component](int64_t lane){return Vector3(component(lane,0),component(lane,1),component(lane,2));});
        }
        if(name=="UVScale") {
            view->add_native_field<double>("uv_flip",[binding](int64_t lane){return double(float(binding->floats[0].number_at(lane)));});
            view->add_native_field<double>("uv_flip_y",[binding](int64_t lane){return double(float(binding->floats[1].number_at(lane)));});
        }
    }
    view->add_field("alive", [](int64_t) { return true; });
    view->add_field("newborn", [](int64_t) { return false; });
    if (!indices.is_empty()) {
        view->add_field("update_execution_index", [indices, metadata](int64_t lane) -> Variant {
            return indices[lane] >= 0 ? Variant(int64_t(indices[lane])) : Dictionary(metadata[lane]).get("update_execution_index", Variant());
        });
        view->add_field("update_execution_count", [indices, counts, metadata](int64_t lane) -> Variant {
            return indices[lane] >= 0 ? Variant(int64_t(counts[lane])) : Dictionary(metadata[lane]).get("update_execution_count", Variant());
        });
    }
    return view;
}
void NBNativeVMParticles::project_field(Dictionary particle, const Dictionary &row, const Dictionary &raw) {
    String name = row["name"];
    Dictionary fields = particle.get("_vm_fields", Dictionary());
    fields[name] = raw;
    particle["_vm_fields"] = fields;
    Type type = type_for(row["type"]);
    Variant value = coerce(raw, type);
    Dictionary attributes = particle.get("attributes", Dictionary());
    attributes["Particles." + name] = value;
    particle["attributes"] = attributes;
    for (auto field : EXTRA)
        if (name == field.attribute) {
            particle[field.view] = value;
            return;
        }
    for (auto field : FIELDS)
        if (name == field.attribute) {
            if (name == "UVScale") {
                Vector2 uv = value;
                particle["uv_flip"] = uv.x;
                particle["uv_flip_y"] = uv.y;
            } else if (name == "SpriteRotation")
                particle[field.view] = double(value) * (3.14159265358979323846 / 180.);
            else if (name.begins_with("DynamicMaterialParameter")) {
                Vector4 color = value;
                particle[field.view] = Color(color.x, color.y, color.z, color.w);
            } else
                particle[field.view] = value;
            return;
        }
}
Dictionary NBNativeVMParticles::run(const Array &particles, const Dictionary &current,
                                    const Dictionary &previous, const Ref<NBNativeVMRandom> &random,
                                    const Dictionary &event_inputs, const Ref<NBNativeVMIDs> &ids,
                                    int offset) {
    Dictionary packed = pack(particles, !capture_execution);
    if (!Array(packed["errors"]).is_empty()) {
        Dictionary result;
        result["errors"] = packed["errors"];
        result["particles"] = Array();
        return result;
    }
    Array sets = packed["datasets"];
    return execute_dataset(sets[0], particles, current, previous, random, event_inputs, ids, offset,
                           Ref<NBNativeParticleBuffer>());
}
Dictionary NBNativeVMParticles::import_buffer(const Array &particles) {
    Dictionary result = pack(particles, true);
    if (!Array(result["errors"]).is_empty())
        return result;
    Ref<NBNativeParticleBuffer> buffer;
    buffer.instantiate();
    Array sets = result["datasets"];
    buffer->initialize(Ref<NBNativeVMParticles>(this), sets[0], particles, true);
    result["buffer"] = buffer;
    return result;
}
Dictionary NBNativeVMParticles::run_buffer(const Ref<NBNativeParticleBuffer> &buffer,
                                           const Dictionary &current, const Dictionary &previous,
                                           const Ref<NBNativeVMRandom> &random,
                                           const Dictionary &event_inputs, const Ref<NBNativeVMIDs> &ids,
                                           int offset) {
    if (!ready || buffer.is_null())
        return failure("Configure an adapter and resident particle buffer first.", "particles");
    Dictionary input =
        buffer->input_for(Ref<NBNativeVMParticles>(this), executor->source_program()["inputs"]);
    if (!Array(input["errors"]).is_empty())
        return input;
    return execute_dataset(input["dataset"], buffer->metadata(), current, previous, random, event_inputs, ids,
                           offset, buffer);
}
Dictionary NBNativeVMParticles::execute_dataset(const Dictionary &dataset, const Array &particles,
                                                const Dictionary &current, const Dictionary &previous,
                                                const Ref<NBNativeVMRandom> &random,
                                                const Dictionary &event_inputs, const Ref<NBNativeVMIDs> &ids,
                                                int offset, const Ref<NBNativeParticleBuffer> &resident) {
    if (offset < 0)
        return failure("Negative native particle dataset offset.", "particles");
    Array sets;
    Dictionary primary = dataset.duplicate();
    for (String type : {String("float32"), String("int32")})
        primary[type] = Array(primary.get(type, Array())).duplicate();
    sets.append(primary);
    primary["instance_offset"] = offset;
    if (offset > 0)
        for (String type : {String("float32"), String("int32")}) {
            Array columns = primary[type];
            for (int c = 0; c < columns.size(); c++) {
                Variant column = columns[c];
                if (column.get_type() == Variant::PACKED_FLOAT32_ARRAY) {
                    PackedFloat32Array values = column, prefixed;
                    prefixed.resize(offset + values.size());
                    float *target = prefixed.ptrw();
                    std::fill(target, target + offset, 0.f);
                    if (values.size())
                        std::memcpy(target + offset, values.ptr(), values.size() * sizeof(float));
                    columns[c] = prefixed;
                    continue;
                }
                if (column.get_type() == Variant::PACKED_INT32_ARRAY) {
                    PackedInt32Array values = column, prefixed;
                    prefixed.resize(offset + values.size());
                    int32_t *target = prefixed.ptrw();
                    std::fill(target, target + offset, 0);
                    if (values.size())
                        std::memcpy(target + offset, values.ptr(), values.size() * sizeof(int32_t));
                    columns[c] = prefixed;
                    continue;
                }
                Array prefixed;
                prefixed.resize(offset);
                prefixed.fill(0);
                prefixed.append_array(columns[c]);
                columns[c] = prefixed;
            }
        }
    Dictionary event_layout_inputs = event_layouts["inputs"];
    Array event_indices = event_layout_inputs.keys();
    for (int n = 0; n < event_indices.size(); n++) {
        int index = int(int64_t(event_indices[n]));
        if (event_inputs.get(index, Variant()).get_type() != Variant::DICTIONARY)
            return failure("Missing native event input dataset.", "particles");
        while (sets.size() <= index)
            sets.append(Dictionary());
        sets[index] = event_inputs[index];
    }
    Dictionary parameters;
    if (capture_execution) {
        Dictionary current_values = external_defaults.duplicate(), previous_values = external_defaults.duplicate();
        current_values.merge(parameter_values(current), true);
        previous_values.merge(parameter_values(previous), true);
        parameters["current"] = current_values;
        parameters["previous"] = previous_values;
        last_execution = Dictionary();
        last_execution["parameters"] = parameters.duplicate(true);
        last_execution["datasets"] = sets.duplicate(true);
        last_execution["instance_count"] = particles.size();
        Ref<NBNativeVMReaders> readers = executor->get_particle_context();
        if (readers.is_valid())
            last_execution["reader_sources"] =
                readers->capture(executor->get_curves(), executor->get_reader_scope());
        if (ids.is_valid()) {
            Array trace_sets = last_execution["datasets"];
            Dictionary trace_primary = trace_sets[0];
            trace_primary["persistent_ids"] = ids->snapshot();
        }
    }
    if (!capture_execution) {
        parameters["current"] = current;
        parameters["previous"] = previous;
    }
    Dictionary id_states;
    if (ids.is_valid())
        id_states[0] = ids;
    Dictionary actual =
        executor->execute_frame(parameters, sets, int(particles.size()), random, id_states, !capture_execution,
                                capture_execution ? nullptr : &external_defaults);
    if (capture_execution) {
        last_execution["result"] = actual.duplicate(true);
        if (ids.is_valid()) {
            Dictionary trace_result = last_execution["result"];
            trace_result["persistent_ids"] = ids->snapshot();
        }
    }
    if (!Array(actual["errors"]).is_empty()) {
        Dictionary result;
        result["errors"] = actual["errors"];
        result["particles"] = Array();
        return result;
    }
    Array output_sets = actual["datasets"], indices = actual["dataset_indices"];
    Dictionary decoded, events;
    if (resident.is_valid()) {
        Array kept = indices[0], metadata;
        for (int n = 0; n < kept.size(); ++n) {
            int64_t index = kept[n];
            if (index < 0 || index >= particles.size())
                return failure("Invalid resident particle compaction index.", "particles");
            metadata.append(particles[index]);
        }
        Ref<NBNativeParticleBuffer> output;
        output.instantiate();
        output->initialize(Ref<NBNativeVMParticles>(this), output_sets[0], metadata);
        output->inherit_tick_metadata(resident, kept);
        decoded["buffer"] = output;
        decoded["errors"] = TypedArray<String>();
    } else
        decoded = unpack(output_sets[0], particles, indices[0]);
    decoded["datasets"] = output_sets;
    decoded["events"] = events;
    Dictionary layouts = event_layouts["outputs"];
    Array keys = layouts.keys(), counts = actual["dataset_counts"];
    for (int n = 0; n < keys.size(); n++) {
        int index = int(int64_t(keys[n]));
        Dictionary row = layouts[index], event;
        event["data"] = output_sets[index];
        event["count"] = counts[index];
        events[row["name"]] = event;
    }
    return decoded;
}
