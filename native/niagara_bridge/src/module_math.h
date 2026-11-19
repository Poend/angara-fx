#pragma once
#include <godot_cpp/classes/ref_counted.hpp>
namespace godot {
class NBNativeCPUMath : public RefCounted {
    GDCLASS(NBNativeCPUMath, RefCounted)
  protected:
    static void _bind_methods();

  public:
    static double f32(double value);
    static int64_t bits(double value);
    static double number(int64_t bits);
    static double reciprocal(double value);
    static double reciprocal_sqrt(double value);
    static double trig(double value, bool cosine);
    static double sin_turn(double value);
    static double cos_turn(double value);
    static double positive_pow(double value, double power);
};
class NBNativeLinearColor : public RefCounted {
    GDCLASS(NBNativeLinearColor, RefCounted)
  protected:
    static void _bind_methods();

  public:
    static Color hue_shift(const Color &color, double hue, double saturation, double value);
    static double row(const Color &color, double v, double c, double n, const Vector3 &a, const Vector3 &b,
                      const Vector3 &d);
    static double f32(double value);
};
} // namespace godot
