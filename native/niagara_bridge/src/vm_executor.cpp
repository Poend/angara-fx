#include "vm_program.h"
#include "vm_scalar.h"
#include "vm_ops.generated.h"
#include <godot_cpp/core/class_db.hpp>
#include <algorithm>
#include <chrono>
#include <memory>
#include <unordered_map>
#include <cstring>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>

using namespace godot;
namespace {
enum Kind {
    Primitive,
    Done,
    AcquireIndex,
    Output,
    ExecIndex,
    ExecIndexF,
    ExecIndexAdd,
    ExternalCall,
    Random,
    RandomAdd,
    Random2x,
    SinCos,
    AcquireId,
    UpdateId
};
int integer(const Dictionary &d, const char *key, int fallback = 0) {
    return int(int64_t(d.get(key, fallback)));
}
bool uint_value(const Variant &v) {
    if (v.get_type() != Variant::INT && v.get_type() != Variant::FLOAT)
        return false;
    double n = v;
    return std::isfinite(n) && n >= 0 && n <= 4294967295. && n == std::floor(n);
}
bool table(const Dictionary &d, const char *key) {
    if (d.get(key, Variant()).get_type() != Variant::ARRAY)
        return false;
    Array a = d[key];
    for (int i = 0; i < a.size(); i++)
        if (a[i].get_type() != Variant::DICTIONARY)
            return false;
    return true;
}
bool scalar_value(const Variant &v, bool is_int) {
    if (v.get_type() == Variant::STRING)
        return !is_int && (v == Variant("-0") || v == Variant("NaN") || v == Variant("Infinity") || v == Variant("-Infinity"));
    if (v.get_type() != Variant::INT && v.get_type() != Variant::FLOAT)
        return false;
    if (!is_int)
        return true;
    double n = v;
    return std::isfinite(n) && n == std::floor(n) && n >= -2147483648. && n <= 2147483647.;
}
uint32_t word(const Variant &v, bool is_int) {
    if (is_int)
        return uint32_t(int64_t(v));
    if (v.get_type() == Variant::STRING) {
        String s = v;
        if (s == "-0") return nbvm::bits(-0.f);
        return nbvm::bits(s == "Infinity" ? INFINITY : s == "-Infinity" ? -INFINITY : NAN);
    }
    return nbvm::bits(float(double(v)));
}
struct Register {
    std::shared_ptr<std::vector<uint32_t>> words;
    size_t offset = 0;
    bool broadcast = false;
    uint32_t read(size_t lane) const {
        return words->at(offset + (broadcast ? lane % 4 : lane));
    }
    void write(size_t lane, uint32_t value) {
        words->at(offset + lane) = value;
    }
};
Register storage(size_t count, bool broadcast = false) {
    return {std::make_shared<std::vector<uint32_t>>(count, 0), 0, broadcast};
}
void compact(const Register &source, Register &target, const std::vector<int> &kept, int count) {
    if (count > 4 && int(kept.size()) == count) {
        if (source.words != target.words || source.offset != target.offset)
            for (int n = 0; n < count; n++)
                target.write(n, source.read(n));
        return;
    }
    if (count > 4 && kept.empty())
        return;
    if (count > 4 && source.broadcast) {
        for (size_t n = 0; n < ((kept.size() + 3) & ~size_t(3)); n++)
            target.write(n, source.read(0));
        return;
    }
    size_t destination = 0, selected = 0;
    for (int group = 0; group < ((count + 3) & ~3); group += 4) {
        std::array<uint32_t, 4> values{};
        int advance = 0;
        while (selected < kept.size() && kept[selected] < group + 4)
            values[advance++] = source.read(kept[selected++]);
        for (int lane = 0; lane < 4; lane++)
            target.write(destination + lane, values[lane]);
        destination += advance;
    }
}
int kind_for(const String &name) {
    if (name == "done")
        return Done;
    if (name == "acquireindex")
        return AcquireIndex;
    if (name == "outputdata_float" || name == "outputdata_int32")
        return Output;
    if (name == "exec_index")
        return ExecIndex;
    if (name == "exec_indexf")
        return ExecIndexF;
    if (name == "exec_index_addi")
        return ExecIndexAdd;
    if (name == "external_func_call")
        return ExternalCall;
    if (name == "random")
        return Random;
    if (name == "random_add")
        return RandomAdd;
    if (name == "random_2x")
        return Random2x;
    if (name == "sin_cos")
        return SinCos;
    if (name == "acquire_id")
        return AcquireId;
    if (name == "update_id")
        return UpdateId;
    return Primitive;
}
} // namespace

