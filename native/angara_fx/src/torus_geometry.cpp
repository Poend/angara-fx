#include "torus_geometry.h"
#include "module_math.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <cmath>
namespace godot {
namespace {
constexpr double TAU = 6.28318530717958647692;
double dot32(const Vector3 &v) {
    auto f = NBNativeCPUMath::f32;
    return f(f(f(double(v.x) * v.x) + f(double(v.y) * v.y)) + f(double(v.z) * v.z));
}
} // namespace
void NBNativeTorusGeometry::_bind_methods() {
    ClassDB::bind_static_method("NBNativeTorusGeometry", D_METHOD("normalize_native", "v"),
                                &NBNativeTorusGeometry::normalize_native);
    ClassDB::bind_static_method("NBNativeTorusGeometry", D_METHOD("trig", "turns"),
                                &NBNativeTorusGeometry::trig);
    ClassDB::bind_static_method(
        "NBNativeTorusGeometry",
        D_METHOD("sample", "mode", "radius", "u", "v", "major", "minor", "q", "p", "tube", "tube_radius"),
        &NBNativeTorusGeometry::sample, DEFVAL(2.), DEFVAL(5.), DEFVAL(false), DEFVAL(10.));
    ClassDB::bind_static_method("NBNativeTorusGeometry", D_METHOD("orient", "result", "axis", "turns"),
                                &NBNativeTorusGeometry::orient);
}
Vector3 NBNativeTorusGeometry::normalize_native(const Vector3 &v) {
    return v * NBNativeCPUMath::reciprocal_sqrt(dot32(v));
}
Vector2 NBNativeTorusGeometry::trig(double turns) {
    auto f = NBNativeCPUMath::f32;
    double angle = f(f(turns) * f(TAU));
    if (std::abs(angle) <= 6.28319)
        return Vector2(NBNativeCPUMath::cos_turn(angle), NBNativeCPUMath::sin_turn(angle));
    return Vector2(f(UtilityFunctions::cos(angle)), f(UtilityFunctions::sin(angle)));
}
Dictionary NBNativeTorusGeometry::sample(const String &mode, double radius, double u, double v, double major,
                                         double minor, double q, double p, bool tube, double tube_radius) {
    auto f = NBNativeCPUMath::f32;
    radius = f(radius);
    u = f(u);
    v = f(v);
    major = f(major);
    minor = f(minor);
    Dictionary result;
    if (mode == "Ring") {
        double radial = radius > 1e-8 ? f(std::sqrt(radius)) : 0.;
        Vector2 uv = trig(u) * f(radial * major);
        Vector3 position(uv.x, uv.y, 1), normal = normalize_native(position);
        result["vector"] = position;
        result["tangent"] = Vector3(0, 0, 1).cross(normal);
        result["normal"] = normal;
        return result;
    }
    double a = f(minor * (mode == "TorusKnot" ? radius : radius > 1e-8 ? f(std::sqrt(radius)) : 0.));
    Vector2 cu = trig(mode == "TorusKnot" ? f(u * q) : u), cv = trig(mode == "TorusKnot" ? f(u * p) : v);
    double width = f(major + f(a * cv.x));
    Vector3 position(width * cu.x, width * cu.y, a * cv.y), tangent, normal;
    if (mode == "TorusKnot") {
        double ap_sin = f(f(a * p) * cv.y), qw = f(q * width);
        tangent = normalize_native(
            Vector3(f(-qw * cu.y) - f(ap_sin * cu.x), f(qw * cu.x) - f(ap_sin * cu.y), f(a * p) * cv.x));
        double apq_sin = f(f(f(a * p) * q) * cv.y), aq2 = f(a * f(q * q)), ap2 = f(a * f(p * p)),
               cq2 = f(major * f(q * q)), twice = f(2 * apq_sin);
        normal = normalize_native(Vector3(-f(f(twice * cu.y) + f(f(f(-aq2 - ap2) * cv.x) - cq2) * cu.x),
                                          -f(f(-f(f(f(aq2 + ap2) * cv.x) + cq2) * cu.y) - f(twice * cu.x)),
                                          ap2 * cv.y));
        if (tube) {
            Vector3 bitangent = normal.cross(tangent);
            Vector2 circle = trig(v) * f(tube_radius);
            Vector3 displacement = bitangent * circle.x + normal * circle.y;
            position += displacement;
            normal = normalize_native(displacement);
        }
    } else {
        tangent = Vector3(-cu.y, cu.x, 0);
        normal = tangent.cross(Vector3(-cv.y * cu.x, -cv.y * cu.y, cv.x));
    }
    result["vector"] = position;
    result["tangent"] = tangent;
    result["normal"] = normal;
    return result;
}
void NBNativeTorusGeometry::orient(Dictionary result, const Vector3 &input_axis, double turns) {
    auto f = NBNativeCPUMath::f32;
    Vector3 axis = dot32(input_axis) < 1e-10 ? Vector3(0, 0, 1) : normalize_native(input_axis);
    double norm = f(std::sqrt(dot32(axis))), w = f(norm + axis.z);
    Quaternion align = w >= f(1e-6 * norm) ? Quaternion(-axis.y, axis.x, 0, w) : Quaternion(0, -1, 0, 0);
    align = norm >= 1e-6 ? align.normalized() : Quaternion();
    Vector2 spin = trig(f(turns * .5));
    Quaternion rotation = align * Quaternion(0, 0, spin.y, spin.x);
    for (const char *key : {"vector", "tangent", "normal"})
        result[key] = rotation.xform(Vector3(result[key]));
}
} // namespace godot
