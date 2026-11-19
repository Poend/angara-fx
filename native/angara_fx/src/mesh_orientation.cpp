#include "mesh_orientation.h"
#include "module_math.h"
#include "renderer_filter.h"
#include "value_evaluator.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <algorithm>
#include <cmath>
namespace godot {
namespace {
constexpr double TAU = 6.28318530717958647692;
double dot32(const Vector3 &v) {
    auto f = NBNativeCPUMath::f32;
    return f(f(f(double(v.x) * v.x) + f(double(v.y) * v.y)) + f(double(v.z) * v.z));
}
Basis axes() {
    return Basis(Vector3(0, 0, -1), Vector3(1, 0, 0), Vector3(0, 1, 0));
}
} // namespace
void NBNativeMeshOrientation::_bind_methods() {
    ClassDB::bind_static_method(
        "NBNativeMeshOrientation", D_METHOD("safe_direction", "value", "fallback", "folded"),
        &NBNativeMeshOrientation::safe_direction, DEFVAL(Vector3(1, 0, 0)), DEFVAL(false));
    ClassDB::bind_static_method("NBNativeMeshOrientation", D_METHOD("vm_normalized", "value"),
                                &NBNativeMeshOrientation::vm_normalized);
    ClassDB::bind_static_method("NBNativeMeshOrientation",
                                D_METHOD("apply_rotation_vector", "turns", "delta", "existing"),
                                &NBNativeMeshOrientation::apply_rotation_vector);
    ClassDB::bind_static_method(
        "NBNativeMeshOrientation",
        D_METHOD("apply_legacy_rotation_vector", "turns", "delta", "existing", "folded"),
        &NBNativeMeshOrientation::apply_legacy_rotation_vector, DEFVAL(false));
    ClassDB::bind_static_method("NBNativeMeshOrientation",
                                D_METHOD("input_is_literal", "operation", "key", "fallback"),
                                &NBNativeMeshOrientation::input_is_literal);
    ClassDB::bind_static_method("NBNativeMeshOrientation",
                                D_METHOD("space_is_simulation", "space", "context"),
                                &NBNativeMeshOrientation::space_is_simulation);
    ClassDB::bind_static_method("NBNativeMeshOrientation",
                                D_METHOD("rotation_is_folded", "operation", "context", "literal_expression"),
                                &NBNativeMeshOrientation::rotation_is_folded);
    ClassDB::bind_static_method("NBNativeMeshOrientation", D_METHOD("random_quaternion", "sample"),
                                &NBNativeMeshOrientation::random_quaternion);
    ClassDB::bind_static_method("NBNativeMeshOrientation", D_METHOD("find_between", "from", "to"),
                                &NBNativeMeshOrientation::find_between);
    ClassDB::bind_static_method("NBNativeMeshOrientation", D_METHOD("find_between_raw", "a", "b"),
                                &NBNativeMeshOrientation::find_between_raw);
    ClassDB::bind_static_method("NBNativeMeshOrientation", D_METHOD("from_axes", "x", "y", "z"),
                                &NBNativeMeshOrientation::from_axes);
    ClassDB::bind_static_method("NBNativeMeshOrientation",
                                D_METHOD("stabilize", "facing", "side", "up", "mode", "reference"),
                                &NBNativeMeshOrientation::stabilize);
    ClassDB::bind_static_method("NBNativeMeshOrientation",
                                D_METHOD("override_orientation", "incoming", "override_value", "alpha"),
                                &NBNativeMeshOrientation::override_orientation);
}
Vector3 NBNativeMeshOrientation::safe_direction(const Vector3 &v, const Vector3 &fallback, bool folded) {
    if (v.length_squared() < 1e-10)
        return fallback;
    return folded ? v.normalized() : vm_normalized(v);
}
Vector3 NBNativeMeshOrientation::vm_normalized(const Vector3 &v) {
    return v.length_squared() > 1e-8 ? v.normalized() : Vector3();
}
Quaternion NBNativeMeshOrientation::apply_rotation_vector(const Vector3 &turns, double delta,
                                                          const Quaternion &existing) {
    Vector3 radians = turns * delta * TAU;
    if (radians.length_squared() < 1e-10 || radians.length_squared() <= 1e-8)
        return existing;
    return Quaternion(radians.normalized(), radians.length()) * existing;
}
Quaternion NBNativeMeshOrientation::apply_legacy_rotation_vector(const Vector3 &turns, double delta,
                                                                 const Quaternion &existing, bool folded) {
    auto f = NBNativeCPUMath::f32;
    Vector3 radians = (turns * f(delta)) * f(TAU);
    double squared = dot32(radians);
    if (squared < 1e-10)
        return existing;
    double inverse = folded ? f(1. / f(std::sqrt(squared))) : NBNativeCPUMath::reciprocal_sqrt(squared);
    Vector3 direction = radians * inverse;
    double length = folded && inverse != 0. ? f(1. / inverse) : NBNativeCPUMath::reciprocal(inverse);
    Vector3 axis = folded ? direction.normalized() : vm_normalized(direction);
    double half = f(length * .5);
    double sine = !folded && std::abs(half) <= 6.28319 ? NBNativeCPUMath::sin_turn(half) : f(UtilityFunctions::sin(half)),
           cosine =
               !folded && std::abs(half) <= 6.28319 ? NBNativeCPUMath::cos_turn(half) : f(UtilityFunctions::cos(half));
    Vector3 xyz = axis * sine;
    return Quaternion(xyz.x, xyz.y, xyz.z, cosine) * existing;
}
bool NBNativeMeshOrientation::input_is_literal(const Ref<Resource> &op, const String &key,
                                               const Variant &fallback) {
    if (op.is_null())
        return false;
    Variant v = op->call("get_input", key, fallback);
    return v.get_type() != Variant::DICTIONARY && v.get_type() != Variant::STRING;
}
bool NBNativeMeshOrientation::space_is_simulation(const String &s, const Dictionary &ctx) {
    return s == "Mesh" || s == "Simulation" || s == "SimulationSpace" ||
           s == (bool(ctx.get("local_space", false)) ? "Local" : "World");
}
bool NBNativeMeshOrientation::rotation_is_folded(const Ref<Resource> &op, const Dictionary &ctx,
                                                 bool literal) {
    if (op.is_null())
        return literal;
    Dictionary scopes = ctx.get("source_cpu_math", Dictionary());
    String id = op->get("stable_id");
    return scopes.has(id) ? !Dictionary(scopes[id]).has("rcp") : literal;
}
Quaternion NBNativeMeshOrientation::random_quaternion(const Vector3 &s) {
    double a = std::sqrt(std::max(1. - double(s.x), 0.)), b = std::sqrt(std::max(double(s.x), 0.));
    return Quaternion(a * UtilityFunctions::sin(TAU * s.y), a * UtilityFunctions::cos(TAU * s.y), b * UtilityFunctions::sin(TAU * s.z),
                      b * UtilityFunctions::cos(TAU * s.z))
        .normalized();
}
Quaternion NBNativeMeshOrientation::find_between(const Vector3 &a, const Vector3 &b) {
    return find_between_raw(safe_direction(a), safe_direction(b));
}
Quaternion NBNativeMeshOrientation::find_between_raw(const Vector3 &a, const Vector3 &b) {
    double norm = std::sqrt(double(a.length_squared()) * b.length_squared());
    if (norm < 1e-6)
        return Quaternion();
    double w = norm + a.dot(b);
    Vector3 xyz = a.cross(b);
    if (w < 1e-6 * norm) {
        xyz = std::abs(a.x) > std::abs(a.y) ? Vector3(-a.z, 0, a.x) : Vector3(0, -a.z, a.y);
        w = 0.;
    }
    return Quaternion(xyz.x, xyz.y, xyz.z, w).normalized();
}
Quaternion NBNativeMeshOrientation::from_axes(const Vector3 &x, const Vector3 &y, const Vector3 &z) {
    double m[3][3] = {{x.x, x.y, x.z}, {y.x, y.y, y.z}, {z.x, z.y, z.z}}, trace = m[0][0] + m[1][1] + m[2][2];
    if (trace > 0) {
        double inv = 1. / std::sqrt(trace + 1.), s = .5 * inv;
        return Quaternion((m[1][2] - m[2][1]) * s, (m[2][0] - m[0][2]) * s, (m[0][1] - m[1][0]) * s,
                          .5 / inv);
    }
    int i = 0;
    if (m[1][1] > m[0][0])
        i = 1;
    if (m[2][2] > m[i][i])
        i = 2;
    int j = (i + 1) % 3, k = (j + 1) % 3;
    double inv = 1. / std::sqrt(m[i][i] - m[j][j] - m[k][k] + 1.), s = .5 * inv;
    Vector4 q;
    q[i] = .5 / inv;
    q[j] = (m[i][j] + m[j][i]) * s;
    q[k] = (m[i][k] + m[k][i]) * s;
    q.w = (m[j][k] - m[k][j]) * s;
    return Quaternion(q.x, q.y, q.z, q.w);
}
Quaternion NBNativeMeshOrientation::stabilize(const Vector3 &facing, const Vector3 &side, const Vector3 &up,
                                              const String &mode, const Vector3 &reference) {
    if (mode == "Facing")
        return find_between(reference, facing);
    if (mode == "Facing/Side/Up")
        return from_axes(facing, side, up);
    Vector3 x = facing.length_squared() >= .0001 ? facing.normalized() : Vector3(1, 0, 0);
    if (mode == "Facing/Up") {
        Vector3 z = up.length_squared() >= .0001 ? up.normalized() : Vector3(0, 1, 0),
                y = vm_normalized(z.cross(x));
        z = vm_normalized(x.cross(y));
        return from_axes(x, y, z);
    }
    Vector3 y = side.length_squared() >= .0001 ? side.normalized() : Vector3(0, 1, 0), z = x.cross(y);
    z = z.length_squared() >= 1e-10 ? vm_normalized(z) : Vector3();
    y = z.cross(x);
    y = y.length_squared() >= 1e-10 ? vm_normalized(y) : Vector3();
    return from_axes(x, y, z);
}
Quaternion NBNativeMeshOrientation::override_orientation(const Quaternion &a, const Quaternion &b,
                                                         double alpha) {
    double signed_alpha = a.dot(b) > 0 ? alpha : -alpha;
    return (a * (1. - alpha) + b * signed_alpha).normalized();
}
void NBNativeMeshFacing::_bind_methods() {
    ClassDB::bind_static_method("NBNativeMeshFacing", D_METHOD("configuration", "renderer"),
                                &NBNativeMeshFacing::configuration);
    ClassDB::bind_static_method(
        "NBNativeMeshFacing",
        D_METHOD("rotation", "config", "position_ue", "velocity_ue", "local_space", "owner", "camera"),
        &NBNativeMeshFacing::rotation);
}
Dictionary NBNativeMeshFacing::configuration(const Ref<Resource> &renderer) {
    Dictionary r;
    TypedArray<String> errors;
    r["mode"] = NBNativeValues::enum_name(NBNativeRendererFilter::input(renderer, "FacingMode", "Default"));
    r["locked"] = NBNativeRendererFilter::input(renderer, "bLockedAxisEnable", false);
    r["axis"] = Vector3(0, 0, 1);
    r["space"] =
        NBNativeValues::enum_name(NBNativeRendererFilter::input(renderer, "LockedAxisSpace", "Simulation"));
    r["errors"] = errors;
    String mode = r["mode"];
    if (mode != "Default" && mode != "Velocity" && mode != "CameraPosition" && mode != "CameraPlane")
        errors.append("Unsupported mesh facing mode: " + mode);
    Variant locked = r["locked"];
    if (locked.get_type() != Variant::BOOL)
        errors.append("Mesh locked-axis enable must be a constant boolean.");
    if (locked.get_type() == Variant::BOOL && bool(locked) && mode != "Default") {
        Variant raw = NBNativeRendererFilter::input(renderer, "LockedAxis", Vector3(0, 0, 1));
        bool numeric = raw.get_type() == Variant::VECTOR3;
        if (raw.get_type() == Variant::ARRAY) {
            Array a = raw;
            numeric = a.size() == 3;
            for (int n = 0; n < a.size(); ++n)
                if ((a[n].get_type() != Variant::FLOAT && a[n].get_type() != Variant::INT) ||
                    !std::isfinite(double(a[n])))
                    numeric = false;
        }
        if (!numeric)
            errors.append("Mesh locked axis must be a finite three-component vector.");
        else {
            Vector3 axis = NBNativeValues::vector3(raw);
            r["axis"] = axis;
            if (!axis.is_finite() || axis.length_squared() == 0)
                errors.append("Zero/nonfinite mesh locked axis has no defined facing frame.");
        }
        String space = r["space"];
        if (space != "Simulation" && space != "World" && space != "Local")
            errors.append("Unsupported mesh locked-axis space: " + space);
    }
    return r;
}
Basis NBNativeMeshFacing::rotation(const Dictionary &config, const Vector3 &position, const Vector3 &velocity,
                                   bool local, const Transform3D &owner, const Transform3D &camera) {
    String mode = config.get("mode", "Default");
    if (mode == "Default")
        return Basis();
    Basis a = axes(), owner_ue = a.transposed() * owner.basis * a,
          owner_rotation(owner_ue.get_column(0).normalized(), owner_ue.get_column(1).normalized(),
                         owner_ue.get_column(2).normalized()),
          to_sim = local ? owner_rotation.transposed() : Basis();
    Vector3 world_position = local ? owner.xform(NBNativeValues::unreal_to_godot(position))
                                   : NBNativeValues::unreal_to_godot(position),
            facing;
    if (mode == "Velocity")
        facing = velocity.length_squared() > 0 ? velocity.normalized() : to_sim.xform(Vector3(0, 0, 1));
    else if (mode == "CameraPosition")
        facing = to_sim.xform(a.transposed().xform((camera.origin - world_position).normalized()));
    else
        facing = to_sim.xform(a.transposed().xform(camera.basis.get_column(2)));
    Vector3 x, y, z;
    if (bool(config.get("locked", false))) {
        Vector3 axis = config.get("axis", Vector3(0, 0, 1));
        String space = config.get("space", "Simulation");
        bool world = space == "World" || (space == "Simulation" && !local);
        if (world && local)
            axis = owner_rotation.transposed().xform(axis);
        else if (!world && !local)
            axis = owner_rotation.xform(axis);
        if (std::abs(facing.dot(axis)) > .99)
            facing = std::abs(axis.z) > .99 ? Vector3(1, 0, 0) : Vector3(0, 0, 1);
        z = axis;
        y = z.cross(facing).normalized();
        x = y.cross(z);
    } else {
        Vector3 reference;
        if (mode == "CameraPlane")
            reference = a.transposed().xform(camera.basis.get_column(1));
        else {
            Vector3 world = local ? owner_rotation.xform(facing) : facing;
            reference = std::abs(world.z) > .99 ? Vector3(world.z > 0   ? -1
                                                          : world.z < 0 ? 1
                                                                        : 0,
                                                          0, 0)
                                                : Vector3(0, 0, 1);
        }
        reference = to_sim.xform(reference);
        x = facing;
        y = reference.cross(facing).normalized();
        z = x.cross(y);
    }
    return a * Basis(x, y, z) * a.transposed();
}
} // namespace godot