void NBNativeVMExecutor::_bind_methods() {
    ClassDB::bind_static_method("NBNativeVMExecutor", D_METHOD("validate_program", "source"),
                                &NBNativeVMExecutor::validate_program);
    ClassDB::bind_static_method("NBNativeVMExecutor", D_METHOD("chunk_capacity", "source", "count"),
                                &NBNativeVMExecutor::chunk_capacity);
    ClassDB::bind_static_method("NBNativeVMExecutor", D_METHOD("parameter_component", "row", "parameters"),
                                &NBNativeVMExecutor::parameter_component);
    ClassDB::bind_static_method("NBNativeVMExecutor",
                                D_METHOD("_curve_table", "source", "interfaces", "parameters"),
                                &NBNativeVMExecutor::curve_table, DEFVAL(Dictionary()));
    ClassDB::bind_static_method("NBNativeVMExecutor", D_METHOD("_sample_curve", "curve", "time"),
                                &NBNativeVMExecutor::sample_curve);
    ClassDB::bind_static_method("NBNativeVMExecutor", D_METHOD("curve_parameter_names", "info", "owner"),
                                &NBNativeVMExecutor::curve_parameter_names, DEFVAL(String()));
    ClassDB::bind_method(D_METHOD("configure", "source", "interfaces", "parameters"),
                         &NBNativeVMExecutor::configure, DEFVAL(Array()), DEFVAL(Dictionary()));
    ClassDB::bind_method(D_METHOD("update_user_curves", "parameters"),
                         &NBNativeVMExecutor::update_user_curves);
    ClassDB::bind_method(
        D_METHOD("execute", "parameters", "datasets", "count", "random_stream", "id_states", "packed_output"),
        &NBNativeVMExecutor::execute, DEFVAL(Variant()), DEFVAL(Dictionary()), DEFVAL(false));
    ClassDB::bind_method(D_METHOD("get_program"), &NBNativeVMExecutor::get_program);
    ClassDB::bind_method(D_METHOD("get_curves"), &NBNativeVMExecutor::get_curves);
    ClassDB::bind_method(D_METHOD("get_interfaces"), &NBNativeVMExecutor::get_interfaces);
    ClassDB::bind_method(D_METHOD("get_errors"), &NBNativeVMExecutor::get_errors);
    ClassDB::bind_method(D_METHOD("get_statistics"), &NBNativeVMExecutor::get_statistics);
    ClassDB::bind_method(D_METHOD("set_particle_context", "value"),
                         &NBNativeVMExecutor::set_particle_context);
    ClassDB::bind_method(D_METHOD("get_particle_context"), &NBNativeVMExecutor::get_particle_context);
    ClassDB::bind_method(D_METHOD("set_collision_context", "value"),
                         &NBNativeVMExecutor::set_collision_context);
    ClassDB::bind_method(D_METHOD("get_collision_context"), &NBNativeVMExecutor::get_collision_context);
    ClassDB::bind_method(D_METHOD("set_reader_scope", "value"), &NBNativeVMExecutor::set_reader_scope);
    ClassDB::bind_method(D_METHOD("get_reader_scope"), &NBNativeVMExecutor::get_reader_scope);
    ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "program", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE),
                 "", "get_program");
    ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "curves", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "",
                 "get_curves");
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "_interfaces", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "",
                 "get_interfaces");
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "errors", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "",
                 "get_errors");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "particle_context", PROPERTY_HINT_RESOURCE_TYPE, "RefCounted",
                              PROPERTY_USAGE_NONE),
                 "set_particle_context", "get_particle_context");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "reader_scope", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE),
                 "set_reader_scope", "get_reader_scope");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "collision_context", PROPERTY_HINT_RESOURCE_TYPE,
                              "NBNativeCollisionWorld", PROPERTY_USAGE_NONE),
                 "set_collision_context", "get_collision_context");
}

