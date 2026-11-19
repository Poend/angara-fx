#pragma once
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/resource.hpp>
namespace godot {
class NBNativeComponentBounds : public RefCounted {
  GDCLASS(NBNativeComponentBounds, RefCounted)
  Dictionary contract, policy, emitters, cached, system_local, component_local;
  Dictionary runtime_fixed_bounds;
  double force_time = 0.;
  TypedArray<String> errors;
  Dictionary configuration_error(const String &message);
  Dictionary emitter_bounds(const Dictionary &source,
                            const Dictionary &snapshot,
                            const Transform3D &owner) const;
  Dictionary fixed_bounds(const Dictionary &source) const;
  static Dictionary collect_for_policy(const Array &emitters, const NBNativeComponentBounds *policy);
  Dictionary aggregate(const Dictionary &cache) const;

protected:
  static void _bind_methods();

public:
  void set_runtime_fixed_bounds(const Dictionary &bounds) { runtime_fixed_bounds = bounds; }
  NBNativeComponentBounds();
  static Dictionary authored_state(const Ref<Resource> &effect);
  static Dictionary authored_inputs(const Array &inputs, const Array &keys);
  static Dictionary collect_snapshots(const Array &emitters);
  Dictionary collect_for_step(const Array &runtime_emitters) const {
    return collect_for_policy(runtime_emitters, this);
  }
  Dictionary configure(const Dictionary &contract);
  void reset();
  Dictionary step(const Array &snapshots, const Transform3D &owner,
                  double delta, bool system_tick, bool component_tick);
  static Dictionary simulation_bounds(const Dictionary &source,
                                      const Dictionary &snapshot,
                                      const Transform3D &owner);
  static Array calculators(const Dictionary &source);
  static Dictionary calculator_box(const Dictionary &calculator,
                                   const Dictionary &snapshot,
                                   const Transform3D &owner,
                                   const Array &minimum, const Array &maximum);
  static String check_calculators(const Dictionary &item);
  static Dictionary box(const Array &minimum, const Array &maximum);
  static Dictionary union_box(const Dictionary &left, const Dictionary &right);
  static Dictionary center_extent(const Array &center, const Array &extent,
                                  double multiplier);
  static Dictionary expand(const Dictionary &box, double multiplier);
  static bool contains(const Dictionary &outer, const Dictionary &inner);
  static Array transform_vector(const Array &vector, const Basis &basis);
  static Dictionary transform_box(const Dictionary &box, const Basis &basis,
                                  const Vector3 &translation);
  static Dictionary world_cm_to_local_cm(const Dictionary &box,
                                         const Transform3D &owner);
  static Dictionary to_world_godot(const Dictionary &box,
                                   const Transform3D &owner);
  static AABB aabb(const Dictionary &box);
  static bool valid_box(const Variant &value);
  static bool number(const Variant &value);
  static bool sha(const Variant &value);
  static String guid(const Variant &value);
  static bool finite_transform(const Transform3D &value);
  static double f32(double value);
  static Dictionary failure(const Array &messages);
};
} // namespace godot
