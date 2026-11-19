#include "module_execution.h"
#include "attribute_values.h"
#include "mesh_orientation.h"
#include "module_math.h"
#include "torus_geometry.h"
#include "value_evaluator.h"
#include <godot_cpp/classes/random_number_generator.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <algorithm>
#include <cmath>
namespace godot {
namespace {
constexpr double TAU = 6.28318530717958647692, PI = 3.14159265358979323846;
Variant raw(const Ref<Resource> &op, const String &key, const Variant &fallback) {
    return op->call("get_input", key, fallback, 0);
}
Variant value(const Ref<Resource> &op, const String &key, const Variant &fallback, const Dictionary &ctx) {
    return NBNativeValues::evaluate(raw(op, key, fallback), ctx,
                                    String(op->get("stable_id")) + String("/") + key + String("/0"));
}
Variant random_range(const Ref<Resource> &op, const String &target, const Variant &lo, const Variant &hi,
                     const Dictionary &ctx, bool integer = false, const Vector3i *seeds = nullptr, int module_draw = 1) {
    if (ctx.has("source_rng")) {
        Dictionary bindings = ctx["source_rng"];
        String key = String(op->get("stable_id")) + String("/$") + target;
        return NBNativeSourceRandom::sample(bindings.get(key, Dictionary()), lo, hi,
                                            ctx.get("source_unique_id", 0), ctx.get("source_seed", 0),
                                            integer, ctx);
    }
    if (seeds) {
        int components = lo.get_type() == Variant::VECTOR4 ? 4 : lo.get_type() == Variant::VECTOR3 ? 3 : lo.get_type() == Variant::VECTOR2 ? 2 : 1;
        return NBNativeSourceRandom::sample_seeded(lo, hi, *seeds, module_draw, components, integer);
    }
    Ref<RandomNumberGenerator> rng = ctx.get("rng", Variant());
    if (rng.is_null())
        return lo;
    if (lo.get_type() == Variant::VECTOR2) {
        Vector2 a = lo, b = hi;
        double x = rng->randf_range(a.x, b.x), y = rng->randf_range(a.y, b.y);
        return Vector2(x, y);
    }
    if (lo.get_type() == Variant::VECTOR3) {
        Vector3 a = lo, b = hi;
        double x = rng->randf_range(a.x, b.x), y = rng->randf_range(a.y, b.y), z = rng->randf_range(a.z, b.z);
        return Vector3(x, y, z);
    }
    if (lo.get_type() == Variant::VECTOR4) {
        Vector4 a = lo, b = hi, sample;
        for (int i = 0; i < 4; ++i)
            sample[i] = rng->randf_range(a[i], b[i]);
        return sample;
    }
    return integer ? Variant(rng->randi_range(lo, hi)) : Variant(rng->randf_range(lo, hi));
}
Vector3 transform(const Vector3 &v, const String &space, const Dictionary &ctx, bool scale = true) {
    return NBNativeValues::transform_between(v, space, "Simulation", ctx, scale);
}
double dot32(const Vector3 &v) {
    auto f = NBNativeCPUMath::f32;
    return f(f(f(double(v.x) * v.x) + f(double(v.y) * v.y)) + f(double(v.z) * v.z));
}
Vector3 box_shell_point(const Ref<Resource> &op, Vector3 size, const Dictionary &ctx, const Vector3i *seeds) {
    float thickness = value(op, "SurfaceOnlyBandThickness", 0.0, ctx);
    String expansion = NBNativeValues::enum_name(raw(op, "SurfaceExpansionMode", "Inside"));
    int steps = expansion == "Outside" ? 2 : expansion == "Centered" ? 1 : 0;
    size += Vector3(1, 1, 1) * (steps * thickness);
    // Niagara bounds the band by the longest expanded dimension, even on a flat box.
    float t = std::min(std::max(thickness, 0.0f), 0.5f * size[size.max_axis_index()]);
    Vector4 sample = random_range(op, "BoxShellPoint/0", Vector4(), Vector4(1, 1, 1, 1), ctx, false, seeds);
    float side = std::nearbyint(sample.x);
    float inner_z = size.z - 2 * t, inner_y = size.y - 2 * t;
    float top = size.x * size.y, top_and_sides = top + size.x * inner_z;
    float area = random_range(op, "BoxShellFace/0", 0.0, top_and_sides + inner_y * inner_z, ctx, false, seeds, 2);
    Vector3 origin, extent;
    if (area < top) {
        origin = Vector3(0, 0, side * (size.z - t));
        extent = Vector3(size.x, size.y, t);
    } else if (area < top_and_sides) {
        origin = Vector3(0, side * (size.y - t), t);
        extent = Vector3(size.x, t, inner_z);
    } else {
        origin = Vector3(side * (size.x - t), t, t);
        extent = Vector3(t, inner_y, inner_z);
    }
    return origin + extent * Vector3(sample.y, sample.z, sample.w) - size * 0.5f;
}

bool inside_kill_volume(const Ref<Resource> &op, const Vector3 &relative, const Dictionary &ctx) {
    auto v = [&](const String &key, const Variant &fallback) { return value(op, key, fallback, ctx); };
    String shape = NBNativeValues::enum_name(raw(op, "KillShape", "Sphere"));
    if (shape == "Box") {
        Vector3 local = NBNativeValues::transform_between(relative, "Simulation", "Local", ctx, false).abs();
        Vector3 half = NBNativeValues::vector3(v("BoxSize", Vector3(100, 100, 100))).abs() * 0.5f;
        return local.x < half.x && local.y < half.y && local.z < half.z;
    }
    if (shape == "Plane")
        return relative.dot(NBNativeValues::vector3(v("PlaneNormal", Vector3(0, 0, 1)))) < 0;
    if (shape == "Slab") {
        Vector3 half = NBNativeValues::vector3(v("SlabAxis", Vector3(0, 0, 1))) * (0.5f * float(v("SlabWidth", 100.0)));
        return std::abs(relative.dot(half)) < half.length_squared();
    }
    float distance = relative.length();
    bool near_origin = distance < 1e-5f;
    if (shape == "Sphere")
        return (near_origin ? 1.0f : distance) < float(v("SphereRadius", 100.0));
    Vector3 axis = NBNativeValues::vector3(v("ConeAxis", Vector3(1, 0, 0))).normalized();
    // The source compares a direction toward the origin and a radial length.
    Vector3 direction = near_origin ? axis : -relative / distance;
    return (near_origin ? axis.length() : distance) < float(v("ConeLength", 200.0)) &&
           direction.dot(axis) > std::cos(float(v("ConeAngle", 45.0)) * float(PI / 360.0));
}
void initialize(const Ref<Resource> &op, Dictionary p, Dictionary ctx) {
    Ref<RandomNumberGenerator> rng = p.get("rng", Variant());
    if (rng.is_null())
        return;
    auto v = [&](const String &key, const Variant &fallback) { return value(op, key, fallback, ctx); };
    auto mode = [&](const String &key, const Variant &fallback) {
        return NBNativeValues::enum_name(raw(op, key, fallback));
    };
    if (mode("LifetimeMode", "DirectSet") == "Random") {
        double a = v("LifetimeMin", 1.), b = v("LifetimeMax", 1.);
        p["lifetime"] = random_range(op, "Lifetime", a, b, ctx);
    } else
        p["lifetime"] = double(v("Lifetime", 1.));
    if (mode("ColorMode", "DirectSet") == "DirectSet")
        p["color"] = NBNativeValues::color(v("Color", Color(1, 1, 1, 1)));
    else if (mode("ColorMode", "DirectSet") == "RandomHue/Saturation/Value") {
        Vector4 adjustments(0, 1, 1, 1);
        const char *components[] = {"Hue", "Saturation", "Value", "Alpha"};
        for (int n = 0; n < 4; ++n) {
            String component = components[n];
            if (!bool(raw(op, String("Adjust") + component, n == 0)))
                continue;
            String key = n == 0   ? String("HueShiftRange")
                         : n == 3 ? String("AlphaScaleRange")
                                  : component + String("Range");
            Vector2 range = NBNativeValues::vector2(v(key, n == 0 ? Vector2(-.1, .1) : Vector2(.8, 1)));
            adjustments[n] = double(random_range(op, String("Color") + component, range.x, range.y, ctx));
        }
        Color c = NBNativeLinearColor::hue_shift(NBNativeValues::color(v("Color", Color(1, 1, 1, 1))),
                                                 adjustments.x, adjustments.y, adjustments.z);
        c.a *= adjustments.w;
        p["color"] = c;
    }
    p["initial_color"] = p["color"];
    String size_mode = mode("SpriteSizeMode", "Uniform");
    if (size_mode == "RandomNonUniform") {
        Vector2 a = NBNativeValues::vector2(v("SpriteSizeMin", Vector2(1, 1))),
                b = NBNativeValues::vector2(v("SpriteSizeMax", Vector2(1, 1)));
        p["size"] = random_range(op, "SpriteSize", a, b, ctx);
    } else if (size_mode == "RandomUniform") {
        double a = v("UniformSpriteSizeMin", 1.), b = v("UniformSpriteSizeMax", 1.);
        p["size"] = Vector2(1, 1) * double(random_range(op, "SpriteSize", a, b, ctx));
    } else if (size_mode == "Uniform")
        p["size"] = Vector2(1, 1) * double(v("UniformSpriteSize", 1.));
    else if (size_mode != "Unset")
        p["size"] = NBNativeValues::vector2(v("SpriteSize", Vector2(1, 1)));
    p["initial_size"] = p["size"];
    String mesh_mode = mode("MeshScaleMode", "Unset");
    if (mesh_mode == "Uniform")
        p["mesh_scale"] = Vector3(1, 1, 1) * double(v("MeshUniformScale", 1.));
    else if (mesh_mode == "NonUniform")
        p["mesh_scale"] = NBNativeValues::vector3(v("MeshScale", Vector3(1, 1, 1)), Vector3(1, 1, 1));
    else if (mesh_mode == "RandomUniform") {
        double a = v("MeshUniformScaleMin", 1.), b = v("MeshUniformScaleMax", 2.);
        p["mesh_scale"] = Vector3(1, 1, 1) * double(random_range(op, "MeshScale", a, b, ctx));
    } else if (mesh_mode == "RandomNonUniform") {
        Vector3 a = NBNativeValues::vector3(v("MeshScaleMin", Vector3(1, 1, 1)), Vector3(1, 1, 1)),
                b = NBNativeValues::vector3(v("MeshScaleMax", Vector3(2, 2, 2)), Vector3(2, 2, 2));
        p["mesh_scale"] = random_range(op, "MeshScale", a, b, ctx);
    }
    String mass_mode = mode("MassMode", "NewEnumerator0");
    if (mass_mode == "Random") {
        double a = v("MassMin", .75), b = v("MassMax", 1.25);
        p["mass"] = random_range(op, "Mass", a, b, ctx);
    } else
        p["mass"] = mass_mode == "DirectSet" ? double(v("Mass", 1.)) : 1.;
    if (bool(raw(op, "UsePositionOffset", false)))
        p["position"] =
            Vector3(p["position"]) + transform(NBNativeValues::vector3(v("PositionOffset", Vector3())),
                                               mode("PositionOffsetCoordinateSpace", "Local"), ctx);
    String uv = mode("SpriteUVMode", "Unset");
    if (uv == "RandomX" || uv == "RandomX/Y")
        p["uv_flip"] = ctx.has("source_rng")
                           ? (int64_t(random_range(op, "UVFlip", 0, 1, ctx, true)) != 0 ? -1. : 1.)
                           : (rng->randf() < .5 ? -1. : 1.);
    if (uv == "RandomY" || uv == "RandomX/Y")
        p["uv_flip_y"] = ctx.has("source_rng")
                             ? (int64_t(random_range(op, "UVFlipY", 0, 1, ctx, true)) != 0 ? -1. : 1.)
                             : (rng->randf() < .5 ? -1. : 1.);
    if (uv == "RandomX")
        p["uv_flip_y"] = 1.;
    else if (uv == "RandomY")
        p["uv_flip"] = 1.;
    if (uv == "Custom") {
        Vector2 scale = NBNativeValues::vector2(v("SpriteUVScale", Vector2(1, 1)));
        p["uv_flip"] = scale.x;
        p["uv_flip_y"] = scale.y;
    }
    if (mode("SpriteRotationMode", "Unset") == "Random") {
        double a = v("SpriteRotationAngleMin", 0.), b = v("SpriteRotationAngleMax", 360.);
        p["rotation"] = Math::deg_to_rad(double(random_range(op, "SpriteRotation", a, b, ctx)));
    }
    if (bool(raw(op, "WriteMaterialRandom", false)))
        p["material_random"] = random_range(op, "MaterialRandom", 0., 1., ctx);
    if (mode("RibbonWidthMode", "Unset") == "DirectSet")
        p["ribbon_width"] = double(v("RibbonWidth", 5.));
}
Vector3 shape_position(const Ref<Resource> &op, const Ref<RandomNumberGenerator> &rng, Dictionary ctx) {
    if (rng.is_null())
        return Vector3();
    auto f = NBNativeCPUMath::f32;
    auto v = [&](const String &key, const Variant &fallback) { return value(op, key, fallback, ctx); };
    if (NBNativeValues::enum_name(raw(op, "ShapePrimitive", "Sphere")) == "Sphere") {
        double radius = std::max(std::abs(double(v("SphereRadius", 100.))), .00001),
               distribution = std::max(double(v("SphereSurfaceDistribution", 0.)), 0.);
        double radial = distribution == 1.
                            ? 1.
                            : UtilityFunctions::pow(rng->randf_range(distribution, 1.), .333333),
               cosine = rng->randf_range(-1., 1.), azimuth = rng->randf_range(0., TAU),
               sine = std::sqrt(std::max(1. - cosine * cosine, 0.));
        if (ctx.has("source_rng")) {
            double sample = random_range(op, "ShapeSphere/0", distribution, 1., ctx);
            radial = distribution == 1. ? 1. : UtilityFunctions::pow(std::max(sample, 0.), f(.333333));
            double theta = f(UtilityFunctions::acos(
                std::clamp(double(random_range(op, "ShapeSphere/1", -1., 1., ctx)), -1., 1.)));
            cosine = f(UtilityFunctions::cos(theta));
            sine = f(UtilityFunctions::sin(theta));
            azimuth = random_range(op, "ShapeSphere/2", 0., TAU, ctx);
        }
        return Vector3(UtilityFunctions::cos(azimuth) * sine, UtilityFunctions::sin(azimuth) * sine, cosine) *
               radius * radial;
    }
    double radius = v("CylinderRadius", 100.), height = v("CylinderHeight", 100.),
           midpoint = v("CylinderHeightMidpoint", .5);
    double x = rng->randf(), y = rng->randf(), z = rng->randf();
    Vector3 sample(x, y, z);
    double band = rng->randf();
    bool top = rng->randf() < .5;
    double surface = rng->randf_range(0., 2.);
    if (ctx.has("source_rng")) {
        sample = NBNativeValues::vector3(random_range(op, "CylinderPoint", Vector3(), Vector3(1, 1, 1), ctx));
        band = random_range(op, "CylinderBand", 0., 1., ctx);
        top = int64_t(random_range(op, "CylinderTop", 0, 1, ctx, true)) != 0;
        surface = random_range(op, "CylinderSurface", 0., 2., ctx);
    }
    bool endcap = false;
    if (bool(raw(op, "UseEndcapsInSurfaceOnlyMode", true))) {
        Vector3 scale =
            NBNativeValues::vector3(v("NonUniformScale", Vector3(1, 1, 1)), Vector3(1, 1, 1)).abs();
        double lateral =
                   PI * std::max(radius, 0.) * (double(scale.x) + scale.y) * std::max(height, 0.) * scale.z,
               caps = TAU * std::max(radius, 0.) * std::max(radius, 0.) * scale.x * scale.y;
        endcap = lateral >= caps && lateral > 0. ? caps / lateral > surface
                 : caps > 0.                     ? lateral / caps < surface
                                                 : false;
    }
    double radial = endcap ? std::sqrt(double(sample.x)) : 1.,
           height_sample =
               endcap ? (top ? std::floor(double(sample.z)) : std::ceil(double(sample.z))) : double(sample.z);
    Vector3 point(UtilityFunctions::cos(double(sample.y) * TAU) * radial,
                  UtilityFunctions::sin(double(sample.y) * TAU) * radial, height_sample);
    double shrink = 1. - band * std::clamp(double(v("SurfaceOnlyBandThickness", 0.)), 0., 1.);
    point = (point - Vector3(0, 0, .5)) * shrink + Vector3(0, 0, .5);
    point.z -= midpoint;
    return point * Vector3(radius, radius, height);
}
Vector3 cone_velocity(const Ref<Resource> &op, const Dictionary &p, Dictionary ctx) {
    Ref<RandomNumberGenerator> rng = p.get("rng", Variant());
    if (rng.is_null())
        return Vector3();
    auto v = [&](const String &key, const Variant &fallback) { return value(op, key, fallback, ctx); };
    double outer = Math::deg_to_rad(double(v("ConeAngle", 0.)) * .5),
           inner = Math::deg_to_rad(double(v("InnerConeAngle", 0.)) * .5),
           cosine = rng->randf_range(UtilityFunctions::cos(outer), UtilityFunctions::cos(inner)),
           azimuth = rng->randf_range(0., TAU), sine = std::sqrt(std::max(1. - cosine * cosine, 0.));
    if (ctx.has("source_rng")) {
        double sample = random_range(
                   op, "Cone/2", UtilityFunctions::cos(outer),
                   UtilityFunctions::cos((1. - double(v("InnerConeAngle", 0.)) * .5 / 360.) * TAU), ctx),
               theta = UtilityFunctions::acos(std::clamp(sample, -1., 1.));
        cosine = UtilityFunctions::cos(theta);
        sine = UtilityFunctions::sin(theta);
        azimuth = random_range(op, "Cone/3", 0., TAU, ctx);
    }
    Vector3 direction(UtilityFunctions::cos(azimuth) * sine, UtilityFunctions::sin(azimuth) * sine, cosine),
        axis = NBNativeValues::vector3(v("ConeAxis", Vector3(0, 0, 1)), Vector3(0, 0, 1));
    if (axis.length_squared() <= 1e-10) {
        if (ctx.has("source_rng"))
            axis = Vector3(1, 0, 0);
        else
            return Vector3();
    }
    direction = Quaternion(Vector3(0, 0, 1), axis.normalized()).xform(direction);
    return direction * double(v("VelocitySpeed", 0.));
}
void torus_location(const Ref<Resource> &op, Dictionary p, Dictionary ctx) {
    auto v = [&](const String &key, const Variant &fallback) { return value(op, key, fallback, ctx); };
    double radius, u, w;
    if (NBNativeValues::enum_name(raw(op, "TorusDistributionMode", "Random")) == "Direct") {
        radius = v("RadiusPosition", 1.);
        u = v("UPosition", 0.);
        w = v("VPosition", 0.);
    } else {
        radius =
            random_range(op, "TorusRadius", std::clamp(double(v("SurfacePosition", 0.)), 0., 999.), 1., ctx);
        u = random_range(op, "TorusU", std::clamp(double(v("UDistribution", 0.)), 0., 1.), 1., ctx);
        w = random_range(op, "TorusV", std::clamp(double(v("VDistribution", 0.)), 0., 1.), 1., ctx);
    }
    double major = v("LargeRadius", 100.), minor = v("HandleRadius", 25.), q = v("KnotHelixLoops", 2),
           loops = v("KnotRingLoops", 5);
    bool tube = raw(op, "EnableTorusKnotRadius", false);
    double tube_radius = v("TorusKnotTubeRadius", 10.);
    Dictionary result =
        NBNativeTorusGeometry::sample(NBNativeValues::enum_name(raw(op, "TorusMode", "Torus")), radius, u, w,
                                      major, minor, q, loops, tube, tube_radius);
    if (bool(raw(op, "EnableScale", false))) {
        Vector3 scale = NBNativeValues::vector3(v("NonUniformScale", Vector3(1, 1, 1)));
        result["vector"] = Vector3(result["vector"]) * scale;
        result["tangent"] = NBNativeTorusGeometry::normalize_native(Vector3(result["tangent"]) * scale);
        result["normal"] = NBNativeTorusGeometry::normalize_native(Vector3(result["normal"]) * scale);
    }
    if (bool(raw(op, "AlignTorusWithAxis", false))) {
        Vector3 axis = NBNativeValues::vector3(v("TorusAxis", Vector3(0, 0, 1)));
        double angle = v("NormalizedAngleAroundTorusAxis", 0.);
        NBNativeTorusGeometry::orient(result, axis, angle);
    }
    String space = NBNativeValues::enum_name(raw(op, "CoordinateSpace", "Local"));
    Vector3 vector = transform(result["vector"], space, ctx, true),
            tangent = transform(result["tangent"], space, ctx, false),
            normal = transform(result["normal"], space, ctx, false);
    if (!bool(raw(op, "MaskSpawnGroup", false)) ||
        int64_t(v("SpawnGroupMask", ctx.get("Emitter.SpawnGroup", 0))) ==
            int64_t(p.get("spawn_group", 0))) {
        Vector3 offset = NBNativeValues::vector3(v("Offset", Vector3())),
                origin = NBNativeValues::vector3(v("TorusOrigin", p["position"]));
        p["position"] = vector + (offset + origin);
    }
    String alias = op->get("alias_name");
    if (alias.is_empty())
        alias = String(op->get("module_name")).get_file();
    String prefix = String("Output.") + alias + String(".");
    ctx[prefix + String("TorusVector")] = vector;
    ctx[prefix + String("TorusTangent")] = tangent;
    ctx[prefix + String("TorusNormal")] = normal;
    ctx[prefix + String("TorusBitangent")] = normal.cross(tangent);
}
void update_mesh_facing(const Ref<Resource> &op, Dictionary p, Dictionary ctx) {
    auto v = [&](const String &key, const Variant &fallback) { return value(op, key, fallback, ctx); };
    Vector3 origin = NBNativeValues::vector3(v("FacingOrigin", Vector3())),
            target = NBNativeValues::vector3(v("FacingPosition", Vector3()));
    if (bool(raw(op, "TransformFacingOrigin", false)))
        origin = NBNativeValues::transform_between(
            origin, NBNativeValues::enum_name(raw(op, "FacingOriginCoordinateSpace", "Simulation")),
            "Simulation", ctx, false, true);
    if (bool(raw(op, "TransformFacingPosition", false)))
        target = NBNativeValues::transform_between(
            target, NBNativeValues::enum_name(raw(op, "FacingPositionCoordinateSpace", "Simulation")),
            "Simulation", ctx, false, true);
    Vector3 difference = target - origin, facing = NBNativeMeshOrientation::safe_direction(difference);
    String state_key = String(op->get("stable_id")) + String("/$mesh_facing");
    Dictionary cache = p.get("cache", Dictionary()), initial;
    initial["previous"] = Quaternion();
    initial["average"] = Vector3();
    Dictionary state = cache.get(state_key, initial);
    if (bool(raw(op, "FacingInterpolation", false))) {
        if (bool(v("FirstFrame", ctx.get("Transient.FirstFrame", false))))
            state["average"] = facing;
        double rate = v("FacingInterpolationRate", 1.);
        state["average"] =
            Vector3(state["average"])
                .lerp(facing, std::clamp(rate * double(ctx.get("Engine.DeltaTime", 0.)), 0., 1.));
        facing = state["average"];
    }
    Vector3 side = NBNativeValues::vector3(v("SideDirection", Vector3(0, 1, 0))),
            up = NBNativeValues::vector3(v("UpDirection", Vector3(0, 0, 1)));
    if (bool(raw(op, "TransformSide", false)))
        side = transform(side, NBNativeValues::enum_name(raw(op, "SideCoordinateSpace", "Simulation")), ctx,
                         false);
    if (bool(raw(op, "TransformUp", false)))
        up = transform(up, NBNativeValues::enum_name(raw(op, "UpCoordinateSpace", "Simulation")), ctx, false);
    Quaternion pre =
        difference.length_squared() < 1e-10
            ? Quaternion()
            : NBNativeMeshOrientation::stabilize(
                  facing, side, up, NBNativeValues::enum_name(raw(op, "StabilizationMode", "Facing")),
                  NBNativeValues::vector3(v("ReferenceAxisVector", Vector3(1, 0, 0))));
    Quaternion incoming = p.get("mesh_orientation", Quaternion());
    if (bool(raw(op, "OverrideQuat", false))) {
        Variant source = v("OverrideQuaternion", Quaternion());
        Quaternion override_value = NBNativeAttributeValues::coerce(source, "quat");
        double alpha = v("OverrideIncomingOrientation", 0.);
        incoming = NBNativeMeshOrientation::override_orientation(incoming, override_value, alpha);
    }
    Quaternion previous = state["previous"];
    p["mesh_orientation"] =
        (pre * (Quaternion(previous.x, previous.y, previous.z, -previous.w) * incoming)).normalized();
    state["previous"] = pre;
    cache[state_key] = state;
    p["cache"] = cache;
}
} // namespace
// Remaining module routing is below; helpers are shared by every effect.
bool NBNativeParticleModules::execute_advanced(const Ref<Resource> &op, Dictionary p, Dictionary ctx,
                                               double delta) {
    String name = op->get("module_name");
    if (name == "V2/InitializeParticle") {
        initialize(op, p, ctx);
        return true;
    }
    if (name == "TorusLocation") {
        torus_location(op, p, ctx);
        return true;
    }
    if (name == "ShapeLocation") {
        Vector3 origin = NBNativeValues::vector3(value(op, "ShapeOrigin", p["position"], ctx));
        Ref<RandomNumberGenerator> rng = p.get("rng", Variant());
        Vector3 vector = shape_position(op, rng, ctx);
        vector *=
            NBNativeValues::vector3(value(op, "NonUniformScale", Vector3(1, 1, 1), ctx), Vector3(1, 1, 1));
        Vector3 shape_vector = transform(vector, "Local", ctx, true);
        if (NBNativeValues::enum_name(raw(op, "OffsetMode", "None")) == "Default")
            shape_vector += transform(NBNativeValues::vector3(value(op, "Offset", Vector3(), ctx)),
                                      NBNativeValues::enum_name(raw(op, "OffsetCoordinateSpace", "Local")), ctx, false);
        p["position"] = origin + shape_vector;
        return true;
    }
    if (name == "AddVelocity@1.0") {
        Vector3 velocity = NBNativeValues::vector3(value(op, "Velocity", Vector3(), ctx));
        Vector3 scale = NBNativeValues::vector3(value(op, "ScaleAddedVelocity", Vector3(1, 1, 1), ctx));
        velocity *= scale;
        p["velocity"] =
            Vector3(p["velocity"]) +
            transform(velocity, NBNativeValues::enum_name(raw(op, "CoordinateSpace", "Local")), ctx, false);
        return true;
    }
    if (name == "AddVelocity") {
        Vector3 velocity;
        if (NBNativeValues::enum_name(raw(op, "VelocityMode", "Linear")) == "InCone")
            velocity = cone_velocity(op, p, ctx);
        else {
            velocity = NBNativeValues::vector3(value(op, "Velocity", Vector3(), ctx));
            double scale = value(op, "VelocitySpeedScale", 1., ctx);
            velocity *= scale;
        }
        p["velocity"] =
            Vector3(p["velocity"]) +
            transform(velocity, NBNativeValues::enum_name(raw(op, "RotationCoordinateSpace", "Local")), ctx,
                      false);
        return true;
    }
    auto v = [&](const String &key, const Variant &fallback) { return value(op, key, fallback, ctx); };
    auto mode = [&](const String &key, const Variant &fallback) {
        return NBNativeValues::enum_name(raw(op, key, fallback));
    };
    auto f = NBNativeCPUMath::f32;
    if (name == "BoxLocation") {
        Vector3i module_seed;
        const Vector3i *seeds = nullptr;
        bool override_mode = raw(op, "OverrideRandomness", false);
        bool fixed = raw(op, "FixedRandomSeed", false);
        if (!ctx.has("source_rng") && (override_mode || fixed)) {
            String selected = override_mode ? mode("RandomnessMode", "SimulationDefaults") : String("SimulationDefaults");
            if (selected == "Deterministic" || selected == "Determinisitic" || selected == "NewEnumerator1" ||
                bool(ctx.get("Emitter.Determinism", false))) {
                int32_t seed = override_mode && bool(raw(op, "OverrideSeed", false)) ?
                    int32_t(v("RandomSeed", 0)) : int32_t(ctx.get("Emitter.RandomSeed", 0));
                module_seed = fixed ? Vector3i(seed, 0, 0) :
                                      Vector3i(int32_t(p["id"]), int32_t(ctx.get("Engine.System.TickCount", 0)), seed);
                seeds = &module_seed;
            }
        }
        Vector3 size = NBNativeValues::vector3(v("BoxSize", Vector3(100, 100, 100)));
        Vector3 point = bool(raw(op, "SurfaceOnly", false)) ? box_shell_point(op, size, ctx, seeds) :
                        Vector3(random_range(op, "BoxPoint/0", Vector3(), size, ctx, false, seeds)) - size * 0.5f;
        Vector3 a = point.abs(), normal = point;
        if (a.x > a.y && a.x > a.z) normal = Vector3(point.x, 0, 0);
        if (a.y > a.x && a.y > a.z) normal = Vector3(0, point.y, 0);
        if (a.z > a.x && a.z > a.y) normal = Vector3(0, 0, point.z);
        normal.normalize();
        if (bool(raw(op, "RotateBox", false))) {
            Vector3 axis = NBNativeValues::vector3(v("RotateBoxOnAxis", Vector3(0, 0, 1))).normalized();
            if (!axis.is_zero_approx()) {
                Basis rotation(axis, double(v("NormalizedBoxRotationAngle", 0.0)) * TAU);
                point = rotation.xform(point);
                normal = rotation.xform(normal);
            }
        }
        String space = mode("CoordinateSpace", "Local");
        Vector3 offset = NBNativeValues::vector3(v("BoxOffset", Vector3()));
        Vector3 vector = transform(point + offset, space, ctx, true);
        String alias = op->get("alias_name");
        String prefix = "Output." + (alias.is_empty() ? name : alias) + ".";
        ctx[prefix + "BoxVector"] = vector;
        ctx[prefix + "BoxNormal"] = transform(normal, space, ctx, false);
        if (!bool(raw(op, "MaskSpawnGroup", false)) ||
            int64_t(v("SpawnGroupMask", 0)) == int64_t(p.get("spawn_group", 0)))
            p["position"] = NBNativeValues::vector3(v("BoxOrigin", p["position"])) + vector;
        return true;
    }
    if (name == "KillParticlesInVolume") {
        if (!bool(v("KillVolumeEnabled", true))) return true;
        Vector3 origin = NBNativeValues::vector3(v("VolumeOrigin", ctx.get("Engine.Emitter.SimulationPosition", Vector3())));
        origin += transform(NBNativeValues::vector3(v("OriginOffset", Vector3())), "Local", ctx, false);
        Vector3 relative = NBNativeValues::vector3(v("Position", p["position"])) - origin;
        bool inside = inside_kill_volume(op, relative, ctx);
        if (inside != bool(v("InvertVolume", false))) p["alive"] = false;
        return true;
    }
    if (name == "WindForce@1.0") {
        Vector3 wind = NBNativeValues::vector3(v("WindSpeed", Vector3(100, 0, 0))) * double(v("WindSpeedScale", 1.0));
        Vector3 velocity = p["velocity"];
        // The source's safe direction helper substitutes both a direction and a length.
        auto direction = [](const Vector3 &x) { return x.length_squared() < 1e-10f ? Vector3(0, 0, 1e-6f) : x.normalized(); };
        auto length = [](const Vector3 &x) { return x.length_squared() < 1e-10f ? 1e-6f : x.length(); };
        bool affected = length(wind) > direction(wind).dot(direction(velocity)) * length(velocity);
        String alias = op->get("alias_name");
        ctx["Output." + (alias.is_empty() ? String("WindForce") : alias) + ".IsAffectedByWind"] = affected;
        if (affected)
            p["force"] = Vector3(p["force"]) + wind * std::clamp(1.0 - double(v("AirResistance", 0.2)), 0.0, 1.0);
        return true;
    }
    if (name == "AddVelocityFromPoint") {
        Vector3 origin = NBNativeValues::vector3(
            v("VelocityOrigin", ctx.get("Engine.Emitter.SimulationPosition", Vector3())));
        Vector3 offset = NBNativeValues::vector3(v("OriginOffset", Vector3()));
        origin += transform(offset, mode("OriginOffsetCoordinateSpace", "Local"), ctx, false);
        Vector3 vector = NBNativeValues::vector3(v("DefaultPosition", p["position"])) - origin;
        Vector2 sample = random_range(op, "PointDirection/0", Vector2(0, -1), Vector2(6.28319, 1), ctx);
        Ref<RandomNumberGenerator> rng = p.get("rng", Variant());
        if (!ctx.has("source_rng") && rng.is_valid())
            rng->randf();
        double distance = 0., squared = dot32(vector);
        if (squared < 1e-10) {
            double sine_squared = f(1. - f(double(sample.y) * sample.y)),
                   sine = sine_squared > 1e-8 ? f(std::sqrt(sine_squared)) : 0.;
            vector = Vector3(NBNativeCPUMath::cos_turn(sample.x) * sine,
                             NBNativeCPUMath::sin_turn(sample.x) * sine, sample.y);
        } else {
            Dictionary scopes = ctx.get("source_cpu_math", Dictionary());
            String id = op->get("stable_id");
            bool folded_rsq = scopes.has(id) && !Dictionary(scopes[id]).has("rsq");
            double inverse =
                folded_rsq ? f(1. / f(std::sqrt(squared))) : NBNativeCPUMath::reciprocal_sqrt(squared);
            vector *= inverse;
            bool folded_rcp = scopes.has(id) && !Dictionary(scopes[id]).has("rcp");
            distance = folded_rcp && inverse != 0. ? f(1. / inverse) : NBNativeCPUMath::reciprocal(inverse);
        }
        double falloff = v("VelocityFalloffDistance", 100.),
               normalized = f(std::min(distance, falloff) / std::max(falloff, f(.0001)));
        if (bool(v("InvertVelocityFalloff", true)))
            normalized = f(1. - normalized);
        double strength = v("VelocityStrength", 100.);
        if (bool(v("UseFalloffDistance", false)))
            strength = f(strength * normalized);
        p["velocity"] = Vector3(p["velocity"]) + vector * strength;
        return true;
    }
    if (name == "V2/SphereLocation") {
        Vector3 origin = NBNativeValues::vector3(v("SphereOrigin", p["position"]));
        double radius = v("SphereRadius", 100.), inner = 0., outer = 1.;
        if (bool(raw(op, "SurfaceOnly", true))) {
            double thickness = v("SurfaceOnlyBandThickness", 0.);
            if (!bool(v("NormalizeThickness", true)))
                thickness = std::abs(radius) > 1e-8 ? thickness / radius : 0.;
            double expansion = mode("SurfaceExpansionMode", "Inside") == "Outside" ? 2. : 0.,
                   raw_inner = f(f(1. - thickness) + f(f(thickness * .5) * expansion));
            inner = std::max(raw_inner, 0.);
            outer = f(raw_inner + thickness);
        }
        double lower = NBNativeCPUMath::positive_pow(std::max(inner, 0.), 3.),
               upper = NBNativeCPUMath::positive_pow(std::max(outer, 0.), 3.);
        Ref<RandomNumberGenerator> rng = p.get("rng", Variant());
        if (rng.is_null())
            return false;
        double azimuth = rng->randf_range(0., TAU), cosine = rng->randf_range(-1., 1.),
               radial = NBNativeCPUMath::positive_pow(std::max(double(rng->randf_range(lower, upper)), 0.),
                                                      .333333);
        if (ctx.has("source_rng")) {
            Vector3 sample = random_range(op, "Sphere/0", Vector3(0, -1, lower), Vector3(TAU, 1, upper), ctx);
            azimuth = sample.x;
            cosine = sample.y;
            radial = NBNativeCPUMath::positive_pow(std::max(0., double(sample.z)), .333333);
        }
        double sine_squared = f(1. - f(cosine * cosine)),
               sine = sine_squared > 1e-8 ? f(std::sqrt(sine_squared)) : 0.;
        Vector3 vector = Vector3(NBNativeCPUMath::cos_turn(azimuth) * sine,
                                 NBNativeCPUMath::sin_turn(azimuth) * sine, cosine) *
                         radial;
        const char *axes[] = {"HemisphereX", "HemisphereY", "HemisphereZ"};
        for (int axis = 0; axis < 3; ++axis)
            if (bool(raw(op, axes[axis], false)))
                vector[axis] = std::abs(vector[axis]);
        vector *= radius;
        if (bool(raw(op, "UseNonUniformScale", false)))
            vector *= NBNativeValues::vector3(v("NonUniformScale", Vector3(1, 1, 1)), Vector3(1, 1, 1));
        vector = transform(vector, mode("SphereCoordinateSpace", "Local"), ctx, true);
        Vector3 offset;
        if (bool(raw(op, "UseOffset", false)))
            offset = transform(NBNativeValues::vector3(v("Offset", Vector3())),
                               mode("OffsetCoordinateSpace", "Local"), ctx, true);
        p["position"] = origin + offset + vector;
        return true;
    }
    if (name == "V3/InitialMeshOrientation@1.0") {
        String space = mode("RotationCoordinateSpace", "Mesh");
        bool mesh = space == "Mesh";
        Quaternion prior = p.get("mesh_orientation", Quaternion()), rotation = prior;
        if (bool(raw(op, "UseRotationVector", false))) {
            Vector3 vector = transform(NBNativeValues::vector3(v("Rotation", Vector3())), space, ctx, false);
            bool literal = NBNativeMeshOrientation::input_is_literal(op, "Rotation", Vector3()) &&
                           NBNativeMeshOrientation::space_is_simulation(space, ctx);
            rotation = NBNativeMeshOrientation::apply_legacy_rotation_vector(
                vector, 1., mesh ? prior : Quaternion(),
                NBNativeMeshOrientation::rotation_is_folded(op, ctx, literal));
        }
        if (bool(raw(op, "UseOrientationVector", true))) {
            String orientation_space = mode("OrientationCoordinateSpace", "Simulation");
            bool literal_axis =
                NBNativeMeshOrientation::input_is_literal(op, "OrientationAxis", Vector3(1, 0, 0));
            Vector3 axis = transform(NBNativeMeshOrientation::safe_direction(
                                         NBNativeValues::vector3(v("OrientationAxis", Vector3(1, 0, 0))),
                                         Vector3(1, 0, 0), literal_axis),
                                     orientation_space, ctx, false);
            Vector3 direction = transform(NBNativeValues::vector3(v("OrientationVector", Vector3(1, 0, 0))),
                                          orientation_space, ctx, false);
            bool literal =
                NBNativeMeshOrientation::input_is_literal(op, "OrientationVector", Vector3(1, 0, 0)) &&
                NBNativeMeshOrientation::space_is_simulation(orientation_space, ctx);
            direction = NBNativeMeshOrientation::safe_direction(direction, axis, literal);
            Quaternion orientation = NBNativeMeshOrientation::find_between_raw(axis, direction);
            rotation = mesh ? orientation * rotation : rotation * orientation;
        }
        p["mesh_orientation"] = rotation;
        p["initial_rotation"] = rotation;
        return true;
    }
    if (name == "V3/InitialMeshOrientation") {
        String space = mode("RotationCoordinateSpace", "Mesh");
        bool mesh = space == "Mesh";
        Quaternion prior = p.get("mesh_orientation", Quaternion()), rotation = prior;
        if (bool(raw(op, "UseRotationVector", false))) {
            Vector3 vector = transform(NBNativeValues::vector3(v("Rotation", Vector3())), space, ctx, false);
            rotation =
                NBNativeMeshOrientation::apply_rotation_vector(vector, 1., mesh ? prior : Quaternion());
        }
        String selection = mode("MeshOrientationMode", "Random");
        if (selection != "None") {
            Quaternion orientation;
            if (selection == "Random")
                orientation = NBNativeMeshOrientation::random_quaternion(
                    random_range(op, "Orientation", Vector3(), Vector3(1, 1, 1), ctx));
            else if (selection == "OrienttoVector") {
                Vector3 axis = NBNativeValues::vector3(v("MeshAxisToOrient", Vector3(1, 0, 0))),
                        direction = transform(NBNativeMeshOrientation::safe_direction(NBNativeValues::vector3(
                                                  v("OrientationVector", Vector3(1, 0, 0)))),
                                              mode("OrientationCoordinateSpace", "Simulation"), ctx, false);
                orientation = NBNativeMeshOrientation::find_between(axis, direction);
            }
            rotation = mesh ? orientation * rotation : rotation * orientation;
        }
        p["mesh_orientation"] = rotation;
        p["initial_rotation"] = rotation;
        return true;
    }
    if (name == "V3/UpdateMeshOrientation" || name == "V3/UpdateMeshOrientation@1.0") {
        if (name == "V3/UpdateMeshOrientation" &&
            mode("OrientationMethod", "RotationRate") == "OrienttoPosition") {
            update_mesh_facing(op, p, ctx);
            return true;
        }
        String space = mode("RotationCoordinateSpace", "Mesh");
        bool mesh = space == "Mesh";
        Vector3 rate = NBNativeValues::vector3(v("RotationVector", Vector3(1, 0, 0)));
        double scale = v("RotationRate", 1.);
        rate *= scale;
        rate = transform(rate, space, ctx, false);
        double dt = v("DeltaTime", delta);
        Quaternion prior = p.get("mesh_orientation", Quaternion()), rotation;
        bool legacy = name.ends_with("@1.0");
        if (legacy) {
            bool literal =
                NBNativeMeshOrientation::input_is_literal(op, "RotationVector", Vector3(1, 0, 0)) &&
                NBNativeMeshOrientation::input_is_literal(op, "RotationRate", 1.) &&
                NBNativeMeshOrientation::input_is_literal(op, "DeltaTime", "Engine.DeltaTime") &&
                NBNativeMeshOrientation::space_is_simulation(space, ctx);
            rotation = NBNativeMeshOrientation::apply_legacy_rotation_vector(
                rate, dt, mesh ? Quaternion() : prior,
                NBNativeMeshOrientation::rotation_is_folded(op, ctx, literal));
        } else
            rotation = NBNativeMeshOrientation::apply_rotation_vector(rate, dt, mesh ? Quaternion() : prior);
        Quaternion result = mesh ? prior * rotation : rotation;
        p["mesh_orientation"] = legacy ? result : result.normalized();
        return true;
    }
    return false;
}
} // namespace godot
