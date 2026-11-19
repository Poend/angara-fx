#include "source_program.h"
#include "effect_compiler.h"
#include "value_evaluator.h"

namespace godot::nbsource {
using nbcompiler::Compiler;
using nbcompiler::equal;
using nbcompiler::named;
void bind_lifecycle(const Ref<Resource> &effect, const Dictionary &plan, const Dictionary &packet,
                    Dictionary result) {
    if (!Array(plan.get("emitter_assignments", Array())).is_empty())
        bind_emitter_spawn(plan, packet, result);
    Array errors = result["errors"];
    Dictionary stages = plan["stages"];
    Array ops = stages.get("EmitterUpdate", Array());
    for (int i = 0; i < ops.size(); ++i) {
        Ref<Resource> op = ops[i];
        if (bool(op->get("enabled")) && equal(op->get("operation_kind"), String("assignment")))
            errors.append("Emitter-update assignment provenance is not implemented for native comparison.");
    }
    int state_count = 0;
    bool before = false;
    for (int i = 0; i < ops.size(); ++i) {
        Ref<Resource> op = ops[i];
        if (!bool(op->get("enabled")))
            continue;
        if (equal(op->get("module_name"), String("EmitterState")))
            ++state_count;
        else if (named(op->get("module_name"),
                       {"SpawnRate", "SpawnBurst_Instantaneous", "SpawnBurst_Instantaneous@1.0"}) &&
                 state_count != 1)
            before = true;
    }
    if (state_count != 1 || before)
        errors.append("Compiled source scheduling requires exactly one EmitterState before spawn modules.");
    Array source_stages = effect->get("stages");
    for (int s = 0; s < source_stages.size(); ++s) {
        Ref<Resource> stage = source_stages[s];
        ops = stage->get("operations");
        for (int i = 0; i < ops.size(); ++i) {
            Ref<Resource> op = ops[i];
            if (!bool(op->get("enabled")))
                continue;
            if (!equal(stage->get("stage_name"), String("SystemUpdate")) ||
                !equal(op->get("module_name"), String("SystemState")))
                errors.append("Source comparison does not implement this system-stage lifecycle/operation.");
        }
    }
}
void bind_emitter_spawn(const Dictionary &plan, const Dictionary &packet, Dictionary result) {
    Array errors = result["errors"];
    Dictionary c = packet.get("EmitterSpawn", Dictionary());
    if (!bool(c.get("complete", false)) || !Array(c.get("diagnostics", Array())).is_empty() ||
        String(c.get("hlsl_sha256", "")).length() != 64) {
        errors.append("Emitter-spawn assignments require a complete paired SystemSpawnScript contract.");
        return;
    }
    Array calls = Array(c.get("calls", Array())).duplicate(true), used, ops = plan["emitter_assignments"];
    Dictionary bindings;
    Ref<Resource> emitter = plan["source"];
    String scope = String(emitter->get("display_name")) + ".";
    for (int i = 0; i < ops.size(); ++i) {
        Ref<Resource> op = ops[i];
        Array inputs = op->get("inputs");
        if (inputs.size() != 1) {
            errors.append("Emitter-spawn assignment requires one typed value.");
            continue;
        }
        Ref<Resource> input = inputs[0];
        Variant value = input->get("value");
        if (!curve_leaves(value, "").is_empty()) {
            errors.append("Emitter-spawn curve LUT provenance is not implemented.");
            continue;
        }
        int64_t count = random_count(value);
        if (count == 0)
            continue;
        Array leaves = random_leaves(value, String(op->get("stable_id")) + "/assignment");
        if (count != 1 || leaves.size() != 1) {
            errors.append("Emitter-spawn assignment requires one unambiguous compiled RNG expression.");
            continue;
        }
        Dictionary leaf = leaves[0];
        String key = leaf["path"];
        int components = equal(Dictionary(leaf["value"])["name"], String("RandomRangeVector")) ? 3 : 1;
        bind(calls, used, bindings, key, scope + String(op->get("module_name")).trim_prefix("Emitter."),
             components, errors);
        if (!bindings.has(key))
            continue;
        Dictionary call = bindings[key];
        String seed1 = String(call.get("seed1_expression", "")).strip_edges();
        if (!seed1.is_valid_int() || !signed32(seed1.to_int())) {
            errors.append("Emitter-spawn RNG requires an emitted literal signed32 seed1.");
            continue;
        }
        call["verified_seed1_constant"] = seed1.to_int();
        if (call.get("call_id", Variant()).get_type() != Variant::INT ||
            int64_t(call.get("counter", 0)) <= 0 ||
            !named(call.get("primitive", ""), {"rand", "rand_float", "rand_int"})) {
            errors.append("Unsupported emitter-spawn RNG primitive/CallID/counter.");
            continue;
        }
        String seed3 = String(call.get("seed3_expression", "")).strip_edges();
        if (seed3.is_valid_int() && signed32(seed3.to_int()))
            call["verified_seed3_constant"] = seed3.to_int();
        else {
            Dictionary input = instance_int_parameter(c, seed3);
            if (input.is_empty()) {
                errors.append("Emitter-spawn RNG seed3 lacks an observed scalar instance input/default: " +
                              seed3);
                continue;
            }
            call["verified_seed3_parameter"] = input["name"];
            call["verified_seed3_default"] = int64_t(input["value"]);
        }
    }
    for (int i = 0; i < calls.size(); ++i)
        if (!used.has(i)) {
            Array targets = Dictionary(calls[i]).get("semantic_targets", Array());
            bool found = false;
            for (int t = 0; t < targets.size(); ++t)
                if (String(targets[t]).begins_with(scope))
                    found = true;
            if (found)
                errors.append("Unmapped emitter-spawn RNG call for " + String(emitter->get("display_name")));
        }
    Dictionary stages = result["stages"], curves = result["curves"];
    stages["EmitterSpawn"] = bindings;
    curves["EmitterSpawn"] = Dictionary();
}
} // namespace godot::nbsource
