#include "rng_contract.h"
#include "effect_compiler.h"
#include <godot_cpp/classes/reg_ex_match.hpp>
#include <algorithm>

namespace godot {
Dictionary NBNativeRNGContract::walk(const String &name, const Array &args, const Dictionary &parent,
                                     const Array &stack, bool under_control) {
    if (stack.size() >= 64 || stack.has(name)) {
        error("Recursive or excessively deep compiled function call: " + name, 0);
        return Dictionary();
    }
    Dictionary definition = functions[name], environment;
    Array parameters = definition["parameters"];
    for (int i = 0; i < parameters.size(); ++i) {
        Dictionary parameter = parameters[i];
        String argument = i < args.size() ? String(args[i]) : String(parameter["name"]);
        Array dependencies = tags(argument, parent);
        Dictionary value;
        value["value"] = resolve(argument, parent);
        value["constant"] = constant_literal(argument, parent);
        value["tags"] = dependencies;
        value["component_tags"] = component_tags(argument, parent);
        value["type"] = parameter["type"];
        environment[parameter["name"]] = value;
        for (int t = 0; t < dependencies.size(); ++t) {
            int64_t tag = dependencies[t];
            if (tag >= 0 && tag < calls.size()) {
                Dictionary use;
                use["function"] = name;
                use["parameter"] = parameter["name"];
                use["line"] = definition["line"];
                Array uses = Dictionary(calls[tag])["parameter_uses"];
                if (!uses.has(use))
                    uses.append(use);
            }
        }
    }
    Array next_stack = stack.duplicate();
    next_stack.append(name);
    Dictionary legacy_environment = environment.duplicate(true);
    for (const char *key : {"In_RandomnessMode", "In_OverrideSeed", "In_FixedOverrideSeed", "In_Seed"})
        if (legacy_environment.has(key)) {
            Dictionary value = legacy_environment[key];
            value["value"] = value.get("constant", "");
        }
    Dictionary legacy = NBNativeLegacyRNGShape::inspect(name, functions, legacy_environment);
    // Unseeded legacy ranges use the GPU lane and dispatch counters. A VectorVM
    // Simulate entry has a different stream, even if it emits the same helper body.
    if (bool(legacy.get("unseeded", false)) && entry != "SimulateMapSpawn" && entry != "SimulateMapUpdate")
        legacy.clear();
    if (!legacy.is_empty()) {
        int64_t index = calls.size();
        String map = legacy["map"];
        Array seeds = Array::make("In_Max - In_Min", map + ".Particles.UniqueID",
                                  map + ".Engine.System.TickCount", map + ".Emitter.RandomSeed");
        bool fixed = legacy.get("fixed", false), unseeded = legacy.get("unseeded", false);
        if (legacy.has("override_seed"))
            seeds[3] = legacy["override_seed"].stringify();
        if (fixed) {
            seeds[1] = seeds[3];
            seeds[2] = "0";
            seeds[3] = "0";
        }
        if (unseeded) {
            ++counter; // The helper evaluates the seeded draw before selecting the unseeded result.
            seeds.resize(1);
        }
        record_rng(legacy["primitive"], seeds, legacy["type"], environment, next_stack, definition["line"],
                   under_control, !unseeded && !fixed);
        Dictionary call = calls[index];
        if (!unseeded) {
            if (fixed) {
                if (legacy.has("override_seed"))
                    call["verified_seed1_constant"] = legacy["override_seed"];
                else
                    call["verified_seed1_emitter"] = true;
                call["verified_seed2_constant"] = 0;
                call["verified_seed3_constant"] = 0;
            } else if (legacy.has("override_seed"))
                call["verified_seed3_constant"] = legacy["override_seed"];
            int discarded = nbcompiler::equal(legacy["primitive"], String("rand_float")) ? components(legacy["type"]) : 1;
            unseeded_counter += discarded;
            call["discarded_unseeded_draw_count"] = discarded;
        }
        call["legacy_helper_shapes"] = Array::make(legacy["range_shape"], legacy["info_shape"]);
        if (!unseeded && !fixed && entry == "SimulateMapUpdate" && map == "Context.MapSpawn") {
            if (zero_spawn_id) {
                call["verified_seed1_constant"] = 0;
                call["seed1_provenance"] =
                    "Zero-initialized MapSpawn ID, GPU update wrapper and reachable writes inspected.";
            } else
                error("Cross-stage legacy RNG ID lacks verified GPU context initialization.",
                      definition["line"]);
        }
        Dictionary value, output;
        value["value"] = "Out_Result";
        value["tags"] = Array::make(index);
        value["component_tags"] = Dictionary();
        value["type"] = legacy["type"];
        output["Out_Result"] = value;
        return output;
    }
    Array controls;
    Dictionary pending, last_condition;
    Ref<RegEx> assignment = regex("^(?:(float[234]?|int[234]?|uint[234]?|bool|Niagara[A-Za-z0-9_]+)[ "
                                  "\\t]+)?([A-Za-z_][A-Za-z0-9_.]*)[ \\t]*=[ \\t]*(.*);$");
    Ref<RegEx> call_pattern = regex("^([A-Za-z_][A-Za-z0-9_]*)\\((.*)\\);$"),
               primitive_pattern = regex("^(rand|rand_float|rand_int)\\((.*)\\)$");
    Array rows = statements(definition["body"], definition["line"]);
    for (int s = 0; s < rows.size(); ++s) {
        Dictionary statement = rows[s];
        int64_t line_number = statement["line"];
        String text = statement["text"];
        if (text.is_empty() || text.begins_with("//"))
            continue;
        if (text.begins_with("if(") || text.begins_with("if (") || text.begins_with("for(") ||
            text.begins_with("for (") || text.begins_with("while(") || text.begins_with("while (") ||
            text.begins_with("switch(")) {
            if (contains_rng_call(text))
                error("RNG in an inline control statement is not supported.", line_number);
            pending = Dictionary();
            pending["expression"] = text;
            pending["tags"] = tags(text, environment);
            pending["active"] = constant_condition(text, environment);
            last_condition = pending;
            if (text.contains("{")) {
                controls.append(pending);
                pending = Dictionary();
            }
            continue;
        }
        if (text.begins_with("else")) {
            if (contains_rng_call(text))
                error("RNG in an inline else statement is not supported.", line_number);
            pending = last_condition.duplicate();
            if (pending.get("active", Variant()).get_type() == Variant::BOOL)
                pending["active"] = !bool(pending["active"]);
            if (text.contains("{")) {
                controls.append(pending);
                pending = Dictionary();
            }
            continue;
        }
        if (text == "{") {
            controls.append(pending);
            pending = Dictionary();
            continue;
        }
        if (text.begins_with("}")) {
            if (contains_rng_call(text))
                error("RNG in a combined closing/control statement is not supported.", line_number);
            if (!controls.is_empty())
                last_condition = controls.pop_back();
            continue;
        }
        bool inactive = false, unknown_control = under_control || !pending.is_empty();
        for (int c = 0; c < controls.size(); ++c) {
            Variant active = Dictionary(controls[c]).get("active", Variant());
            if (nbcompiler::equal(active, false))
                inactive = true;
            if (active.get_type() != Variant::BOOL)
                unknown_control = true;
        }
        if (inactive)
            continue;
        Ref<RegExMatch> matched = assignment->search(text);
        if (matched.is_valid()) {
            String target = matched->get_string(2), expression = matched->get_string(3).strip_edges(),
                   type = matched->get_string(1);
            Array dependencies = tags(expression, environment);
            for (int c = 0; c < controls.size(); ++c)
                dependencies = unite(dependencies, Dictionary(controls[c]).get("tags", Array()));
            if (!controls.is_empty())
                dependencies = unite(dependencies, tags(target, environment));
            if (target == "RandomCounterDeterministic") {
                if (expression != "0")
                    error("Unsupported deterministic counter assignment.", line_number);
                if (unknown_control)
                    error("Conditional RNG counter reset is not implemented.", line_number);
                counter = 0;
                continue;
            }
            Ref<RegExMatch> primitive = primitive_pattern->search(expression);
            if (primitive.is_valid()) {
                dependencies = Array::make(calls.size());
                record_rng(primitive->get_string(1), arguments(primitive->get_string(2)), type, environment,
                           next_stack, line_number, unknown_control);
            } else if (expression.contains("rand(") || expression.contains("rand_float(") ||
                       expression.contains("rand_int("))
                error("Nested RNG expression is outside the supported emitted form.", line_number);
            else if (contains_rng_call(expression))
                error("RNG-containing function in an expression requires explicit data-flow support.",
                      line_number);
            Dictionary value;
            value["value"] = resolve(expression, environment);
            value["constant"] = unknown_control ? String() : constant_literal(expression, environment);
            value["tags"] = dependencies;
            value["component_tags"] = component_tags(expression, environment);
            value["type"] = type;
            store(target, value, environment);
            continue;
        }
        matched = call_pattern->search(text);
        if (matched.is_valid()) {
            String function = matched->get_string(1);
            Array child_args = arguments(matched->get_string(2));
            if (functions.has(function)) {
                Dictionary output = walk(function, child_args, environment, next_stack, unknown_control);
                Array child_parameters = Dictionary(functions[function])["parameters"];
                for (int i = 0; i < std::min(child_args.size(), child_parameters.size()); ++i) {
                    Dictionary parameter = child_parameters[i];
                    if (bool(parameter["out"]) &&
                        !nbcompiler::equal(parameter["type"], String("FSimulationContext")) &&
                        output.has(parameter["name"])) {
                        Dictionary value = output[parameter["name"]];
                        if (function.ends_with("_Emitter_Func")) {
                            Dictionary sample;
                            sample["kind"] = "value_call";
                            sample["function"] = function;
                            sample["parameter"] = parameter["name"];
                            sample["semantic_targets"] = Array();
                            sample["call_stack"] = next_stack.duplicate();
                            sample["line"] = line_number;
                            samples.append(sample);
                            value["tags"] = unite(value["tags"], Array::make(-samples.size()));
                        }
                        store(child_args[i], value, environment);
                    }
                }
            } else if (function.begins_with("SampleCurve_") || function.begins_with("SampleColorCurve_") ||
                       function.begins_with("SampleVector2DCurve_") ||
                       function.begins_with("SampleVectorCurve_")) {
                if (child_args.size() != 2)
                    error("Unsupported emitted curve sampling signature.", line_number);
                else {
                    Dictionary sample;
                    sample["function"] = function;
                    sample["index_expression"] = resolve(child_args[0], environment);
                    sample["semantic_targets"] = Array();
                    sample["call_stack"] = next_stack.duplicate();
                    sample["line"] = line_number;
                    samples.append(sample);
                    String type = function.begins_with("SampleColorCurve_")      ? "float4"
                                  : function.begins_with("SampleVector2DCurve_") ? "float2"
                                  : function.begins_with("SampleVectorCurve_")   ? "float3"
                                                                                 : "float";
                    Dictionary value;
                    value["value"] = child_args[1];
                    value["tags"] = Array::make(-samples.size());
                    value["type"] = type;
                    store(child_args[1], value, environment);
                }
            } else if (function.to_lower().contains("random") || function.begins_with("rand"))
                error("Unresolved RNG function: " + function, line_number);
            continue;
        }
        if (text.contains("rand(") || text.contains("rand_float(") || text.contains("rand_int("))
            error("Unsupported RNG statement.", line_number);
        else if (contains_rng_call(text))
            error("RNG-containing function occurs in an unsupported emitted statement.", line_number);
    }
    Dictionary outputs;
    for (int i = 0; i < parameters.size(); ++i) {
        Dictionary p = parameters[i];
        if (bool(p["out"])) {
            Array dependencies = tags(p["name"], environment);
            if (environment.has(p["name"]) || !dependencies.is_empty()) {
                Dictionary value;
                value["value"] = resolve(p["name"], environment);
                value["tags"] = dependencies;
                value["component_tags"] = component_tags(p["name"], environment);
                value["type"] = p["type"];
                outputs[p["name"]] = value;
            }
        }
    }
    return outputs;
}
} // namespace godot
