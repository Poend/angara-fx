#pragma once
#include "material_authoring.h"
namespace godot {
class NBNativeSceneContext : public RefCounted {
    GDCLASS(NBNativeSceneContext, RefCounted)
  protected:
    static void _bind_methods();

  public:
    static TypedArray<String> validate(const Ref<Resource> &context);
    static bool matches_render_inputs(const Ref<Resource> &context, const Ref<Resource> &other);
    static bool same_gpu_values(const Variant &a, const Variant &b);
    static double number(const Dictionary &record, const String &key, TypedArray<String> errors);
    static bool boolean(const Dictionary &record, const String &key, TypedArray<String> errors);
    static Vector3 vector(const Dictionary &record, const String &key, TypedArray<String> errors);
    static Vector3 direction_ue(const Vector3 &value);
    static int64_t cvar(const Dictionary &values, const String &name, TypedArray<String> errors);
    static Dictionary from_packet(const Variant &packet, const Ref<Script> &schema,
                                  const Ref<Script> &fog_schema);
};
} // namespace godot
