#pragma once
#include "material_programs.h"
#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/classes/mesh.hpp>
namespace godot {
class NBNativeMaterialAuthoring : public RefCounted {
    GDCLASS(NBNativeMaterialAuthoring, RefCounted)
  protected:
    static void _bind_methods();

  public:
    static Ref<Material> bind(const Ref<Material> &material, const Ref<Mesh> &mesh, int kind,
                              TypedArray<Dictionary> diagnostics, const String &location, int surface,
                              const Variant &component_bounds);
    static void apply(const Ref<Resource> &effect, TypedArray<Dictionary> diagnostics);
    static Dictionary build(const String &manifest, const String &material, const PackedStringArray &evidence,
                            const Dictionary &textures, const Ref<Script> &schema);
    static Error save(const Dictionary &result, const String &directory, const String &basename);
    static Dictionary report(const Dictionary &result);
    static TypedArray<String> validate_height_fog(const Ref<Resource> &fog);
    static Dictionary height_fog_uniforms(const Ref<Resource> &fog);
};
} // namespace godot
