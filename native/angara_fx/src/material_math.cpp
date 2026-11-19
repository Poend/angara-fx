#include "material_compiler.h"
#include <cmath>
namespace godot::nbmaterial {
Dictionary Context::node_math(const String &op, const Dictionary &n, const Dictionary &scope, bool &handled) {
    Dictionary p = n["properties"], inputs = n["inputs"], value = constant(0.0);
    handled = true;
    if (op == "BlackBody") {
        if (!inputs.has("Temp") || text(Dictionary(inputs["Temp"]), "source").is_empty()) {
            error("blackbody_temperature", "BlackBody requires a connected temperature input in kelvins.");
            return value;
        }
        feature("blackbody");
        require("blackbody_uses_linear_srgb_working_space");
        value = expression("nb_blackbody(" + code(cast(input(n, "Temp", scope, value), 1)) + ")", 3);
    } else if (op == "Add" || op == "Multiply" || op == "Subtract" || op == "Divide") {
        Dictionary a = input(n, "A", scope, constant(p.get("ConstA", 0.0))),
                   b = input(n, "B", scope, constant(p.get("ConstB", 1.0))), literal_b = b;
        int channels = common(a, b);
        String symbol = op == "Add" ? "+" : (op == "Multiply" ? "*" : (op == "Subtract" ? "-" : "/"));
        if (op == "Divide" && is_uniform(a) && is_uniform(b)) {
            feature("safe_divisor");
            b = expression("nb_safe_divisor(" + code(cast(b, channels)) + ")", channels);
            b["uniform"] = true;
        }
        value = expression("(" + code(cast(a, channels)) + " " + symbol + " " + code(cast(b, channels)) + ")",
                           channels);
        Dictionary folded = fold_binary(op, a, literal_b, channels);
        if (!folded.is_empty())
            value = folded;
    } else if (op == "Min" || op == "Max") {
        Dictionary a = input(n, "A", scope, constant(p.get("ConstA", 0.0))),
                   b = input(n, "B", scope, constant(p.get("ConstB", 1.0)));
        value = expression(op.to_lower() + "(" + code(a) + ", " + code(cast(b, width(a))) + ")", width(a));
        Dictionary folded = fold_binary(op, a, b, width(a));
        if (!folded.is_empty())
            value = folded;
    } else if (op == "Step") {
        Dictionary edge = input(n, "Y", scope, constant(p.get("ConstY", 0.0))),
                   sample = input(n, "X", scope, constant(p.get("ConstX", 1.0)));
        int size = common(edge, sample);
        value = expression("step(" + code(cast(edge, size)) + ", " + code(cast(sample, size)) + ")", size);
        if (edge.has("constant") && sample.has("constant")) {
            Array a = literal_values(edge, size), b = literal_values(sample, size), numbers;
            for (int i = 0; i < size; ++i)
                numbers.append(double(b[i]) >= double(a[i]) ? 1.0 : 0.0);
            value = constant(numbers);
        }
    } else if (op == "SmoothStep") {
        Dictionary low = input(n, "Min", scope, constant(p.get("ConstMin", 0.0))),
                   high = input(n, "Max", scope, constant(p.get("ConstMax", 1.0))),
                   sample = input(n, "Value", scope, constant(p.get("ConstValue", 0.0)));
        int size = common(low, high);
        size = common(dict({{"channels", size}}), sample);
        if (same_expression(sample, low))
            value = cast(constant(0.0), size);
        else if (same_expression(sample, high))
            value = cast(constant(1.0), size);
        else if (same_expression(low, high))
            value = expression("step(" + code(cast(low, size)) + ", " + code(cast(sample, size)) + ")", size);
        else if (low.has("constant") && high.has("constant") && sample.has("constant")) {
            Array a = literal_values(low, size), b = literal_values(high, size),
                  x = literal_values(sample, size), numbers;
            for (int i = 0; i < size; ++i) {
                double lo = a[i], hi = b[i], v = x[i];
                if (v < lo)
                    numbers.append(0.0);
                else if (v >= hi)
                    numbers.append(1.0);
                else {
                    double t = float32(float32(v - lo) / float32(hi - lo));
                    numbers.append(float32(float32(t * t) * float32(3.0 - float32(2.0 * t))));
                }
            }
            value = constant(numbers);
        } else
            value = expression("smoothstep(" + code(cast(low, size)) + ", " + code(cast(high, size)) + ", " +
                                   code(cast(sample, size)) + ")",
                               size);
    } else if (op == "SphereMask") {
        for (const char *required : {"A", "B"})
            if (!inputs.has(required))
                error("sphere_mask_input", String("SphereMask requires ") + required);
        Dictionary a = input(n, "A", scope, constant(0.0)), b = input(n, "B", scope, constant(0.0));
        int size = common(a, b);
        String delta = "(" + code(cast(a, size)) + " - " + code(cast(b, size)) + ")",
               distance = (size == 1 ? String("abs") : String("length")) + "(" + delta + ")";
        String inverse_radius =
            number(float32(1.0 / MAX(float32(0.00001), float32(p.get("AttenuationRadius", 256.0)))));
        if (inputs.has("Radius"))
            inverse_radius =
                "(1.0 / max(0.00001, " + code(cast(input(n, "Radius", scope, constant(0.0)), 1)) + "))";
        Dictionary inverse_hardness = constant(float32(
            1.0 / MAX(float32(1.0 - float32(float32(p.get("HardnessPercent", 100.0)) * float32(0.01))),
                      float32(0.00001))));
        if (inputs.has("Hardness")) {
            Dictionary hardness = input(n, "Hardness", scope, constant(0.0));
            inverse_hardness =
                expression("(" + code(cast(constant(1.0), width(hardness))) + " / max(" +
                               code(cast(constant(1.0), width(hardness))) + " - " + code(hardness) + ", " +
                               code(cast(constant(0.00001), width(hardness))) + "))",
                           width(hardness));
        }
        value = expression("clamp((1.0 - " + distance + " * " + inverse_radius + ") * " +
                               code(inverse_hardness) + ", 0.0, 1.0)",
                           width(inverse_hardness));
    } else if (op == "LinearInterpolate") {
        Dictionary a = input(n, "A", scope, constant(p.get("ConstA", 0.0))),
                   b = input(n, "B", scope, constant(p.get("ConstB", 1.0))),
                   alpha = input(n, "Alpha", scope, constant(p.get("ConstAlpha", 0.5)));
        int channels = common(a, b);
        if (width(alpha) != 1 && width(alpha) != channels)
            error("lerp_dimensions", "Lerp alpha must be scalar or match its inputs.");
        value = expression("mix(" + code(cast(a, channels)) + ", " + code(cast(b, channels)) + ", " +
                               code(alpha) + ")",
                           channels);
    } else if (op == "OneMinus") {
        Dictionary v = input(n, "Input", scope, constant(0.0));
        value = expression("(" + code(cast(constant(1.0), width(v))) + " - " + code(v) + ")", width(v));
    } else if (op == "AppendVector") {
        if (!inputs.has("A") || !inputs.has("B"))
            error("append_input", "AppendVector requires both inputs.");
        Dictionary a = input(n, "A", scope, constant(0.0)), b = input(n, "B", scope, constant(0.0));
        int channels = width(a) + width(b);
        if (channels > 4)
            error("append_dimensions", "AppendVector exceeds four components.");
        value = expression(type(channels) + "(" + code(a) + ", " + code(b) + ")", channels);
    } else if (op == "ComponentMask" || op == "StaticComponentMaskParameter") {
        Dictionary v = input(n, "Input", scope, constant(0.0));
        Array flags;
        if (op == "StaticComponentMaskParameter") {
            Dictionary parameters = property("parameters"), masks = parameters["static_component_mask"];
            flags = masks.get(text(p, "ParameterName", "Param"),
                              list({p.get("DefaultR", false), p.get("DefaultG", false),
                                    p.get("DefaultB", false), p.get("DefaultA", false)}));
        } else
            flags = list({p.get("R", false), p.get("G", false), p.get("B", false), p.get("A", false)});
        Array mask;
        for (int i = 0; i < 4; ++i)
            if (bool(flags[i]))
                mask.append(i);
        if (mask.is_empty())
            error("empty_mask", "Component mask selects no channel.");
        value = swizzle(v, mask);
    } else if (op == "Clamp" || op == "Saturate") {
        Dictionary v = input(n, "Input", scope, constant(0.0)),
                   low = cast(input(n, "Min", scope, constant(p.get("MinDefault", 0.0))), width(v)),
                   high = cast(input(n, "Max", scope, constant(p.get("MaxDefault", 1.0))), width(v));
        String mode = text(p, "ClampMode", "CMODE_Clamp");
        value = expression(mode == "CMODE_ClampMin"
                               ? "max(" + code(v) + ", " + code(low) + ")"
                               : (mode == "CMODE_ClampMax"
                                      ? "min(" + code(v) + ", " + code(high) + ")"
                                      : "clamp(" + code(v) + ", " + code(low) + ", " + code(high) + ")"),
                           width(v));
    } else if (op == "SquareRoot" || op == "Normalize" || op == "Ceil" || op == "Floor" || op == "Abs" ||
               op == "Frac") {
        String key = op == "Normalize" ? "VectorInput" : "Input";
        if (!inputs.has(key))
            error("unary_input", op + String(" requires ") + key);
        Dictionary v = input(n, key, scope, constant(0.0));
        String operation =
            op == "SquareRoot"
                ? "sqrt"
                : (op == "Normalize"
                       ? "normalize"
                       : (op == "Ceil" ? "ceil"
                                       : (op == "Floor" ? "floor" : (op == "Abs" ? "abs" : "fract"))));
        value = expression(operation + "(" + code(v) + ")", width(v));
        if (op == "Normalize" && is_uniform(v))
            value["code"] =
                "(" + code(v) + " / sqrt(max(dot(" + code(v) + ", " + code(v) + "), 0.0000000001)))";
    } else if (op == "Round") {
        if (!inputs.has("Input"))
            error("unary_input", "Round requires Input");
        Dictionary v = input(n, "Input", scope, constant(0.0));
        if (is_uniform(v))
            value =
                expression("floor(" + code(v) + " + " + code(cast(constant(0.5), width(v))) + ")", width(v));
        else {
            feature("round_even");
            value = expression("nb_round_even(" + code(v) + ")", width(v));
        }
    } else if (op == "Sine" || op == "Cosine") {
        Dictionary v = input(n, "Input", scope, constant(0.0));
        double period = p.get("Period", 1.0);
        value = expression((op == "Sine" ? String("sin") : String("cos")) + "(" + code(v) + " * " +
                               number(period > 0 ? 6.283185307179586476925286766559 / period : 1.0) + ")",
                           width(v));
    } else if (op == "DotProduct" || op == "Distance") {
        Dictionary a = input(n, "A", scope, constant(0.0)), b = input(n, "B", scope, constant(0.0));
        int size = common(a, b);
        value = expression((op == "DotProduct" ? String("dot") : String("distance")) + "(" +
                               code(cast(a, size)) + ", " + code(cast(b, size)) + ")",
                           1);
    } else if (op == "Power") {
        if (!inputs.has("Base"))
            error("power_base", "Power requires a Base input.");
        Dictionary base = input(n, "Base", scope, constant(0.0)),
                   exponent = input(n, "Exponent", scope, constant(p.get("ConstExponent", 2.0)));
        int size = common(base, exponent);
        Variant literal = exponent.get("constant", Variant());
        if (nbdocument::number(literal) && double(literal) == 0)
            value = cast(constant(1.0), size);
        else if (nbdocument::number(literal) && double(literal) == 1)
            value = cast(base, size);
        else {
            feature("positive_power");
            value = expression("nb_positive_power(" + code(cast(base, size)) + ", " +
                                   code(cast(exponent, size)) + ")",
                               size);
        }
    } else if (op == "Desaturation") {
        Dictionary v = input(n, "Input", scope, constant(0.0));
        if (width(v) == 2) {
            Dictionary extended = expression("vec3(" + code(v) + ",0.0)", 3);
            extended["uniform"] = is_uniform(v);
            v = extended;
        } else
            v = cast(v, 3);
        Variant factors = p.get("LuminanceFactors", Variant());
        if (factors.get_type() == Variant::NIL) {
            factors = dict({{"R", 0.2126390059}, {"G", 0.7151686788}, {"B", 0.0721923153}});
            require("unserialized_luminance_factors_require_source_working_color_space");
        }
        String gray = "dot(" + code(v) + ", " + code(constant(components(factors, 3))) + ")";
        value = expression(gray, 1);
        if (inputs.has("Fraction")) {
            Dictionary fraction = input(n, "Fraction", scope, constant(0.0));
            value = expression("mix(" + code(v) + ", vec3(" + gray + "), " + code(fraction) + ")", 3);
        }
    } else
        handled = false;
    return value;
}
} // namespace godot::nbmaterial