TypedArray<String> NBNativeVMExecutor::validate_program(const Dictionary &d) {
    TypedArray<String> out;
    if (!uint_value(d.get("schema_version", Variant())) || int64_t(d.get("schema_version", 0)) != 1 ||
        d.get("available", Variant()).get_type() != Variant::BOOL ||
        d.get("complete", Variant()).get_type() != Variant::BOOL ||
        d.get("errors", Variant()).get_type() != Variant::ARRAY) {
        out.append("Malformed optimized VM program header.");
        return out;
    }
    if (!bool(d["available"]) || !bool(d["complete"])) {
        out.append("Optimized CPU program is unavailable or incomplete.");
        return out;
    }
    for (const char *key :
         {"temporary_register_count", "constant_register_count", "input_register_count",
          "output_register_count", "register_count", "source_byte_count", "trailing_done_padding",
          "constant_table_byte_size", "input_dataset_count", "output_dataset_count"})
        if (!uint_value(d.get(key, Variant())))
            out.append(String("Invalid VM count: ") + key);
    if (!out.is_empty())
        return out;
    if (integer(d, "register_count") > 65535 || integer(d, "input_dataset_count") > 256 ||
        integer(d, "output_dataset_count") > 256) {
        out.append("VM register/dataset count exceeds supported index space.");
        return out;
    }
    if (d.get("batch_layout", Variant()).get_type() != Variant::DICTIONARY)
        out.append("Invalid native VM batch layout.");
    else {
        Dictionary b = d["batch_layout"];
        if (!uint_value(b.get("overhead_bytes", Variant())) ||
            !uint_value(b.get("page_size_bytes", Variant())) || integer(b, "register_bytes") != 16 ||
            integer(b, "vector_width") != 4 || integer(b, "loop_count_alignment") != 4 ||
            b.get("has_random_instructions", Variant()).get_type() != Variant::BOOL)
            out.append("Invalid native VM batch layout.");
    }
    for (const char *key :
         {"constants", "inputs", "outputs", "external_functions", "instructions", "constant_buffers"})
        if (!table(d, key))
            out.append(String("Invalid VM table: ") + key);
    if (!out.is_empty())
        return out;
    Array cs = d["constants"], ins = d["inputs"], outs = d["outputs"], ext = d["external_functions"],
          instructions = d["instructions"];
    int temporary = integer(d, "temporary_register_count"), constant = integer(d, "constant_register_count"),
        input = integer(d, "input_register_count"), output = integer(d, "output_register_count"),
        input_start = temporary + constant, output_start = input_start + input * 2;
    if (integer(d, "register_count") != output_start + output || cs.size() != constant ||
        ins.size() != input || outs.size() != output)
        out.append("VM register regions/table sizes differ.");
    for (int n = 0; n < cs.size(); n++) {
        Dictionary row = cs[n];
        if (integer(row, "register", -1) != temporary + n ||
            !uint_value(row.get("constant_word", Variant())) ||
            int64_t(row.get("constant_word", 0)) * 4 >= int64_t(d["constant_table_byte_size"]))
            out.append("Invalid VM constant address.");
        String type = row.get("type", "");
        String buffer = row.get("buffer", "");
        if (type != "float32" && type != "int32")
            out.append("Unsupported VM constant type.");
        if (row.get("parameter", Variant()).get_type() != Variant::STRING ||
            row.get("previous", Variant()).get_type() != Variant::BOOL ||
            !uint_value(row.get("component", Variant())) ||
            (buffer != "global" && buffer != "system" && buffer != "owner" && buffer != "emitter" &&
             buffer != "external" && buffer != "literals"))
            out.append("Unbound VM constant.");
        if (buffer == "literals" && !scalar_value(row.get("value", Variant()), type == "int32"))
            out.append("Invalid typed VM literal.");
    }
    for (int direction = 0; direction < 2; direction++) {
        Array rows = direction ? outs : ins;
        for (int n = 0; n < rows.size(); n++) {
            Dictionary row = rows[n];
            String type = row.get("type", "");
            if (integer(row, "register", -1) != (direction ? output_start : input_start) + n ||
                !uint_value(row.get("dataset", Variant())) ||
                integer(row, "dataset") >=
                    integer(d, direction ? "output_dataset_count" : "input_dataset_count") ||
                !uint_value(row.get("component", Variant())) || integer(row, "component") > 65535 ||
                (type != "float32" && type != "int32"))
                out.append("Invalid/unsupported VM dataset mapping.");
            if (!direction && (integer(row, "base_register", -1) != input_start + input + n ||
                               row.get("no_advance", Variant()).get_type() != Variant::BOOL))
                out.append("Invalid VM input base mapping.");
        }
    }
    for (int n = 0; n < ext.size(); n++) {
        Dictionary row = ext[n];
        if (integer(row, "index", -1) != n || !uint_value(row.get("num_inputs", Variant())) ||
            !uint_value(row.get("num_outputs", Variant())))
            out.append("Invalid VM external signature.");
    }
    int64_t offset = 0;
    bool done = false;
    for (int n = 0; n < instructions.size(); n++) {
        Dictionary row = instructions[n];
        String op = row.get("op", "");
        if (done || op.is_empty() || !uint_value(row.get("source_offset", Variant())) ||
            int64_t(row.get("source_offset", 0)) != offset ||
            !uint_value(row.get("source_size", Variant())) || integer(row, "source_size") < 1) {
            out.append("Invalid VM instruction order or extent.");
            break;
        }
        offset += int64_t(row["source_size"]);
        if (row.has("dataset") &&
            (!uint_value(row["dataset"]) || integer(row, "dataset") >= integer(d, "output_dataset_count")))
            out.append("VM instruction dataset exceeds output table.");
        bool arrays = true;
        for (const char *key : {"inputs", "outputs"}) {
            if (row.get(key, Variant()).get_type() != Variant::ARRAY) {
                out.append("Missing VM instruction operands.");
                arrays = false;
                continue;
            }
            Array operands = row[key];
            for (int j = 0; j < operands.size(); j++)
                if (!uint_value(operands[j]) ||
                    (int64_t(operands[j]) >= integer(d, "register_count") &&
                     !(op == "external_func_call" && int64_t(operands[j]) == 65535)))
                    out.append("Invalid VM register address.");
        }
        if (!arrays)
            continue;
        Array a = row["inputs"], b = row["outputs"];
        int kind = kind_for(op);
        if (kind == Primitive) {
            auto info = nbvm::find_op(op.utf8().get_data());
            if (info.op == nbvm::Op::Invalid)
                out.append("Unsupported optimized CPU opcode: " + op);
            else if (a.size() != info.inputs || b.size() != info.outputs)
                out.append("Invalid VM opcode arity: " + op);
        } else if (kind == ExternalCall) {
            int f = integer(row, "function", -1);
            if (!uint_value(row.get("function", Variant())) || f < 0 || f >= ext.size())
                out.append("Invalid external function index.");
            else {
                Dictionary signature = ext[f];
                if (a.size() != integer(signature, "num_inputs") ||
                    b.size() != integer(signature, "num_outputs"))
                    out.append("External function arity mismatch.");
            }
        } else {
            int expected_in = 0, expected_out = 0;
            switch (kind) {
            case Done:
                break;
            case AcquireIndex:
                expected_in = 1;
                break;
            case Output:
                expected_in = int(a.size());
                expected_out = expected_in;
                break;
            case ExecIndex:
            case ExecIndexF:
                expected_out = 1;
                break;
            case ExecIndexAdd:
            case Random:
                expected_in = 1;
                expected_out = 1;
                break;
            case RandomAdd:
                expected_in = 2;
                expected_out = 1;
                break;
            case Random2x:
            case SinCos:
                expected_in = 1;
                expected_out = 2;
                break;
            case AcquireId:
                expected_out = 2;
                break;
            case UpdateId:
                expected_in = 2;
                break;
            }
            if (a.size() != expected_in || b.size() != expected_out)
                out.append("Invalid special opcode arity: " + op);
        }
        if (kind == Done) {
            done = true;
            if (integer(row, "source_size") != 1)
                out.append("Malformed VM terminator.");
        }
    }
    if (!done || integer(d, "trailing_done_padding") != 8 || offset + 8 != int64_t(d["source_byte_count"]) ||
        !Array(d["errors"]).is_empty() || d.get("semantic_roundtrip_matches", false) != Variant(true))
        out.append("Invalid complete VM termination or native errors.");
    return out;
}

