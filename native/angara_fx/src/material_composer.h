#pragma once
#include "material_programs.h"
namespace godot {
class NBNativeMaterialComposer : public RefCounted {
    GDCLASS(NBNativeMaterialComposer, RefCounted)
  protected:
    static void _bind_methods();

  public:
    static Dictionary build(const TypedArray<Dictionary> &contexts, const String &render_pass,
                            const Ref<Script> &schema);
    static Array common_modes(const Array &modes);
    static Dictionary check_binding_limits(int64_t textures, int64_t samplers, const Dictionary &reported,
                                           const String &driver);
};
} // namespace godot
