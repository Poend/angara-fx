#pragma once
#include "material_composer.h"
#include "raster_material_program.h"
namespace godot {
class NBNativeMaterialDerived : public RefCounted {
    GDCLASS(NBNativeMaterialDerived, RefCounted)
  protected:
    static void _bind_methods();

  public:
    static TypedArray<String> validate(const Ref<ShaderMaterial> &material, const Ref<Script> &schema);
    static Dictionary current_values(const Ref<ShaderMaterial> &material);
    static Dictionary distortion(const Ref<ShaderMaterial> &material, const Ref<Script> &schema);
    static Dictionary surface(const Ref<ShaderMaterial> &material, const Ref<Script> &schema,
                              const Ref<Script> &fog_schema);
};
} // namespace godot
