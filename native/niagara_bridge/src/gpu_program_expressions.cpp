#include "gpu_program.h"
#include "effect_compiler.h"
#include "compiler_frontend.h"
#include "attribute_values.h"
#include "value_evaluator.h"

namespace godot::nbgpu {
using nbcompiler::named;
using nbcompiler::reference;
String Builder::expression(const Variant &value, const String &type, const String &path) {
    if (value.get_type() != Variant::DICTIONARY)
        return literal(value, type);
    Dictionary d = value;
    String kind = d.get("kind", ""), name = d.get("name", "");
    auto zero = [&]() { return cat({type, "(0.0)"}); };
    if (kind == "reference") {
        if (module_outputs.has(name))
            return typed_read(module_outputs[name], module_output_types[name], type);
        if (literal_outputs.has(name))
            return literal(literal_outputs[name], type);
        if (attribute_rows.has(name)) {
            Dictionary row = attribute_rows[name];
            String actual = NBNativeAttributeValues::shader_type(row["type"]),
                   storage = cat({"p.attributes[", row["slot"].stringify(), "]"});
            String text = actual == "int"    ? cat({"floatBitsToInt(", storage, ".x)"})
                          : actual == "bool" ? cat({"(", storage, ".x!=0.0)"})
                                             : cat({storage, actual == "float"  ? ".x"
                                                             : actual == "vec2" ? ".xy"
                                                             : actual == "vec3" ? ".xyz"
                                                                                : ""});
            return typed_read(text, actual, type);
        }
        if (builtins.has(name)) {
            Array builtin = builtins[name];
            String text = builtin[2];
            if (name == "Particles.UVScale")
                text = "vec2(p.uv_flip,p.uv_flip_y)";
            else if (name == "Particles.Alive")
                text = "(p.alive>0.0)";
            else if (name == "Particles.MeshIndex")
                text = "int(p.mesh_index)";
            else if (name == "Particles.SpriteRotation")
                text = "degrees(p.rotation)";
            if (text.is_empty())
                return unsupported("GPU attribute read: " + name);
            return typed_read(text, NBNativeAttributeValues::shader_type(builtin[0]), type);
        }
        if (references.has(name))
            return references[name];
        if (((name.begins_with("User.") || name.begins_with("Emitter.")) && user_parameters.has(name)) ||
            ((name.begins_with("Emitter.") || name.begins_with("System.")) &&
             fields.has(name.get_slice(".", 1)))) {
            String actual = NBNativeAttributeValues::shader_type(attribute_types.get(name, type));
            if (actual.is_empty())
                actual = type;
            int index = -1;
            for (int i = 0; i < parameters.size(); ++i) {
                Dictionary parameter = parameters[i];
                if (nbcompiler::equal(parameter["name"], name) &&
                    nbcompiler::equal(parameter["type"], actual))
                    index = i;
            }
            if (index < 0) {
                index = parameters.size();
                Dictionary parameter;
                parameter["name"] = name;
                parameter["type"] = actual;
                parameters.append(parameter);
            }
            String storage = cat({"params.data[", integer(8 + index), "]"});
            String text = actual == "bool"  ? cat({"(", storage, ".x!=0.0)"})
                          : actual == "int" ? cat({"floatBitsToInt(", storage, ".x)"})
                                            : cat({storage, actual == "float"  ? ".x"
                                                            : actual == "vec2" ? ".xy"
                                                            : actual == "vec3" ? ".xyz"
                                                            : actual == "vec4" ? ""
                                                                               : ".x"});
            if (name == "Emitter.Age" && stage_name == "ParticleSpawn" && interpolated_spawn) {
                int previous = -1;
                for (int i = 0; i < parameters.size(); ++i)
                    if (nbcompiler::equal(Dictionary(parameters[i])["name"],
                                          String("_NB.PreviousEmitterAge")))
                        previous = i;
                if (previous < 0) {
                    previous = parameters.size();
                    Dictionary parameter;
                    parameter["name"] = "_NB.PreviousEmitterAge";
                    parameter["type"] = "float";
                    parameters.append(parameter);
                }
                text = cat({"mix(params.data[", integer(8 + previous), "].x,", text,
                            ",params.data[0].x>0.0?(p.birth_time-params.data[0].y+params.data[0].x)/"
                            "params.data[0].x:1.0)"});
            }
            return typed_read(text, actual, type);
        }
        errors.append("Unsupported GPU reference: " + name);
        return zero();
    }
    if (kind == "curve")
        return cat({curve(value), "(nb_age(p))"});
    if (kind != "call") {
        errors.append("Unsupported GPU expression kind: " + kind);
        return zero();
    }
    Array args = d.get("arguments", Array());
    auto arg = [&](const String &key, const Variant &fallback = Variant()) {
        return NBNativeValues::input_value(args, key, fallback);
    };
    auto expr = [&](const String &key, const Variant &fallback, const String &expected,
                    const String &suffix) {
        return expression(arg(key, fallback), expected, cat({path, "/", suffix}));
    };
    if (name == "V2/RandomVector" && reader_modules.has(operation->get("stable_id"))) {
        if (type != "vec3") return unsupported("random vector input type");
        const String phi_z = source_rng.is_empty() ? String("vec2(nb_range(p,0.0,6.28319),nb_range(p,-1.0,1.0))")
            : native_range(path, {"vec2(0.0,-1.0)","vec2(6.28319,1.0)"}, "vec2");
        String helper = "nb_unit_sphere";
        String definition = "vec3 nb_unit_sphere(vec2 angles){float radial=sqrt(1.0-angles.y*angles.y); return vec3(cos(angles.x)*radial,sin(angles.x)*radial,angles.y);}";
        if (!helpers.has(definition)) helpers.append(definition);
        Variant scale = arg("VectorScale",1.0);
        String scale_type = NBNativeCompilerAttributes::value_type(scale,attribute_types,builtins,specs);
        if (scale_type != "float" && scale_type != "int" && scale_type != "vec3")
            return unsupported("random vector scale type");
        return "(" + helper + "(" + phi_z + ")*" +
               expr("VectorScale",1.0,scale_type == "vec3" ? "vec3" : "float","scale") + ")";
    }
    if (type == "int" && name == "RandomRangeFloat")
        return cat({"int(", expression(value, "float", path), ")"});
    if (named(name, {"RandomRangeFloat", "RandomRangeVector", "RandomRangeInt"})) {
        String minimum = expr("Minimum", 0.0, type, "minimum"),
               maximum = expr("Maximum", 1.0, type, "maximum");
        if (!source_rng.is_empty())
            return native_range(path, {minimum, maximum}, type, name == "RandomRangeInt");
        String fn = "nb_expression_" + integer(helpers.size()),
               body = cat({type, " lo=", minimum, "; ", type, " hi=", maximum, "; ", type, " value;\n"});
        if (type == "int")
            body += "value=min(hi,lo+int(nb_random(p)*float(hi-lo+1)));\n";
        else if (type == "float")
            body +=
                cat({"value=",
                     name == "RandomRangeInt" ? "min(hi,floor(nb_range(p,lo,hi+1.0)))" : "nb_range(p,lo,hi)",
                     ";\n"});
        else {
            int count = type.right(1).to_int();
            for (int i = 0; i < count; ++i) {
                String component = String("xyzw").substr(i, 1);
                body += cat({"value.", component, "=nb_range(p,lo.", component, ",hi.", component, ");\n"});
            }
        }
        if (NBNativeValues::enum_name(arg("EvaluationType", "")) == "SpawnOnly") {
            if (!cache_slots.has(path)) {
                Dictionary slot;
                slot["index"] = cache_slots.size();
                slot["type"] = type;
                cache_slots[path] = slot;
            }
            int slot = Dictionary(cache_slots[path])["index"];
            if (slot >= cache_limit) {
                errors.append("GPU expression cache exceeds 16 SpawnOnly inputs.");
                return zero();
            }
            String suffix = type == "float" || type == "int" ? ".x"
                            : type == "vec2"                 ? ".xy"
                            : type == "vec3"                 ? ".xyz"
                                                             : "";
            String packed = type == "float"  ? "vec4(value,0,0,0)"
                            : type == "int"  ? "vec4(intBitsToFloat(value),0,0,0)"
                            : type == "vec2" ? "vec4(value,0,0)"
                            : type == "vec3" ? "vec4(value,0)"
                                             : "value";
            String cached = cat({"p.cache[", integer(slot), "]", suffix});
            if (type == "int")
                cached = cat({"floatBitsToInt(", cached, ")"});
            body =
                cat({"if((p.cache_mask & (1u<<", integer(slot), "u))!=0u) return ", cached, ";\n", body,
                     "p.cache[", integer(slot), "]=", packed, "; p.cache_mask|=1u<<", integer(slot), "u;\n"});
        }
        helpers.append(cat({type, " ", fn, "(inout Particle p,float dt,float time,bool spawning) { ", body,
                            "return value; }"}));
        return fn + "(p,dt,time,spawning)";
    }
    if (name == "ReturnNormalizedExecIndex") {
        String inclusive = expr("NormalizedIndexIncludesOne", true, "bool", "normalizedindexincludesone"),
               scale = expr("NormalizedIndexScale", 1.0, "float", "normalizedindexscale");
        return cat({"(p.execution.x/(max(p.execution.y,1.0)-((", inclusive,
                    " && p.execution.y>1.0)?1.0:0.0))*", scale, ")"});
    }
    if (name == "FloatFromCurve")
        return cat({"(", curve(source_curve(cat({path, "/FloatCurve"}), arg("FloatCurve", Dictionary()))),
                    "(", expr("CurveIndex", reference("Particles.NormalizedAge"), "float", "index"), ")*",
                    expr("ScaleCurve", 1.0, "float", "scale"), ")"});
    if (name == "ColorFromCurve")
        return cat({curve(source_curve(cat({path, "/ColorCurve"}), arg("ColorCurve", Dictionary())), 4), "(",
                    expr("CurveIndex", reference("Particles.NormalizedAge"), "float", "index"), ")"});
    if (name == "Vector2DFromCurve" || name == "VectorFromCurve") {
        bool vector2 = name == "Vector2DFromCurve";
        String key = vector2 ? "Vector2Curve" : "VectorCurve";
        Dictionary resolved = vector2 ? NBNativeValues::resolve_vector2_curve(arg(key), user_parameters)
                                      : NBNativeValues::resolve_vector3_curve(arg(key), user_parameters);
        return cat({"(", curve(source_curve(cat({path, "/", key}), resolved), vector2 ? 2 : 3), "(",
                    expr("CurveIndex", reference("Particles.NormalizedAge"), "float", "index"), ")*",
                    expr("ScaleCurve", vector2 ? Array::make(1, 1) : Array::make(1, 1, 1),
                         vector2 ? "vec2" : "vec3", "scale"),
                    ")"});
    }
    if (name == "VectorFromFloat")
        return cat({"vec3(", expr("Value", 1.0, "float", "value"), ")"});
    if (specs.has(name)) {
        Dictionary inputs = Dictionary(specs[name])["inputs"], expressions;
        Array keys = inputs.keys();
        for (int i = 0; i < keys.size(); ++i) {
            Array spec = inputs[keys[i]];
            expressions[keys[i]] = expression(arg(keys[i], spec[1]), spec[0],
                                              cat({path, "/", NBNativeDynamicMath::argument_path(keys[i])}));
        }
        if (name == "Lerp_Float")
            return cat({"mix(", expressions["A"], ",", expressions["B"], ",", expressions["Alpha"], ")"});
        if (name == "Subtract_Float" || name == "Divide_Float")
            return cat({"(", expressions["A"], name == "Subtract_Float" ? "-" : "/", expressions["B"], ")"});
        if (name == "Multiply_Vector2D_ByFloat")
            return cat({"(", expressions["Vector2D"], "*", expressions["Float"], ")"});
        if (name == "InvertBool")
            return cat({"(!", expressions["Boolean"], ")"});
    }
    if (name == "Vector2DFromFloat")
        return cat({"vec2(", expr("Value", 1.0, "float", "value"), ")"});
    if (name == "Add_Float" || name == "Multiply_Float") {
        double fallback = name == "Multiply_Float" ? 1.0 : 0.0;
        return cat({"(", expr("A", fallback, "float", "a"), name == "Multiply_Float" ? "*" : "+",
                    expr("B", fallback, "float", "b"), ")"});
    }
    if (name == "Multiply_Vector" || name == "Add_Vector") {
        Array fallback = name == "Multiply_Vector" ? Array::make(1, 1, 1) : Array::make(0, 0, 0);
        return cat({"(", expr("A", fallback, "vec3", "a"), name == "Multiply_Vector" ? "*" : "+",
                    expr("B", fallback, "vec3", "b"), ")"});
    }
    if (name == "Multiply_VectorByFloat")
        return cat({"(", expr("Vector", Array::make(1, 1, 1), "vec3", "vector"), "*",
                    expr("Float", 1.0, "float", "float"), ")"});
    if (name == "AddVectorToPosition")
        return cat({"(", expr("Vector", Array::make(0, 0, 0), "vec3", "vector"), "+",
                    expr("Position", Array::make(0, 0, 0), "vec3", "position"), ")"});
    if (name == "TransformVector") {
        String from = NBNativeValues::enum_name(arg("SourceSpace", "Local")),
               to = NBNativeValues::enum_name(arg("DestinationSpace", "Simulation"));
        return cat({"nb_transform_between(", expr("Vector", Array::make(0, 0, 0), "vec3", "vector"), ",",
                    from == "Local"   ? "1"
                    : from == "World" ? "2"
                                      : "0",
                    ",",
                    to == "Local"   ? "1"
                    : to == "World" ? "2"
                                    : "0",
                    ",", bool(arg("ApplyScale", false)) ? "true" : "false", ",",
                    bool(arg("TransformVectorasPosition", false)) ? "true" : "false", ",p,spawning)"});
    }
    if (name == "MakeVector" || name == "MakeVector2D") {
        Array parts;
        for (const char *key : {"X", "Y", "Z"}) {
            if (String(key) == "Z" && name == "MakeVector2D")
                break;
            parts.append(expr(key, 0.0, "float", String(key).to_lower()));
        }
        return cat({name == "MakeVector2D" ? "vec2" : "vec3", "(", joined(parts, ","), ")"});
    }
    if (name == "MakeFloatFromVector2d")
        return cat({"(", expr("Vector2D", Array::make(0, 0), "vec2", "vector"), ").",
                    NBNativeValues::enum_name(arg("Channel", "X")) == "Y" ? "y" : "x"});
    if (name == "MakeVectorFromLinearColorRGB")
        return cat({"(", expr("Color", Array::make(0, 0, 0, 1), "vec4", "color"), ").rgb"});
    errors.append("Unsupported GPU dynamic call: " + name);
    return zero();
}
} // namespace godot::nbgpu
