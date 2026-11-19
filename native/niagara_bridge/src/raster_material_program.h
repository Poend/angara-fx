#pragma once
#include "material_programs.h"
namespace godot {
class NBNativeRasterProgram : public RefCounted {
    GDCLASS(NBNativeRasterProgram, RefCounted)
  protected:
    static void _bind_methods();

  public:
    static String source_key(const Ref<ShaderMaterial> &material);
    static Dictionary lower(const Dictionary &compiled, bool batch, const String &epilogue,
                            const String &extra_header, const String &vertex_epilogue);
};
} // namespace godot
