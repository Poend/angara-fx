#pragma once
#include <godot_cpp/classes/ref_counted.hpp>
namespace godot {
class NBNativeCurves : public RefCounted {
    GDCLASS(NBNativeCurves, RefCounted)
  protected:
    static void _bind_methods();

  public:
    static Variant prepare(const Variant &value, const String &type);
    static Dictionary expand_literal(const Dictionary &literal, const String &type, Array errors);
    static Dictionary normalize_channel(const Dictionary &raw, Array errors);
    static double sample(const Dictionary &curve, double time);
    static double sample_channel(const Dictionary &channel, double time);
    static Color sample_color(const Dictionary &curve, double time);
    static Vector2 sample_vector2(const Dictionary &curve, double time);
    static Vector3 sample_vector3(const Dictionary &curve, double time);
    static Array sample_di(const Dictionary &curve, double time);
    static PackedFloat32Array build_lut(const Dictionary &curve, int64_t count);
    static bool passes_threshold(const PackedFloat32Array &original, const PackedFloat32Array &candidate,
                                 int64_t channels, double threshold);
    static Dictionary default_vector2_curve();
    static Dictionary default_vector3_curve();
    static void clear_cache();
};
} // namespace godot
