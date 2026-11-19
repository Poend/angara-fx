#include "source_program.h"
#include "effect_compiler.h"
#include "value_evaluator.h"

namespace godot::nbsource {
using nbcompiler::Compiler;
using nbcompiler::equal;
using nbcompiler::named;
Dictionary module_programs(const Dictionary &plan, const Dictionary &packet, Dictionary result) {
    Array errors = result["errors"];
    Dictionary source_stages = plan["stages"], result_stages = result["stages"],
               result_math = result["cpu_math"], result_curves = result["curves"];
    Ref<Resource> emitter = plan["source"];
    String backend = plan["backend"];
    for (const char *stage_key : {"ParticleSpawn", "ParticleUpdate"}) {
        String stage_name = stage_key;
        Dictionary c = packet.get(stage_name, Dictionary());
        if (!bool(c.get("complete", false)) || !Array(c.get("diagnostics", Array())).is_empty() ||
            String(c.get("hlsl_sha256", "")).length() != 64) {
            errors.append("Incomplete/unpaired compiled RNG stage: " + stage_name);
            continue;
        }
        Array calls = Array(c.get("calls", Array())).duplicate(true), used;
        Dictionary bindings, math_bindings;
        if (backend == "gpu_compute" && stage_name == "ParticleSpawn") {
            int64_t count = result["gpu_spawn_unseeded_calls"];
            for (int i = 0; i < calls.size(); ++i) {
                Dictionary call = calls[i];
                if (equal(call.get("random_mode", "seeded"), String("unseeded")))
                    count += int64_t(call.get("unseeded_draw_count", 1));
            }
            result["gpu_spawn_unseeded_calls"] = int64_t(c.get("unseeded_draw_count", count));
        }
        Array operations = source_stages.get(stage_name, Array());
        for (int i = 0; i < operations.size(); ++i) {
            Ref<Resource> op = operations[i];
            if (!bool(op->get("enabled")))
                continue;
            String module = op->get("module_name"), alias = op->get("alias_name"), id = op->get("stable_id");
            if (alias.is_empty())
                alias = module.get_slice("@", 0).get_file();
            Dictionary reader = Dictionary(plan.get("gpu_readers", Dictionary())).get(id, Dictionary());
            if (!reader.is_empty()) alias = reader["alias"];
            if (backend == "cpu" && c.has("vm_math")) {
                Dictionary math = c["vm_math"];
                if (!bool(math.get("complete", false)) ||
                    !equal(math.get("hlsl_sha256", ""), c["hlsl_sha256"]) ||
                    String(math.get("vm_sha256", "")).length() != 64)
                    errors.append("Unverified native VM arithmetic scope.");
                else {
                    Dictionary scopes = math.get("scopes", Dictionary());
                    if (scopes.has(alias + "_Emitter_Func"))
                        math_bindings[id] = Dictionary(scopes[alias + "_Emitter_Func"]).duplicate(true);
                }
            }
            if (module == "ShapeLocation") {
                String shape = Compiler::mode(op, "ShapePrimitive", "Sphere");
                if (shape == "Cylinder") {
                    bind(calls, used, bindings, id + "/$CylinderPoint", "Local." + alias + ".RandomVector01",
                         3, errors);
                    bind(calls, used, bindings, id + "/$CylinderBand",
                         "Local." + alias + ".Random0to1Float_01", 1, errors);
                    bind(calls, used, bindings, id + "/$CylinderTop", "Local." + alias + ".RandomBool", 1,
                         errors);
                    bind(calls, used, bindings, id + "/$CylinderSurface",
                         "Local." + alias + ".Random0to2Float_01", 1, errors);
                } else if (shape == "Sphere")
                    bind_intrinsic(calls, used, bindings, id + "/$ShapeSphere", alias,
                                   "CalculateRandomPointInConeSphereIntersection_v1_1_", 1, 3, errors);
                else
                    errors.append("Intrinsic geometric RNG mapping is not implemented for this ShapeLocation "
                                  "primitive.");
            }
            if (module == "BoxLocation") {
                if (bool(Compiler::input(op, "SurfaceOnly", false))) {
                    bind_intrinsic(calls, used, bindings, id + "/$BoxShellPoint", alias, "RandomPointInBox_", 4, 1, errors);
                    bind_intrinsic(calls, used, bindings, id + "/$BoxShellFace", alias, "RandomPointInBox_", 1, 1, errors);
                } else
                    bind_intrinsic(calls, used, bindings, id + "/$BoxPoint", alias, "RandomPointInBox_", 3, 1, errors);
            }
            if (module == "V2/SphereLocation")
                bind_intrinsic(calls, used, bindings, id + "/$Sphere", alias, "CalculateRandomPointInSphere_",
                               3, 1, errors);
            if (module == "TorusLocation" &&
                Compiler::mode(op, "TorusDistributionMode", "Random") == "Random")
                for (const char *field : {"Radius", "U", "V"})
                    bind(calls, used, bindings, id + "/$Torus" + field, "Local." + alias + ".Random" + field,
                         1, errors);
            if (module == "AddVelocityFromPoint") {
                bind_intrinsic(calls, used, bindings, id + "/$PointDirection", alias, "RandomUnitVector_", 2,
                               1, errors);
                bind_intrinsic(calls, used, bindings, id + "/$PointUnused", alias, "RandomUnitVector_", 1, 1,
                               errors);
            }
            if (module == "AddVelocity" && Compiler::mode(op, "VelocityMode", "Linear") == "InCone")
                bind_intrinsic(calls, used, bindings, id + "/$Cone", alias, "", 1, 4, errors);
            if (named(module, {"V2/CurlNoiseForce@1.0", "V2/CurlNoiseForce@1.1", "V2/CurlNoiseForce@1.2"}))
                bind_curl(calls, used, bindings, op, alias, result["seed"], errors);
            if (module == "V3/InitialMeshOrientation" &&
                Compiler::mode(op, "MeshOrientationMode", "Random") == "Random")
                bind_orientation(calls, used, bindings, op, alias, errors);
            if (module == "V2/InitializeParticle") {
                Array required;
                if (Compiler::mode(op, "ColorMode", "DirectSet") == "RandomHue/Saturation/Value")
                    for (const char *component : {"Hue", "Saturation", "Value", "Alpha"})
                        if (bool(Compiler::input(op, String("Adjust") + component,
                                                 String(component) == "Hue")))
                            bind_color(calls, used, bindings, id + "/$Color" + component, alias, component,
                                       errors);
                if (bool(Compiler::input(op, "WriteMaterialRandom", false)))
                    required.append(Array::make("MaterialRandom", 1, "Particles.MaterialRandom"));
                if (Compiler::mode(op, "LifetimeMode", "DirectSet") == "Random")
                    required.append(Array::make("Lifetime", 1, "Particles.Lifetime"));
                String size = Compiler::mode(op, "SpriteSizeMode", "Uniform");
                if (named(size, {"RandomUniform", "RandomNonUniform"}))
                    required.append(Array::make("SpriteSize", size == "RandomNonUniform" ? 2 : 1,
                                                "Particles.SpriteSize"));
                if (Compiler::mode(op, "MassMode", "Unset") == "Random")
                    required.append(Array::make("Mass", 1, "Particles.Mass"));
                String mesh = Compiler::mode(op, "MeshScaleMode", "Unset");
                if (named(mesh, {"RandomUniform", "RandomNonUniform"}))
                    required.append(
                        Array::make("MeshScale", mesh == "RandomNonUniform" ? 3 : 1, "Particles.Scale"));
                String uv = Compiler::mode(op, "SpriteUVMode", "Unset");
                if (named(uv, {"RandomX", "RandomX/Y"}))
                    required.append(Array::make("UVFlip", 1, "Particles.UVScale.x"));
                if (named(uv, {"RandomY", "RandomX/Y"}))
                    required.append(Array::make("UVFlipY", 1, "Particles.UVScale.y"));
                if (Compiler::mode(op, "SpriteRotationMode", "Unset") == "Random")
                    required.append(Array::make("SpriteRotation", 1, "Particles.SpriteRotation"));
                for (int r = 0; r < required.size(); ++r) {
                    Array row = required[r];
                    bind(calls, used, bindings, id + "/$" + String(row[0]), row[2], row[1], errors);
                }
            }
            Array inputs = op->get("inputs");
            for (int n = 0; n < inputs.size(); ++n) {
                Ref<Resource> input = inputs[n];
                Variant value = input->get("value");
                if (!reader.is_empty() && value.get_type() == Variant::DICTIONARY &&
                    Dictionary(value).get("name","") == Variant("V2/RandomVector")) {
                    String pin = input->get("input_name"), key = id + "/" + pin + "/0";
                    bind(calls, used, bindings, key, alias + "." + pin, 2, errors);
                    if (!bool(Dictionary(bindings.get(key,Dictionary())).get("unit_sphere_draw",false)))
                        errors.append("Unverified random unit-vector implementation: " + alias + "." + pin);
                    continue;
                }
                int64_t count = random_count(value);
                if (count == 0)
                    continue;
                bool assignment = equal(op->get("operation_kind"), String("assignment"));
                String name = input->get("input_name"),
                       path = assignment ? id + "/assignment" : id + "/" + name + "/0";
                Array leaves = random_leaves(value, path);
                if (count != 1 || leaves.size() != 1) {
                    errors.append(
                        "Multiple composed RNG expressions require unambiguous compiled value mapping: " +
                        alias + "." + name);
                    continue;
                }
                Dictionary leaf = leaves[0];
                int components =
                    equal(Dictionary(leaf["value"])["name"], String("RandomRangeVector")) ? 3 : 1;
                bind(calls, used, bindings, leaf["path"], assignment ? module : alias + "." + name,
                     components, errors);
            }
        }
        for (int i = 0; i < calls.size(); ++i) {
            Dictionary call = calls[i];
            if (!used.has(i) && !bool(call.get("unused_circle_draw",false)))
                errors.append("Unmapped compiled RNG call at line " + call.get("line", 0).stringify() +
                              " in " + stage_name);
            if (equal(call.get("random_mode", "seeded"), String("unseeded"))) {
                if (backend != "gpu_compute")
                    errors.append("Native unseeded CPU RNG requires VectorVM stream/batch execution: " +
                                  stage_name + "/" + call.get("line", 0).stringify());
                else if (call.get("unseeded_call_index", Variant()).get_type() != Variant::INT ||
                         int64_t(call["unseeded_call_index"]) < 1 || !call.has("counter") ||
                         int64_t(call["counter"]) != 0)
                    errors.append("Unseeded GPU call lacks an independent counter.");
                else
                    result["uses_unseeded_gpu"] = true;
                continue;
            }
            if (String(call.get("seed3_expression", "")).ends_with(".Engine.Emitter.InstanceSeed")) {
                call["verified_instance_seed"] = true;
                result["uses_instance_seed"] = true;
                int64_t seed_max = result["instance_seed_max"];
                if (seed_max < 1 || seed_max > 2147483647)
                    errors.append("InstanceSeed requires the original platform RAND_MAX export.");
            }
            if ((call.get("call_id", Variant()).get_type() != Variant::INT &&
                 !call.has("verified_seed2_constant") && !bool(call.get("verified_tick_seed", false))) ||
                int64_t(call.get("counter", 0)) <= 0 ||
                !named(call.get("primitive", ""), {"rand", "rand_float", "rand_int"}))
                errors.append("Unsupported compiled RNG primitive/CallID/counter.");
            if ((!String(call.get("seed1_expression", "")).ends_with(".Particles.UniqueID") &&
                 !call.has("verified_seed1_constant") && !bool(call.get("verified_seed1_emitter", false))) ||
                (!String(call.get("seed3_expression", "")).ends_with(".Emitter.RandomSeed") &&
                 !call.has("verified_seed3_constant") && !bool(call.get("verified_instance_seed", false))))
                errors.append("Compiled RNG seed assembly is outside Particle UniqueID / emitter seed or "
                              "verified intrinsic constant-seed support.");
        }
        result_stages[stage_name] = bindings;
        result_math[stage_name] = math_bindings;
        result_curves[stage_name] = bind_curves(operations, c, emitter->get("display_name"), errors);
    }
    if (backend == "cpu" &&
        !equal(plan.get("spawn_update_mode", "RunUpdateScript"), String("NoInterpolation"))) {
        Dictionary math = Dictionary(packet.get("ParticleSpawn", Dictionary())).get("vm_math", Dictionary()),
                   first;
        if (bool(math.get("complete", false))) {
            Array operations = source_stages.get("ParticleUpdate", Array());
            Dictionary scopes = math["scopes"];
            for (int i = 0; i < operations.size(); ++i) {
                Ref<Resource> op = operations[i];
                String alias = op->get("alias_name");
                if (alias.is_empty())
                    alias = String(op->get("module_name")).get_slice("@", 0).get_file();
                if (scopes.has(alias + "_Emitter_Func"))
                    first[op->get("stable_id")] = Dictionary(scopes[alias + "_Emitter_Func"]).duplicate(true);
            }
        }
        result_math["InterpolatedUpdate"] = first;
    }
    result["supported"] = errors.is_empty();
    return result;
}
} // namespace godot::nbsource
