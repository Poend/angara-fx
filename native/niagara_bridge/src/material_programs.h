#pragma once
#include "material_compiler.h"
#include <godot_cpp/classes/shader_material.hpp>
namespace godot {
class NBNativeMaterialPrograms : public RefCounted {
    GDCLASS(NBNativeMaterialPrograms, RefCounted)
  protected:
    static void _bind_methods();

  public:
    static Dictionary seal(const Dictionary &result);
    static TypedArray<String> validate(const Ref<ShaderMaterial> &material, const Ref<Script> &schema);
    static String graph_hash(const Ref<Resource> &graph);
    static int64_t fingerprint(const Ref<ShaderMaterial> &material);
    static TypedArray<String> cached_validate(const Ref<ShaderMaterial> &material, const Ref<Script> &schema);
    static Dictionary current_values(const Ref<ShaderMaterial> &material);
    static Dictionary seal_system(const String &shader, const TypedArray<Dictionary> &contexts,
                                  const TypedArray<Dictionary> &aliases, const Dictionary &defaults);
    static TypedArray<String> validate_system(const Ref<ShaderMaterial> &material, const Ref<Script> &schema);
    static int64_t fingerprint_system(const Ref<ShaderMaterial> &material);
    static TypedArray<String> cached_validate_system(const Ref<ShaderMaterial> &material,
                                                     const Ref<Script> &schema);
    static String context_hash(const Array &contexts, const Array &aliases);
    static Dictionary system_values(const Ref<ShaderMaterial> &material);
    static TypedArray<Dictionary> source_contexts(const Ref<ShaderMaterial> &material);
    static void clear_cache();
    static int64_t shader_code_hash(const Ref<Shader> &shader);
    static String shader_code_sha256(const Ref<Shader> &shader);
    static Dictionary shader_cache_statistics();
};
} // namespace godot
