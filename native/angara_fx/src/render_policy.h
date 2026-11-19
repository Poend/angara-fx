#pragma once
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/script.hpp>
namespace godot {
class NBNativeRenderPolicy : public RefCounted {
    GDCLASS(NBNativeRenderPolicy, RefCounted)
  protected:
    static void _bind_methods();

  public:
    static Dictionary refraction(const Variant &policy);
    static String fingerprint(const Dictionary &policy);
    static Dictionary for_effect(const Ref<Resource> &effect);
    static bool valid_coverage_policy(const Variant &policy);
    static Dictionary coverage_configuration(const Ref<Resource> &renderer, const Dictionary &order,
                                             const Ref<Script> &program);
};
} // namespace godot
