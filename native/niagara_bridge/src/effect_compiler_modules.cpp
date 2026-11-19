#include "effect_compiler.h"
#include "attribute_values.h"
#include "runtime_control.h"
#include "value_evaluator.h"
#include <cmath>

namespace godot::nbcompiler {
void Compiler::validate_operation(const Ref<Resource> &emitter, const Ref<Resource> &op, Dictionary plan,
                                  Dictionary result, const Dictionary &p, const Dictionary &scoped,
                                  const Dictionary &options) const {
    String name = op->get("module_name");
    Dictionary types = plan["attribute_types"];
    auto error = [&](const String &message) {
        issue(result, emitter, op, "error", message);
        plan["runnable"] = false;
    };
    auto errors = [&](const Array &messages) {
        for (int i = 0; i < messages.size(); ++i)
            error(messages[i]);
    };
    auto check_signed_pin = [&](const String &pin) {
        Variant value = input(op, pin, 0);
        if (type(value, types) != "int" ||
            (number(value) && (double(value) < INT32_MIN || double(value) > INT32_MAX)))
            error(name + " requires a signed 32-bit integer for " + pin);
    };
    if (name == "EmitterState") {
        for (const char *feature :
             {"EnableDistanceCulling", "EnableVisibilityCulling", "bApplySpawnCountScaleByDistance"})
            if (bool(input(op, feature, false)))
                error(String("EmitterState view/scalability execution is not implemented: ") + feature);
        if (bool(input(op, "ScaleSpawnCount", false)) && !equal(input(op, "SpawnCountScale", 1.0), 1.0))
            error("EmitterState spawn-count scalability is not implemented.");
        if (plan.has("lifecycle"))
            error("Multiple EmitterState modules are not implemented.");
        Dictionary lifecycle = NBNativeLifecycle::configuration(op);
        plan["lifecycle"] = lifecycle;
        plan["loop"] = equal(lifecycle["behavior"], String("Infinite"));
        errors(lifecycle_errors(op, scoped, types, false));
    }
    if (name == "KillParticles" && type(input(op, "KillParticles", true), types) != "bool")
        error("KillParticles requires a supported Boolean expression.");
    if (named(name, {"BoxLocation", "KillParticlesInVolume", "WindForce@1.0"})) {
        auto check = [&](const String &pin, const Variant &fallback, const String &kind) {
            String actual = type(input(op, pin, fallback), types);
            bool valid = kind == "vec3" ? named(actual, {"vec3", "position"}) :
                         kind == "float" ? named(actual, {"int", "float"}) : actual == kind;
            if (!valid) error(name + " requires " + kind + " for " + pin);
        };
        if (name == "BoxLocation") {
            check("BoxSize", Array::make(100,100,100), "vec3");
            check("BoxOrigin", reference("Particles.Position"), "vec3");
            check("BoxOffset", Array::make(0,0,0), "vec3");
            if (!named(mode(op, "CoordinateSpace", "Local"), {"Local","World","Simulation","SimulationSpace"}))
                error("Unsupported BoxLocation coordinate space.");
            for (const char *pin : {"MaskSpawnGroup","OverrideRandomness","OverrideSeed","FixedRandomSeed"})
                if (input(op, pin, false).get_type() != Variant::BOOL) error(String("BoxLocation requires a constant boolean for ") + pin);
            check_signed_pin("SpawnGroupMask");
            check_signed_pin("RandomSeed");
            if (bool(input(op, "OverrideRandomness", false)) &&
                !named(mode(op, "RandomnessMode", "SimulationDefaults"), {"SimulationDefaults", "NewEnumerator0", "Deterministic", "Determinisitic", "NewEnumerator1"}))
                error("Unsupported BoxLocation randomness mode.");
            if (bool(input(op, "SurfaceOnly", false))) {
                check("SurfaceOnlyBandThickness", 0.0, "float");
                if (!named(mode(op, "SurfaceExpansionMode", "Inside"), {"Inside","Centered","Outside","NewEnumerator0"}))
                    error("Unsupported BoxLocation surface expansion mode.");
            }
            if (input(op, "RotateBox", false).get_type() != Variant::BOOL) error("RotateBox requires a constant switch.");
            check("RotateBoxOnAxis", Array::make(0,0,1), "vec3");
            check("NormalizedBoxRotationAngle", 0.0, "float");
        } else if (name == "KillParticlesInVolume") {
            String shape = mode(op, "KillShape", "Sphere");
            if (shape == "Box") check("BoxSize", Array::make(100,100,100), "vec3");
            else if (shape == "Sphere") check("SphereRadius", 100.0, "float");
            else if (shape == "Plane") check("PlaneNormal", Array::make(0,0,1), "vec3");
            else if (shape == "Slab") {
                check("SlabAxis", Array::make(0,0,1), "vec3");
                check("SlabWidth", 100.0, "float");
            } else if (shape == "Cone") {
                check("ConeAxis", Array::make(1,0,0), "vec3");
                check("ConeAngle", 45.0, "float");
                check("ConeLength", 200.0, "float");
            } else error("Unknown kill volume shape: " + shape);
            check("Position", reference("Particles.Position"), "vec3");
            check("VolumeOrigin", reference("Engine.Emitter.SimulationPosition"), "vec3");
            check("OriginOffset", Array::make(0,0,0), "vec3");
            check("InvertVolume", false, "bool");
            check("KillVolumeEnabled", true, "bool");
        } else {
            check("WindSpeed", Array::make(100,0,0), "vec3");
            check("WindSpeedScale", 1.0, "float");
            check("AirResistance", 0.2, "float");
        }
    }
    if (name == "AddVelocity@1.0")
        for (const char *key : {"Velocity", "ScaleAddedVelocity"})
            if (type(input(op, key,
                           String(key) == "ScaleAddedVelocity" ? Array::make(1, 1, 1) : Array::make(0, 0, 0)),
                     types) != "vec3")
                error(String("AddVelocity1.0 requires a vector input: ") + key);
    if (name == "AddVelocityFromPoint") {
        Dictionary defaults;
        defaults["VelocityOrigin"] = reference("Engine.Emitter.SimulationPosition");
        defaults["DefaultPosition"] = reference("Particles.Position");
        defaults["OriginOffset"] = Array::make(0, 0, 0);
        defaults["VelocityStrength"] = 100.0;
        defaults["VelocityFalloffDistance"] = 100.0;
        defaults["UseFalloffDistance"] = false;
        defaults["InvertVelocityFalloff"] = true;
        Array keys = defaults.keys();
        for (int i = 0; i < keys.size(); ++i) {
            String key = keys[i], actual = type(input(op, key, defaults[key]), types);
            bool valid = named(key, {"VelocityOrigin", "DefaultPosition", "OriginOffset"})
                             ? named(actual, {"vec3", "position"})
                         : named(key, {"UseFalloffDistance", "InvertVelocityFalloff"})
                             ? actual == "bool"
                             : named(actual, {"float", "int"});
            if (!valid)
                error("AddVelocityFromPoint input has an unsupported type: " + key);
        }
        if (!named(mode(op, "OriginOffsetCoordinateSpace", "Local"),
                   {"World", "Local", "Simulation", "SimulationSpace"}))
            error("Unsupported point velocity offset coordinate space.");
    }
    if (name == "AddVelocity") {
        String velocity = mode(op, "VelocityMode", "Linear");
        if (!named(velocity, {"Linear", "InCone"}) || mode(op, "RotationMode", "Default") != "Default")
            error("Only Linear/InCone velocity with Default rotation is implemented.");
        if (velocity == "InCone" &&
            (mode(op, "ConeAngleMode", "Degrees") != "Degrees" ||
             bool(input(op, "UseVelDistribution", false)) || bool(input(op, "UseConeFalloff", false)) ||
             bool(input(op, "OverrideSeed", false))))
            error(
                "Cone velocity supports degrees, constant radial distribution and no falloff/seed override.");
    }
    if (name == "V2/SphereLocation") {
        if (mode(op, "SphereDistribution", "Random") != "Random" ||
            bool(input(op, "UseSphereOrientation", false)) || bool(input(op, "OverrideSeed", false)) ||
            bool(input(op, "MaskSpawnGroup", false)) || !bool(input(op, "WritetoIntrinsicVariables", true)))
            error("SphereLocation supports random volume/shell and hemisphere samples, without "
                  "orientation/seed/group overrides, and with intrinsic position writes.");
        if (!named(mode(op, "SurfaceExpansionMode", "Inside"), {"Inside", "Outside", "NewEnumerator0"}))
            error("Unsupported sphere surface expansion mode.");
        for (const char *key : {"SphereCoordinateSpace", "OffsetCoordinateSpace"})
            if (!named(mode(op, key, "Local"), {"Local", "World", "Simulation", "SimulationSpace"}))
                error(String("Unsupported sphere coordinate space: ") + key);
        Dictionary defaults;
        defaults["SphereRadius"] = 100.0;
        defaults["SurfaceOnlyBandThickness"] = 0.0;
        defaults["NormalizeThickness"] = true;
        defaults["SphereOrigin"] = reference("Particles.Position");
        defaults["Offset"] = Array::make(0, 0, 0);
        defaults["NonUniformScale"] = Array::make(1, 1, 1);
        Array keys = defaults.keys();
        for (int i = 0; i < keys.size(); ++i) {
            String key = keys[i], actual = type(input(op, key, defaults[key]), types);
            bool valid = named(key, {"SphereRadius", "SurfaceOnlyBandThickness"})
                             ? named(actual, {"float", "int"})
                         : key == "NormalizeThickness" ? actual == "bool"
                                                       : named(actual, {"vec3", "position"});
            if (!valid)
                error("SphereLocation input has an unsupported type: " + key);
        }
    }
    if (name == "ShapeLocation")
        errors(shape_errors(op, p));
    if (name == "TorusLocation")
        errors(torus_errors(op, types));
    if (name == "V2/InitializeParticle" &&
        !named(mode(op, "MassMode", "NewEnumerator0"), {"NewEnumerator0", "Unset", "DirectSet", "Random"}))
        error("Only verified Unset / Mass of 1, DirectSet and Random mass initialization branches are "
              "implemented.");
    if (name == "V2/InitializeParticle") {
        Dictionary branches;
        branches["LifetimeMode"] = Array::make("DirectSet", "Random");
        branches["ColorMode"] = Array::make("DirectSet", "Unset", "RandomHue/Saturation/Value");
        branches["PositionMode"] = Array::make("SimulationPosition");
        branches["SpriteSizeMode"] =
            Array::make("Uniform", "RandomUniform", "RandomNonUniform", "NonUniform", "DirectSet", "Unset");
        branches["SpriteRotationMode"] = Array::make("Unset", "Random");
        branches["SpriteUVMode"] = Array::make("Unset", "RandomX", "RandomY", "RandomX/Y", "Custom");
        branches["MeshRendererArrayVisibilityMode"] = Array::make("Unset");
        branches["RibbonWidthMode"] = Array::make("Unset", "DirectSet");
        branches["RibbonFacingMode"] = Array::make("Unset");
        branches["RibbonTwistMode"] = Array::make("Unset");
        Array keys = branches.keys();
        for (int i = 0; i < keys.size(); ++i) {
            Array allowed = branches[keys[i]];
            if (!allowed.has(mode(op, keys[i], allowed[0])))
                error("Unsupported InitializeParticle branch: " + String(keys[i]));
        }
    }
    if (name == "V2/InitializeParticle" &&
        !named(mode(op, "MeshScaleMode", "Unset"),
               {"Unset", "Uniform", "NonUniform", "RandomUniform", "RandomNonUniform"}))
        error("Unsupported mesh scale initialization mode.");
    if (named(name, {"V3/InitialMeshOrientation@1.0", "V3/UpdateMeshOrientation@1.0"}))
        errors(orientation_errors(op, types));
    if (named(name, {"V3/InitialMeshOrientation", "V3/UpdateMeshOrientation"})) {
        Dictionary allowed;
        allowed["RotationCoordinateSpace"] =
            Array::make("Mesh", "Local", "World", "Simulation", "SimulationSpace");
        if (name == "V3/InitialMeshOrientation") {
            allowed["MeshOrientationMode"] = Array::make("None", "Random", "OrienttoVector");
            allowed["OrientationCoordinateSpace"] =
                Array::make("Local", "World", "Simulation", "SimulationSpace");
        } else {
            allowed["OrientationMethod"] = Array::make("RotationRate", "OrienttoPosition");
            allowed["WritetoOrientation"] = Array::make("Apply");
            if (mode(op, "OrientationMethod", "RotationRate") == "OrienttoPosition") {
                allowed["StabilizationMode"] =
                    Array::make("Facing", "Facing/Side", "Facing/Up", "Facing/Side/Up");
                for (const char *key : {"FacingOriginCoordinateSpace", "FacingPositionCoordinateSpace",
                                        "SideCoordinateSpace", "UpCoordinateSpace"})
                    allowed[key] = Array::make("Simulation", "SimulationSpace", "Local", "World");
                Array required =
                    Array::make("FacingOrigin", "FacingPosition", "FacingInterpolation", "StabilizationMode");
                if (bool(input(op, "OverrideQuat", false)))
                    required.append_array(Array::make("OverrideQuaternion", "OverrideIncomingOrientation"));
                for (int i = 0; i < required.size(); ++i)
                    if (input(op, required[i]).get_type() == Variant::NIL)
                        error("OrienttoPosition requires an explicit source/default binding for " +
                              String(required[i]) + ".");
            }
        }
        Array keys = allowed.keys();
        for (int i = 0; i < keys.size(); ++i) {
            Array modes = allowed[keys[i]];
            if (!modes.has(mode(op, keys[i], modes[0])))
                error("Mesh orientation branch is not implemented: " + String(keys[i]));
        }
    }
    if (named(name, {"V2/CurlNoiseForce@1.0", "V2/CurlNoiseForce@1.1", "V2/CurlNoiseForce@1.2"})) {
        errors(curl_errors(op, types));
        if (!bool(options.get("use_compiled_source_rng", false)))
            issue(result, emitter, op, "warning",
                  "Curl Noise uses a stable ordinary field phase. Exact source seed/counter matching is "
                  "available in paired validation; visual equivalence requires comparison.");
    }
    if (named(name, {"VortexForce", "VortexForce@1.1", "LineAttractionForce", "LineAttractionForce@1.0"})) {
        for (const char *key :
             {"WritetoIntrinsicParameters", "OriginPullOnlyOnVortexAxis", "UseInfluenceFalloff",
              "UseOriginOffset", "LineSegmentinLocalspace", "UseAttractionFalloff"})
            if (input(op, key, true).get_type() != Variant::BOOL)
                error(String("Force switch must be an authored boolean: ") + key);
        for (const char *key : {"VortexAxisCoordinateSpace", "VortexOriginOffsetCoordinateSpace"})
            if (!named(mode(op, key, "Simulation"), {"Simulation", "SimulationSpace", "World", "Local"}))
                error(String("Unsupported force coordinate space: ") + key);
    }
    if (named(name, {"AddVelocity", "AddVelocity@1.0", "GravityForce"})) {
        String key = name == "AddVelocity" ? "RotationCoordinateSpace" : "CoordinateSpace";
        if (!named(mode(op, key, name == "GravityForce" ? "World" : "Local"),
                   {"World", "Local", "Simulation", "SimulationSpace"}))
            error("Unsupported coordinate space.");
    }
    if (name == "SolveForcesAndVelocity" &&
        (bool(input(op, "ClampVelocity", false)) || bool(input(op, "LimitAcceleration", false)) ||
         bool(input(op, "ManuallyEnableRotationalSolver", false)) ||
         !bool(input(op, "WritetoIntrinsicProperties", true))))
        error(
            "Velocity/acceleration limits, rotational solving and non-intrinsic output are not implemented.");
    if (named(name, {"SpawnRate", "SpawnBurst_Instantaneous", "SpawnBurst_Instantaneous@1.0"})) {
        if (input(op, "SpawnCount", 1).get_type() == Variant::DICTIONARY ||
            input(op, "SpawnTime", 0.0).get_type() == Variant::DICTIONARY ||
            !rate_supported(input(op, "SpawnRate", 0.0), p) ||
            bool(input(op, "UseSpawnProbability", false)) || bool(input(op, "UseLoopCountLimit", false)))
            error("Spawn scheduling supports constant bursts and finite nonnegative constant/User scalar "
                  "rates; probability and loop-count limits remain unsupported.");
    }
    if (named(name, {"SpawnRate", "SpawnBurst_Instantaneous", "SpawnBurst_Instantaneous@1.0"}))
        check_signed_pin("SpawnGroup");
    if (name == "SpawnBurst_Instantaneous@1.0") {
        Variant count = input(op, "SpawnCount", 0), at = input(op, "SpawnTime", 0.0);
        if (!number(count) || !std::isfinite(double(count)) || double(count) != std::floor(double(count)) ||
            double(count) < 0.0 || double(count) > 2147483647.0)
            error("Burst1.0 requires a constant nonnegative signed32 integer SpawnCount.");
        if (!number(at) || !std::isfinite(double(at)))
            error("Burst1.0 requires a finite constant SpawnTime.");
        if (mode(op, "Age", reference("Emitter.LoopedAge")) != "Emitter.LoopedAge")
            error("Burst1.0 requires the source Emitter.LoopedAge binding.");
    }
    if (name == "ScaleSpriteSize" && mode(op, "ScaleSpriteSizeMode", "UniformCurve") != "UniformCurve")
        error("Only UniformCurve sprite scaling is implemented.");
    if (name == "ScaleSpriteSizeBySpeed" && bool(input(op, "SampleScaleFactorByCurve", false))) {
        Dictionary curve = NBNativeValues::resolve_curve(input(op, "ScaleFactorCurve"), p);
        if (!named(curve.get("kind", ""), {"curve", "curve_di"}))
            error("Speed scaling requires an explicit ScaleFactorCurve resource.");
    }
    if (name == "V2/SubUVAnimation@1.0" && mode(op, "SubUVAnimationMode", "Linear") != "Linear")
        error("Only the extracted SubUVAnimation 1.0 Linear branch is implemented.");
    if (named(name, {"DynamicMaterialParameters@1.0", "DynamicMaterialParameters@1.1"}))
        for (int index = 0; index < 4; ++index)
            for (int component = 1; component < 5; ++component) {
                String key = "Index" + String::num_int64(index) + "Param" + String::num_int64(component);
                if (!named(type(input(op, key, 0.0), types), {"float", "int"}))
                    error("Dynamic material component requires a scalar: " + key);
            }
}
} // namespace godot::nbcompiler
