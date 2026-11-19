#pragma once
#include <godot_cpp/classes/ref_counted.hpp>
namespace godot {
class NBNativeNiagaraRNG : public RefCounted {
    GDCLASS(NBNativeNiagaraRNG, RefCounted)
  protected:
    static void _bind_methods();

  public:
    static int64_t u32(int64_t value);
    static int64_t mul32(int64_t a, int64_t b);
    static int64_t lcg(int64_t value);
    static Vector3 rand3(int64_t a, int64_t b, int64_t c, int64_t counter);
    static Vector4 rand4(int64_t a, int64_t b, int64_t c, int64_t counter);
};
class NBNativeSourceRandom : public RefCounted {
    GDCLASS(NBNativeSourceRandom, RefCounted)
  protected:
    static void _bind_methods();

  public:
    static Variant sample(const Dictionary &call, const Variant &minimum, const Variant &maximum,
                          int64_t unique_id, int64_t emitter_seed, bool integer = false,
                          const Dictionary &parameters = Dictionary());
    static Variant sample_seeded(const Variant &minimum, const Variant &maximum, const Vector3i &seeds,
                                 int64_t counter, int components, bool integer);
    static double range(double minimum, double maximum, double random);
    static double f32(double value);
};
class NBNativeDynamicMath : public RefCounted {
    GDCLASS(NBNativeDynamicMath, RefCounted)
  protected:
    static void _bind_methods();

  public:
    static Variant evaluate_cpu(const String &name, const Dictionary &values);
    static String argument_path(const String &name);
    static double f32(double value);
};
class NBNativeValues : public RefCounted {
    GDCLASS(NBNativeValues, RefCounted)
    static Variant evaluate_depth(const Variant &value, const Dictionary &context, const String &path,
                                  int depth);

  protected:
    static void _bind_methods();

  public:
    static Variant input_value(const Array &inputs, const String &name, const Variant &fallback = Variant(),
                               int64_t occurrence = 0);
    static String enum_name(const Variant &value);
    static Variant indexed_argument(const Dictionary &expression, const Array &arguments, const String &name,
                                    const Variant &fallback = Variant());
    static Variant evaluate(const Variant &value, const Dictionary &context, const String &path = String());
    static double default_curve_index(const Dictionary &context);
    static Dictionary resolve_curve(Variant value, const Dictionary &context);
    static Dictionary resolve_vector2_curve(Variant value, const Dictionary &context);
    static Dictionary resolve_vector3_curve(Variant value, const Dictionary &context);
    static double curve_sample(const Dictionary &curve, double time);
    static Vector3 vector3(const Variant &value, const Vector3 &fallback = Vector3());
    static Vector2 vector2(const Variant &value, const Vector2 &fallback = Vector2(1, 1));
    static Color color(const Variant &value, const Color &fallback = Color(1, 1, 1, 1));
    static Quaternion quaternion(const Variant &value);
    static Basis unreal_rotation_to_godot(const Quaternion &rotation);
    static Vector3 unreal_to_godot(const Vector3 &value);
    static Vector3 godot_to_unreal(const Vector3 &value);
    static Vector3 transform_between(const Vector3 &value, String source, String destination,
                                     const Dictionary &context, bool apply_scale = false,
                                     bool as_position = false);
};
} // namespace godot
