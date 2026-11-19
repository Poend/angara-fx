#include "value_evaluator.h"
#include "curve_evaluator.h"
#include <godot_cpp/classes/random_number_generator.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <algorithm>
#include <cmath>
namespace godot {
namespace {
uint32_t lcg32(uint32_t v) {
    return v * 1664525u + 1013904223u;
}
double component(const Variant &v, int n) {
    bool valid = false, out = false;
    Variant r = v.get_indexed(n, valid, out);
    return valid && !out ? double(r) : 0.;
}
bool numeric(const Variant &v) {
    return v.get_type() == Variant::FLOAT || v.get_type() == Variant::INT;
}
String name_of(const Dictionary &v) {
    return v.get("name", "");
}
double f(double v) {
    return double(float(v));
}
} // namespace
void NBNativeNiagaraRNG::_bind_methods() {
    ClassDB::bind_static_method("NBNativeNiagaraRNG", D_METHOD("_u32", "value"), &NBNativeNiagaraRNG::u32);
    ClassDB::bind_static_method("NBNativeNiagaraRNG", D_METHOD("_mul32", "a", "b"),
                                &NBNativeNiagaraRNG::mul32);
    ClassDB::bind_static_method("NBNativeNiagaraRNG", D_METHOD("_lcg", "value"), &NBNativeNiagaraRNG::lcg);
    ClassDB::bind_static_method("NBNativeNiagaraRNG", D_METHOD("rand3", "seed1", "seed2", "seed3", "counter"),
                                &NBNativeNiagaraRNG::rand3);
    ClassDB::bind_static_method("NBNativeNiagaraRNG", D_METHOD("rand4", "seed1", "seed2", "seed3", "counter"),
                                &NBNativeNiagaraRNG::rand4);
}
int64_t NBNativeNiagaraRNG::u32(int64_t v) {
    return uint32_t(v);
}
int64_t NBNativeNiagaraRNG::mul32(int64_t a, int64_t b) {
    return uint32_t(a) * uint32_t(b);
}
int64_t NBNativeNiagaraRNG::lcg(int64_t v) {
    return lcg32(uint32_t(v));
}
Vector3 NBNativeNiagaraRNG::rand3(int64_t a, int64_t b, int64_t c, int64_t counter) {
    uint32_t x = lcg32(uint32_t(a)), y = lcg32(uint32_t(b)),
             z = lcg32(uint32_t(counter) | (uint32_t(c) << 16));
    x += y * z;
    y += z * x;
    z += x * y;
    x += y * z;
    y += z * x;
    z += x * y;
    return Vector3(float((x >> 8) & 0xffffff), float((y >> 8) & 0xffffff), float((z >> 8) & 0xffffff)) /
           16777216.f;
}
Vector4 NBNativeNiagaraRNG::rand4(int64_t a, int64_t b, int64_t c, int64_t counter) {
    uint32_t x = lcg32(uint32_t(counter)), y = lcg32(uint32_t(a)), z = lcg32(uint32_t(b)),
             w = lcg32(uint32_t(c));
    x += y * w;
    y += z * x;
    z += x * y;
    w += y * z;
    x += y * w;
    y += z * x;
    z += x * y;
    w += y * z;
    return Vector4(float((x >> 8) & 0xffffff), float((y >> 8) & 0xffffff), float((z >> 8) & 0xffffff),
                   float((w >> 8) & 0xffffff)) /
           16777216.f;
}
void NBNativeSourceRandom::_bind_methods() {
    ClassDB::bind_static_method("NBNativeSourceRandom",
                                D_METHOD("sample", "call", "minimum", "maximum", "unique_id", "emitter_seed",
                                         "integer", "parameters"),
                                &NBNativeSourceRandom::sample, DEFVAL(false), DEFVAL(Dictionary()));
    ClassDB::bind_static_method("NBNativeSourceRandom", D_METHOD("_range", "minimum", "maximum", "r"),
                                &NBNativeSourceRandom::range);
    ClassDB::bind_static_method("NBNativeSourceRandom", D_METHOD("_f32", "value"),
                                &NBNativeSourceRandom::f32);
}
double NBNativeSourceRandom::f32(double v) {
    return f(v);
}
double NBNativeSourceRandom::range(double lo, double hi, double r) {
    return f(f(lo) + f(f(f(hi) - f(lo)) * r));
}
Variant NBNativeSourceRandom::sample(const Dictionary &call, const Variant &minimum, const Variant &maximum,
                                     int64_t unique, int64_t seed, bool integer,
                                     const Dictionary &parameters) {
    if (bool(call.get("verified_seed1_emitter", false))) unique = seed;
    seed = call.get("verified_seed3_constant", seed);
    if (bool(call.get("verified_instance_seed", false)))
        seed = parameters.get("Engine.Emitter.InstanceSeed", 0);
    if (call.has("verified_seed3_parameter"))
        seed =
            parameters.get(String(call["verified_seed3_parameter"]), call.get("verified_seed3_default", 0));
    int components = call.get("components", 1);
    if (components < 1 || components > 4)
        return Variant();
    int64_t s1 = call.get("verified_seed1_constant", unique),
            s2 = call.get("verified_seed2_constant", call.get("call_id", Variant()).get_type() != Variant::NIL
                                                         ? call["call_id"]
                                                         : Variant(0));
    if (bool(call.get("verified_tick_seed", false)))
        s2 = parameters.get("Engine.System.TickCount", 0);
    return sample_seeded(minimum, maximum, Vector3i(int32_t(s1), int32_t(s2), int32_t(seed)),
                         call.get("counter", 0), components, integer);
}
Variant NBNativeSourceRandom::sample_seeded(const Variant &minimum, const Variant &maximum, const Vector3i &seeds,
                                            int64_t counter, int components, bool integer) {
    Vector4 r;
    if (components == 4)
        r = NBNativeNiagaraRNG::rand4(seeds.x, seeds.y, seeds.z, counter);
    else {
        Vector3 raw = NBNativeNiagaraRNG::rand3(seeds.x, seeds.y, seeds.z, counter);
        r = Vector4(raw.x, raw.y, raw.z, 0);
    }
    if (integer)
        return int64_t(minimum) + int64_t(f(r.x * double(int64_t(maximum) - int64_t(minimum) + 1)));
    if (components == 1)
        return range(minimum, maximum, r.x);
    Array values;
    for (int n = 0; n < components; ++n)
        values.append(range(component(minimum, n), component(maximum, n), r[n]));
    if (minimum.get_type() == Variant::VECTOR2)
        return Vector2(values[0], values[1]);
    if (minimum.get_type() == Variant::VECTOR3)
        return Vector3(values[0], values[1], values[2]);
    if (minimum.get_type() == Variant::VECTOR4)
        return Vector4(values[0], values[1], values[2], values[3]);
    return values;
}
void NBNativeDynamicMath::_bind_methods() {
    ClassDB::bind_static_method("NBNativeDynamicMath", D_METHOD("evaluate_cpu", "name", "values"),
                                &NBNativeDynamicMath::evaluate_cpu);
    ClassDB::bind_static_method("NBNativeDynamicMath", D_METHOD("argument_path", "name"),
                                &NBNativeDynamicMath::argument_path);
    ClassDB::bind_static_method("NBNativeDynamicMath", D_METHOD("f32", "value"), &NBNativeDynamicMath::f32);
}
double NBNativeDynamicMath::f32(double v) {
    return f(v);
}
String NBNativeDynamicMath::argument_path(const String &n) {
    return n == "Vector2D" ? String("vector") : n.to_lower();
}
Variant NBNativeDynamicMath::evaluate_cpu(const String &name, const Dictionary &v) {
    if (name == "Lerp_Float") {
        double alpha = f(v.get("Alpha", .5));
        return f(f(f(v.get("A", 0.)) * f(1 - alpha)) + f(f(v.get("B", 1.)) * alpha));
    }
    if (name == "Subtract_Float")
        return f(f(v.get("A", 0.)) - f(v.get("B", 0.)));
    if (name == "Divide_Float") {
        double b = f(v.get("B", 1.));
        return std::abs(b) > f(1e-8) ? f(f(v.get("A", 1.)) / b) : 0.;
    }
    if (name == "Multiply_Vector2D_ByFloat")
        return Vector2(v.get("Vector2D", Vector2(1, 1))) * f(v.get("Float", 1.));
    if (name == "InvertBool")
        return !bool(v.get("Boolean", false));
    return Variant();
}
void NBNativeValues::_bind_methods() {
    ClassDB::bind_static_method("NBNativeValues",
                                D_METHOD("input_value", "inputs", "input_name", "fallback", "occurrence"),
                                &NBNativeValues::input_value, DEFVAL(Variant()), DEFVAL(0));
    ClassDB::bind_static_method("NBNativeValues", D_METHOD("enum_name", "value"), &NBNativeValues::enum_name);
    ClassDB::bind_static_method("NBNativeValues",
                                D_METHOD("indexed_argument", "expression", "arguments", "name", "fallback"),
                                &NBNativeValues::indexed_argument, DEFVAL(Variant()));
    ClassDB::bind_static_method("NBNativeValues", D_METHOD("evaluate", "value", "context", "path"),
                                &NBNativeValues::evaluate, DEFVAL(String()));
    ClassDB::bind_static_method("NBNativeValues", D_METHOD("_default_curve_index", "context"),
                                &NBNativeValues::default_curve_index);
    ClassDB::bind_static_method("NBNativeValues", D_METHOD("resolve_curve", "value", "context"),
                                &NBNativeValues::resolve_curve);
    ClassDB::bind_static_method("NBNativeValues", D_METHOD("resolve_vector2_curve", "value", "context"),
                                &NBNativeValues::resolve_vector2_curve);
    ClassDB::bind_static_method("NBNativeValues", D_METHOD("resolve_vector3_curve", "value", "context"),
                                &NBNativeValues::resolve_vector3_curve);
    ClassDB::bind_static_method("NBNativeValues", D_METHOD("curve_sample", "curve", "time"),
                                &NBNativeValues::curve_sample);
    ClassDB::bind_static_method("NBNativeValues", D_METHOD("vector3", "value", "fallback"),
                                &NBNativeValues::vector3, DEFVAL(Vector3()));
    ClassDB::bind_static_method("NBNativeValues", D_METHOD("vector2", "value", "fallback"),
                                &NBNativeValues::vector2, DEFVAL(Vector2(1, 1)));
    ClassDB::bind_static_method("NBNativeValues", D_METHOD("color", "value", "fallback"),
                                &NBNativeValues::color, DEFVAL(Color(1, 1, 1, 1)));
    ClassDB::bind_static_method("NBNativeValues", D_METHOD("quaternion", "value"),
                                &NBNativeValues::quaternion);
    ClassDB::bind_static_method("NBNativeValues", D_METHOD("unreal_rotation_to_godot", "rotation"),
                                &NBNativeValues::unreal_rotation_to_godot);
    ClassDB::bind_static_method("NBNativeValues", D_METHOD("unreal_to_godot", "value"),
                                &NBNativeValues::unreal_to_godot);
    ClassDB::bind_static_method("NBNativeValues", D_METHOD("godot_to_unreal", "value"),
                                &NBNativeValues::godot_to_unreal);
    ClassDB::bind_static_method("NBNativeValues",
                                D_METHOD("transform_between", "value", "source_space", "destination_space",
                                         "context", "apply_scale", "as_position"),
                                &NBNativeValues::transform_between, DEFVAL(false), DEFVAL(false));
}
Variant NBNativeValues::input_value(const Array &inputs, const String &name, const Variant &fallback,
                                    int64_t occurrence) {
    if (occurrence < 0)
        return fallback;
    for (int n = 0; n < inputs.size(); ++n) {
        const Variant item = inputs[n];
        String entry;
        Variant value;
        if (item.get_type() == Variant::DICTIONARY) {
            Dictionary d = item;
            entry = d.get("name", "");
            value = d.get("value", Variant());
        } else if (item.get_type() == Variant::OBJECT) {
            Object *o = item;
            if (!o)
                continue;
            entry = o->get("input_name");
            value = o->get("value");
        }
        if (entry == name && occurrence-- == 0)
            return value;
    }
    return fallback;
}
String NBNativeValues::enum_name(const Variant &value) {
    if (value.get_type() == Variant::DICTIONARY) {
        Dictionary d = value;
        if (d.get("kind", "") == Variant("reference"))
            return d.get("name", "");
    }
    return UtilityFunctions::str(value);
}
Variant NBNativeValues::indexed_argument(const Dictionary &expression, const Array &arguments,
                                         const String &name, const Variant &fallback) {
    if (expression.has("_indexed_arguments"))
        return Dictionary(expression["_indexed_arguments"]).get(name, fallback);
    return input_value(arguments, name, fallback);
}
double NBNativeValues::default_curve_index(const Dictionary &context) {
    return context.get("Particles.NormalizedAge",
                       context.get("Emitter.NormalizedLoopAge", context.get("System.Age", 0.)));
}
Dictionary NBNativeValues::resolve_curve(Variant value, const Dictionary &context) {
    if (value.get_type() == Variant::DICTIONARY) {
        Dictionary d = value;
        if (d.get("kind", "") == Variant("reference"))
            value = context.get(name_of(d), Variant());
    }
    if (value.get_type() != Variant::DICTIONARY)
        return Dictionary();
    Dictionary d = value;
    if (d.get("kind", "") == Variant("curve"))
        return d.has("_prepared_curve") ? d["_prepared_curve"] : NBNativeCurves::prepare(d, "DI<Curve>");
    return d;
}
Dictionary NBNativeValues::resolve_vector2_curve(Variant value, const Dictionary &context) {
    if (value.get_type() == Variant::NIL)
        return NBNativeCurves::default_vector2_curve();
    if (value.get_type() == Variant::DICTIONARY) {
        Dictionary d = value;
        if (d.get("kind", "") == Variant("reference"))
            value = context.get(name_of(d), Variant());
    }
    return NBNativeCurves::prepare(value, "DI<Vector2DCurve>");
}
Dictionary NBNativeValues::resolve_vector3_curve(Variant value, const Dictionary &context) {
    if (value.get_type() == Variant::NIL)
        return NBNativeCurves::default_vector3_curve();
    if (value.get_type() == Variant::DICTIONARY) {
        Dictionary d = value;
        if (d.get("kind", "") == Variant("reference"))
            value = context.get(name_of(d), Variant());
    }
    return NBNativeCurves::prepare(value, "DI<VectorCurve>");
}
double NBNativeValues::curve_sample(const Dictionary &curve, double time) {
    return NBNativeCurves::sample(curve.get("_prepared_curve", curve), time);
}
Vector3 NBNativeValues::vector3(const Variant &v, const Vector3 &fallback) {
    if (v.get_type() == Variant::VECTOR3)
        return v;
    if (v.get_type() == Variant::ARRAY && Array(v).size() >= 3)
        return Vector3(component(v, 0), component(v, 1), component(v, 2));
    return fallback;
}
Vector2 NBNativeValues::vector2(const Variant &v, const Vector2 &fallback) {
    if (v.get_type() == Variant::VECTOR2)
        return v;
    if (v.get_type() == Variant::ARRAY && Array(v).size() >= 2)
        return Vector2(component(v, 0), component(v, 1));
    if (numeric(v))
        return Vector2(1, 1) * double(v);
    return fallback;
}
Color NBNativeValues::color(const Variant &v, const Color &fallback) {
    if (v.get_type() == Variant::COLOR)
        return v;
    if (v.get_type() == Variant::VECTOR4) {
        Vector4 channels = v;
        return Color(channels.x, channels.y, channels.z, channels.w);
    }
    if (v.get_type() == Variant::ARRAY && Array(v).size() >= 3)
        return Color(component(v, 0), component(v, 1), component(v, 2),
                     Array(v).size() >= 4 ? component(v, 3) : 1.);
    return fallback;
}
Quaternion NBNativeValues::quaternion(const Variant &v) {
    Quaternion q;
    if (v.get_type() == Variant::QUATERNION)
        q = v;
    else if (v.get_type() == Variant::ARRAY && Array(v).size() >= 4)
        q = Quaternion(component(v, 0), component(v, 1), component(v, 2), component(v, 3));
    return q.length_squared() > 1e-10 ? q.normalized() : Quaternion();
}
Basis NBNativeValues::unreal_rotation_to_godot(const Quaternion &q) {
    Basis c(Vector3(0, 0, -1), Vector3(1, 0, 0), Vector3(0, 1, 0));
    return c * Basis(q.normalized()) * c.inverse();
}
Vector3 NBNativeValues::unreal_to_godot(const Vector3 &v) {
    return Vector3(v.y, v.z, -v.x) * .01;
}
Vector3 NBNativeValues::godot_to_unreal(const Vector3 &v) {
    return Vector3(-v.z, v.x, v.y) * 100.;
}
Vector3 NBNativeValues::transform_between(const Vector3 &value, String source, String destination,
                                          const Dictionary &context, bool scale, bool position) {
    String simulation = bool(context.get("local_space", false)) ? "Local" : "World";
    if (source == "Simulation" || source == "SimulationSpace" || source == "Mesh")
        source = simulation;
    if (destination == "Simulation" || destination == "SimulationSpace" || destination == "Mesh")
        destination = simulation;
    if (source == destination)
        return value;
    Transform3D transform = context.get("source_transform", Transform3D());
    Basis basis = scale ? transform.basis : transform.basis.orthonormalized();
    Vector3 v = unreal_to_godot(value);
    if (source == "Local")
        v = basis.xform(v) + (position ? transform.origin : Vector3());
    else
        v = basis.inverse().xform(v - (position ? transform.origin : Vector3()));
    return godot_to_unreal(v);
}
Variant NBNativeValues::evaluate(const Variant &value, const Dictionary &context, const String &path) {
    return evaluate_depth(value, context, path, 0);
}
Variant NBNativeValues::evaluate_depth(const Variant &value, const Dictionary &context, const String &path,
                                       int depth) {
    if (value.get_type() != Variant::DICTIONARY)
        return value;
    if (depth > 256)
        return Variant();
    Dictionary expression = value;
    String kind = expression.get("kind", "");
    if (kind == "reference") {
        String name = name_of(expression);
        return context.get(name, name);
    }
    if (kind == "curve")
        return curve_sample(expression, default_curve_index(context));
    if (kind != "call")
        return Variant();
    Array arguments = expression.get("arguments", Array());
    String name = name_of(expression);
    auto argument = [&](const String &key, const Variant &fallback = Variant()) {
        return indexed_argument(expression, arguments, key, fallback);
    };
    auto eval = [&](const String &key, const Variant &fallback, const String &suffix) {
        return evaluate_depth(argument(key, fallback), context, path + suffix, depth + 1);
    };
    if (name == "ReturnNormalizedExecIndex") {
        int64_t count = std::max(int64_t(1), int64_t(context.get("_execution_count", 1)));
        bool inclusive = eval("NormalizedIndexIncludesOne", true, "/normalizedindexincludesone");
        int64_t denominator = inclusive && count > 1 ? count - 1 : count;
        double scale = eval("NormalizedIndexScale", 1., "/normalizedindexscale"),
               index = f(context.get("_execution_index", 0));
        return f(f(index / f(double(denominator))) * f(scale));
    }
    if (name == "Lerp_Float" || name == "Subtract_Float" || name == "Divide_Float" ||
        name == "Multiply_Vector2D_ByFloat" || name == "InvertBool") {
        Dictionary v;
        if (name == "Multiply_Vector2D_ByFloat") {
            v["Vector2D"] = vector2(eval("Vector2D", Vector2(1, 1), "/vector"));
            v["Float"] = eval("Float", 1., "/float");
        } else if (name == "InvertBool")
            v["Boolean"] = eval("Boolean", false, "/boolean");
        else {
            double a = name == "Divide_Float" ? 1. : 0., b = name == "Subtract_Float" ? 0. : 1.;
            v["A"] = eval("A", a, "/a");
            v["B"] = eval("B", b, "/b");
            if (name == "Lerp_Float")
                v["Alpha"] = eval("Alpha", .5, "/alpha");
        }
        return NBNativeDynamicMath::evaluate_cpu(name, v);
    }
    if (name == "RandomRangeFloat" || name == "RandomRangeVector" || name == "RandomRangeInt") {
        Dictionary cache = context.get("cache", Dictionary());
        bool cached = enum_name(argument("EvaluationType", "")) == "SpawnOnly";
        if (cached && cache.has(path) && !context.has("source_rng"))
            return cache[path];
        Variant lo = eval("Minimum", 0., "/minimum"), hi = eval("Maximum", 1., "/maximum");
        if (context.has("source_rng")) {
            Dictionary calls = context["source_rng"];
            return NBNativeSourceRandom::sample(
                calls.get(path, Dictionary()), lo, hi, context.get("source_unique_id", 0),
                context.get("source_seed", 0), name == "RandomRangeInt", context);
        }
        Object *object = context.get("rng", Variant());
        RandomNumberGenerator *rng = Object::cast_to<RandomNumberGenerator>(object);
        if (!rng)
            return Variant();
        Variant result;
        if (lo.get_type() == Variant::ARRAY && hi.get_type() == Variant::ARRAY) {
            Array components;
            for (int n = 0; n < std::min(Array(lo).size(), Array(hi).size()); ++n)
                components.append(rng->randf_range(component(lo, n), component(hi, n)));
            result = components;
        } else if (lo.get_type() == Variant::VECTOR3 || hi.get_type() == Variant::VECTOR3) {
            Vector3 a = vector3(lo), b = vector3(hi, Vector3(1, 1, 1));
            double x = rng->randf_range(a.x, b.x), y = rng->randf_range(a.y, b.y),
                   z = rng->randf_range(a.z, b.z);
            result = Vector3(x, y, z);
        } else if (lo.get_type() == Variant::VECTOR2 || hi.get_type() == Variant::VECTOR2) {
            Vector2 a = vector2(lo, Vector2()), b = vector2(hi);
            double x = rng->randf_range(a.x, b.x), y = rng->randf_range(a.y, b.y);
            result = Vector2(x, y);
        } else if (name == "RandomRangeInt")
            result = rng->randi_range(lo, hi);
        else
            result = rng->randf_range(lo, hi);
        if (cached)
            cache[path] = result;
        return result;
    }
    if (name == "FloatFromCurve" || name == "ColorFromCurve" || name == "Vector2DFromCurve" ||
        name == "VectorFromCurve") {
        String key = name == "FloatFromCurve"      ? "FloatCurve"
                     : name == "ColorFromCurve"    ? "ColorCurve"
                     : name == "Vector2DFromCurve" ? "Vector2Curve"
                                                   : "VectorCurve";
        Dictionary curve;
        if (name == "Vector2DFromCurve")
            curve = resolve_vector2_curve(argument(key), context);
        else if (name == "VectorFromCurve")
            curve = resolve_vector3_curve(argument(key), context);
        else
            curve = resolve_curve(argument(key, Dictionary()), context);
        Dictionary sources = context.get("source_curves", Dictionary());
        String capture = path + String("/") + key;
        if (sources.has(capture))
            curve = sources[capture];
        double time = eval("CurveIndex", default_curve_index(context), "/index");
        if (name == "ColorFromCurve")
            return NBNativeCurves::sample_color(curve, time);
        if (name == "FloatFromCurve")
            return curve_sample(curve, time) * double(eval("ScaleCurve", 1., "/scale"));
        if (name == "Vector2DFromCurve")
            return NBNativeCurves::sample_vector2(curve, time) *
                   vector2(eval("ScaleCurve", Vector2(1, 1), "/scale"));
        return NBNativeCurves::sample_vector3(curve, time) *
               vector3(eval("ScaleCurve", Vector3(1, 1, 1), "/scale"));
    }
    if (name == "VectorFromFloat")
        return Vector3(1, 1, 1) * double(eval("Value", 1., "/value"));
    if (name == "Vector2DFromFloat")
        return Vector2(1, 1) * double(eval("Value", 1., "/value"));
    if (name == "Add_Float" || name == "Multiply_Float") {
        double fallback = name == "Add_Float" ? 0. : 1., a = eval("A", fallback, "/a"),
               b = eval("B", fallback, "/b");
        return name == "Add_Float" ? a + b : a * b;
    }
    if (name == "Multiply_Vector" || name == "Add_Vector") {
        Vector3 fallback = name == "Multiply_Vector" ? Vector3(1, 1, 1) : Vector3(),
                a = vector3(eval("A", fallback, "/a"), fallback),
                b = vector3(eval("B", fallback, "/b"), fallback);
        return name == "Multiply_Vector" ? a * b : a + b;
    }
    if (name == "Multiply_VectorByFloat") {
        Vector3 a = vector3(eval("Vector", Vector3(1, 1, 1), "/vector"), Vector3(1, 1, 1));
        double b = eval("Float", 1., "/float");
        return a * b;
    }
    if (name == "AddVectorToPosition") {
        Vector3 a = vector3(eval("Vector", Vector3(), "/vector")),
                b = vector3(eval("Position", Vector3(), "/position"));
        return a + b;
    }
    if (name == "TransformVector") {
        Vector3 v = vector3(eval("Vector", Vector3(), "/vector"));
        return transform_between(v, enum_name(argument("SourceSpace", "Local")),
                                 enum_name(argument("DestinationSpace", "Simulation")), context,
                                 bool(argument("ApplyScale", false)),
                                 bool(argument("TransformVectorasPosition", false)));
    }
    if (name == "MakeVector" || name == "MakeVector2D") {
        double x = eval("X", 0., "/x"), y = eval("Y", 0., "/y");
        if (name == "MakeVector2D")
            return Vector2(x, y);
        return Vector3(x, y, eval("Z", 0., "/z"));
    }
    if (name == "MakeFloatFromVector2d") {
        Vector2 v = vector2(eval("Vector2D", Vector2(), "/vector"), Vector2());
        return enum_name(argument("Channel", "X")) == "Y" ? v.y : v.x;
    }
    if (name == "MakeVectorFromLinearColorRGB") {
        Color c = color(eval("Color", Color(0, 0, 0, 1), "/color"));
        return Vector3(c.r, c.g, c.b);
    }
    return Variant();
}
} // namespace godot
