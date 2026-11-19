#include "material_compiler.h"
#include <godot_cpp/variant/utility_functions.hpp>
#include <cmath>
#include <limits>
namespace godot::nbmaterial {
String text(const Dictionary &d, const String &key, const String &fallback) {
    return Variant(d.get(key, fallback)).stringify();
}
String code(const Dictionary &value) {
    return text(value, "code");
}
int width(const Dictionary &value) {
    return int64_t(value.get("channels", 0));
}
Dictionary expression(const String &source, int channels) {
    return dict({{"code", source}, {"channels", channels}});
}
Array list(std::initializer_list<Variant> values) {
    Array result;
    for (const Variant &value : values)
        result.append(value);
    return result;
}
String join(const Array &values, const String &separator) {
    PackedStringArray strings;
    for (int64_t i = 0; i < values.size(); ++i)
        strings.append(Variant(values[i]).stringify());
    return separator.join(strings);
}
Dictionary Context::property(const StringName &name) const {
    return graph->get(name);
}
void Context::require(const String &value) {
    if (!requirements.has(value))
        requirements.append(value);
}
void Context::error(const String &value, const String &message) {
    errors.append(dict({{"severity", "error"}, {"code", value}, {"message", message}}));
}
void Context::feature(const String &name) {
    features[name] = true;
}
String Context::type(int channels) {
    return channels == 1 ? String("float") : String("vec") + String::num_int64(channels);
}
bool Context::is_uniform(const Dictionary &value) {
    return bool(value.get("uniform", value.has("constant")));
}
void Context::note_uniform(const Dictionary &value) {
    if (!uniform_stack.empty() && !is_uniform(value))
        uniform_stack.back() = false;
}
double Context::float32(const Variant &value) {
    return double(float(double(value)));
}
Array Context::components(const Variant &value, int count) {
    Array result;
    const char *rgba[] = {"R", "G", "B", "A"}, *xyzw[] = {"X", "Y", "Z", "W"};
    for (int i = 0; i < count; ++i) {
        if (value.get_type() == Variant::ARRAY) {
            Array a = value;
            result.append(i < a.size() ? a[i] : Variant(0.0));
        } else if (value.get_type() == Variant::DICTIONARY) {
            Dictionary d = value;
            result.append(d.get(rgba[i], d.get(xyzw[i], 0.0)));
        } else
            result.append(double(value));
    }
    return result;
}
String Context::number(const Variant &value) {
    if (!nbdocument::number(value) || !std::isfinite(double(value)) ||
        std::abs(double(value)) > 3.402823466e38) {
        error("numeric_value", "Shader numeric value must be a finite float32.");
        return "0.0";
    }
    String result = UtilityFunctions::var_to_str(float32(value));
    return result.contains(".") || result.contains("e") || result.contains("E") ? result : result + ".0";
}
Dictionary Context::constant(Variant value) {
    if (value.get_type() == Variant::DICTIONARY)
        value = components(value, 4);
    if (value.get_type() == Variant::ARRAY) {
        Array values = value;
        if (values.size() == 1)
            return constant(values[0]);
        PackedStringArray numbers;
        for (int64_t i = 0; i < values.size(); ++i)
            numbers.append(number(values[i]));
        return dict({{"code", type(int(values.size())) + "(" + String(", ").join(numbers) + ")"},
                     {"channels", values.size()},
                     {"constant", value}});
    }
    return dict({{"code", number(value)}, {"channels", 1}, {"constant", value}});
}
Array Context::literal_values(const Dictionary &value, int channels) {
    Array result = components(value["constant"], channels);
    for (int64_t i = 0; i < result.size(); ++i)
        result[i] = float32(result[i]);
    return result;
}
bool Context::same_expression(const Dictionary &a, const Dictionary &b) {
    return width(a) == width(b) && code(a) == code(b);
}
int Context::common(const Dictionary &a, const Dictionary &b) {
    int x = width(a), y = width(b);
    if (x != y && x != 1 && y != 1)
        error("arithmetic_dimensions", "Arithmetic vector dimensions differ.");
    return MAX(x, y);
}
Dictionary Context::fold_binary(const String &op, const Dictionary &a, const Dictionary &b, int channels) {
    if (!a.has("constant") || !b.has("constant"))
        return Dictionary();
    Array x = literal_values(a, channels), y = literal_values(b, channels), values;
    bool numerator_zero = true, denominator_zero = true;
    for (int i = 0; i < channels; ++i) {
        numerator_zero &= double(x[i]) == 0;
        denominator_zero &= double(y[i]) == 0;
    }
    for (int i = 0; i < channels; ++i) {
        double left = x[i], right = y[i], result = 0;
        if (op == "Add")
            result = left + right;
        else if (op == "Subtract")
            result = left - right;
        else if (op == "Multiply")
            result = left * right;
        else if (op == "Divide")
            result = numerator_zero
                         ? 0.0
                         : (denominator_zero
                                ? left / float32(0.00001)
                                : (right != 0 ? left / right : std::numeric_limits<double>::infinity()));
        else if (op == "Min")
            result = MIN(left, right);
        else if (op == "Max")
            result = MAX(left, right);
        values.append(float32(result));
    }
    return constant(values);
}
Dictionary Context::cast(const Dictionary &value, int channels) {
    int size = width(value);
    if (size == channels)
        return value;
    if (size == 1) {
        if (value.has("constant"))
            return constant(literal_values(value, channels));
        Dictionary result = expression(type(channels) + "(" + code(value) + ")", channels);
        result["uniform"] = is_uniform(value);
        return result;
    }
    if (channels < size && channels > 0) {
        Array mask;
        for (int i = 0; i < channels; ++i)
            mask.append(i);
        return swizzle(value, mask);
    }
    error("cast_dimensions",
          "Vector widening requires an explicit source operation: " + String::num_int64(size) + " -> " +
              String::num_int64(channels) + " (" + code(value) + ").");
    return constant(0.0);
}
Dictionary Context::swizzle(const Dictionary &value, const Array &mask) {
    if (mask.is_empty())
        return value;
    if (width(value) == 1)
        return cast(value, int(mask.size()));
    String suffix;
    for (int64_t i = 0; i < mask.size(); ++i) {
        int64_t channel = mask[i];
        if (channel >= width(value)) {
            error("mask_dimensions", "Component mask exceeds the connected vector width.");
            return constant(0.0);
        }
        suffix += String("xyzw").substr(channel, 1);
    }
    if (value.has("constant")) {
        Array numbers = literal_values(value, width(value)), selected;
        for (int64_t i = 0; i < mask.size(); ++i)
            selected.append(numbers[int64_t(mask[i])]);
        return constant(selected);
    }
    Dictionary result = expression("(" + code(value) + ")." + suffix, int(mask.size()));
    result["uniform"] = is_uniform(value);
    return result;
}
Array Context::output_mask(const String &op, int output) {
    int count = 0;
    if (op == "DynamicParameter" || op == "Constant3Vector")
        count = 4;
    else if (op == "Constant2Vector")
        count = 3;
    else if (op == "ParticleColor" || op == "VectorParameter" || op == "TextureSample" ||
             op == "TextureSampleParameter2D" || op == "TextureSampleParameterVolume" || op == "ParticleSubUV" ||
             op == "TextureSampleParameterSubUV" || op == "Constant4Vector")
        count = 6;
    if (count && (output < 0 || output >= count)) {
        error("output_index", "Invalid output index for " + op);
        return Array();
    }
    if (op == "DynamicParameter")
        return output >= 0 && output < 4 ? list({output}) : Array();
    bool sampled = op == "ParticleColor" || op == "VectorParameter" || op == "TextureSample" ||
                   op == "TextureSampleParameter2D" || op == "TextureSampleParameterVolume" || op == "ParticleSubUV" ||
                   op == "TextureSampleParameterSubUV";
    if (sampled) {
        if (output == 0)
            return list({0, 1, 2});
        if (output >= 1 && output <= 4)
            return list({output - 1});
        if (output == 5)
            return list({0, 1, 2, 3});
    }
    if (op == "Constant4Vector") {
        if (output == 0)
            return list({0, 1, 2, 3});
        if (output >= 1 && output <= 4)
            return list({output - 1});
        if (output == 5)
            return list({0, 1, 2});
    }
    if (op == "Constant3Vector") {
        if (output == 0)
            return list({0, 1, 2});
        if (output >= 1 && output <= 3)
            return list({output - 1});
    }
    if (op == "Constant2Vector") {
        if (output == 0)
            return list({0, 1});
        if (output >= 1 && output <= 2)
            return list({output - 1});
    }
    return Array();
}
Dictionary Context::parameter(const String &kind, const String &name, const Variant &initial, int channels) {
    Variant value = initial;
    if (channels == 1 && !nbdocument::number(value)) {
        error("parameter_type", "Scalar parameter is not numeric: " + name);
        value = 0.0;
    }
    String key = kind + String(":") + name, uniform = text(uniform_keys, key);
    if (uniform.is_empty()) {
        String base = "p" + String::num_int64(uniform_keys.size());
        uniform = text(options, "symbol_prefix") + base;
        aliases[uniform] = base;
        uniform_keys[key] = uniform;
        Dictionary literal =
            constant(channels == 1 ? Variant(double(value)) : Variant(components(value, channels)));
        declarations.append("uniform " + type(channels) + " " + uniform + " = " + code(literal) + ";");
        if (channels == 1)
            uniforms[uniform] = double(value);
        else {
            Array numbers = literal["constant"];
            uniforms[uniform] =
                Vector4(double(numbers[0]), double(numbers[1]), double(numbers[2]), double(numbers[3]));
        }
        if (bool(options.get("embedded", false))) {
            Dictionary values = options.get("values", Dictionary());
            uniforms[uniform] = values.get(base, uniforms[uniform]);
        }
        bindings.append(dict({{"kind", kind}, {"parameter", name}, {"uniform", uniform}}));
    }
    return expression(uniform, channels);
}
String Context::uv(int index) {
    if (index != 0 && index != 1)
        error("uv_channel", "Only UV0 and UV1 are available.");
    require("uv_channel_" + String::num_int64(index));
    feature("particle");
    return "(nb_renderer_kind == 1 ? nb_source_sprite_uv(UV, " + String::num_int64(index) +
           ") : " + (index == 0 ? String("UV") : String("UV2")) + ")";
}
} // namespace godot::nbmaterial
