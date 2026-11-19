#pragma once
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/resource.hpp>
namespace godot {
class NBNativeRendererFilter : public RefCounted {
    GDCLASS(NBNativeRendererFilter, RefCounted)
  protected:
    static void _bind_methods();

  public:
    static bool int32(const Variant &value);
    static String binding_reference(const Ref<Resource> &renderer, const String &key, const String &property,
                                    const String &fallback);
    static bool host_binding(const Ref<Resource> &renderer, const String &key, const String &property,
                             const String &name);
    static Dictionary configuration(const Ref<Resource> &renderer, const Dictionary &types);
    static bool host(const String &name);
    static Dictionary apply(const Dictionary &config, const Array &particles, const Dictionary &parameters);
    static Variant input(const Ref<Resource> &renderer, const String &name,
                         const Variant &fallback = Variant());
};
} // namespace godot
