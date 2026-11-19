#include "effect_compiler.h"
#include "effect_revision.h"
#include "attribute_values.h"
#include "particle_forces.h"
#include "value_evaluator.h"
#include "renderer_filter.h"
#include <cmath>

namespace godot::nbcompiler {
TypedArray<String> Compiler::orientation_errors(const Ref<Resource> &op, const Dictionary &types) const {
    TypedArray<String> errors;
    if (!named(mode(op, "RotationCoordinateSpace", "Mesh"),
               {"Mesh", "Local", "World", "Simulation", "SimulationSpace"}))
        errors.append("Unsupported legacy mesh rotation coordinate space.");
    bool initial = equal(op->get("module_name"), String("V3/InitialMeshOrientation@1.0"));
    if (initial) {
        if (!named(mode(op, "OrientationCoordinateSpace", "Simulation"),
                   {"Local", "World", "Simulation", "SimulationSpace"}))
            errors.append("Unsupported legacy mesh orientation coordinate space.");
        for (const char *key : {"UseOrientationVector", "UseRotationVector"})
            if (input(op, key, String(key) == "UseOrientationVector").get_type() != Variant::BOOL)
                errors.append(String("Legacy orientation switch requires a constant boolean: ") + key);
    }
    Array vectors = initial ? Array::make("Rotation", "OrientationVector", "OrientationAxis")
                            : Array::make("RotationVector");
    for (int i = 0; i < vectors.size(); ++i) {
        String key = vectors[i];
        if (type(input(op, key, key == "Rotation" ? Array::make(0, 0, 0) : Array::make(1, 0, 0)), types) !=
            "vec3")
            errors.append("Legacy mesh orientation requires a vector input: " + key);
    }
    if (!initial)
        for (const char *key : {"RotationRate", "DeltaTime"})
            if (!named(type(input(op, key,
                                  String(key) == "RotationRate" ? Variant(1.0)
                                                                : Variant(reference("Engine.DeltaTime"))),
                            types),
                       {"float", "int"}))
                errors.append(String("Legacy mesh orientation requires a scalar input: ") + key);
    return errors;
}
TypedArray<String> Compiler::torus_errors(const Ref<Resource> &op, const Dictionary &types) const {
    TypedArray<String> errors;
    if (!named(mode(op, "TorusMode", "Torus"), {"Torus", "Ring", "TorusKnot"}))
        errors.append("Unsupported TorusLocation branch: TorusMode");
    if (!named(mode(op, "TorusDistributionMode", "Random"), {"Random", "Direct"}))
        errors.append("Unsupported TorusLocation branch: TorusDistributionMode");
    if (!named(mode(op, "CoordinateSpace", "Local"), {"Local", "World", "Simulation", "SimulationSpace"}))
        errors.append("Unsupported TorusLocation branch: CoordinateSpace");
    for (const char *key : {"AlignTorusWithAxis", "EnableTorusKnotRadius", "EnableScale", "MaskSpawnGroup",
                            "OverrideRandomness", "OverrideSeed", "FixedRandomSeed"})
        if (input(op, key, false).get_type() != Variant::BOOL)
            errors.append(String("TorusLocation switch requires a constant boolean: ") + key);
    if (bool(input(op, "OverrideRandomness", false)) || bool(input(op, "OverrideSeed", false)) ||
        bool(input(op, "FixedRandomSeed", false)) ||
        !named(mode(op, "RandomnessMode", "SimulationDefaults"), {"SimulationDefaults", "NewEnumerator0"}))
        errors.append("TorusLocation random seed/mode overrides are not yet validated.");
    Dictionary defaults;
    defaults["RadiusPosition"] = 1.0;
    defaults["UPosition"] = 0.0;
    defaults["VPosition"] = 0.0;
    defaults["SurfacePosition"] = 0.0;
    defaults["UDistribution"] = 0.0;
    defaults["VDistribution"] = 0.0;
    defaults["LargeRadius"] = 100.0;
    defaults["HandleRadius"] = 25.0;
    defaults["KnotHelixLoops"] = 2;
    defaults["KnotRingLoops"] = 5;
    defaults["TorusKnotTubeRadius"] = 10.0;
    defaults["NormalizedAngleAroundTorusAxis"] = 0.0;
    defaults["SpawnGroupMask"] = 0;
    defaults["TorusAxis"] = Array::make(0, 0, 1);
    defaults["NonUniformScale"] = Array::make(1, 1, 1);
    defaults["Offset"] = Array::make(0, 0, 0);
    defaults["TorusOrigin"] = reference("Particles.Position");
    Array keys = defaults.keys();
    for (int i = 0; i < keys.size(); ++i) {
        String key = keys[i], actual = type(input(op, key, defaults[key]), types);
        Variant v = defaults[key];
        bool valid = v.get_type() == Variant::ARRAY || v.get_type() == Variant::DICTIONARY
                         ? named(actual, {"vec3", "position"})
                     : named(key, {"KnotHelixLoops", "KnotRingLoops", "SpawnGroupMask"})
                         ? actual == "int"
                         : named(actual, {"float", "int"});
        if (!valid)
            errors.append("TorusLocation input has an unsupported type: " + key);
    }
    return errors;
}
TypedArray<String> Compiler::shape_errors(const Ref<Resource> &op, const Dictionary &p) {
    TypedArray<String> errors;
    Dictionary defaults;
    defaults["ApplyToParticlePosition"] = "Apply";
    defaults["TransformOrder"] = "Scale/Rotate/Offset";
    defaults["ScaleMode"] = "Default";
    defaults["RotationMode"] = "Default";
    defaults["TransformMethod"] = "Default";
    defaults["RotationCoordinateSpace"] = "Local";
    defaults["RandomnessMode"] = "SimulationDefaults";
    Array keys = defaults.keys();
    String offset_mode = mode(op, "OffsetMode", "None");
    if (!named(offset_mode, {"Default", "None"}) ||
        (offset_mode == "Default" && !named(mode(op, "OffsetCoordinateSpace", "Local"),
                                            {"Local", "World", "Simulation", "SimulationSpace"})))
        errors.append("Unsupported ShapeLocation offset mode or coordinate space.");
    for (int i = 0; i < keys.size(); ++i)
        if (!equal(mode(op, keys[i], defaults[keys[i]]), defaults[keys[i]]))
            errors.append("ShapeLocation supports the documented Default/Local transform branch only: " +
                          String(keys[i]));
    if (bool(input(op, "OverrideSeed", false)) || bool(input(op, "MaskSpawnGroup", false)) ||
        !bool(input(op, "UseOwnerScale", true)) ||
        mode(op, "ApplyOwnerScale", "Engine.Owner.Scale") != "Engine.Owner.Scale")
        errors.append("ShapeLocation requires owner scale and no seed/spawn-group overrides.");
    String shape = mode(op, "ShapePrimitive", "Sphere");
    if (shape == "Sphere") {
        if (mode(op, "SphereDistribution", "Random") != "Random" ||
            mode(op, "HemisphereAngleType", "NormalizedAngle") != "NormalizedAngle" ||
            NBNativeValues::vector2(input(op, "HemisphereDistribution", Array::make(1, 1))) != Vector2(1, 1))
            errors.append("ShapeLocation Sphere supports random full-sphere distribution only.");
    } else if (shape == "Cylinder") {
        if (mode(op, "CylinderDistribution", "Random") != "Random" || !bool(input(op, "SurfaceOnly", true)) ||
            bool(input(op, "HemicircleX", false)) || bool(input(op, "HemicircleY", false)) ||
            bool(input(op, "EnableLatheProfile", false)))
            errors.append("ShapeLocation Cylinder supports random full-cylinder SurfaceOnly with optional "
                          "endcaps and band; hemispheres/lathe profiles are not implemented.");
        for (const char *key : {"CylinderRadius", "CylinderHeight"}) {
            Variant v = NBNativeValues::evaluate(input(op, key, 100.0), p);
            if (!number(v) || !std::isfinite(double(v)) || double(v) <= 0.0)
                errors.append(String("ShapeLocation Cylinder requires a positive finite constant/User ") +
                              key + "; degenerate surface-area divisions are unsupported.");
        }
    } else
        errors.append("ShapeLocation primitive is not implemented: " + shape);
    return errors;
}
TypedArray<String> Compiler::curl_errors(const Ref<Resource> &op, const Dictionary &types) const {
    TypedArray<String> errors;
    if (!NBNativeCurlNoise::valid_seed(input(op, "RandomSeed", 11)))
        errors.append("Curl Noise requires a constant signed32 RandomSeed.");
    if (NBNativeCurlNoise::quality(op) < 0)
        errors.append("Curl Noise procedural Ultra quality is not implemented yet.");
    for (const char *key : {"WritetoIntrinsicParameters", "RandomizeNoiseSample",
                            "ScaleCurlLookupByEngineOwnerScale", "MaskCurlNoise", "PanNoise"})
        if (input(op, key, false).get_type() != Variant::BOOL)
            errors.append(String("Curl Noise switch requires a constant Boolean: ") + key);
    if (!bool(input(op, "WritetoIntrinsicParameters", true)) || bool(input(op, "MaskCurlNoise", false)))
        errors.append("Curl Noise cone mask and output-only branches are not implemented yet.");
    if (!named(mode(op, "CoordinateSpace", "World"), {"World", "Local", "Simulation", "SimulationSpace"}))
        errors.append("Unsupported Curl Noise coordinate space.");
    for (const char *key : {"NoiseStrength", "NoiseFrequency"})
        if (!named(type(input(op, key, 10.0), types), {"float", "int"}))
            errors.append(String("Curl Noise requires a scalar input: ") + key);
    for (const char *key : {"RandomizationVector", "SamplePosition", "PanNoiseField"})
        if (!NBNativeAttributeValues::compatible(
                type(input(op, key,
                           String(key) == "SamplePosition" ? Variant(reference("Particles.Position"))
                                                           : Variant(Array::make(0.0, 0.0, 0.0))),
                     types),
                "vec3"))
            errors.append(String("Curl Noise requires a vector/position input: ") + key);
    return errors;
}
String Compiler::camera_offset(const Ref<Resource> &renderer) {
    auto *revision = NBNativeEffectRevision::current_revision;
    uint64_t id = renderer->get_instance_id();
    if (revision) {
        auto cached = revision->saved_camera_offsets.find(id);
        if (cached != revision->saved_camera_offsets.end())
            return cached->second;
    }
    String reference = "Particles.CameraOffset";
    Array bindings = renderer->get("bindings");
    for (int i = 0; i < bindings.size(); ++i) {
        Dictionary b = bindings[i];
        if (equal(b.get("name", ""), String("CameraOffset"))) {
            reference = bool(b.get("enabled", true)) ? NBNativeValues::enum_name(b.get("value", "None")) : String("None");
            break;
        }
    }
    if (revision) {
        Ref<Script> script = renderer->get_script();
        if (script.is_valid() && script->get_path() == "res://addons/angara_fx/document/nb_renderer.gd")
            revision->saved_camera_offsets.emplace(id, reference);
    }
    return reference;
}
TypedArray<String> Compiler::camera_errors(const Ref<Resource> &renderer, const Dictionary &types,
                                           const Dictionary &p, const Dictionary &layout) const {
    TypedArray<String> errors;
    if (!named(renderer->get("renderer_type"), {"SpriteRenderer", "MeshRenderer"}) ||
        mode(renderer, "SourceMode", "Particles") == "Emitter")
        return errors;
    String name = camera_offset(renderer);
    if (named(name, {"None", "", "Particles.CameraOffset"}))
        return errors;
    String actual = builtins.has(name) ? String(Array(builtins[name])[0]) : String(types.get(name, ""));
    if (actual != "float" ||
        !(name.begins_with("Particles.") || name.begins_with("Emitter.") || name.begins_with("User.")))
        errors.append("CameraOffset binding requires a supported declared float attribute: " + name);
    else if (name.begins_with("Emitter.") && !Dictionary(layout.get("declarations", Dictionary())).has(name))
        errors.append("CameraOffset emitter binding requires an explicitly authored float variable: " + name);
    else if (name.begins_with("User.") &&
             (!p.has(name) || !number(p[name]) || !std::isfinite(double(p[name]))))
        errors.append("CameraOffset user binding requires a supplied finite float value: " + name);
    return errors;
}
TypedArray<String> Compiler::written_errors(const Ref<Resource> &renderer, const Dictionary &layout) const {
    TypedArray<String> errors;
    Array unsupported;
    if (equal(renderer->get("renderer_type"), String("RibbonRenderer"))) {
        unsupported =
            Array::make("Particles.VisibilityTag", "Particles.RendererVisibilityTag", "Particles.RibbonID",
                        "Particles.RibbonLinkOrder", "Particles.RibbonTwist");
        bool distance = false;
        for (int channel = 0; channel < 2; ++channel) {
            String n = String::num_int64(channel);
            Dictionary uv = external("ribbons", "settings", Array::make(renderer, "UV" + n + "Settings"));
            distance =
                distance || equal(uv.get("distributionMode", ""), String("TiledFromStartOverRibbonLength"));
            if (bool(uv.get("bEnablePerParticleUOverride", false)))
                unsupported.append("Particles.RibbonU" + n + "Override");
            if (bool(uv.get("bEnablePerParticleVRangeOverride", false)))
                unsupported.append("Particles.RibbonV" + n + "RangeOverride");
        }
        if (distance)
            unsupported.append("Particles.RibbonUVDistance");
    }
    Dictionary declarations = layout["declarations"];
    for (int i = 0; i < unsupported.size(); ++i)
        if (declarations.has(unsupported[i]))
            errors.append("A written attribute requires unsupported renderer consumption: " +
                          String(unsupported[i]));
    return errors;
}
Dictionary Compiler::spawn_order(const Ref<Resource> &emitter) {
    Dictionary result, order;
    Array errors;
    result["order"] = order;
    result["errors"] = errors;
    Dictionary lineage = emitter->get("source_lineage");
    if (!bool(lineage.get("compiled_spawn_order_available", false)))
        return result;
    Variant raw = lineage.get("compiled_spawn_attributes", Variant());
    if (raw.get_type() != Variant::ARRAY || !bool(lineage.get("verified", false))) {
        errors.append("Compiled SpawnInfo order requires verified source lineage.");
        return result;
    }
    Ref<Resource> stage = emitter->call("get_stage", "EmitterUpdate");
    if (stage.is_null())
        return result;
    Array rows = raw, used, ops = stage->get("operations");
    String prefix = String(lineage.get("native_unique_name", "")) + ".";
    for (int i = 0; i < ops.size(); ++i) {
        Ref<Resource> op = ops[i];
        String name = op->get("module_name");
        if (!bool(op->get("enabled")) ||
            !named(name, {"SpawnRate", "SpawnBurst_Instantaneous", "SpawnBurst_Instantaneous@1.0"}))
            continue;
        String alias = op->get("alias_name");
        if (alias.is_empty())
            alias = name.get_slice("@", 0).get_file();
        Array matches;
        for (int r = 0; r < rows.size(); ++r)
            if (rows[r].get_type() == Variant::STRING && String(rows[r]).begins_with(prefix + alias + "."))
                matches.append(r);
        if (matches.size() != 1 || used.has(matches[0])) {
            errors.append("Missing/ambiguous native SpawnInfo binding for " + alias);
            continue;
        }
        used.append(matches[0]);
        order[op->get("stable_id")] = matches[0];
    }
    return result;
}
Vector3 Compiler::mesh_scale(const Ref<Resource> &emitter) {
    Array stages = emitter->get("stages");
    for (int i = 0; i < stages.size(); ++i) {
        Ref<Resource> stage = stages[i];
        if (!named(stage->get("stage_name"), {"ParticleSpawn", "ParticleUpdate"}))
            continue;
        Array ops = stage->get("operations");
        for (int o = 0; o < ops.size(); ++o) {
            Ref<Resource> op = ops[o];
            if (!bool(op->get("enabled")))
                continue;
            if (named(op->get("module_name"), {"Particles.Scale", "ScaleMeshSize"}) ||
                (equal(op->get("module_name"), String("V2/InitializeParticle")) &&
                 mode(op, "MeshScaleMode", "Unset") != "Unset"))
                return Vector3();
        }
    }
    return Vector3(1, 1, 1);
}
int64_t Compiler::dynamic_mask(const Dictionary &plan, const Ref<Resource> &renderer) const {
    int64_t mask = 0;
    for (int index = 0; index < 4; ++index) {
        String suffix = index == 0 ? String() : String::num_int64(index), key = "DynamicMaterial" + suffix,
               expected = "Particles.DynamicMaterialParameter" + suffix;
        String name = NBNativeRendererFilter::binding_reference(renderer, key, key + "Binding", expected);
        bool emitter_mode = mode(renderer, "SourceMode", "Particles") == "Emitter";
        if (named(renderer->get("renderer_type"), {"SpriteRenderer", "MeshRenderer"}) &&
            (emitter_mode || name != expected)) {
            if (!named(name, {"", "None"})) {
                bool host = NBNativeRendererFilter::host_binding(renderer, key, key + "Binding", name),
                     particle = name.begins_with("Particles.") && !emitter_mode;
                Variant raw = input(renderer, key + "Binding");
                Dictionary types =
                    plan.get("renderer_parameter_types", plan.get("attribute_types", Dictionary()));
                if ((host || particle) &&
                    (types.has(name) ||
                     (raw.get_type() == Variant::DICTIONARY &&
                      equal(Dictionary(raw).get("bBindingExistsOnSource", Variant()), true))))
                    mask |= int64_t(15) << (index * 4);
            }
            continue;
        }
        bool enabled = true;
        Array bindings = renderer->get("bindings");
        for (int i = 0; i < bindings.size(); ++i) {
            Dictionary b = bindings[i];
            if (equal(b.get("name", ""), key))
                enabled =
                    bool(b.get("enabled", true)) && NBNativeValues::enum_name(b.get("value", "")) != "None";
        }
        if (!enabled)
            continue;
        if (bool(plan.get("native_vm_requested", false)) &&
            Dictionary(Dictionary(plan["attribute_layout"])["declarations"]).has(expected)) {
            mask |= int64_t(15) << (index * 4);
            continue;
        }
        Dictionary stages = plan["stages"];
        for (const char *stage : {"ParticleSpawn", "ParticleUpdate"}) {
            Array ops = stages.get(stage, Array());
            for (int i = 0; i < ops.size(); ++i) {
                Ref<Resource> op = ops[i];
                if (!bool(op->get("enabled")))
                    continue;
                if (equal(op->get("operation_kind"), String("assignment")) &&
                    equal(op->get("module_name"), expected))
                    mask |= int64_t(15) << (index * 4);
                if (named(op->get("module_name"),
                          {"DynamicMaterialParameters@1.0", "DynamicMaterialParameters@1.1"}) &&
                    bool(input(op, "WriteParameterIndex" + String::num_int64(index), index == 0)))
                    mask |= int64_t(15) << (index * 4);
            }
        }
    }
    return mask;
}
} // namespace godot::nbcompiler
