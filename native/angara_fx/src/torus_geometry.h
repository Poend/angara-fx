#pragma once
#include <godot_cpp/classes/ref_counted.hpp>
namespace godot {
class NBNativeTorusGeometry : public RefCounted {
    GDCLASS(NBNativeTorusGeometry, RefCounted)
  protected:
    static void _bind_methods();

  public:
    static Vector3 normalize_native(const Vector3 &value);
    static Vector2 trig(double turns);
    static Dictionary sample(const String &mode, double radius, double u, double v, double major,
                             double minor, double q = 2, double p = 5, bool tube = false,
                             double tube_radius = 10);
    static void orient(Dictionary result, const Vector3 &axis, double turns);
};
} // namespace godot
