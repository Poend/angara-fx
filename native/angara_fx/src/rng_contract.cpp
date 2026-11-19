#include "rng_contract.h"
#include "gpu_particle_reader.h"
#include "effect_compiler.h"
#include <godot_cpp/classes/reg_ex_match.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <vector>

namespace godot {
namespace {
// Unreal emits the particle permutation at stage zero alongside the Grid3D permutations.
// Keep line numbers and unknown preprocessor branches for the conservative RNG analysis.
String particle_permutation(const String &hlsl) {
    if (!hlsl.contains("SimulationStageIndex"))
        return hlsl;
    struct Branch { bool parent, may_fall_through; };
    std::vector<Branch> stack;
    auto stage_condition = [](String condition) {
        condition = condition.get_slice("//", 0).replace(" ", "").replace("\t", "").strip_edges();
        // Only the OR of equality tests emitted for shared Niagara stage functions.
        // Other preprocessor expressions remain unknown.
        PackedStringArray terms = condition.split("||");
        bool includes_zero = false;
        for (int i = 0; i < terms.size(); ++i) {
            String term = terms[i].strip_edges();
            while (term.begins_with("(")) term = term.substr(1);
            while (term.ends_with(")")) term = term.left(-1);
            if (!term.begins_with("SimulationStageIndex==")) return -1;
            String index = term.trim_prefix("SimulationStageIndex==");
            if (!index.is_valid_int()) return -1;
            includes_zero |= index.to_int() == 0;
        }
        return int(includes_zero);
    };
    PackedStringArray lines = hlsl.split("\n");
    bool visible = true;
    for (int i = 0; i < lines.size(); ++i) {
        String line = lines[i].strip_edges();
        if (!line.begins_with("#")) {
            if (!visible) lines.set(i, "");
            continue;
        }
        String directive = line.substr(1).strip_edges().replace("\t", " ");
        String keyword = directive.get_slice(" ", 0);
        if (keyword == "if" || keyword == "ifdef" || keyword == "ifndef") {
            int test = keyword == "if" ? stage_condition(directive.substr(2)) : -1;
            stack.push_back({visible, test != 1});
            visible &= test != 0;
        } else if (!stack.empty() && (keyword == "else" || keyword == "elif")) {
            int test = keyword == "else" ? 1 : stage_condition(directive.substr(4));
            visible = stack.back().parent && stack.back().may_fall_through && test != 0;
            stack.back().may_fall_through &= test != 1;
        } else if (keyword == "endif" && !stack.empty()) {
            visible = stack.back().parent;
            stack.pop_back();
        }
    }
    return String("\n").join(lines);
}
} // namespace
void NBNativeRNGContract::_bind_methods() {
    ClassDB::bind_method(D_METHOD("extract", "hlsl", "entry_function"), &NBNativeRNGContract::extract);
    ClassDB::bind_method(D_METHOD("get_functions"), &NBNativeRNGContract::get_functions);
    ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "_functions", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE),
                 "", "get_functions");
}
Dictionary NBNativeRNGContract::get_functions() const {
    std::lock_guard<std::mutex> guard(lock);
    return functions.duplicate(true);
}
Dictionary NBNativeRNGContract::extract(const String &hlsl, const String &entry_function) {
    std::lock_guard<std::mutex> guard(lock);
    source = (entry_function == "SimulateMapSpawn" || entry_function == "SimulateMapUpdate")
                 ? particle_permutation(hlsl) : hlsl;
    entry = entry_function;
    functions = Dictionary();
    globals = Dictionary();
    calls = Array();
    samples = Array();
    diagnostics = Array();
    counter = 0;
    unseeded_counter = 0;
    read_functions();
    zero_spawn_id = entry_function == "SimulateMapUpdate" &&
                    NBNativeLegacyRNGShape::zero_spawn_id_on_update(source, functions);
    if (!functions.has(entry_function))
        error("Missing compiled stage entry: " + entry_function, 0);
    else
        walk(entry_function, Array(), Dictionary(), Array(), false);
    for (int i = 0; i < calls.size(); ++i) {
        Dictionary call = calls[i];
        Array targets = call["semantic_targets"];
        targets.sort();
        Array stack = call["call_stack"];
        if (stack.size() == 4 && String(stack[2]).begins_with("CalculateRandomUnitVector_") &&
            functions.has(stack[1]) && functions.has(stack[2])) {
            // The standard full-sphere helper computes both a sphere and a circle.
            // Check both function bodies before discarding a draw for the unused circle.
            String outer = Dictionary(functions[stack[1]])["body"], unit = Dictionary(functions[stack[2]])["body"];
            const String alias = String(stack[1]).trim_suffix("_Emitter_Func");
            outer = outer.replace("." + alias + ".VectorScale", ".UnitVector.VectorScale");
            auto generated = regex("(CalculateRandom(?:UnitVector|RangeFloat|Range))_[A-Z0-9]+_Func");
            if (NBNativeLegacyRNGShape::signature(generated->sub(outer,"$1_Func",true)) ==
                    "c154279684336f84cbde98ef801dd69e32e0bf2c4e138b9424bf7bdfcc0f95dc" &&
                NBNativeLegacyRNGShape::signature(generated->sub(unit,"$1_Func",true)) ==
                    "ad54beb5143df33a3d331c1a7c7b6e4221f0df00365f7d5a30319acfa904fdab") {
                if (String(stack[3]).begins_with("CalculateRandomRangeFloat_") && targets.is_empty() &&
                    call.get("random_mode","") == Variant("seeded"))
                    call["unused_circle_draw"] = true;
                else if (String(stack[3]).begins_with("CalculateRandomRange_") && int64_t(call["components"]) == 2)
                    call["unit_sphere_draw"] = true;
            }
        }
        // The legacy unit-vector helper consumes a scalar for its circle output even when
        // the caller only uses the sphere. Verify both bodies before accepting that draw.
        if (targets.is_empty() && call.has("legacy_helper_shapes") && stack.size() >= 3 &&
            int64_t(call["components"]) == 1) {
            String unit = stack[stack.size() - 2], caller = stack[stack.size() - 3];
            if (unit.begins_with("RandomUnitVector_") && caller.begins_with("RandomVector_")) {
                auto generated = regex("(Random(?:UnitVector|RangeFloat))_[A-Z0-9]+_Func");
                auto signature = [&](const String &function) {
                    return NBNativeLegacyRNGShape::signature(generated->sub(Dictionary(functions[function])["body"], "$1_Func", true));
                };
                if (signature(unit) == "e119c4b2151787ff120a2d56b52339211132e0b13c9b60206d500c33502c3393" &&
                    signature(caller) == "b6dbc6312702f1e9636c90cfb973b01c8b8af391201ec71b7b33393373b5640b")
                    call["unused_circle_draw"] = true;
            }
        }
        if (targets.is_empty() && !bool(call.get("verified_tick_seed", false)) && !bool(call.get("unused_circle_draw",false)))
            error("RNG call has no traceable parameter-map destination.", call["line"]);
    }
    Array curves, values;
    for (int i = 0; i < samples.size(); ++i) {
        Dictionary item = samples[i];
        if (nbcompiler::equal(item.get("kind", "curve_sample"), String("curve_sample")))
            curves.append(item);
        if (nbcompiler::equal(item.get("kind", ""), String("value_call")))
            values.append(item);
    }
    Dictionary result;
    result["schema_version"] = 1;
    result["hlsl_sha256"] = hlsl.sha256_text();
    result["entry_function"] = entry_function;
    if (functions.has(entry_function) &&
        (entry_function == "SimulateMapSpawn" || entry_function == "SimulateMapUpdate")) {
        const String map = entry_function == "SimulateMapSpawn" ? "MapSpawn" : "MapUpdate";
        const String body = Dictionary(functions[entry_function])["body"];
        Dictionary readers;
        Array function_names = functions.keys();
        for (int i = 0; i < function_names.size(); ++i) {
            String name = function_names[i];
            if (!name.ends_with("_Emitter_Func") || !body.contains(name + "(Context);")) continue;
            String module_body = Dictionary(functions[name])["body"];
            if (!module_body.contains("GetVectorByIndex_")) continue;
            String alias = name.trim_suffix("_Emitter_Func");
            Dictionary reader = nbreader::lower(module_body, map, alias);
            if (!reader.is_empty()) readers[alias] = reader;
        }
        if (!readers.is_empty()) result["indexed_reader_modules"] = readers;
        // Only a constant definition, a store and its sole direct use on consecutive lines.
        // There can be no intervening branch, parameter update or module invocation.
        String field = "Context\\." + map + "\\.OUTPUT_VAR\\.([A-Za-z_]\\w*\\.[A-Za-z_]\\w*)";
        String pattern = "(?m)^\\s*float (Constant[0-9]+) = ([-+0-9.eE]+);[\\t \\r]*\\n"
                         "[\\t ]*" + field + " = \\1;[\\t \\r]*\\n"
                         "[\\t ]*Context\\." + map + "\\.[\\w.]+ = Context\\." + map + "\\.OUTPUT_VAR\\.\\3;";
        Dictionary defaults;
        for (const Ref<RegExMatch> &match : regex(pattern)->search_all(body)) {
            String name = match->get_string(3), literal = match->get_string(2);
            if (literal.is_valid_float() && source.count("Context." + map + ".OUTPUT_VAR." + name) == 2)
                defaults["Output." + name] = double(float(literal.to_float()));
        }
        if (!defaults.is_empty()) result["literal_module_outputs"] = defaults;
    }
    result["calls"] = calls.duplicate(true);
    result["unseeded_draw_count"] = unseeded_counter;
    result["curve_samples"] = curves.duplicate(true);
    result["value_calls"] = values.duplicate(true);
    result["diagnostics"] = diagnostics.duplicate(true);
    result["complete"] = diagnostics.is_empty();
    result["analysis"] = "straight-line emitted function calls with scalar/vector dependency propagation; "
                         "dynamic RNG control flow rejected";
    return result;
}
void NBNativeRNGContract::read_functions() {
    TypedArray<RegExMatch> matches =
        regex("(?m)^void[ \\t]+([A-Za-z_][A-Za-z0-9_]*)[ \\t]*\\(([^;\\n]*)\\)[ \\t\\r\\n]*\\{")
            ->search_all(source);
    for (int i = 0; i < matches.size(); ++i) {
        Ref<RegExMatch> match = matches[i];
        String name = match->get_string(1);
        if (name.begins_with("SampleCurve_") || name.begins_with("SampleColorCurve_") ||
            name.begins_with("SampleVector2DCurve_") || name.begins_with("SampleVectorCurve_"))
            continue;
        int64_t begin = match->get_end() - 1, end = close_brace(source, begin);
        if (end < 0) {
            error("Unbalanced emitted function braces: " + name, line(begin));
            continue;
        }
        if (functions.has(name)) {
            error("Multiple emitted definitions need preprocessing: " + name, line(begin));
            continue;
        }
        Array parameters, args = arguments(match->get_string(2));
        for (int a = 0; a < args.size(); ++a) {
            String argument = args[a];
            PackedStringArray words = argument.strip_edges().split(" ", false);
            if (words.size() < 2) {
                error("Unsupported emitted function parameter: " + argument, line(begin));
                continue;
            }
            Dictionary p;
            p["name"] = words[words.size() - 1];
            p["type"] = words[words.size() - 2];
            p["out"] = words[0] == "out" || words[0] == "inout";
            parameters.append(p);
        }
        Dictionary definition;
        definition["body"] = source.substr(begin + 1, end - begin - 1);
        definition["parameters"] = parameters;
        definition["line"] = line(begin);
        functions[name] = definition;
    }
}
} // namespace godot
