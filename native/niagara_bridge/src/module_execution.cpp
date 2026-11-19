#include "module_execution.h"
#include "attribute_values.h"
#include "module_math.h"
#include "particle_forces.h"
#include "value_evaluator.h"
#include <godot_cpp/core/class_db.hpp>
#include <algorithm>
#include <cmath>
namespace godot {
namespace {
Variant input(const Ref<Resource> &op, const String &name, const Variant &fallback, int occurrence = 0) {
    return op->call("get_input", name, fallback, occurrence);
}
Variant value(const Ref<Resource> &op, const String &name, const Variant &fallback, const Dictionary &ctx,
              int occurrence = 0) {
    return NBNativeValues::evaluate(input(op, name, fallback, occurrence), ctx,
                                    String(op->get("stable_id")) + String("/") + name + String("/") +
                                        String::num_int64(occurrence));
}
Dictionary curve(const Ref<Resource> &op, const String &name, const Dictionary &ctx) {
    String key = String(op->get("stable_id")) + String("/") + name + String("/0");
    Dictionary curves = ctx.get("source_curves", Dictionary());
    if (curves.has(key))
        return curves[key];
    return NBNativeValues::resolve_curve(input(op, name, Dictionary()), ctx);
}
double number(const Dictionary &p, const char *key) {
    return double(p.get(key, 0.));
}
} // namespace
void NBNativeParticleModules::_bind_methods() {
    ClassDB::bind_static_method("NBNativeParticleModules",
                                D_METHOD("spawn", "emitter", "birth_time", "birth_transform",
                                         "execution_index", "execution_count", "frame", "schema"),
                                &NBNativeParticleModules::spawn);
    ClassDB::bind_static_method("NBNativeParticleModules",
                                D_METHOD("update", "emitter", "delta", "time", "frame", "schema"),
                                &NBNativeParticleModules::update);
    ClassDB::bind_static_method("NBNativeParticleModules",
                                D_METHOD("execute", "operation", "particle", "context", "delta", "builtins"),
                                &NBNativeParticleModules::execute);
}
bool NBNativeParticleModules::execute(const Ref<Resource> &op, Dictionary p, Dictionary ctx, double delta,
                                      const Dictionary &builtins) {
    if (op.is_null())
        return false;
    if (String(op->get("operation_kind")) == "assignment") {
        Array inputs = op->get("inputs");
        if (!inputs.is_empty()) {
            Ref<Resource> first = inputs[0];
            Variant v = NBNativeValues::evaluate(first->get("value"), ctx,
                                                 String(op->get("stable_id")) + String("/assignment"));
            NBNativeAttributeValues::write_particle(
                p, op->get("module_name"), NBNativeAttributeValues::type_name(op->get("target_type")), v,
                builtins);
        }
        return true;
    }
    String name = op->get("module_name");
    if (name == "V2/CurlNoiseForce@1.0" || name == "V2/CurlNoiseForce@1.1" ||
        name == "V2/CurlNoiseForce@1.2") {
        p["force"] = Vector3(p.get("force", Vector3())) + NBNativeCurlNoise::force(op, ctx);
        return true;
    }
    if (name == "VortexForce" || name == "VortexForce@1.1") {
        Vector3 force = NBNativeAttractionForces::vortex(op, ctx);
        String alias = op->get("alias_name");
        if (alias.is_empty())
            alias = "VortexForce";
        ctx[String("Output.") + alias + String(".VortexForce")] = force;
        if (bool(input(op, "WritetoIntrinsicParameters", true)))
            p["force"] = Vector3(p.get("force", Vector3())) + force;
        return true;
    }
    if (name == "LineAttractionForce" || name == "LineAttractionForce@1.0") {
        p["force"] = Vector3(p.get("force", Vector3())) + NBNativeAttractionForces::line(op, ctx);
        return true;
    }
    auto f = NBNativeCPUMath::f32;
    auto v = [&](const String &key, const Variant &fallback, int occurrence = 0) {
        return value(op, key, fallback, ctx, occurrence);
    };
    auto raw = [&](const String &key, const Variant &fallback) { return input(op, key, fallback); };
    if (name == "ParticleState@1.0" || name == "ParticleState@1.1") {
        bool first = number(p, "age") <= double(ctx.get("Engine.DeltaTime", 0.));
        p["first_frame"] = first;
        double lifetime = f(v("Lifetime", p.get("lifetime", 1.))), safe = f(std::max(lifetime, .00001));
        p["age"] = f(number(p, "age") + f(v("DeltaTime", delta)));
        double age = number(p, "age"), normalized = 0.;
        bool alive = p.get("alive", false);
        if (bool(raw("KillParticlesWhenLifetimeHasElapsed", true))) {
            if (name == "ParticleState@1.0") {
                alive = alive && age < f(safe - f(.0001));
                normalized = age / safe;
            } else {
                alive = alive && ((first && lifetime > .000001) || age < safe);
                normalized = std::clamp(age / safe, 0., 1.);
            }
        } else {
            bool die = bool(raw("LetInfinitelyLivedParticlesDieWhenEmitterDeactivates", false)) &&
                       (int64_t(ctx.get("Emitter.ExecutionState", 0)) != 0 ||
                        int64_t(ctx.get("System.ExecutionState", 0)) != 0);
            if (bool(raw("LoopParticlesLifetime", false)) && !die) {
                age = f(std::fmod(age, safe));
                p["age"] = age;
            }
            if (die && age > f(safe - f(.0001)))
                alive = false;
            normalized = age / safe;
        }
        p["alive"] = alive;
        p["normalized_age"] = f(normalized);
        return true;
    }
    if (name == "CameraOffset") {
        p["camera_offset"] = double(v("CameraOffsetAmount", 0.));
        return true;
    }
    if (name == "KillParticles") {
        if (bool(v("KillParticles", true)))
            p["alive"] = false;
        return true;
    }
    if (name == "SpriteRotationRate") {
        double rate = f(v("RotationRate", 1.)), dt = f(v("DeltaTime", delta)),
               degrees = f(f(Math::rad_to_deg(number(p, "rotation"))) + f(rate * dt));
        p["rotation"] = Math::deg_to_rad(degrees);
        return true;
    }
    if (name == "GravityForce") {
        Vector3 acceleration = NBNativeValues::vector3(v("Gravity", Vector3(0, 0, -980)));
        acceleration = NBNativeValues::transform_between(
            acceleration, NBNativeValues::enum_name(raw("CoordinateSpace", "World")), "Simulation", ctx,
            false);
        p["force"] = Vector3(p.get("force", Vector3())) + acceleration * number(p, "mass");
        return true;
    }
    if (name == "Drag@1.0" || name == "Drag" || name == "Drag@1.1") {
        if (bool(raw("UseLinearDrag", true)))
            p["drag"] = number(p, "drag") + double(v("Drag", 0.));
        if (name != "Drag@1.0")
            p["drag_ignore_mass"] = bool(raw("IgnoreMass", true));
        if (bool(raw("UseRotationalDrag", false)))
            p["rotational_drag"] = number(p, "rotational_drag") + double(v("RotationalDrag", 1.)) * 100.;
        if (name != "Drag@1.0")
            p["rotational_drag"] = number(p, "rotational_drag") * .01;
        return true;
    }
    if (name == "V2/SubUVAnimation@1.0") {
        double first = v("StartFrame", 0), last = v("EndFrame", 63),
               index = v("SubUVLookupIndex", ctx.get("Particles.NormalizedAge", 0.)),
               loops = v("SubUVLoopCount", 1.);
        p["subimage_index"] = Math::lerp(first, last, std::fmod(index * loops, 1.));
        return true;
    }
    if (name == "SolveForcesAndVelocity") {
        double dt = f(v("DeltaTime", delta));
        Vector3 position = NBNativeValues::vector3(v("Position", p.get("position", Vector3()))),
                velocity = NBNativeValues::vector3(v("Velocity", p.get("velocity", Vector3()))),
                force = NBNativeValues::vector3(v("Force", p.get("force", Vector3())));
        double mass = f(std::max(double(v("Mass", p.get("mass", 1.))), .000001));
        Dictionary scopes = ctx.get("source_cpu_math", Dictionary());
        String id = op->get("stable_id");
        bool folded = scopes.has(id) && !Dictionary(scopes[id]).has("rcp");
        double inverse = folded ? f(1. / mass) : NBNativeCPUMath::reciprocal(mass);
        if (bool(raw("WritetoPresolveProperties", true))) {
            p["presolve_position"] = position;
            p["presolve_velocity"] = velocity;
            p["presolve_force"] = force;
        }
        p["previous_velocity"] = velocity;
        Vector3 after = velocity + force * inverse * dt;
        double drag = std::max(bool(p.get("drag_ignore_mass", true)) ? number(p, "drag")
                                                                     : f(number(p, "drag") * inverse),
                               0.),
               denominator = f(1. + f(drag * dt));
        Vector3 advection = p.get("advection_velocity", Vector3());
        Vector3 solved = (after - advection) / denominator + advection;
        p["velocity"] = solved;
        p["position"] = position + solved * dt;
        p["force"] = Vector3();
        p["drag"] = 0.;
        return true;
    }
    if (name == "Color") {
        Color color = NBNativeValues::color(v("Color", Color(1, 1, 1, 1)));
        Vector3 scale = NBNativeValues::vector3(v("ScaleColor", Vector3(1, 1, 1)), Vector3(1, 1, 1));
        double alpha = v("ScaleAlpha", 1.);
        p["color"] = Color(color.r * scale.x, color.g * scale.y, color.b * scale.z,
                           double(color.a) * alpha * double(ctx.get("Emitter.AlphaScale", 1.)));
        return true;
    }
    if (name == "ScaleColor@1.0") {
        Color color =
            NBNativeValues::color(v("ColorValueToScale", p.get("initial_color", Color(1, 1, 1, 1))));
        if (bool(raw("ScaleRGB", true))) {
            Vector3 rgb = NBNativeValues::vector3(v("ScaleRGB", Vector3(1, 1, 1), 1), Vector3(1, 1, 1));
            color.r *= rgb.x;
            color.g *= rgb.y;
            color.b *= rgb.z;
        }
        if (bool(raw("ScaleA", true)))
            color.a *= double(v("ScaleAlpha", 1.));
        p["color"] = color;
        return true;
    }
    if (name == "ScaleSpriteSize" || name == "ScaleSpriteSize@1.0") {
        Vector2 size, factor = p.get("size_scale_factor", Vector2(1, 1));
        if (name == "ScaleSpriteSize") {
            size = NBNativeValues::vector2(v("InitialSpriteSize", p.get("initial_size", Vector2(1, 1))));
            Dictionary c = curve(op, "UniformCurveSpriteScale", ctx);
            double time = v("UniformCurveIndex", ctx.get("Particles.NormalizedAge", 0.)),
                   sample = NBNativeValues::curve_sample(c, time), scale = v("UniformCurveScale", 1.);
            factor *= sample * scale;
        } else {
            factor *= NBNativeValues::vector2(v("ScaleFactor", Vector2(1, 1)));
            size = NBNativeValues::vector2(v("InitialSpriteSize", p.get("initial_size", Vector2(1, 1))));
        }
        p["size_scale_factor"] = factor;
        p["size"] = size * factor;
        return true;
    }
    if (name == "ScaleMeshSize") {
        Vector3 factor = Vector3(p.get("mesh_scale_factor", Vector3(1, 1, 1))) *
                         NBNativeValues::vector3(v("ScaleFactor", Vector3(1, 1, 1)), Vector3(1, 1, 1));
        p["mesh_scale_factor"] = factor;
        p["mesh_scale"] =
            NBNativeValues::vector3(v("InitialMeshScale", p.get("initial_mesh_scale", Vector3(1, 1, 1))),
                                    Vector3(1, 1, 1)) *
            factor;
        return true;
    }
    if (name == "ScaleSpriteSizeBySpeed") {
        double threshold = std::max(double(v("VelocityThreshold", 1.)), .0001),
               speed = NBNativeValues::vector3(v("SourceVelocity", p.get("velocity", Vector3()))).length();
        if (speed <= .00001)
            speed = 0.;
        Vector2 minimum = NBNativeValues::vector2(v("MinScaleFactor", Vector2(1, 1))),
                maximum = NBNativeValues::vector2(v("MaxScaleFactor", Vector2(1, 1)));
        double alpha = std::min(std::max(speed, .0001), threshold) / threshold;
        if (bool(raw("SampleScaleFactorByCurve", false)))
            alpha = NBNativeValues::curve_sample(curve(op, "ScaleFactorCurve", ctx), alpha);
        Vector2 factor = Vector2(p.get("size_scale_factor", Vector2(1, 1))) *
                         minimum.lerp(maximum, std::clamp(alpha, 0., 1.));
        p["size_scale_factor"] = factor;
        p["size"] =
            NBNativeValues::vector2(v("InitialSpriteSize", p.get("initial_size", Vector2(1, 1)))) * factor;
        return true;
    }
    if (name == "DynamicMaterialParameters@1.0" || name == "DynamicMaterialParameters@1.1") {
        for (int index = 0; index < 4; ++index) {
            String slot = String::num_int64(index);
            if (!bool(raw(String("WriteParameterIndex") + slot, index == 0)))
                continue;
            String suffix = index == 0 ? String() : slot,
                   source = index == 1 && name.ends_with("@1.0") ? String() : suffix;
            Color color = p.get(String("dynamic") + source, Color(0, 0, 0, 0));
            for (int component = 0; component < 4; ++component)
                if (bool(raw(String("Param") + String::num_int64(component) + String("WriteEnabled") + suffix,
                             true)))
                    color[component] = double(
                        v(String("Index") + slot + String("Param") + String::num_int64(component + 1), 0.));
            p[String("dynamic") + suffix] = color;
        }
        return true;
    }
    return execute_advanced(op, p, ctx, delta);
}
} // namespace godot
