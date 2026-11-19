#pragma once
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/resource.hpp>
namespace godot {
class NBNativeMeshOrientation : public RefCounted {
    GDCLASS(NBNativeMeshOrientation, RefCounted)
  protected:
    static void _bind_methods();

  public:
    static Vector3 safe_direction(const Vector3 &value, const Vector3 &fallback = Vector3(1, 0, 0),
                                  bool folded = false);
    static Vector3 vm_normalized(const Vector3 &value);
    static Quaternion apply_rotation_vector(const Vector3 &turns, double delta, const Quaternion &existing);
    static Quaternion apply_legacy_rotation_vector(const Vector3 &turns, double delta,
                                                   const Quaternion &existing, bool folded = false);
    static bool input_is_literal(const Ref<Resource> &operation, const String &key, const Variant &fallback);
    static bool space_is_simulation(const String &space, const Dictionary &context);
    static bool rotation_is_folded(const Ref<Resource> &operation, const Dictionary &context,
                                   bool literal_expression);
    static Quaternion random_quaternion(const Vector3 &sample);
    static Quaternion find_between(const Vector3 &from, const Vector3 &to);
    static Quaternion find_between_raw(const Vector3 &a, const Vector3 &b);
    static Quaternion from_axes(const Vector3 &x, const Vector3 &y, const Vector3 &z);
    static Quaternion stabilize(const Vector3 &facing, const Vector3 &side, const Vector3 &up,
                                const String &mode, const Vector3 &reference);
    static Quaternion override_orientation(const Quaternion &incoming, const Quaternion &value, double alpha);
};
class NBNativeMeshFacing : public RefCounted {
    GDCLASS(NBNativeMeshFacing, RefCounted)
  protected:
    static void _bind_methods();

  public:
    static Dictionary configuration(const Ref<Resource> &renderer);
    static Basis rotation(const Dictionary &config, const Vector3 &position, const Vector3 &velocity,
                          bool local, const Transform3D &owner, const Transform3D &camera);
};
} // namespace godot
