#include "runtime_operation.h"
#include "curve_evaluator.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
namespace godot {
void NBNativeRuntimeOperations::_bind_methods() {
    ClassDB::bind_static_method(
        "NBNativeRuntimeOperations",
        D_METHOD("refresh_emitters", "emitters", "runtime_script", "input_script", "writes"),
        &NBNativeRuntimeOperations::refresh_emitters);
    ClassDB::bind_static_method("NBNativeRuntimeOperations", D_METHOD("references", "value", "output"),
                                &NBNativeRuntimeOperations::references);
    ClassDB::bind_static_method("NBNativeRuntimeOperations", D_METHOD("signature", "operation"),
                                &NBNativeRuntimeOperations::signature);
    ClassDB::bind_static_method("NBNativeRuntimeOperations",
                                D_METHOD("create", "operation", "runtime_script", "input_script"),
                                &NBNativeRuntimeOperations::create);
    ClassDB::bind_static_method("NBNativeRuntimeOperations", D_METHOD("prepare_value", "value", "type"),
                                &NBNativeRuntimeOperations::prepare_value, DEFVAL(String()));
    ClassDB::bind_static_method("NBNativeRuntimeOperations",
                                D_METHOD("input_value", "indexed", "key", "fallback", "occurrence"),
                                &NBNativeRuntimeOperations::input_value, DEFVAL(Variant()), DEFVAL(0));
}
void NBNativeRuntimeOperations::refresh_emitters(const Array &emitters, const Ref<Script> &runtime_script,
                                                 const Ref<Script> &input_script, const Dictionary &writes) {
    if (runtime_script.is_null() || input_script.is_null())
        return;
    for (int n = 0; n < emitters.size(); ++n) {
        Dictionary emitter = emitters[n];
        if (emitter.has("native_vm") || emitter.has("gpu"))
            continue;
        Dictionary plan = emitter["plan"], stages = plan["stages"];
        bool changed = false;
        if (!plan.has("runtime_source_stages"))
            plan["runtime_source_stages"] = stages.duplicate();
        Dictionary source_stages = plan["runtime_source_stages"];
        Array names = source_stages.keys();
        for (int stage = 0; stage < names.size(); ++stage) {
            Array source = source_stages[names[stage]], current = stages[names[stage]];
            if (current.size() != source.size()) {
                current = source.duplicate();
                stages[names[stage]] = current;
            }
            for (int index = 0; index < source.size(); ++index) {
                Ref<Resource> prepared = current[index], operation = source[index];
                if (prepared.is_null() || !runtime_script->instance_has(prepared.ptr()) ||
                    prepared->get("source") != Variant(operation) ||
                    int64_t(prepared->get("source_signature")) != signature(operation)) {
                    if (current == source) {
                        current = source.duplicate();
                        stages[names[stage]] = current;
                    }
                    current[index] = create(operation, runtime_script, input_script);
                    changed = true;
                }
            }
        }
        if (changed) {
            Dictionary reads;
            reads["Particles.Position"] = true;
            reads["StackContext.Position"] = true;
            reads["Particles.NormalizedAge"] = true;
            bool known = true;
            Array operations = source_stages.get("ParticleUpdate", Array());
            for (int i = 0; i < operations.size(); ++i) {
                Ref<Resource> operation = operations[i];
                if (operation.is_null() || !bool(operation->get("enabled")))
                    continue;
                if (String(operation->get("operation_kind")) != "assignment" &&
                    !writes.has(operation->get("module_name")))
                    known = false;
                Array inputs = operation->get("inputs");
                for (int j = 0; j < inputs.size(); ++j) {
                    Ref<Resource> input = inputs[j];
                    if (input.is_valid())
                        references(input->get("value"), reads);
                }
            }
            if (known)
                plan["context_reads"] = reads;
            else
                plan.erase("context_reads");
        }
    }
}
void NBNativeRuntimeOperations::references(const Variant &value, Dictionary output) {
    references_depth(value, output, 0);
}
void NBNativeRuntimeOperations::references_depth(const Variant &value, Dictionary output, int depth) {
    if (depth > 256)
        return;
    if (value.get_type() == Variant::DICTIONARY) {
        Dictionary d = value;
        if (d.get("kind", Variant()) == Variant("reference"))
            output[String(d.get("name", ""))] = true;
        Array children = d.values();
        for (int n = 0; n < children.size(); ++n)
            references_depth(children[n], output, depth + 1);
    } else if (value.get_type() == Variant::ARRAY) {
        Array children = value;
        for (int n = 0; n < children.size(); ++n)
            references_depth(children[n], output, depth + 1);
    }
}
int64_t NBNativeRuntimeOperations::signature(const Ref<Resource> &operation) {
    if (operation.is_null())
        return 0;
    Array values;
    for (const char *field :
         {"stable_id", "module_name", "alias_name", "enabled", "operation_kind", "target_type"})
        values.append(operation->get(field));
    Array inputs = operation->get("inputs");
    for (int n = 0; n < inputs.size(); ++n) {
        Ref<Resource> input = inputs[n];
        if (input.is_null())
            continue;
        Array row;
        row.append(input->get("input_name"));
        row.append(input->get("target_type"));
        row.append(input->get("value"));
        values.append(row);
    }
    return UtilityFunctions::hash(values);
}
Ref<Resource> NBNativeRuntimeOperations::create(const Ref<Resource> &operation,
                                                const Ref<Script> &runtime_script,
                                                const Ref<Script> &input_script) {
    if (operation.is_null() || runtime_script.is_null() || input_script.is_null())
        return Ref<Resource>();
    Ref<Resource> result = runtime_script->call("new");
    if (result.is_null())
        return result;
    result->set("source", operation);
    result->set("source_signature", signature(operation));
    for (const char *field :
         {"stable_id", "module_name", "alias_name", "enabled", "line", "operation_kind", "target_type"})
        result->set(field, operation->get(field));
    Array inputs = operation->get("inputs");
    TypedArray<Resource> prepared;
    Dictionary indexed;
    for (int n = 0; n < inputs.size(); ++n) {
        Ref<Resource> input = inputs[n], copy = input_script->call("new");
        if (input.is_null() || copy.is_null())
            return Ref<Resource>();
        String name = input->get("input_name"), type = input->get("target_type");
        Variant value = prepare_value(input->get("value"), type);
        copy->set("input_name", name);
        copy->set("target_type", type);
        copy->set("value", value);
        prepared.append(copy);
        Array occurrences = indexed.get(name, Array());
        occurrences.append(value);
        indexed[name] = occurrences;
    }
    result->set("inputs", prepared);
    result->set("indexed", indexed);
    return result;
}
Variant NBNativeRuntimeOperations::prepare_value(const Variant &value, const String &type) {
    return prepare_depth(value, type, 0);
}
Variant NBNativeRuntimeOperations::prepare_depth(const Variant &value, const String &type, int depth) {
    if (depth > 256)
        return value;
    bool curve_type = type == "DI<Curve>" || type == "DI<ColorCurve>" || type == "DI<Vector2DCurve>" ||
                      type == "DI<VectorCurve>";
    if (curve_type && !(value.get_type() == Variant::DICTIONARY &&
                        Dictionary(value).get("kind", Variant()) == Variant("reference")))
        return NBNativeCurves::prepare(value, type);
    if (value.get_type() != Variant::DICTIONARY)
        return value;
    Dictionary original = value, result = original.duplicate();
    String kind = original.get("kind", "");
    if (kind == "curve")
        result["_prepared_curve"] = NBNativeCurves::prepare(value, "DI<Curve>");
    if (kind != "call")
        return result;
    String call = original.get("name", ""), curve_key, curve_target;
    if (call == "FloatFromCurve") {
        curve_key = "FloatCurve";
        curve_target = "DI<Curve>";
    } else if (call == "ColorFromCurve") {
        curve_key = "ColorCurve";
        curve_target = "DI<ColorCurve>";
    } else if (call == "Vector2DFromCurve") {
        curve_key = "Vector2Curve";
        curve_target = "DI<Vector2DCurve>";
    } else if (call == "VectorFromCurve") {
        curve_key = "VectorCurve";
        curve_target = "DI<VectorCurve>";
    }
    Array arguments = original.get("arguments", Array()), prepared;
    Dictionary indexed;
    for (int n = 0; n < arguments.size(); ++n) {
        Dictionary argument = arguments[n], copy = argument.duplicate();
        String name = copy.get("name", "");
        copy["value"] =
            prepare_depth(argument.get("value", Variant()),
                          !curve_target.is_empty() && name == curve_key ? curve_target : String(), depth + 1);
        prepared.append(copy);
        if (!indexed.has(name))
            indexed[name] = copy["value"];
    }
    result["arguments"] = prepared;
    result["_indexed_arguments"] = indexed;
    return result;
}
Variant NBNativeRuntimeOperations::input_value(const Dictionary &indexed, const String &key,
                                               const Variant &fallback, int64_t occurrence) {
    Array values = indexed.get(key, Array());
    return occurrence >= 0 && occurrence < values.size() ? values[occurrence] : fallback;
}
} // namespace godot
