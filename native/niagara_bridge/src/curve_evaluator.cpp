#include "curve_evaluator.h"
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <algorithm>
#include <cmath>
#include <map>
namespace godot {
namespace {
std::map<std::string, Dictionary> prepared;
double f32(double x) {
    return double(float(x));
}
double lerp32(double a, double b, double alpha) {
    return f32(a + f32(alpha * f32(b - a)));
}
double number(const Dictionary &d, const char *key, double fallback = 0) {
    return double(d.get(key, fallback));
}
String text(const Dictionary &d, const char *key, const char *fallback = "") {
    return d.get(key, fallback);
}
double numeric(const Variant &v, Array errors, const String &label) {
    if ((v.get_type() != Variant::FLOAT && v.get_type() != Variant::INT) || !std::isfinite(double(v))) {
        errors.append("Curve " + label + " must be a finite number.");
        return 0;
    }
    return v;
}
bool has(const String &value, std::initializer_list<const char *> values) {
    for (auto s : values)
        if (value == s)
            return true;
    return false;
}
} // namespace
void NBNativeCurves::_bind_methods() {
    ClassDB::bind_static_method("NBNativeCurves", D_METHOD("prepare", "value", "target_type"),
                                &NBNativeCurves::prepare);
    ClassDB::bind_static_method("NBNativeCurves", D_METHOD("sample", "curve", "time"),
                                &NBNativeCurves::sample);
    ClassDB::bind_static_method("NBNativeCurves", D_METHOD("sample_channel", "channel", "time"),
                                &NBNativeCurves::sample_channel);
    ClassDB::bind_static_method("NBNativeCurves", D_METHOD("sample_color", "curve", "time"),
                                &NBNativeCurves::sample_color);
    ClassDB::bind_static_method("NBNativeCurves", D_METHOD("sample_vector2", "curve", "time"),
                                &NBNativeCurves::sample_vector2);
    ClassDB::bind_static_method("NBNativeCurves", D_METHOD("sample_vector3", "curve", "time"),
                                &NBNativeCurves::sample_vector3);
    ClassDB::bind_static_method("NBNativeCurves", D_METHOD("default_vector2_curve"),
                                &NBNativeCurves::default_vector2_curve);
    ClassDB::bind_static_method("NBNativeCurves", D_METHOD("default_vector3_curve"),
                                &NBNativeCurves::default_vector3_curve);
    ClassDB::bind_static_method("NBNativeCurves", D_METHOD("_sample_di", "curve", "time"),
                                &NBNativeCurves::sample_di);
    ClassDB::bind_static_method("NBNativeCurves", D_METHOD("_build_lut", "curve", "count"),
                                &NBNativeCurves::build_lut);
    ClassDB::bind_static_method(
        "NBNativeCurves", D_METHOD("_passes_threshold", "original", "candidate", "channels", "threshold"),
        &NBNativeCurves::passes_threshold);
    ClassDB::bind_static_method("NBNativeCurves",
                                D_METHOD("_expand_literal", "literal", "target_type", "errors"),
                                &NBNativeCurves::expand_literal);
    ClassDB::bind_static_method("NBNativeCurves", D_METHOD("_normalize_channel", "raw", "errors"),
                                &NBNativeCurves::normalize_channel);
}
void NBNativeCurves::clear_cache() {
    prepared.clear();
}
Variant NBNativeCurves::prepare(const Variant &value, const String &type) {
    if (!has(type, {"DI<Curve>", "DI<ColorCurve>", "DI<Vector2DCurve>", "DI<VectorCurve>"}))
        return value;
    if (value.get_type() == Variant::DICTIONARY &&
        has(text(value, "kind"), {"curve_di", "color_curve_di", "vector2_curve_di", "vector3_curve_di"}))
        return value;
    String key = type + String(":") +
                 (value.get_type() == Variant::STRING ? String(value) : UtilityFunctions::var_to_str(value));
    std::string cache_key = key.utf8().get_data();
    auto found = prepared.find(cache_key);
    if (found != prepared.end())
        return found->second;
    Variant parsed = value.get_type() == Variant::STRING ? JSON::parse_string(value) : value;
    Dictionary result;
    Array channels, errors;
    result["kind"] = type == "DI<Curve>"           ? "curve_di"
                     : type == "DI<ColorCurve>"    ? "color_curve_di"
                     : type == "DI<Vector2DCurve>" ? "vector2_curve_di"
                                                   : "vector3_curve_di";
    result["channels"] = channels;
    result["errors"] = errors;
    result["uses_lut"] = true;
    result["lut"] = PackedFloat32Array();
    result["min_time"] = 0.;
    result["max_time"] = 1.;
    result["inv_range"] = 1.;
    if (parsed.get_type() == Variant::DICTIONARY && text(parsed, "kind") == "curve")
        parsed = expand_literal(parsed, type, errors);
    if (parsed.get_type() != Variant::DICTIONARY)
        errors.append("Curve data interface requires a JSON object.");
    else {
        Dictionary data = parsed;
        for (const char *flag : {"bUseLUT", "bOptimizeLUT", "bOverrideOptimizeThreshold"})
            if (data.has(flag) && data[flag].get_type() != Variant::BOOL)
                errors.append("Curve flag " + String(flag) + " must be Boolean.");
        result["uses_lut"] = bool(data.get("bUseLUT", true));
        Array names;
        if (type == "DI<Curve>")
            names.append("Curve");
        else if (type == "DI<ColorCurve>") {
            names.append("RedCurve");
            names.append("GreenCurve");
            names.append("BlueCurve");
            names.append("AlphaCurve");
        } else {
            names.append("XCurve");
            names.append("YCurve");
            if (type == "DI<VectorCurve>")
                names.append("ZCurve");
        }
        for (int n = 0; n < names.size(); ++n) {
            String name = names[n];
            if (data.get(name, Variant()).get_type() != Variant::DICTIONARY) {
                errors.append("Missing curve channel " + name + ".");
                continue;
            }
            channels.append(normalize_channel(data[name], errors));
        }
        if (errors.is_empty()) {
            bool initialized = false;
            double first = 0, last = 1;
            for (int n = 0; n < channels.size(); ++n) {
                Dictionary channel = channels[n];
                Array keys = channel["keys"];
                if (keys.is_empty())
                    continue;
                double a = number(keys[0], "time"), b = number(keys[keys.size() - 1], "time");
                first = initialized ? std::min(first, a) : a;
                last = initialized ? std::max(last, b) : b;
                initialized = true;
            }
            result["min_time"] = first;
            result["max_time"] = last;
            result["inv_range"] = first != last ? f32(1 / (last - first)) : 0.;
            if (bool(result["uses_lut"])) {
                PackedFloat32Array full = build_lut(result, 128);
                result["lut"] = full;
                double threshold =
                    f32(bool(data.get("bOverrideOptimizeThreshold", false))
                            ? numeric(data.get("OptimizeThreshold", .01), errors, "OptimizeThreshold")
                            : .01);
                if (bool(data.get("bOptimizeLUT", true)) && threshold > 0)
                    for (int count = 1; count < 128; ++count) {
                        auto candidate = build_lut(result, count);
                        if (passes_threshold(full, candidate, channels.size(), threshold)) {
                            result["lut"] = candidate;
                            break;
                        }
                    }
            }
        }
    }
    if (prepared.size() >= 64)
        prepared.erase(prepared.begin());
    prepared[cache_key] = result;
    return result;
}
Dictionary NBNativeCurves::expand_literal(const Dictionary &literal, const String &type, Array errors) {
    if (!has(type, {"DI<Curve>", "DI<Vector2DCurve>", "DI<VectorCurve>"})) {
        errors.append("Compact curve literals are implemented for scalar and vector interfaces only.");
        return {};
    }
    Variant raw = literal.get("keys", Array());
    if (raw.get_type() != Variant::ARRAY || Array(raw).is_empty()) {
        errors.append("Compact curve requires a nonempty key array.");
        return {};
    }
    Array keys = raw, expanded;
    for (int n = 0; n < keys.size(); ++n) {
        if (keys[n].get_type() != Variant::DICTIONARY) {
            errors.append("Malformed compact curve key.");
            continue;
        }
        Dictionary key = keys[n], seen;
        Array attributes = key.get("attributes", Array());
        for (int a = 0; a < attributes.size(); ++a) {
            Dictionary attribute = attributes[a];
            String name = text(attribute, "name").to_lower();
            if (!has(name, {"interp", "arrive", "leave", "tangent"}) || seen.has(name))
                errors.append("Unknown/repeated compact curve attribute: " + name);
            seen[name] = true;
        }
        String interp = text(key, "interp", "Auto").to_lower();
        String mode;
        if (interp == "auto" || interp == "cubic")
            mode = "Cubic";
        else if (interp == "linear")
            mode = "Linear";
        else if (interp == "constant")
            mode = "Constant";
        else {
            errors.append("Unsupported compact curve interpolation: " + interp);
            mode = "Cubic";
        }
        String tangent =
            String(key.get("tangent", key.has("arrive") || key.has("leave") ? "User" : "Auto")).to_lower();
        if (!has(tangent, {"auto", "user", "break", "none"}))
            errors.append("Unsupported compact curve tangent mode: " + tangent);
        Dictionary k;
        k["time"] = key.get("time", 0.);
        k["value"] = key.get("value", 0.);
        k["interpMode"] = mode;
        k["arriveTangent"] = key.get("arrive", 0.);
        k["leaveTangent"] = key.get("leave", 0.);
        k["tangentWeightMode"] = "RCTWM_WeightedNone";
        expanded.append(k);
    }
    Dictionary channel;
    channel["keys"] = expanded;
    Dictionary result;
    if (type == "DI<Curve>")
        result["Curve"] = channel;
    else {
        result["XCurve"] = channel;
        result["YCurve"] = channel;
        if (type == "DI<VectorCurve>")
            result["ZCurve"] = channel;
    }
    return result;
}
Dictionary NBNativeCurves::normalize_channel(const Dictionary &raw, Array errors) {
    Dictionary channel;
    Array keys;
    channel["keys"] = keys;
    channel["pre"] = text(raw, "preInfinityExtrap", "RCCE_Constant").trim_prefix("RCCE_");
    channel["post"] = text(raw, "postInfinityExtrap", "RCCE_Constant").trim_prefix("RCCE_");
    double initial = numeric(raw.get("defaultValue", 0.), errors, "default");
    channel["default"] = initial < 3.4028234e38 ? f32(initial) : 0.;
    for (const char *side : {"pre", "post"})
        if (!has(channel[side], {"Constant", "Linear", "Cycle", "CycleWithOffset", "Oscillate", "None"}))
            errors.append("Unsupported curve extrapolation " + String(channel[side]) + ".");
    Variant key_data = raw.get("keys", Array());
    if (key_data.get_type() != Variant::ARRAY) {
        errors.append("Curve keys must be an array.");
        return channel;
    }
    Array source = key_data;
    for (int n = 0; n < source.size(); ++n) {
        if (source[n].get_type() != Variant::DICTIONARY) {
            errors.append("Malformed curve key.");
            continue;
        }
        Dictionary k = source[n], key;
        key["time"] = f32(numeric(k.get("time", 0.), errors, "time"));
        key["value"] = f32(numeric(k.get("value", 0.), errors, "value"));
        key["arrive"] = f32(numeric(k.get("arriveTangent", 0.), errors, "arriveTangent"));
        key["leave"] = f32(numeric(k.get("leaveTangent", 0.), errors, "leaveTangent"));
        key["interp"] = text(k, "interpMode", "RCIM_Linear").trim_prefix("RCIM_");
        if (!has(key["interp"], {"Constant", "Linear", "Cubic"}))
            errors.append("Unsupported curve interpolation " + String(key["interp"]) + ".");
        if (text(k, "tangentWeightMode", "RCTWM_WeightedNone") != "RCTWM_WeightedNone")
            errors.append("Weighted curve tangents are retained but not implemented.");
        for (const char *field : {"time", "value", "arrive", "leave"})
            if (!std::isfinite(double(key[field])))
                errors.append("Non-finite curve " + String(field) + ".");
        if (!keys.is_empty() && number(key, "time") < number(keys[keys.size() - 1], "time"))
            errors.append("Curve keys are not ordered by time.");
        keys.append(key);
    }
    return channel;
}
double NBNativeCurves::sample_channel(const Dictionary &channel, double time) {
    Array keys = channel.get("keys", Array());
    if (keys.is_empty())
        return number(channel, "default");
    if (keys.size() == 1)
        return number(keys[0], "value");
    Dictionary begin = keys[0], end = keys[keys.size() - 1];
    double first = number(begin, "time"), last = number(end, "time"), offset = 0;
    String extrap = text(channel, time < first ? "pre" : "post", "Constant");
    if ((time < first || time > last) && extrap == "Linear") {
        Dictionary a = keys[time < first ? 0 : keys.size() - 2], b = keys[time < first ? 1 : keys.size() - 1];
        double duration = f32(number(b, "time") - number(a, "time"));
        if (std::abs(duration) < 1e-8)
            return number(time < first ? a : b, "value");
        return f32(number(a, "value") + f32(f32(number(b, "value") - number(a, "value")) / duration) *
                                            f32(time - number(a, "time")));
    }
    if ((time < first || time > last) && !has(extrap, {"Constant", "Linear"}) && last > first) {
        double initial = time, cycles;
        if (time > last) {
            cycles = std::floor((last - time) / (last - first));
            time += (last - first) * cycles;
        } else {
            cycles = std::floor((time - first) / (last - first));
            time -= (last - first) * cycles;
        }
        if (time == last && initial < first)
            time = first;
        if (time == first && initial > last)
            time = last;
        cycles = std::abs(cycles);
        if (extrap == "CycleWithOffset")
            offset = (number(begin, "value") - number(end, "value")) * cycles * (initial < first ? 1 : -1);
        if (extrap == "Oscillate" && std::fmod(cycles, 2) != 0)
            time = first + last - time;
    }
    if (time <= first)
        return number(begin, "value") + offset;
    for (int n = 1; n < keys.size(); ++n) {
        Dictionary right = keys[n];
        if (time > number(right, "time"))
            continue;
        Dictionary left = keys[n - 1];
        double duration = f32(number(right, "time") - number(left, "time"));
        double a = number(left, "value"), b = number(right, "value");
        if (duration <= 0)
            return a + offset;
        double alpha = f32(f32(time - number(left, "time")) / duration);
        String mode = text(left, "interp", "Linear").to_lower();
        if (mode == "constant")
            return (time < number(right, "time") ? a : b) + offset;
        if (mode == "cubic") {
            double p1 = f32(a + f32(f32(number(left, "leave") * duration) * f32(1. / 3.))),
                   p2 = f32(b - f32(f32(number(right, "arrive") * duration) * f32(1. / 3.)));
            return lerp32(lerp32(lerp32(a, p1, alpha), lerp32(p1, p2, alpha), alpha),
                          lerp32(lerp32(p1, p2, alpha), lerp32(p2, b, alpha), alpha), alpha) +
                   offset;
        }
        return lerp32(a, b, alpha) + offset;
    }
    return number(end, "value") + offset;
}
Array NBNativeCurves::sample_di(const Dictionary &curve, double time) {
    Array channels = curve.get("channels", Array()), values;
    if (channels.is_empty()) {
        values.append(0.);
        return values;
    }
    Array errors = curve.get("errors", Array());
    if (!errors.is_empty()) {
        for (int n = 0; n < channels.size(); ++n)
            values.append(0.);
        return values;
    }
    if (!bool(curve.get("uses_lut", false))) {
        for (int n = 0; n < channels.size(); ++n)
            values.append(sample_channel(channels[n], time));
        return values;
    }
    PackedFloat32Array lut = curve.get("lut", PackedFloat32Array());
    int last = int(lut.size() / channels.size()) - 1;
    if (last < 0 || !std::isfinite(time)) {
        for (int n = 0; n < channels.size(); ++n)
            values.append(0.);
        return values;
    }
    double x = std::clamp(f32(f32(f32(time - number(curve, "min_time")) * number(curve, "inv_range")) * last),
                          0., double(last));
    int a = int(x), b = std::min(a + 1, last);
    for (int n = 0; n < channels.size(); ++n)
        values.append(lerp32(lut[a * channels.size() + n], lut[b * channels.size() + n], x - a));
    return values;
}
double NBNativeCurves::sample(const Dictionary &curve, double time) {
    return text(curve, "kind") == "curve_di" ? double(sample_di(curve, time)[0])
                                             : sample_channel(curve, time);
}
Color NBNativeCurves::sample_color(const Dictionary &curve, double time) {
    Array a = sample_di(curve, time);
    return a.size() == 4 ? Color(a[0], a[1], a[2], a[3]) : Color(0, 0, 0, 0);
}
Vector2 NBNativeCurves::sample_vector2(const Dictionary &curve, double time) {
    Array a = sample_di(curve, time);
    return a.size() == 2 ? Vector2(a[0], a[1]) : Vector2();
}
Vector3 NBNativeCurves::sample_vector3(const Dictionary &curve, double time) {
    Array a = sample_di(curve, time);
    return a.size() == 3 ? Vector3(a[0], a[1], a[2]) : Vector3();
}
Dictionary NBNativeCurves::default_vector2_curve() {
    Dictionary key0, key1, channel, input;
    key0["time"] = 0.;
    key0["value"] = 1.;
    key1["time"] = 1.;
    key1["value"] = 0.;
    Array keys;
    keys.append(key0);
    keys.append(key1);
    channel["keys"] = keys;
    input["XCurve"] = channel;
    input["YCurve"] = channel;
    return prepare(input, "DI<Vector2DCurve>");
}
Dictionary NBNativeCurves::default_vector3_curve() {
    Dictionary input, channel;
    Array keys;
    Dictionary a, b;
    a["time"] = 0.;
    a["value"] = 1.;
    b["time"] = 1.;
    b["value"] = 0.;
    keys.append(a);
    keys.append(b);
    channel["keys"] = keys;
    input["XCurve"] = channel;
    input["YCurve"] = channel;
    input["ZCurve"] = channel;
    return prepare(input, "DI<VectorCurve>");
}
PackedFloat32Array NBNativeCurves::build_lut(const Dictionary &curve, int64_t count) {
    PackedFloat32Array table;
    Array channels = curve.get("channels", Array());
    if (count < 1 || count > 1048576 || channels.is_empty())
        return table;
    table.resize(count * channels.size());
    double inv = count > 1 ? f32(1. / (count - 1)) : 0, range = number(curve, "inv_range"),
           start = number(curve, "min_time");
    for (int64_t n = 0; n < count; ++n) {
        double time = range != 0 ? f32(f32(f32(n * inv) / range) + start) : start;
        for (int c = 0; c < channels.size(); ++c)
            table[n * channels.size() + c] = float(sample_channel(channels[c], time));
    }
    return table;
}
bool NBNativeCurves::passes_threshold(const PackedFloat32Array &original, const PackedFloat32Array &candidate,
                                      int64_t channels, double threshold) {
    if (channels <= 0 || original.size() % channels || candidate.size() % channels)
        return false;
    int64_t old_count = original.size() / channels, new_count = candidate.size() / channels;
    if (old_count < 2 || new_count < 1)
        return false;
    for (int64_t n = 0; n < old_count; ++n) {
        double t = f32(double(n) / (old_count - 1)), x = f32(t * old_count), y = f32(t * new_count);
        int64_t a = std::min(int64_t(std::floor(x)), old_count - 1), b = std::min(a + 1, old_count - 1),
                c = std::min(int64_t(std::floor(y)), new_count - 1), d = std::min(c + 1, new_count - 1);
        for (int64_t k = 0; k < channels; ++k) {
            double lhs = lerp32(original[a * channels + k], original[b * channels + k], x - std::floor(x));
            double rhs = lerp32(candidate[c * channels + k], candidate[d * channels + k], y - std::floor(y));
            if (std::abs(lhs - rhs) > threshold)
                return false;
        }
    }
    return true;
}
} // namespace godot
