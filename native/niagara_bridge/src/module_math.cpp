#include "module_math.h"
#include "vm_scalar.h"
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
namespace godot {
void NBNativeCPUMath::_bind_methods() {
    ClassDB::bind_static_method("NBNativeCPUMath", D_METHOD("f32", "x"), &NBNativeCPUMath::f32);
    ClassDB::bind_static_method("NBNativeCPUMath", D_METHOD("_bits", "x"), &NBNativeCPUMath::bits);
    ClassDB::bind_static_method("NBNativeCPUMath", D_METHOD("_float", "bits"), &NBNativeCPUMath::number);
    ClassDB::bind_static_method("NBNativeCPUMath", D_METHOD("reciprocal", "x"), &NBNativeCPUMath::reciprocal);
    ClassDB::bind_static_method("NBNativeCPUMath", D_METHOD("reciprocal_sqrt", "x"),
                                &NBNativeCPUMath::reciprocal_sqrt);
    ClassDB::bind_static_method("NBNativeCPUMath", D_METHOD("_trig", "x", "cosine"), &NBNativeCPUMath::trig);
    ClassDB::bind_static_method("NBNativeCPUMath", D_METHOD("sin_turn", "x"), &NBNativeCPUMath::sin_turn);
    ClassDB::bind_static_method("NBNativeCPUMath", D_METHOD("cos_turn", "x"), &NBNativeCPUMath::cos_turn);
    ClassDB::bind_static_method("NBNativeCPUMath", D_METHOD("positive_pow", "x", "y"),
                                &NBNativeCPUMath::positive_pow);
}
double NBNativeCPUMath::f32(double x) {
    return double(float(x));
}
int64_t NBNativeCPUMath::bits(double x) {
    return nbvm::bits(float(x));
}
double NBNativeCPUMath::number(int64_t bits) {
    return nbvm::number(uint32_t(bits));
}
double NBNativeCPUMath::reciprocal(double x) {
    x = f32(x);
    if (std::abs(x) <= 1e-8)
        return 0.;
    uint32_t raw = uint32_t(bits(x));
    int exponent = (raw >> 23) & 255;
    if (exponent == 255)
        return std::isinf(x) ? 0. : NAN;
    Ref<Resource> profile =
        ResourceLoader::get_singleton()->load("res://addons/niagara_bridge/data/math/ue58_win64_amd.tres");
    if (profile.is_null())
        return NAN;
    int64_t shift = profile->get("reciprocal_mantissa_shift");
    PackedInt32Array table = profile->get("reciprocal_bits");
    if (shift < 0 || shift > 23)
        return NAN;
    uint32_t index = (raw & 0x7fffff) >> shift;
    if (index >= table.size())
        return NAN;
    int64_t value = int64_t(table[index]) - int64_t(exponent - 127) * (int64_t(1) << 23);
    if (value < 0x800000)
        // The former GDScript -0.0 literal is canonicalized to +0. Preserve
        // that established helper result; the source VM has its own SSE path.
        return 0.;
    return number(uint32_t(value) | (raw & 0x80000000));
}
double NBNativeCPUMath::reciprocal_sqrt(double x) {
    x = f32(x);
    return x > 1e-8 ? f32(1. / f32(std::sqrt(x))) : 0.;
}
double NBNativeCPUMath::trig(double x, bool cosine) {
    x = f32(x);
    ERR_FAIL_COND_V_MSG(!(std::abs(x) <= 6.28319), 0.,
                        "Spawn trigonometry requires a finite angle within one turn.");
    return nbvm::trig(float(x), cosine);
}
double NBNativeCPUMath::sin_turn(double x) {
    return trig(x, false);
}
double NBNativeCPUMath::cos_turn(double x) {
    return trig(x, true);
}
double NBNativeCPUMath::positive_pow(double x, double y) {
    x = f32(x);
    y = f32(y);
    if (x <= 1e-8)
        return 0.;
    if (!std::isfinite(x) || !std::isfinite(y))
        return f32(std::pow(x, y));
    uint32_t raw = uint32_t(bits(x)), fraction = raw & 0x7fffff, carry = (fraction + 0x4afb10) & 0x800000;
    double m = number(fraction | (0x3f800000 ^ carry));
    int e = int((raw >> 23) & 255) - 127 + int(carry >> 23);
    double estimate = reciprocal(f32(1. + m)), total = 1. + m, negative = estimate * (total * estimate - 2.);
    double z = (total * negative + 2.) * ((2. - total) * negative), z2 = z * z, p = .23801162294990488;
    for (double c : {.28524917668329924, .40000555065237864, .666666643542379, 2.})
        p = p * z2 + c;
    double t = ((z * 1.4426950408889634) * p + e) * y;
    if (t > 128.)
        return INFINITY;
    if (t < -150.)
        return 0.;
    double rounded = 6755399441055744. + t, r = t + (6755399441055744. - rounded);
    uint64_t u;
    std::memcpy(&u, &rounded, 8);
    u = ((u + 1010) << 52) | UINT64_C(0x41d33371366f6);
    double scale;
    std::memcpy(&scale, &u, 8);
    double p1 = (6.858109015731002 + r) * r + 13.207354074378483,
           p2 = (4.186611958611718 + r) * r + 16.625772605936234,
           p3 = (-2.3127417210384476 + r) * r + 29.676485611755382;
    return f32(scale * ((p1 * p2) * p3));
}
void NBNativeLinearColor::_bind_methods() {
    ClassDB::bind_static_method("NBNativeLinearColor",
                                D_METHOD("hue_shift", "color", "hue", "saturation", "value"),
                                &NBNativeLinearColor::hue_shift);
    ClassDB::bind_static_method("NBNativeLinearColor",
                                D_METHOD("_row", "color", "v", "c", "n", "a", "b", "d"),
                                &NBNativeLinearColor::row);
    ClassDB::bind_static_method("NBNativeLinearColor", D_METHOD("_f", "value"), &NBNativeLinearColor::f32);
}
double NBNativeLinearColor::f32(double x) {
    return double(float(x));
}
double NBNativeLinearColor::row(const Color &color, double v, double c, double n, const Vector3 &a,
                                const Vector3 &b, const Vector3 &d) {
    auto f = f32;
    double x = f(f(f(a.x * v) + f(b.x * c)) + f(d.x * n)), y = f(f(f(a.y * v) + f(b.y * c)) + f(d.y * n)),
           z = f(f(f(a.z * v) + f(b.z * c)) + f(d.z * n));
    return f(f(f(x * color.r) + f(y * color.g)) + f(z * color.b));
}
Color NBNativeLinearColor::hue_shift(const Color &color, double hue, double saturation, double value) {
    auto f = f32;
    double v = std::max(value, 0.), s = std::max(saturation, 0.), degrees = f(hue * 360.),
           angle = f(f(degrees * f(3.1415927)) / 180.), c = f(f(v * s) * f(std::cos(angle))),
           n = f(f(v * s) * f(std::sin(angle)));
    double r = row(color, v, c, n, Vector3(.299, .587, .114), Vector3(.701, -.587, -.114),
                   Vector3(.168, .330, -.497));
    double g = row(color, v, c, n, Vector3(.299, .587, .114), Vector3(-.299, .413, -.114),
                   Vector3(-.328, .035, .292));
    double b = row(color, v, c, n, Vector3(.299, .587, .114), Vector3(-.300, -.588, .886),
                   Vector3(1.25, -1.05, -.203));
    return Color(r, g, b, color.a);
}
} // namespace godot