int NBNativeVMExecutor::chunk_capacity(const Dictionary &source, int count) {
    Dictionary layout = source.get("batch_layout", Dictionary());
    int64_t available = int64_t(layout.get("page_size_bytes", 0)) - int64_t(layout.get("overhead_bytes", 0));
    if (count < 0 || available <= 0)
        return 0;
    int64_t loops = std::max<int64_t>((int64_t(count) + 3) >> 2, 1),
            loop_bytes = 16 * int64_t(source.get("temporary_register_count", 0)) +
                         int64_t(source.get("output_dataset_count", 0)),
            total = ((loops + 3) & ~int64_t(3)) * loop_bytes;
    int64_t chunks = available < total ? (total + available - 1) / available : 1;
    return int(((loops + chunks - 1) / chunks) * 4);
}
namespace {
Variant component_value(Variant value, const Variant &type, int component, bool particle_value) {
    if (particle_value) {
        switch (value.get_type()) {
            case Variant::BOOL:
                return component == 0 ? Variant(bool(value) ? -1 : 0) : Variant();
            case Variant::VECTOR2:
            case Variant::VECTOR3:
            case Variant::VECTOR4:
            case Variant::COLOR:
            case Variant::QUATERNION: {
                bool valid = false, out_of_bounds = false;
                auto part = value.get_indexed(component, valid, out_of_bounds);
                return valid && !out_of_bounds ? part : Variant();
            }
            default: break;
        }
    }
    if (value.get_type() == Variant::DICTIONARY) {
        Dictionary d = value;
        value = d.get(type, Variant());
    }
    if (value.get_type() == Variant::INT || value.get_type() == Variant::FLOAT ||
        value.get_type() == Variant::STRING)
        return component == 0 ? value : Variant();
    if (value.get_type() == Variant::ARRAY) {
        Array a = value;
        return component < a.size() ? a[component] : Variant();
    }
    if (value.get_type() == Variant::PACKED_FLOAT32_ARRAY) {
        PackedFloat32Array a = value;
        return component < a.size() ? Variant(a[component]) : Variant();
    }
    if (value.get_type() == Variant::PACKED_FLOAT64_ARRAY) {
        PackedFloat64Array a = value;
        return component < a.size() ? Variant(a[component]) : Variant();
    }
    if (value.get_type() == Variant::PACKED_INT32_ARRAY) {
        PackedInt32Array a = value;
        return component < a.size() ? Variant(a[component]) : Variant();
    }
    if (value.get_type() == Variant::PACKED_INT64_ARRAY) {
        PackedInt64Array a = value;
        return component < a.size() ? Variant(a[component]) : Variant();
    }
    return Variant();
}

} // namespace
Variant NBNativeVMExecutor::parameter_component(const Dictionary &row, const Dictionary &parameters) {
    if (row.get("buffer", "") == Variant("literals"))
        return row.get("value", Variant());
    Dictionary frame =
        parameters.get(bool(row.get("previous", false)) ? "previous" : "current", Dictionary());
    String name = row.get("parameter", "");
    if (!frame.has(name))
        return Variant();
    Variant value = frame[name];
    int component = integer(row, "component");
    return component_value(value, row.get("type", ""), component, false);
}

