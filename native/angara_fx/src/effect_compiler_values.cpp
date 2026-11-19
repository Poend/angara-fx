#include "effect_compiler.h"
#include "compiler_frontend.h"
#include "attribute_values.h"
#include "curve_evaluator.h"
#include "runtime_control.h"
#include "value_evaluator.h"
#include "source_program.h"
#include "gpu_program.h"
#include "ribbon_geometry.h"
#include "vm_events.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <cmath>

namespace godot::nbcompiler {
bool number(const Variant &v) {
    return v.get_type() == Variant::INT || v.get_type() == Variant::FLOAT;
}
bool equal(const Variant &a, const Variant &b) {
    Variant result;
    bool valid = false;
    Variant::evaluate(Variant::OP_EQUAL, a, b, result, valid);
    return valid && bool(result);
}
bool named(const Variant &v, std::initializer_list<const char *> values) {
    for (const char *value : values)
        if (equal(v, String(value)))
            return true;
    return false;
}
Dictionary reference(const String &name) {
    Dictionary d;
    d["kind"] = "reference";
    d["name"] = name;
    return d;
}
void append(Array target, const Array &values) {
    target.append_array(values);
}
Compiler::Compiler(const Dictionary &d, const Dictionary &c)
    : dependencies(d), contracts(c), builtins(c.get("builtins", Dictionary())),
      specs(c.get("dynamic_specs", Dictionary())), fields(c.get("lifecycle_fields", Dictionary())) {}
Variant Compiler::external(const String &dependency, const StringName &method, const Array &args) const {
    if (dependency == "source_rng" && method == StringName("build"))
        return NBNativeSourceProgram::build(args[0], args[1], args[2]);
    if (dependency == "gpu" && method == StringName("build") && contracts.has("gpu_contracts"))
        return NBNativeGPUProgram::build(args[0], args[1], contracts["gpu_contracts"]);
    if (dependency == "events" && method == StringName("graph"))
        return NBNativeVMEvents::graph(args[0]);
    if (dependency == "ribbons" && method == StringName("settings"))
        return NBNativeRibbonGeometry::settings(args[0], args[1]);
    if (dependency == "ribbons" && method == StringName("errors"))
        return NBNativeRibbonGeometry::errors(args[0]);
    Ref<Script> script = dependencies.get(dependency, Variant());
    ERR_FAIL_COND_V_MSG(script.is_null(), Variant(), "Missing Niagara compiler dependency: " + dependency);
    return script->callv(method, args);
}
String Compiler::type(const Variant &v, const Dictionary &known) const {
    return NBNativeCompilerAttributes::value_type(v, known, builtins, specs);
}
Variant Compiler::input(const Ref<Resource> &r, const String &key, const Variant &fallback) {
    return r->call("get_input", key, fallback);
}
String Compiler::mode(const Ref<Resource> &r, const String &key, const Variant &fallback) {
    return NBNativeValues::enum_name(input(r, key, fallback));
}
void Compiler::issue(Dictionary result, const Ref<Resource> &emitter, const Ref<Resource> &op,
                     const String &severity, const String &message) {
    Dictionary row;
    row["severity"] = severity;
    row["emitter"] = emitter.is_valid() ? emitter->get("display_name") : Variant("");
    row["module"] = op.is_valid() ? op->get("module_name") : Variant("");
    row["line"] = op.is_valid() ? op->get("line") : Variant(0);
    row["message"] = message;
    Array diagnostics = result["diagnostics"];
    diagnostics.append(row);
    if (severity == "error" || severity == "warning")
        result["fully_supported"] = false;
}
Dictionary Compiler::parameters(const Ref<Resource> &effect, const Dictionary &overrides) const {
    Dictionary p;
    if (effect.is_valid()) {
        Variant raw = effect->get("user_parameters");
        if (raw.get_type() == Variant::ARRAY) {
            Array declarations = raw;
            for (int i = 0; i < declarations.size(); ++i) {
                Ref<Resource> d = declarations[i];
                String name = d->get("input_name");
                if (bool(d->get("has_default")))
                    p[name.begins_with("User.") ? name : "User." + name] = d->get("value");
            }
        }
    }
    p.merge(overrides, true);
    if (effect.is_valid()) {
        Array declarations = effect->get("user_parameters");
        for (int i = 0; i < declarations.size(); ++i) {
            Ref<Resource> d = declarations[i];
            String name = d->get("input_name");
            if (!name.begins_with("User."))
                name = "User." + name;
            if (p.has(name))
                p[name] = NBNativeCurves::prepare(p[name], d->get("target_type"));
        }
    }
    return p;
}
bool Compiler::check_value(const Variant &v, const Dictionary &p) const {
    if (v.get_type() != Variant::DICTIONARY)
        return true;
    Dictionary d = v;
    String kind = d.get("kind", ""), name = d.get("name", "");
    if (kind == "call") {
        Array dynamic = contracts["dynamic_calls"];
        if (!dynamic.has(name) && !specs.has(name))
            return false;
        if (specs.has(name) && !math_arguments(d, p))
            return false;
        if (name == "ReturnNormalizedExecIndex" && !index_arguments(d, p))
            return false;
        Array args = d.get("arguments", Array());
        if (name.begins_with("RandomRange")) {
            if (NBNativeValues::enum_name(NBNativeValues::input_value(
                    args, "RandomnessMode", "SimulationDefaults")) != "SimulationDefaults")
                return false;
            if (!named(NBNativeValues::enum_name(NBNativeValues::input_value(args, "EvaluationType", "")),
                       {"", "SpawnOnly", "EveryFrame"}))
                return false;
            for (int i = 0; i < args.size(); ++i)
                if (!named(Dictionary(args[i]).get("name", ""),
                           {"Minimum", "Maximum", "RandomnessMode", "EvaluationType"}))
                    return false;
        }
        if (name == "MakeFloatFromVector2d" &&
            !named(NBNativeValues::enum_name(NBNativeValues::input_value(args, "Channel", "X")), {"X", "Y"}))
            return false;
        if (name == "TransformVector")
            for (const char *key : {"SourceSpace", "DestinationSpace"})
                if (!named(NBNativeValues::enum_name(NBNativeValues::input_value(args, key, "Simulation")),
                           {"Local", "World", "Simulation", "SimulationSpace"}))
                    return false;
        for (int i = 0; i < args.size(); ++i)
            if (!check_value(Dictionary(args[i]).get("value", Variant()), p))
                return false;
        if (name == "FloatFromCurve" || name == "ColorFromCurve") {
            bool color = name == "ColorFromCurve";
            Dictionary curve = NBNativeValues::resolve_curve(
                NBNativeValues::input_value(args, color ? "ColorCurve" : "FloatCurve", Dictionary()), p);
            if (color ? !equal(curve.get("kind", ""), String("color_curve_di"))
                      : !named(curve.get("kind", ""), {"curve", "curve_di"}))
                return false;
        }
        if (name == "Vector2DFromCurve") {
            Dictionary curve =
                NBNativeValues::resolve_vector2_curve(NBNativeValues::input_value(args, "Vector2Curve"), p);
            if (!equal(curve.get("kind", ""), String("vector2_curve_di")) ||
                !Array(curve.get("errors", Array())).is_empty())
                return false;
        }
        if (name == "VectorFromCurve") {
            Dictionary known, seen;
            known["Emitter.NormalizedLoopAge"] = "float";
            known["System.Age"] = "float";
            Array keys = p.keys();
            for (int i = 0; i < keys.size(); ++i)
                known[keys[i]] = type(p[keys[i]], Dictionary());
            for (int i = 0; i < args.size(); ++i) {
                Dictionary arg = args[i];
                String key = arg.get("name", "");
                if (!named(key, {"VectorCurve", "CurveIndex", "ScaleCurve"}) || seen.has(key))
                    return false;
                seen[key] = true;
                if (key == "VectorCurve")
                    continue;
                String actual = type(arg.get("value", Variant()), known);
                if ((key == "CurveIndex" && !named(actual, {"float", "int"})) ||
                    (key == "ScaleCurve" && actual != "vec3"))
                    return false;
            }
            Dictionary curve =
                NBNativeValues::resolve_vector3_curve(NBNativeValues::input_value(args, "VectorCurve"), p);
            if (!equal(curve.get("kind", ""), String("vector3_curve_di")) ||
                !Array(curve.get("errors", Array())).is_empty())
                return false;
        }
    } else if (named(kind, {"curve_di", "color_curve_di", "vector2_curve_di", "vector3_curve_di"}))
        return Array(d.get("errors", Array())).is_empty();
    else if (kind != "reference" && kind != "curve")
        return false;
    else if (kind == "reference") {
        if ((name.begins_with("Emitter.") || name.begins_with("System.")) &&
            fields.has(name.get_slice(".", 1)))
            return true;
        if ((name.begins_with("Emitter.") || name.begins_with("Particles.") || name.begins_with("Output.")) &&
            p.has(name))
            return true;
        if (builtins.has(name) || name == "Emitter.NormalizedLoopAge")
            return true;
        if (name.begins_with("User."))
            return p.has(name) && p[name].get_type() != Variant::NIL && check_value(p[name], Dictionary());
        Array references = contracts["references"];
        if (name.contains(".") && !references.has(name))
            return named(name,
                         {"Engine.Owner.Scale", "Engine.Emitter.SimulationPosition", "StackContext.Position",
                          "Particles.Initial.Position", "Particles.InitialRotation", "Transient.FirstFrame",
                          "Particles.Initial.Scale", "Particles.Initial.MeshOrientation",
                          "Transient.MeshScaleFactor"});
    }
    return true;
}
bool Compiler::math_arguments(const Dictionary &d, const Dictionary &p) const {
    Dictionary spec = specs[d["name"]], inputs = spec["inputs"], known, seen;
    Array keys = p.keys();
    for (int i = 0; i < keys.size(); ++i)
        known[keys[i]] = type(p[keys[i]], Dictionary());
    for (const char *name :
         {"Engine.DeltaTime", "Emitter.LoopedAge", "Emitter.NormalizedLoopAge", "Emitter.Age", "System.Age"})
        known[name] = "float";
    keys = fields.keys();
    for (const char *prefix : {"Emitter", "System"})
        for (int i = 0; i < keys.size(); ++i)
            known[String(prefix) + "." + String(keys[i])] = fields[keys[i]];
    Array args = d.get("arguments", Array());
    for (int i = 0; i < args.size(); ++i) {
        Dictionary arg = args[i];
        String key = arg.get("name", "");
        if (!inputs.has(key) || seen.has(key))
            return false;
        seen[key] = true;
        Variant item = arg.get("value", Variant());
        String expected = Array(inputs[key])[0], actual = type(item, known);
        if (actual != expected && !(expected == "float" && actual == "int"))
            return false;
        if (number(item) && (!std::isfinite(double(item)) || std::abs(double(item)) > 3.402823466e38))
            return false;
    }
    return true;
}
bool Compiler::index_arguments(const Dictionary &d, const Dictionary &p) const {
    Dictionary known, seen;
    for (const char *key : {"Emitter.NormalizedLoopAge", "Emitter.Age", "System.Age"})
        known[key] = "float";
    Array keys = p.keys();
    for (int i = 0; i < keys.size(); ++i)
        known[keys[i]] = type(p[keys[i]], Dictionary());
    Array args = d.get("arguments", Array());
    for (int i = 0; i < args.size(); ++i) {
        Dictionary arg = args[i];
        String name = arg.get("name", "");
        if (!named(name, {"NormalizedIndexScale", "NormalizedIndexIncludesOne"}) || seen.has(name))
            return false;
        seen[name] = true;
        Variant item = arg.get("value", Variant());
        String actual = type(item, known);
        if ((name == "NormalizedIndexIncludesOne" && actual != "bool") ||
            (name == "NormalizedIndexScale" && !named(actual, {"float", "int"})))
            return false;
        if (number(item) && (!std::isfinite(double(item)) || std::abs(double(item)) > 3.402823466e38))
            return false;
    }
    return true;
}
bool Compiler::uses_index(const Variant &v) const {
    if (v.get_type() != Variant::DICTIONARY)
        return false;
    Dictionary d = v;
    if (!equal(d.get("kind", Variant()), String("call")))
        return false;
    if (equal(d.get("name", Variant()), String("ReturnNormalizedExecIndex")))
        return true;
    Array args = d.get("arguments", Array());
    for (int i = 0; i < args.size(); ++i)
        if (uses_index(Dictionary(args[i]).get("value", Variant())))
            return true;
    return false;
}
String Compiler::lifecycle_expression(const Variant &v, bool system) const {
    if (v.get_type() != Variant::DICTIONARY)
        return "";
    Dictionary d = v;
    if (uses_index(v))
        return "Normalized execution index requires a supported particle execution stage.";
    if (equal(d.get("kind", Variant()), String("reference"))) {
        String name = d.get("name", "");
        if (name.begins_with("Particles.") || (system && name.begins_with("Emitter.")))
            return "Lifecycle inputs cannot read a particle or child emitter scope: " + name;
    }
    if (equal(d.get("kind", Variant()), String("call")) && String(d.get("name", "")).begins_with("Random"))
        return "Lifecycle-stage random stream provenance is not implemented.";
    Array args = d.get("arguments", Array());
    for (int i = 0; i < args.size(); ++i) {
        String error = lifecycle_expression(Dictionary(args[i]).get("value", Variant()), system);
        if (!error.is_empty())
            return error;
    }
    return "";
}
TypedArray<String> Compiler::curve_errors(const Variant &v, const Dictionary &p) const {
    TypedArray<String> errors;
    if (v.get_type() != Variant::DICTIONARY)
        return errors;
    Dictionary d = v;
    String name = d.get("name", "");
    if (equal(d.get("kind", ""), String("reference")) && p.has(name)) {
        Variant resolved = p[name];
        if (resolved.get_type() == Variant::DICTIONARY &&
            named(Dictionary(resolved).get("kind", ""),
                  {"curve_di", "color_curve_di", "vector2_curve_di", "vector3_curve_di"})) {
            Array reasons = Dictionary(resolved).get("errors", Array());
            for (int i = 0; i < reasons.size(); ++i)
                errors.append(name + ": " + String(reasons[i]));
        }
    } else if (equal(d.get("kind", ""), String("call"))) {
        Array args = d.get("arguments", Array());
        for (int i = 0; i < args.size(); ++i)
            errors.append_array(curve_errors(Dictionary(args[i]).get("value", Variant()), p));
    }
    return errors;
}
TypedArray<String> Compiler::missing_parameters(const Variant &v, const Dictionary &p) const {
    TypedArray<String> missing;
    if (v.get_type() != Variant::DICTIONARY)
        return missing;
    Dictionary d = v;
    String name = d.get("name", "");
    if (equal(d.get("kind", ""), String("reference"))) {
        if (name.begins_with("User.") && (!p.has(name) || p[name].get_type() == Variant::NIL))
            missing.append(name);
    } else if (equal(d.get("kind", ""), String("call"))) {
        Array args = d.get("arguments", Array());
        for (int i = 0; i < args.size(); ++i)
            missing.append_array(missing_parameters(Dictionary(args[i]).get("value", Variant()), p));
    }
    return missing;
}
bool Compiler::rate_supported(Variant v, const Dictionary &p) {
    if (v.get_type() == Variant::DICTIONARY) {
        Dictionary d = v;
        String name = d.get("name", "");
        if (!equal(d.get("kind", ""), String("reference")) || !name.begins_with("User."))
            return false;
        v = p.get(name, Variant());
    }
    return number(v) && std::isfinite(double(v)) && double(v) >= 0.0;
}
TypedArray<String> Compiler::lifecycle_errors(const Ref<Resource> &op, const Dictionary &p,
                                              const Dictionary &types, bool system) const {
    TypedArray<String> errors;
    Dictionary c = NBNativeLifecycle::configuration(op, system);
    if (!named(c["mode"], {"Self", "System"}) || !named(c["scalability"], {"Self", "System"}) ||
        !named(c["behavior"], {"Infinite", "Once", "Multiple"}) ||
        !named(c["duration_mode"], {"Fixed", "Infinite"}) ||
        !(system ? named(c["response"], {"Complete", "Kill"})
                 : named(c["response"], {"Complete", "Kill", "Continue"})))
        errors.append("Unsupported lifecycle mode, loop behavior or inactive response.");
    for (const char *key : {"UseLoopDelay", "DelayFirstLoopOnly", "RecalculateDurationEachLoop"})
        if (input(op, key, false).get_type() != Variant::BOOL)
            errors.append(String("Lifecycle switch requires an authored Boolean: ") + key);
    Array inputs = op->get("inputs");
    for (int i = 0; i < inputs.size(); ++i) {
        Ref<Resource> item = inputs[i];
        Variant value = item->get("value");
        if (!check_value(value, p))
            errors.append("Unsupported lifecycle input: " + String(item->get("input_name")));
        errors.append_array(curve_errors(value, p));
        String error = lifecycle_expression(value, system);
        if (!error.is_empty())
            errors.append(error);
    }
    for (const char *key : {"LoopDuration", "LoopDelay", "LoopCount"}) {
        Variant value = input(op, key, String(key) == "LoopCount" ? Variant(1) : Variant(0.0));
        String actual = type(value, types);
        if (!named(actual, {"float", "int"}) || (String(key) == "LoopCount" && actual != "int"))
            errors.append(String("Lifecycle input requires ") +
                          (String(key) == "LoopCount" ? "an integer: " : "a scalar: ") + key);
        if (number(value) && (!std::isfinite(double(value)) || std::abs(double(value)) > 3.402823466e38))
            errors.append(String("Lifecycle input is outside finite float32 range: ") + key);
    }
    return errors;
}
Dictionary Compiler::attribute_types(const Ref<Resource> &effect, const Dictionary &layout) const {
    Dictionary types = Dictionary(layout["declarations"]).duplicate();
    types.merge(layout.get("outputs", Dictionary()), true);
    Array rows = layout["particle"];
    for (int i = 0; i < rows.size(); ++i) {
        Dictionary row = rows[i];
        types[row["name"]] = row["type"];
    }
    Array declarations = effect->get("user_parameters");
    for (int i = 0; i < declarations.size(); ++i) {
        Ref<Resource> d = declarations[i];
        String name = d->get("input_name");
        if (!name.begins_with("User."))
            name = "User." + name;
        String actual = NBNativeAttributeValues::type_name(d->get("target_type"));
        if (actual.is_empty() && bool(d->get("has_default")))
            actual = type(d->get("value"), Dictionary());
        types[name] = actual;
    }
    for (const char *name : {"Engine.DeltaTime", "Emitter.LoopedAge", "Emitter.NormalizedLoopAge",
                             "Emitter.Age", "System.Age", "Emitter.AlphaScale"})
        types[name] = "float";
    Array keys = fields.keys();
    for (const char *prefix : {"Emitter", "System"})
        for (int i = 0; i < keys.size(); ++i)
            types[String(prefix) + "." + String(keys[i])] = fields[keys[i]];
    for (const char *name :
         {"Engine.Owner.Scale", "Engine.Emitter.SimulationPosition", "StackContext.Position",
          "Particles.Initial.Position", "Particles.Initial.Scale", "Particles.Previous.Velocity",
          "Transient.PhysicsForce", "Transient.AdvectionVelocity", "Particles.Presolve.Position",
          "Particles.Presolve.Velocity", "Particles.Presolve.PhysicsForce"})
        types[name] = "vec3";
    types["Particles.Initial.Color"] = "color";
    types["Particles.Initial.SpriteSize"] = "vec2";
    types["Transient.FirstFrame"] = "bool";
    types["Particles.Initial.MeshOrientation"] = "quat";
    types["Particles.InitialRotation"] = "quat";
    return types;
}
} // namespace godot::nbcompiler
