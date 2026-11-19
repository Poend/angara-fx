#pragma once
#include "particle_view.h"
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/typed_array.hpp>
namespace godot {
class NBNativeRibbonGeometry : public RefCounted {
    GDCLASS(NBNativeRibbonGeometry, RefCounted)
  protected:
    static void _bind_methods();

  public:
    static Dictionary settings(const Ref<Resource> &source, const String &key);
    static TypedArray<String> errors(const Ref<Resource> &source);
    static double acos_fast(double value);
    static Dictionary topology(const Array &particles, bool unique_id_order, double tension);
    static Dictionary resident_history(const Ref<NBNativeParticleView> &view,bool unique_id_order,double tension);
    static void update_history(const Dictionary &data, Dictionary history);
    static int tessellation(const Dictionary &data, const Ref<Resource> &source, Dictionary history,
                            double distance_cm, bool advance_history);
    static Vector3 uv_parameters(const Dictionary &uv, const Dictionary &data);
    static Vector2 uv_vector(const Variant &value, const Vector2 &fallback);
    static Dictionary build(const Array &particles, const Ref<Resource> &source, const Transform3D &space,
                            Camera3D *camera, Dictionary history, bool advance_history);
};
} // namespace godot