TypedArray<String> NBNativeVMExecutor::configure(const Dictionary &source, const Array &data_interfaces,
                                                 const Dictionary &parameters) {
    program.clear();
    curves.clear();
    code.clear();
    constants.clear();
    inputs.clear();
    outputs.clear();
    external.clear();
    interfaces = data_interfaces.duplicate(true);
    errors = validate_program(source);
    if (!errors.is_empty())
        return errors.duplicate();
    program = source.duplicate(true);
    auto map = [&](const char *name, std::vector<Binding> &target) {
        Array rows = program[name];
        for (int n = 0; n < rows.size(); n++) {
            Dictionary row = rows[n];
            Binding b;
            b.reg = integer(row, "register");
            b.base = integer(row, "base_register");
            b.dataset = integer(row, "dataset");
            b.component = integer(row, "component");
            b.integer = row.get("type", "") == Variant("int32");
            b.broadcast = bool(row.get("no_advance", false));
            if (&target == &constants) {
                b.name = row["parameter"];
                b.type = row["type"];
                b.previous = row["previous"];
                b.literal = row["buffer"] == Variant("literals");
                if (b.literal) b.literal_word = word(row["value"], b.integer);
            }
            target.push_back(b);
        }
    };
    map("constants", constants);
    map("inputs", inputs);
    map("outputs", outputs);
    Array instructions = program["instructions"];
    for (int n = 0; n < instructions.size(); n++) {
        Dictionary row = instructions[n];
        String name = row["op"];
        Instruction op;
        op.kind = kind_for(name);
        op.primitive = int(nbvm::find_op(name.utf8().get_data()).op);
        if (op.kind == Primitive)
            op.kernel = nbvm::kernel_for(nbvm::Op(op.primitive));
        op.dataset = integer(row, "dataset");
        op.function = integer(row, "function");
        for (const char *key : {"inputs", "outputs"}) {
            Array a = row[key];
            auto &target = String(key) == "inputs" ? op.inputs : op.outputs;
            for (int j = 0; j < a.size(); j++)
                target.push_back(int(int64_t(a[j])));
        }
        code.push_back(std::move(op));
    }
    bind_external(parameters);
    register_reader_fields(get_particle_context());
    if (!errors.is_empty())
        program.clear();
    return errors.duplicate();
}
bool NBNativeVMExecutor::bind_external(const Dictionary &parameters) {
    uses_system_interfaces = false;
    external.clear();
    curves.clear();
    Array functions = program.get("external_functions", Array());
    for (int index = 0; index < functions.size(); index++) {
        Dictionary function = functions[index], info;
        int matches = 0;
        for (int n = 0; n < interfaces.size(); n++) {
            if (interfaces[n].get_type() != Variant::DICTIONARY)
                continue;
            Dictionary candidate = interfaces[n];
            if (candidate.get("name", Variant()) == function.get("owner_name", Variant()) ||
                candidate.get("compile_name", Variant()) == function.get("owner_name", Variant())) {
                info = candidate;
                matches++;
            }
        }
        if (matches == 0 && function.get("owner_name", Variant()) == Variant("FastPathLibrary") &&
            function.get("name", Variant()) == Variant("FastMatrixToQuaternion")) {
            info["type"] = "FastPathLibrary";
            matches = 1;
        }
        if (matches != 1) {
            errors.append("Missing or ambiguous native external interface: " +
                          String(function.get("owner_name", "")));
            continue;
        }
        nbvm::ExternalBinding bound;
        if (!bound.bind(info, function, parameters, errors))
            continue;
        if (bound.writes_system_state() && !allow_system_interfaces) {
            errors.append("This scene interface requires a system instance.");
            continue;
        }
        uses_system_interfaces |= bound.writes_system_state();
        curves[index] = bound.metadata;
        external.push_back(std::move(bound));
    }
    return errors.is_empty();
}
TypedArray<String> NBNativeVMExecutor::update_user_curves(const Dictionary &parameters) {
    errors.clear();
    if (program.is_empty())
        errors.append("Configure a valid native program first.");
    else
        bind_external(parameters);
    return errors.duplicate();
}
Dictionary NBNativeVMExecutor::curve_table(const Dictionary &source, const Array &interfaces,
                                           const Dictionary &parameters) {
    Ref<NBNativeVMExecutor> vm;
    vm.instantiate();
    vm->program = source.duplicate(true);
    vm->interfaces = interfaces.duplicate(true);
    vm->bind_external(parameters);
    Dictionary result;
    result["errors"] = vm->errors;
    result["curves"] = vm->curves;
    return result;
}
TypedArray<String> NBNativeVMExecutor::curve_parameter_names(const Dictionary &info, const String &owner) {
    TypedArray<String> names;
    if (owner.begins_with("User."))
        names.append(owner);
    for (const char *key :
         {"name", "compile_name", "registered_parameter_map_read", "registered_parameter_map_write"}) {
        Variant value = info.get(key, Variant());
        if (value.get_type() == Variant::STRING) {
            String name = value;
            if (name.begins_with("User.") && !names.has(name))
                names.append(name);
        }
    }
    return names;
}
Array NBNativeVMExecutor::sample_curve(const Dictionary &curve, double time) {
    int width = integer(curve, "elements_per_sample");
    Dictionary function;
    function["name"] = width == 1   ? "SampleCurve"
                       : width == 2 ? "SampleVector2DCurve"
                       : width == 3 ? "SampleVectorCurve"
                                    : "SampleColorCurve";
    function["num_inputs"] = 1;
    function["num_outputs"] = width;
    function["owner_name"] = curve.get("name", "");
    Dictionary parameters;
    if (curve.has("runtime_curve"))
        parameters[curve.get("runtime_parameter", "")] = curve["runtime_curve"];
    nbvm::ExternalBinding binding;
    TypedArray<String> invalid;
    Array result;
    if (!binding.bind(curve, function, parameters, invalid))
        return result;
    nbvm::ExternalInputs words{};
    words[0] = nbvm::bits(float(time));
    nbvm::ExternalOutputs values{};
    binding.sample(words, Dictionary(), values);
    for (int n = 0; n < binding.width; n++)
        result.append(nbvm::number(values[n]));
    return result;
}

