#include "compiler_frontend.h"
#include <godot_cpp/classes/reg_ex.hpp>
#include <godot_cpp/classes/reg_ex_match.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <cmath>
#include <cstring>

namespace godot {
namespace {
bool numeric(const Variant &v) { return v.get_type() == Variant::INT || v.get_type() == Variant::FLOAT; }
bool equal(const Variant &a, const Variant &b) {
    Variant result; bool valid = false; Variant::evaluate(Variant::OP_EQUAL, a, b, result, valid);
    return valid && bool(result);
}
bool named(const Variant &v, std::initializer_list<const char *> names) {
    for (const char *name : names) if (equal(v, String(name))) return true;
    return false;
}
Ref<RegEx> regex(const String &pattern) { return RegEx::create_from_string(pattern); }
bool table(const Variant &v) {
    if (v.get_type() != Variant::ARRAY) return false;
    Array rows = v;
    for (int i = 0; i < rows.size(); ++i) if (rows[i].get_type() != Variant::DICTIONARY) return false;
    return true;
}
}
void NBNativeProgramSchema::_bind_methods() {
    ClassDB::bind_static_method("NBNativeProgramSchema", D_METHOD("unsigned_integer", "value"), &NBNativeProgramSchema::unsigned_integer);
    ClassDB::bind_static_method("NBNativeProgramSchema", D_METHOD("binding", "row"), &NBNativeProgramSchema::binding);
    ClassDB::bind_static_method("NBNativeProgramSchema", D_METHOD("scalar", "value", "type"), &NBNativeProgramSchema::scalar);
    ClassDB::bind_static_method("NBNativeProgramSchema", D_METHOD("same_constants", "left", "right"), &NBNativeProgramSchema::same_constants);
    ClassDB::bind_static_method("NBNativeProgramSchema", D_METHOD("validate", "value"), &NBNativeProgramSchema::validate);
    ClassDB::bind_static_method("NBNativeProgramSchema", D_METHOD("extract_math", "hlsl", "assembly"), &NBNativeProgramSchema::extract_math);
}
bool NBNativeProgramSchema::unsigned_integer(const Variant &v) {
    return numeric(v) && std::isfinite(double(v)) && double(v) == std::floor(double(v)) && double(v) >= 0.0 && double(v) <= 4294967295.0;
}
bool NBNativeProgramSchema::binding(const Dictionary &row) {
    return unsigned_integer(row.get("dataset", Variant())) && unsigned_integer(row.get("component", Variant())) && named(row.get("type", Variant()), {"float32", "int32", "float16"});
}
bool NBNativeProgramSchema::scalar(const Variant &v, const String &type) {
    if (type == "int32") return numeric(v) && std::isfinite(double(v)) && double(v) == std::floor(double(v)) && double(v) >= -2147483648.0 && double(v) <= 2147483647.0;
    return numeric(v) || named(v, {"-0", "NaN", "Infinity", "-Infinity"});
}
bool NBNativeProgramSchema::same_constants(const Array &left, const Array &right) {
    if (left.size() != right.size()) return false;
    for (int i = 0; i < left.size(); ++i) {
        if (left[i].get_type() != Variant::DICTIONARY || right[i].get_type() != Variant::DICTIONARY) return false;
        Dictionary a = Dictionary(left[i]).duplicate(), b = Dictionary(right[i]).duplicate();
        for (Dictionary row : {a, b}) {
            Variant scalar = row.get("value", Variant());
            if (equal(row.get("buffer", Variant()), String("literals")) && equal(row.get("type", Variant()), String("float32")) && (numeric(scalar) || equal(scalar, String("-0")))) {
                float value = numeric(scalar) ? float(double(scalar)) : -0.f;
                uint32_t bits; std::memcpy(&bits, &value, sizeof(bits)); row["value"] = int64_t(bits);
            }
        }
        if (!equal(a, b)) return false;
    }
    return true;
}
TypedArray<String> NBNativeProgramSchema::validate(const Variant &value) {
    TypedArray<String> errors;
    if (value.get_type() != Variant::DICTIONARY) { errors.append("Malformed optimized VM program header."); return errors; }
    Dictionary d = value;
    if (!equal(d.get("schema_version", Variant()), 1) || d.get("available", Variant()).get_type() != Variant::BOOL || d.get("complete", Variant()).get_type() != Variant::BOOL || d.get("errors", Variant()).get_type() != Variant::ARRAY) {
        errors.append("Malformed optimized VM program header."); return errors;
    }
    if (!bool(d["available"])) { if (bool(d["complete"])) errors.append("Unavailable VM program cannot be complete."); return errors; }
    for (const char *field : {"temporary_register_count", "constant_register_count", "input_register_count", "output_register_count", "register_count", "source_byte_count", "trailing_done_padding", "constant_table_byte_size", "input_dataset_count", "output_dataset_count"})
        if (!unsigned_integer(d.get(field, Variant()))) errors.append(String("Invalid VM count: ") + field);
    if (!errors.is_empty()) return errors;
    Variant batch = d.get("batch_layout", Variant()); bool batch_ok = batch.get_type() == Variant::DICTIONARY;
    if (batch_ok) {
        Dictionary b = batch;
        batch_ok = unsigned_integer(b.get("overhead_bytes", Variant())) && unsigned_integer(b.get("page_size_bytes", Variant())) && equal(b.get("register_bytes", Variant()), 16) && equal(b.get("vector_width", Variant()), 4) && equal(b.get("loop_count_alignment", Variant()), 4) && b.get("has_random_instructions", Variant()).get_type() == Variant::BOOL;
    }
    if (!batch_ok) errors.append("Invalid native VM batch layout.");
    for (const char *field : {"constants", "inputs", "outputs", "external_functions", "instructions", "constant_buffers"}) if (!table(d.get(field, Variant()))) errors.append(String("Invalid VM table: ") + field);
    if (!errors.is_empty()) return errors;
    int64_t temporary = d["temporary_register_count"], constants = d["constant_register_count"], inputs = d["input_register_count"], outputs = d["output_register_count"];
    int64_t input_start = temporary + constants, output_start = input_start + inputs * 2;
    Array cs = d["constants"], ins = d["inputs"], outs = d["outputs"], external = d["external_functions"], instructions = d["instructions"];
    if (int64_t(d["register_count"]) != output_start + outputs) errors.append("VM register regions do not match the total count.");
    if (cs.size() != constants || ins.size() != inputs || outs.size() != outputs) errors.append("VM mapping table sizes differ from the register regions.");
    for (int i = 0; i < cs.size(); ++i) {
        Dictionary row = cs[i];
        if (!equal(row.get("register", Variant()), temporary + i) || !unsigned_integer(row.get("constant_word", Variant())) || int64_t(row.get("constant_word", 0)) * 4 >= int64_t(d["constant_table_byte_size"])) errors.append("Invalid VM constant address.");
        if (bool(d["complete"])) {
            if (!named(row.get("type", Variant()), {"float32", "int32", "float16"}) || row.get("parameter", Variant()).get_type() != Variant::STRING || row.get("previous", Variant()).get_type() != Variant::BOOL || !unsigned_integer(row.get("component", Variant())) || !named(row.get("buffer", Variant()), {"global", "system", "owner", "emitter", "external", "literals"})) errors.append("Unbound VM constant.");
            else if (equal(row["buffer"], String("literals")) && !scalar(row.get("value", Variant()), row["type"])) errors.append("Invalid typed VM literal.");
        }
    }
    for (int i = 0; i < ins.size(); ++i) {
        Dictionary row = ins[i];
        if (!equal(row.get("register", Variant()), input_start + i) || !equal(row.get("base_register", Variant()), input_start + inputs + i) || row.get("no_advance", Variant()).get_type() != Variant::BOOL || !binding(row)) errors.append("Invalid VM input mapping.");
        if (unsigned_integer(row.get("dataset", Variant())) && int64_t(row["dataset"]) >= int64_t(d["input_dataset_count"])) errors.append("VM input dataset index exceeds its table.");
    }
    for (int i = 0; i < outs.size(); ++i) {
        Dictionary row = outs[i];
        if (!equal(row.get("register", Variant()), output_start + i) || !binding(row)) errors.append("Invalid VM output mapping.");
        if (unsigned_integer(row.get("dataset", Variant())) && int64_t(row["dataset"]) >= int64_t(d["output_dataset_count"])) errors.append("VM output dataset index exceeds its table.");
    }
    for (int i = 0; i < external.size(); ++i) {
        Dictionary row = external[i];
        if (!equal(row.get("index", Variant()), i) || !unsigned_integer(row.get("num_inputs", Variant())) || !unsigned_integer(row.get("num_outputs", Variant()))) errors.append("Invalid VM external function signature.");
    }
    int64_t offset = 0; bool done = false;
    for (int i = 0; i < instructions.size(); ++i) {
        Dictionary row = instructions[i]; String op = row.get("op", "");
        if (done || row.get("op", Variant()).get_type() != Variant::STRING || op.is_empty() || !equal(row.get("source_offset", Variant()), offset) || !unsigned_integer(row.get("source_size", Variant())) || int64_t(row.get("source_size", 0)) < 1) { errors.append("Invalid VM instruction order or extent."); break; }
        offset += int64_t(row["source_size"]);
        if (row.has("dataset") && (!unsigned_integer(row["dataset"]) || int64_t(row["dataset"]) >= int64_t(d["output_dataset_count"]))) errors.append("VM instruction dataset index exceeds its output table.");
        for (const char *field : {"inputs", "outputs"}) {
            if (row.get(field, Variant()).get_type() != Variant::ARRAY) { errors.append("Missing VM instruction operands."); continue; }
            Array operands = row[field];
            for (int n = 0; n < operands.size(); ++n)
                if (!unsigned_integer(operands[n]) || (int64_t(operands[n]) >= int64_t(d["register_count"]) && !(op == "external_func_call" && int64_t(operands[n]) == 65535))) errors.append("VM instruction addresses an invalid register.");
        }
        if (op == "external_func_call") {
            if (!unsigned_integer(row.get("function", Variant())) || int64_t(row.get("function", -1)) >= external.size()) errors.append("VM instruction addresses an invalid external function.");
            else {
                Dictionary signature = external[int64_t(row["function"])];
                if (row.get("inputs", Variant()).get_type() != Variant::ARRAY || row.get("outputs", Variant()).get_type() != Variant::ARRAY || !equal(Array(row["inputs"]).size(), signature["num_inputs"]) || !equal(Array(row["outputs"]).size(), signature["num_outputs"])) errors.append("VM external function argument count differs from its signature.");
            }
        }
        if (op == "done") {
            done = true;
            if (!equal(row.get("inputs", Variant()), Array()) || !equal(row.get("outputs", Variant()), Array()) || !equal(row["source_size"], 1)) errors.append("Malformed VM terminator.");
        }
    }
    if (bool(d["complete"]) && (!done || int64_t(d["trailing_done_padding"]) != 8 || offset + int64_t(d["trailing_done_padding"]) != int64_t(d["source_byte_count"]) || !Array(d["errors"]).is_empty() || !equal(d.get("semantic_roundtrip_matches", Variant()), true))) errors.append("Complete VM program has invalid termination or native errors.");
    return errors;
}
Dictionary NBNativeProgramSchema::extract_math(const String &hlsl, const String &assembly) {
    Dictionary result, labels, constants, scopes; Array errors;
    result["complete"] = false; result["errors"] = errors; result["scopes"] = scopes;
    result["hlsl_sha256"] = hlsl.sha256_text(); result["vm_sha256"] = assembly.sha256_text();
    TypedArray<RegExMatch> matches = regex("EnterStatScope\\(([0-9]+)[ \\t]*/\\*\\*([A-Za-z_][A-Za-z0-9_]*)\\*/\\)")->search_all(hlsl);
    for (int i = 0; i < matches.size(); ++i) {
        Ref<RegExMatch> match = matches[i]; int64_t id = match->get_string(1).to_int(); String label = match->get_string(2);
        if (labels.has(id) && !equal(labels[id], label)) errors.append("Ambiguous native VM scope label.");
        labels[id] = label; scopes[label] = Dictionary();
    }
    matches = regex("(?m)^([0-9]+)[ \\t]*\\|[ \\t]*(-?[0-9]+)[ \\t]*\\r?$")->search_all(assembly);
    for (int i = 0; i < matches.size(); ++i) { Ref<RegExMatch> match = matches[i]; constants[match->get_string(1).to_int()] = match->get_string(2).to_int(); }
    Ref<RegEx> instruction_pattern = regex("(?m)^[0-9]+[ \\t]*\\|[ \\t]*(.+);[ \\t]*\\r?$"), enter_pattern = regex("^enter_stat_scope\\(C\\[([0-9]+)\\]\\)$"), operator_pattern = regex("=[ \\t]*(rcp|rsq|sqrt|sin|cos|pow)\\(");
    Array stack; int count = 0; matches = instruction_pattern->search_all(assembly);
    for (int i = 0; i < matches.size(); ++i) {
        ++count; Ref<RegExMatch> match = matches[i]; String instruction = match->get_string(1);
        Ref<RegExMatch> enter = enter_pattern->search(instruction);
        if (enter.is_valid()) {
            int64_t constant = enter->get_string(1).to_int();
            if (!constants.has(constant) || !labels.has(constants[constant])) { errors.append("Unknown native VM scope constant."); continue; }
            stack.append(constants[constant]);
        } else if (instruction == "exit_stat_scope()") {
            if (stack.is_empty()) errors.append("Unbalanced native VM scope exit."); else stack.pop_back();
        } else {
            Ref<RegExMatch> op = operator_pattern->search(instruction);
            if (op.is_valid()) for (int s = 0; s < stack.size(); ++s) {
                Dictionary counts = scopes[labels[stack[s]]]; String name = op->get_string(1); counts[name] = int64_t(counts.get(name, 0)) + 1;
            }
        }
    }
    if (count == 0 || !stack.is_empty() || labels.is_empty()) errors.append("Incomplete native VM scope evidence.");
    result["complete"] = errors.is_empty(); return result;
}
} // namespace godot
