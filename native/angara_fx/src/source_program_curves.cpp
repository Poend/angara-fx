#include "source_program.h"
#include "effect_compiler.h"
#include "value_evaluator.h"
#include <cmath>

namespace godot::nbsource {
using nbcompiler::Compiler;
using nbcompiler::equal;
using nbcompiler::named;
using nbcompiler::number;
String curve_identifier(const String &name) {
    return name.replace(" ", "").replace(".", "_");
}
String curve_owner(const Dictionary &sample) {
    Array stack = sample.get("call_stack", Array());
    if (stack.is_empty() || !String(stack[stack.size() - 1]).ends_with("_Emitter_Func"))
        return "";
    return String(stack[stack.size() - 1]).trim_suffix("_Emitter_Func");
}
Array curve_leaves(const Variant &value, const String &path, const Array &arguments, const Array &calls) {
    Array leaves;
    if (value.get_type() != Variant::DICTIONARY)
        return leaves;
    Dictionary d = value;
    if (!equal(d.get("kind", Variant()), String("call")))
        return leaves;
    Array current = calls.duplicate();
    current.append(d.get("name", ""));
    String name = d.get("name", "");
    if (named(name, {"FloatFromCurve", "ColorFromCurve", "Vector2DFromCurve", "VectorFromCurve"})) {
        Dictionary leaf;
        leaf["path"] = path;
        leaf["value"] = value;
        leaf["argument_path"] = arguments.duplicate();
        leaf["call_path"] = current;
        leaf["input"] = name == "FloatFromCurve"      ? "FloatCurve"
                        : name == "ColorFromCurve"    ? "ColorCurve"
                        : name == "Vector2DFromCurve" ? "Vector2Curve"
                                                      : "VectorCurve";
        leaves.append(leaf);
    }
    Array args = d.get("arguments", Array());
    for (int i = 0; i < args.size(); ++i) {
        Dictionary arg = args[i];
        String key = arg.get("name", "");
        Array child = arguments.duplicate();
        child.append(key);
        String suffix = key == "CurveIndex"   ? "index"
                        : key == "ScaleCurve" ? "scale"
                        : key == "Vector2D"   ? "vector"
                                              : key.to_lower();
        leaves.append_array(curve_leaves(arg.get("value", Variant()), path + String("/") + suffix, child, current));
    }
    return leaves;
}
Dictionary curve_sample(const Dictionary &c, const String &target, const Array &arguments,
                        const Array &calls) {
    Array nodes = c.get("value_calls", Array());
    if (!nodes.is_empty()) {
        Array selected, roots;
        for (int i = 0; i < nodes.size(); ++i)
            if (Array(Dictionary(nodes[i]).get("semantic_targets", Array())).has(target))
                selected.append(nodes[i]);
        for (int i = 0; i < selected.size(); ++i) {
            Dictionary node = selected[i];
            bool child = false;
            for (int p = 0; p < selected.size(); ++p) {
                Dictionary parent = selected[p];
                if (equal(parent, node))
                    continue;
                String owner = String(parent["function"]).trim_suffix("_Emitter_Func");
                Array edges = node["semantic_targets"];
                for (int e = 0; e < edges.size(); ++e)
                    if (String(edges[e]).begins_with(owner + "."))
                        child = true;
            }
            if (!child)
                roots.append(node);
        }
        if (roots.size() != 1 || calls.is_empty())
            return Dictionary();
        String owner = String(Dictionary(roots[0])["function"]).trim_suffix("_Emitter_Func"),
               expected = curve_identifier(calls[0]);
        if (owner != expected && !(owner.begins_with(expected) && owner.trim_prefix(expected).is_valid_int()))
            return Dictionary();
        if (!arguments.is_empty())
            return curve_sample(c, owner + "." + String(arguments[0]), arguments.slice(1), calls.slice(1));
        Array samples = c.get("curve_samples", Array()), matches;
        for (int i = 0; i < samples.size(); ++i) {
            Dictionary sample = samples[i];
            if (Array(sample.get("semantic_targets", Array())).has(target) && curve_owner(sample) == owner)
                matches.append(sample);
        }
        return matches.size() == 1 ? Dictionary(matches[0]) : Dictionary();
    }
    Array samples = c.get("curve_samples", Array()), matches, roots;
    for (int i = 0; i < samples.size(); ++i)
        if (Array(Dictionary(samples[i]).get("semantic_targets", Array())).has(target))
            matches.append(samples[i]);
    for (int i = 0; i < matches.size(); ++i) {
        Dictionary sample = matches[i];
        bool child = false;
        for (int p = 0; p < matches.size(); ++p) {
            Dictionary parent = matches[p];
            if (equal(parent, sample))
                continue;
            String owner = curve_owner(parent);
            Array targets = sample["semantic_targets"];
            if (!owner.is_empty() &&
                (targets.has(owner + ".ScaleCurve") || targets.has(owner + ".CurveIndex")))
                child = true;
        }
        if (!child)
            roots.append(sample);
    }
    if (roots.size() != 1)
        return Dictionary();
    if (arguments.is_empty())
        return roots[0];
    String owner = curve_owner(roots[0]);
    if (owner.is_empty() || !named(arguments[0], {"ScaleCurve", "CurveIndex"}))
        return Dictionary();
    return curve_sample(c, owner + "." + String(arguments[0]), arguments.slice(1));
}
Dictionary compiled_curve(const Dictionary &c, const String &owner, const String &emitter, Array errors) {
    Array interfaces = c.get("cached_default_data_interfaces", Array()), matches;
    for (int i = 0; i < interfaces.size(); ++i) {
        Dictionary d = interfaces[i];
        if (curve_identifier(d.get("compile_name", "")) == curve_identifier(owner) &&
            equal(d.get("source_emitter", ""), emitter))
            matches.append(d);
    }
    if (matches.size() != 1) {
        errors.append("Missing/ambiguous captured curve LUT: " + owner);
        return Dictionary();
    }
    Dictionary d = matches[0];
    int channels = int64_t(d.get("elements_per_sample", 0));
    Variant lut_value = d.get("shader_lut", Array());
    bool valid = bool(d.get("use_lut", false)) && channels >= 1 && channels <= 4 &&
                 lut_value.get_type() == Variant::ARRAY;
    Array lut;
    if (valid) {
        lut = lut_value;
        valid = lut.size() >= channels && lut.size() % channels == 0;
        for (int i = 0; valid && i < lut.size(); ++i)
            if (!number(lut[i]) || !std::isfinite(double(lut[i])))
                valid = false;
    }
    if (!valid) {
        errors.append("Unsupported/malformed compiled curve LUT: " + owner);
        return Dictionary();
    }
    for (const char *field : {"time_min", "time_max", "inv_time_range"}) {
        Variant value = d.get(field, Variant());
        if (String(field) == "inv_time_range" && value.get_type() == Variant::STRING &&
            equal(value, String("Infinity")) &&
            equal(d.get("time_min", Variant()), d.get("time_max", Variant()))) {
            bool constant = true;
            for (int i = channels; i < lut.size(); ++i)
                if (!equal(lut[i], lut[i % channels])) {
                    constant = false;
                    break;
                }
            if (constant)
                continue;
        }
        if (!number(value) || !std::isfinite(double(value))) {
            errors.append("Malformed compiled curve time range: " + owner);
            return Dictionary();
        }
    }
    Array records;
    for (int i = 0; i < channels; ++i)
        records.append(Dictionary());
    PackedFloat32Array packed;
    packed.resize(lut.size());
    for (int i = 0; i < lut.size(); ++i)
        packed.set(i, float(double(lut[i])));
    Dictionary result;
    result["kind"] = channels == 1   ? "curve_di"
                     : channels == 2 ? "vector2_curve_di"
                     : channels == 3 ? "vector3_curve_di"
                                     : "color_curve_di";
    result["channels"] = records;
    result["errors"] = Array();
    result["uses_lut"] = true;
    result["lut"] = packed;
    result["min_time"] = double(float(double(d["time_min"])));
    result["max_time"] = double(float(double(d["time_max"])));
    result["inv_range"] =
        d["inv_time_range"].get_type() == Variant::STRING ? 0.0 : double(float(double(d["inv_time_range"])));
    result["compiled_owner"] = d["compile_name"];
    return result;
}
Dictionary bind_curves(const Array &operations, const Dictionary &c, const String &emitter, Array errors) {
    Dictionary bindings;
    for (int i = 0; i < operations.size(); ++i) {
        Ref<Resource> op = operations[i];
        if (!bool(op->get("enabled")))
            continue;
        String alias = op->get("alias_name"), module = op->get("module_name"), id = op->get("stable_id");
        if (alias.is_empty())
            alias = module.get_slice("@", 0).get_file();
        Array direct;
        if (module == "ScaleSpriteSize")
            direct.append("UniformCurveSpriteScale");
        if (module == "ScaleSpriteSizeBySpeed" &&
            bool(Compiler::input(op, "SampleScaleFactorByCurve", false)))
            direct.append("ScaleFactorCurve");
        for (int d = 0; d < direct.size(); ++d) {
            String input = direct[d], owner = "Emitter." + alias + "." + input;
            Variant authored = Compiler::input(op, input, Dictionary());
            if (authored.get_type() == Variant::DICTIONARY &&
                equal(Dictionary(authored).get("kind", Variant()), String("reference")))
                continue;
            Dictionary curve = compiled_curve(c, owner, emitter, errors);
            if (!curve.is_empty())
                bindings[id + "/" + input + "/0"] = curve;
        }
        Array inputs = op->get("inputs");
        for (int n = 0; n < inputs.size(); ++n) {
            Ref<Resource> input = inputs[n];
            String key = input->get("input_name");
            Array leaves = curve_leaves(input->get("value"), id + "/" + key + "/0");
            for (int l = 0; l < leaves.size(); ++l) {
                Dictionary leaf = leaves[l], value = leaf["value"];
                String curve_input = leaf["input"];
                Variant authored =
                    NBNativeValues::input_value(value.get("arguments", Array()), curve_input, Dictionary());
                if (authored.get_type() == Variant::DICTIONARY &&
                    equal(Dictionary(authored).get("kind", Variant()), String("reference")))
                    continue;
                String target = alias + "." + key;
                Array path = leaf["argument_path"];
                Dictionary sample = curve_sample(c, target, path, leaf["call_path"]);
                if (sample.is_empty()) {
                    String joined;
                    for (int p = 0; p < path.size(); ++p) {
                        if (p)
                            joined += "/";
                        joined += String(path[p]);
                    }
                    errors.append("Missing/ambiguous emitted curve dependency path: " + target + "/" +
                                  joined);
                    continue;
                }
                Array owners, interfaces = c.get("cached_default_data_interfaces", Array());
                for (int d = 0; d < interfaces.size(); ++d) {
                    Dictionary descriptor = interfaces[d];
                    String owner = descriptor.get("compile_name", "");
                    String prefix = curve_input == "FloatCurve"     ? "SampleCurve_"
                                    : curve_input == "ColorCurve"   ? "SampleColorCurve_"
                                    : curve_input == "Vector2Curve" ? "SampleVector2DCurve_"
                                                                    : "SampleVectorCurve_";
                    if (equal(prefix + curve_identifier(owner), sample["function"]))
                        owners.append(owner);
                }
                if (owners.size() != 1) {
                    errors.append("Curve sample has no unique compiled data-interface owner: " +
                                  String(sample["function"]));
                    continue;
                }
                Dictionary curve = compiled_curve(c, owners[0], emitter, errors);
                if (!curve.is_empty())
                    bindings[String(leaf["path"]) + "/" + curve_input] = curve;
            }
        }
    }
    return bindings;
}
} // namespace godot::nbsource