Dictionary NBNativeVMExecutor::execute(const Dictionary &parameters, const Array &datasets, int count,
                                       const Ref<NBNativeVMRandom> &random, const Dictionary &id_states,
                                       bool packed_output) {
    return execute_frame(parameters, datasets, count, random, id_states, packed_output, nullptr);
}
Dictionary NBNativeVMExecutor::execute_frame(const Dictionary &parameters, const Array &datasets, int count,
                                              const Ref<NBNativeVMRandom> &random, const Dictionary &id_states,
                                              bool packed_output, const Dictionary *particle_defaults) {
    auto start_time = std::chrono::steady_clock::now();
    last_output_packed = packed_output;
    Dictionary result;
    TypedArray<String> problems = errors.duplicate();
    Array result_sets, result_counts, result_indices;
    result["errors"] = problems;
    result["datasets"] = result_sets;
    result["dataset_counts"] = result_counts;
    result["dataset_indices"] = result_indices;
    result["max_instances_per_chunk"] = 0;
    auto fail = [&](const String &message) -> Dictionary {
        problems.append(message);
        result["errors"] = problems;
        return result;
    };
    if (program.is_empty())
        return fail("Configure a supported optimized CPU program first.");
    if (!problems.is_empty())
        return result;
    Ref<NBNativeVMReaders> readers = get_particle_context();
    if (count > 0 && readers.is_null() && std::any_of(external.begin(), external.end(), [](const auto &b) {
            return b.kind == nbvm::ExternalBinding::Reader;
        }))
        return fail("Native particle readers require a native per-player context.");
    if (count > 0 && collision_context.is_null() &&
        std::any_of(external.begin(), external.end(),
                    [](const auto &b) { return b.kind == nbvm::ExternalBinding::Collision; }))
        return fail("Native collision programs require the player's shared Godot Jolt context.");
    int capacity = chunk_capacity(program, count);
    if (count < 0 || count > 1000000 || capacity <= 0)
        return fail("Invalid VM instance count or batch capacity.");
    if (int64_t(capacity) * integer(program, "temporary_register_count") > 134217728)
        return fail("VM temporary working set is too large.");
    Ref<NBNativeVMRandom> stream = random;
    Dictionary batch = program["batch_layout"];
    if (count > 0 && bool(batch["has_random_instructions"]) && stream.is_null()) {
        stream.instantiate();
        stream->initialize_runtime();
    }
    for (const auto &op : code)
        if (op.kind == AcquireId || op.kind == UpdateId) {
            Ref<NBNativeVMIDs> ids = id_states.get(op.dataset, Variant());
            if (ids.is_null())
                return fail("Missing native persistent ID state.");
        }
    int set_count =
        std::max(integer(program, "input_dataset_count"), integer(program, "output_dataset_count"));
    struct Dataset {
        int offset = 0, count = 0;
        std::unordered_map<int, Register> floats, ints;
        std::vector<int> indices;
    };
    std::vector<Dataset> sets(set_count);
    std::vector<Register> registers(integer(program, "register_count"));
    int padded = ((count + 3) & ~3) + 4;
    for (int n = 0; n < set_count; n++) {
        if (n < datasets.size()) {
            if (datasets[n].get_type() != Variant::DICTIONARY)
                return fail("Invalid VM input dataset.");
            Dictionary d = datasets[n];
            if (!uint_value(d.get("instance_offset", 0)) || int64_t(d.get("instance_offset", 0)) > 1000000)
                return fail("Invalid VM dataset instance offset.");
            sets[n].offset = integer(d, "instance_offset");
        }
        result_counts.append(0);
        result_indices.append(Array());
    }
    const int temporary_count = integer(program, "temporary_register_count");
    for (int n = 0; n < temporary_count; ++n)
        registers[n] = storage(capacity);
    const Dictionary current = parameters.get("current", Dictionary());
    const Dictionary previous = parameters.get("previous", Dictionary());
    auto constant_words = std::make_shared<std::vector<uint32_t>>(4 * constants.size());
    size_t offset = 0;
    for (const auto &binding : constants) {
        uint32_t bits = binding.literal_word;
        if (!binding.literal) {
            const Dictionary &frame = binding.previous ? previous : current;
            Variant value = frame.get(binding.name, Variant());
            if (particle_defaults && value.get_type() == Variant::NIL && !frame.has(binding.name))
                value = particle_defaults->get(binding.name, Variant());
            value = component_value(value, binding.type, binding.component, particle_defaults != nullptr);
            if (value.get_type() == Variant::NIL || !scalar_value(value, binding.integer))
                return fail("Missing or invalid CPU parameter: " + String(binding.name));
            bits = word(value, binding.integer);
        }
        std::fill_n(constant_words->data() + offset, 4, bits);
        registers[binding.reg] = {constant_words, offset, true};
        offset += 4;
    }
    for (const auto &binding : inputs) {
        if (binding.dataset >= datasets.size())
            return fail("Missing VM input dataset.");
        Dictionary d = datasets[binding.dataset];
        Array columns = d.get(binding.integer ? "int32" : "float32", Array());
        int offset = sets[binding.dataset].offset;
        if (binding.component >= columns.size())
            return fail("Missing VM input column.");
        Variant column = columns[binding.component];
        Register reg = storage(padded + offset);
        int required = offset + (binding.broadcast ? 1 : count);
        if (!binding.integer && column.get_type() == Variant::PACKED_FLOAT32_ARRAY) {
            PackedFloat32Array values = column;
            if (values.size() < required)
                return fail("Short packed VM input column.");
            size_t copied = std::min<size_t>(values.size(), reg.words->size());
            if (copied)
                std::memcpy(reg.words->data(), values.ptr(), copied * sizeof(uint32_t));
        } else if (binding.integer && column.get_type() == Variant::PACKED_INT32_ARRAY) {
            PackedInt32Array values = column;
            if (values.size() < required)
                return fail("Short packed VM input column.");
            size_t copied = std::min<size_t>(values.size(), reg.words->size());
            if (copied)
                std::memcpy(reg.words->data(), values.ptr(), copied * sizeof(uint32_t));
        } else if (column.get_type() == Variant::ARRAY) {
            Array values = column;
            if (values.size() < required)
                return fail("Short VM input column.");
            for (int n = 0; n < std::min<int64_t>(values.size(), int64_t(reg.words->size())); n++) {
                if (!scalar_value(values[n], binding.integer))
                    return fail("Invalid typed VM input scalar.");
                (*reg.words)[n] = word(values[n], binding.integer);
            }
        } else {
            return fail("VM column requires Array or matching PackedFloat32Array/PackedInt32Array.");
        }
        reg.offset = offset;
        if (binding.broadcast) {
            auto broadcast = storage(4, true);
            std::fill(broadcast.words->begin(), broadcast.words->end(), (*reg.words)[offset]);
            reg = broadcast;
        }
        registers[binding.reg] = reg;
        registers[binding.base] = reg;
    }
    for (const auto &binding : outputs) {
        auto &columns = binding.integer ? sets[binding.dataset].ints : sets[binding.dataset].floats;
        auto it = columns.find(binding.component);
        if (it == columns.end())
            it = columns.emplace(binding.component, storage(padded + sets[binding.dataset].offset)).first;
        registers[binding.reg] = it->second;
        registers[binding.reg].offset = sets[binding.dataset].offset;
    }
    // Reject reads/writes to unmapped registers before entering the native loop.
    for (const auto &op : code) {
        for (size_t n = 0; n < op.inputs.size(); n++) {
            int r = op.inputs[n];
            if (r == 65535) {
                if (op.kind != ExternalCall ||
                    !external[op.function].has_instance_pointer() ||
                    n != 0)
                    return fail("Invalid ignored external input register.");
            } else if (!registers[r].words)
                return fail("VM reads an unbound register.");
        }
        for (int r : op.outputs)
            if (r != 65535 && (!registers[r].words || registers[r].broadcast))
                return fail("VM writes an unbound or constant register.");
    }
    for (int start = 0; start < count; start += capacity) {
        int actual = std::min(capacity, count - start), lanes = (actual + 3) & ~3;
        std::vector<std::vector<int>> masks(set_count);
        std::vector<bool> acquired(set_count, false);
        for (const auto &b : inputs)
            if (!b.broadcast)
                registers[b.reg].offset = sets[b.dataset].offset + start;
        for (const auto &b : outputs)
            registers[b.reg].offset = sets[b.dataset].offset + sets[b.dataset].count;
        for (const auto &op : code) {
            if (op.kind == Done)
                break;
            if (op.kind == AcquireIndex) {
                auto &mask = masks[op.dataset];
                mask.clear();
                for (int lane = 0; lane < actual; lane++)
                    if (registers[op.inputs[0]].read(lane) & 0x80000000u)
                        mask.push_back(lane);
                acquired[op.dataset] = true;
            } else if (op.kind == Output) {
                if (!acquired[op.dataset])
                    return fail("VM output before acquireindex.");
                for (size_t col = 0; col < op.inputs.size(); col++)
                    compact(registers[op.inputs[col]], registers[op.outputs[col]], masks[op.dataset], actual);
            } else if (op.kind == ExecIndex || op.kind == ExecIndexF || op.kind == ExecIndexAdd) {
                for (int lane = 0; lane < lanes; lane++) {
                    uint32_t index = uint32_t(lane + (lanes > 4 ? start : 0));
                    uint32_t value = op.kind == ExecIndexF ? nbvm::bits(float(index)) : index;
                    if (op.kind == ExecIndexAdd)
                        value += registers[op.inputs[0]].read(lane);
                    registers[op.outputs[0]].write(lane, value);
                }
            } else if (op.kind == AcquireId) {
                Ref<NBNativeVMIDs> ids = id_states[op.dataset];
                Dictionary values = ids->acquire(actual);
                TypedArray<String> invalid = values["errors"];
                if (!invalid.is_empty())
                    return fail(invalid[0]);
                Array data = values["ids"];
                for (int lane = 0; lane < actual; lane++) {
                    registers[op.outputs[0]].write(lane, uint32_t(int64_t(data[lane])));
                    registers[op.outputs[1]].write(lane, uint32_t(ids->get_acquire_tag()));
                }
            } else if (op.kind == UpdateId) {
                if (!acquired[op.dataset])
                    return fail("Persistent ID update before acquireindex.");
                Ref<NBNativeVMIDs> ids = id_states[op.dataset];
                Array values, kept;
                for (int lane = 0; lane < actual; lane++)
                    values.append(nbvm::signed_word(registers[op.inputs[0]].read(lane)));
                for (int n : masks[op.dataset])
                    kept.append(n);
                auto invalid = ids->update(values, kept, sets[op.dataset].offset + sets[op.dataset].count);
                if (!invalid.is_empty())
                    return fail(invalid[0]);
            } else if (op.kind == Random || op.kind == RandomAdd || op.kind == Random2x) {
                for (int group = 0; group < lanes; group += 4) {
                    std::array<uint32_t, 4> a{}, b{};
                    if (stream.is_null() || !stream->next(a) || (op.kind == Random2x && !stream->next(b)))
                        return fail("Native random state is not initialized.");
                    for (int lane = 0; lane < 4; lane++) {
                        uint32_t multiplier = registers[op.inputs[0]].read(group + lane),
                                 value = nbvm::VectorMultiply(nbvm::bits(float(a[lane] >> 9) / 8388608.f),
                                                              multiplier);
                        if (op.kind == RandomAdd)
                            value = nbvm::VectorAdd(value, registers[op.inputs[1]].read(group + lane));
                        registers[op.outputs[0]].write(group + lane, value);
                        if (op.kind == Random2x)
                            registers[op.outputs[1]].write(
                                group + lane, nbvm::VectorMultiply(
                                                  nbvm::bits(float(b[lane] >> 9) / 8388608.f), multiplier));
                    }
                }
            } else if (op.kind == ExternalCall) {
                if (op.function >= int(external.size()))
                    return fail("Unbound native external function.");
                const auto &binding = external[op.function];
                Dictionary reader_data;
                if (binding.kind == nbvm::ExternalBinding::Reader)
                    reader_data = readers->source(binding.metadata, reader_scope);
                for (int lane = 0; lane < actual; lane++) {
                    nbvm::ExternalInputs operands{};
                    for (size_t n = binding.has_instance_pointer() ? 1 : 0;
                         n < op.inputs.size(); n++)
                        operands[n] = registers[op.inputs[n]].read(lane);
                    nbvm::ExternalOutputs values{};
                    if (binding.kind == nbvm::ExternalBinding::Collision) {
                        if (!binding.collide(operands, *collision_context.ptr(), reader_scope, values))
                            return fail(collision_context->get_error());
                    } else if (binding.writes_system_state()) {
                        if (count != 1) return fail("Scene interface requires a single system instance.");
                        binding.control_volume(operands, system_interfaces, values);
                    } else
                        binding.sample(operands, reader_data, values);
                    for (int c = 0; c < binding.width; c++)
                        if (op.outputs[c] != 65535)
                            registers[op.outputs[c]].write(lane, values[c]);
                }
            } else if (op.kind == Primitive) {
                std::array<nbvm::KernelRead, 5> source{};
                std::array<uint32_t *, 2> target{};
                for (size_t n = 0; n < op.inputs.size(); n++) {
                    const auto &reg = registers[op.inputs[n]];
                    size_t required = reg.broadcast ? 1 : size_t(lanes);
                    if (reg.offset > reg.words->size() || required > reg.words->size() - reg.offset)
                        return fail("VM kernel input extent exceeds native storage.");
                    source[n] = {reg.words->data() + reg.offset, reg.broadcast};
                }
                for (size_t n = 0; n < op.outputs.size(); n++) {
                    auto &reg = registers[op.outputs[n]];
                    if (reg.offset > reg.words->size() || size_t(lanes) > reg.words->size() - reg.offset)
                        return fail("VM kernel output extent exceeds native storage.");
                    target[n] = reg.words->data() + reg.offset;
                }
                if (!op.kernel)
                    return fail("VM scalar kernel was not compiled.");
                op.kernel(source, target, lanes);
            } else {
                for (int lane = 0; lane < lanes; lane++) {
                    std::array<uint32_t, 5> operands{};
                    for (size_t n = 0; n < op.inputs.size(); n++)
                        operands[n] = registers[op.inputs[n]].read(lane);
                    if (op.kind == SinCos) {
                        registers[op.outputs[0]].write(lane, nbvm::VectorSin(operands[0]));
                        registers[op.outputs[1]].write(lane, nbvm::VectorCos(operands[0]));
                    } else {
                        uint32_t value = nbvm::scalar(nbvm::Op(op.primitive), operands);
                        for (int out : op.outputs)
                            registers[out].write(lane, value);
                    }
                }
            }
        }
        for (int n = 0; n < set_count; n++)
            if (acquired[n]) {
                sets[n].count += int(masks[n].size());
                for (int lane : masks[n])
                    sets[n].indices.push_back(start + lane);
            }
    }
    for (int n = 0; n < set_count; n++) {
        Dictionary output;
        for (int kind = 0; kind < 2; kind++) {
            Array columns;
            auto &source = kind ? sets[n].ints : sets[n].floats;
            for (const auto &entry : source) {
                if (entry.first == 65535)
                    continue;
                while (columns.size() <= entry.first)
                    columns.append(Array());
                if (packed_output) {
                    if (kind) {
                        PackedInt32Array values;
                        values.resize(sets[n].count);
                        if (sets[n].count)
                            std::memcpy(values.ptrw(), entry.second.words->data() + sets[n].offset,
                                        sets[n].count * sizeof(uint32_t));
                        columns[entry.first] = values;
                    } else {
                        PackedFloat32Array values;
                        values.resize(sets[n].count);
                        if (sets[n].count)
                            std::memcpy(values.ptrw(), entry.second.words->data() + sets[n].offset,
                                        sets[n].count * sizeof(uint32_t));
                        columns[entry.first] = values;
                    }
                } else {
                    Array values;
                    values.resize(sets[n].count);
                    for (int row = 0; row < sets[n].count; row++) {
                        uint32_t v = (*entry.second.words)[sets[n].offset + row];
                        values[row] = kind ? Variant(nbvm::signed_word(v)) : Variant(nbvm::number(v));
                    }
                    columns[entry.first] = values;
                }
            }
            output[kind ? "int32" : "float32"] = columns;
        }
        result_sets.append(output);
        result_counts[n] = sets[n].count;
        Array indices;
        for (int index : sets[n].indices)
            indices.append(index);
        result_indices[n] = indices;
    }
    result["max_instances_per_chunk"] = capacity;
    last_execute_usec =
        std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start_time).count();
    executions++;
    return result;
}
Dictionary NBNativeVMExecutor::get_statistics() const {
    Dictionary d;
    d["native_execution"] = true;
    d["packed_output"] = last_output_packed;
    d["requires_collision"] = std::any_of(external.begin(), external.end(), [](const auto &b) {
        return b.kind == nbvm::ExternalBinding::Collision;
    });
    d["executions"] = executions;
    d["last_execute_usec"] = last_execute_usec;
    d["compiled_instructions"] = int64_t(code.size());
    d["registered_scalar_opcodes"] = 150;
    d["configured"] = !program.is_empty() && errors.is_empty();
    return d;
}
