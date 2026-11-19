#pragma once
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/resource.hpp>
namespace godot {
class NBNativeParticleLights : public RefCounted {
    GDCLASS(NBNativeParticleLights, RefCounted)
  protected:
    static void _bind_methods();

  public:
    static bool number(const Variant &value);
    static bool int32(const Variant &value);
    static bool vector(const Variant &value, int64_t size);
    static bool typed(const Variant &value, const String &type);
    static TypedArray<String> policy_errors(const Variant &policy);
    static Dictionary configuration(const Ref<Resource> &renderer, const Dictionary &source_order);
    static bool proves_empty(const Dictionary &config);
    static TypedArray<String> translucency_configuration_errors(const Dictionary &config);
    static Dictionary generate(const Dictionary &config, const Array &particles, const Dictionary &parameters,
                               const Transform3D &owner, bool local, int64_t primitive_id = 0);
    static double exposure_scale(double exposure, double inverse_blend);
    static Dictionary translucency_lights(const Array &records, double exposure);
};
} // namespace godot
