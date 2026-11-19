#include "source_program.h"
#include "effect_compiler.h"
#include "value_evaluator.h"
#include <cmath>

namespace godot::nbsource {
using nbcompiler::Compiler;
using nbcompiler::equal;
using nbcompiler::named;
using nbcompiler::number;
bool signed32(const Variant &v) {
    return number(v) && std::isfinite(double(v)) && double(v) == std::floor(double(v)) &&
           double(v) >= -2147483648.0 && double(v) <= 2147483647.0;
}
Dictionary instance_int_parameter(const Dictionary &c, const String &expression) {
    Array rows = c.get("instance_int_bindings", Array()), bindings;
    for (int i = 0; i < rows.size(); ++i)
        if (equal(Dictionary(rows[i]).get("expression", Variant()), expression))
            bindings.append(rows[i]);
    if (bindings.size() != 1)
        return Dictionary();
    rows = c.get("instance_input_layout", Array());
    Array layout;
    for (int i = 0; i < rows.size(); ++i) {
        Dictionary row = rows[i];
        if (int64_t(row.get("int32_components", 0)) == 1 && int64_t(row.get("float32_components", 0)) == 0 &&
            int64_t(row.get("int32_start", -1)) == int64_t(Dictionary(bindings[0])["int32_start"]))
            layout.append(row);
    }
    if (layout.size() != 1)
        return Dictionary();
    rows = c.get("instance_parameter_defaults", Array());
    Array defaults;
    Dictionary entry = layout[0];
    for (int i = 0; i < rows.size(); ++i) {
        Dictionary row = rows[i];
        if (equal(row.get("name", Variant()), entry["name"]) &&
            equal(row.get("type", Variant()), String("int32")))
            defaults.append(row);
    }
    if (defaults.is_empty() && equal(entry.get("parameter_store_bound", true), false) &&
        equal(entry.get("constant_buffer_bound", true), false) &&
        equal(c.get("instance_input_initialization_producer", ""),
              String("FNiagaraDataSet::Allocate; compiled ParameterStore and constant-buffer bindings "
                     "inspected")) &&
        signed32(entry.get("fresh_allocation_int32", Variant())) &&
        int64_t(entry["fresh_allocation_int32"]) == 0) {
        Dictionary result;
        result["name"] = entry["name"];
        result["value"] = 0;
        result["binding"] = "native_unbound_zero_initialization";
        return result;
    }
    if (defaults.size() != 1 || !signed32(Dictionary(defaults[0]).get("value", Variant())))
        return Dictionary();
    return defaults[0];
}
void bind(const Array &calls, Array used, Dictionary bindings, const String &key, const String &target,
          int components, Array errors) {
    Array matches;
    for (int i = 0; i < calls.size(); ++i) {
        Dictionary call = calls[i];
        if (Array(call.get("semantic_targets", Array())).has(target) &&
            int64_t(call.get("components", 0)) == components)
            matches.append(i);
    }
    if (matches.size() != 1 || used.has(matches.is_empty() ? Variant(-1) : matches[0])) {
        errors.append("Missing or ambiguous compiled RNG target: " + target);
        return;
    }
    used.append(matches[0]);
    bindings[key] = calls[int64_t(matches[0])];
}
void bind_intrinsic(const Array &calls, Array used, Dictionary bindings, const String &key,
                    const String &alias, const String &helper, int components, int count, Array errors) {
    Array matches;
    for (int i = 0; i < calls.size(); ++i) {
        Dictionary call = calls[i];
        Array stack = call.get("call_stack", Array());
        if (int64_t(call.get("components", 0)) != components || !stack.has(alias + String("_Emitter_Func")))
            continue;
        if (!helper.is_empty()) {
            bool found = false;
            for (int s = 0; s < stack.size(); ++s)
                if (String(stack[s]).begins_with(helper))
                    found = true;
            if (!found)
                continue;
        }
        matches.append(i);
    }
    if (matches.size() != count) {
        errors.append("Missing/ambiguous compiled intrinsic RNG sequence: " + alias);
        return;
    }
    for (int i = 0; i < matches.size(); ++i) {
        if (used.has(matches[i])) {
            errors.append("Compiled intrinsic RNG overlaps another binding: " + alias);
            continue;
        }
        used.append(matches[i]);
        bindings[key + String("/") + String::num_int64(i)] = calls[int64_t(matches[i])];
    }
}
void bind_curl(const Array &calls, Array used, Dictionary bindings, const Ref<Resource> &op,
               const String &alias, int64_t emitter_seed, Array errors) {
    String key = String(op->get("stable_id")) + "/$CurlOffset";
    bind(calls, used, bindings, key, "Local." + alias + ".RandomizationOffset", 1, errors);
    if (!bindings.has(key))
        return;
    Dictionary call = bindings[key];
    Variant seed = Compiler::input(op, "RandomSeed", 11);
    if (!signed32(seed)) {
        errors.append("Curl Noise currently requires a constant signed32 RandomSeed.");
        return;
    }
    String a = String(call.get("seed1_expression", "")).strip_edges(),
           b = String(call.get("seed2_expression", call.get("call_id", ""))).strip_edges(),
           c = String(call.get("seed3_expression", "")).strip_edges(),
           range = String(call.get("range_expression", "")).strip_edges();
    bool randomized = bool(Compiler::input(op, "RandomizeNoiseSample", true));
    if (!a.is_valid_int() || a.to_int() != int64_t(seed) || c != "0" ||
        (randomized ? !b.ends_with(".Emitter.RandomSeed") : b != "0") || !range.is_valid_float() ||
        range.to_float() != 4096.0 || !equal(call.get("primitive", ""), String("rand_float"))) {
        errors.append("Compiled Curl Noise offset seed assembly differs from the authored module.");
        return;
    }
    call["verified_seed1_constant"] = int64_t(seed);
    call["verified_seed2_constant"] = randomized ? emitter_seed : 0;
    call["verified_seed3_constant"] = 0;
}
void bind_orientation(const Array &calls, Array used, Dictionary bindings, const Ref<Resource> &op,
                      const String &alias, Array errors) {
    String key = String(op->get("stable_id")) + "/$Orientation";
    bind_intrinsic(calls, used, bindings, key, alias, "RandomQuaternion_v1_1_", 3, 1, errors);
    if (!bindings.has(key + "/0"))
        return;
    Dictionary call = bindings[key + "/0"];
    String expression = String(call.get("seed3_expression", "")).strip_edges();
    Variant seed = Compiler::input(op, "RandomSeed", 1);
    if (!expression.is_valid_int() || !signed32(seed) || expression.to_int() != int64_t(seed)) {
        errors.append("RandomQuaternion requires a compiled literal Seed3 that matches the authored signed32 "
                      "RandomSeed input: " +
                      alias);
        return;
    }
    call["verified_seed3_constant"] = int64_t(seed);
    bindings.erase(key + "/0");
    bindings[key] = call;
}
void bind_color(const Array &calls, Array used, Dictionary bindings, const String &key, const String &alias,
                const String &component, Array errors) {
    Array matches;
    for (int i = 0; i < calls.size(); ++i) {
        Dictionary call = calls[i];
        if (int64_t(call.get("components", 0)) != 1 ||
            !Array(call.get("call_stack", Array())).has(alias + String("_Emitter_Func")))
            continue;
        bool matched =
            component == "Alpha" && Array(call.get("semantic_targets", Array())).has("Particles.Color.w");
        if (component != "Alpha") {
            Array uses = call.get("parameter_uses", Array());
            for (int u = 0; u < uses.size(); ++u) {
                Dictionary use = uses[u];
                if (String(use.get("function", "")).begins_with("HueShiftLinearColor_") &&
                    equal(use.get("parameter", ""), "In_" + component))
                    matched = true;
            }
        }
        if (matched)
            matches.append(i);
    }
    if (matches.size() != 1 || used.has(matches.is_empty() ? Variant(-1) : matches[0])) {
        errors.append("Missing/ambiguous compiled color-adjustment draw: " + alias + "/" + component);
        return;
    }
    used.append(matches[0]);
    bindings[key] = calls[int64_t(matches[0])];
}
int64_t random_count(const Variant &value) {
    if (value.get_type() != Variant::DICTIONARY)
        return 0;
    Dictionary d = value;
    if (!equal(d.get("kind", Variant()), String("call")))
        return 0;
    int64_t count = String(d.get("name", "")).begins_with("RandomRange") ? 1 : 0;
    Array args = d.get("arguments", Array());
    for (int i = 0; i < args.size(); ++i)
        count += random_count(Dictionary(args[i]).get("value", Variant()));
    return count;
}
Array random_leaves(const Variant &value, const String &path) {
    Array leaves;
    if (value.get_type() != Variant::DICTIONARY)
        return leaves;
    Dictionary d = value;
    if (!equal(d.get("kind", Variant()), String("call")))
        return leaves;
    if (named(d.get("name", ""), {"RandomRangeFloat", "RandomRangeVector", "RandomRangeInt"})) {
        Dictionary leaf;
        leaf["path"] = path;
        leaf["value"] = value;
        leaves.append(leaf);
    }
    Array args = d.get("arguments", Array());
    for (int i = 0; i < args.size(); ++i) {
        Dictionary arg = args[i];
        String name = arg.get("name", ""), child = name == "CurveIndex"   ? "index"
                                                   : name == "ScaleCurve" ? "scale"
                                                   : name == "Vector2D"   ? "vector"
                                                                          : name.to_lower();
        leaves.append_array(random_leaves(arg.get("value", Variant()), path + String("/") + child));
    }
    return leaves;
}
} // namespace godot::nbsource
