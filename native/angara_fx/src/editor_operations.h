#pragma once
#include "document_values.h"
#include <godot_cpp/classes/camera3d.hpp>
namespace godot {
class NBNativeEditorOperations : public RefCounted {
    GDCLASS(NBNativeEditorOperations, RefCounted)
  protected:
    static void _bind_methods();

  public:
    static String encode(const Ref<Resource> &module, const String &stage);
    static Dictionary decode(const String &text, const Ref<Script> &module_schema,
                             const Ref<Script> &input_schema);
    static Dictionary pack(const Variant &value);
    static Dictionary unpack(const Variant &encoded, int64_t depth);
    static TypedArray<Dictionary> timeline(const Ref<Resource> &effect, const Dictionary &dependencies,
                                           const Dictionary &contracts);
    static Dictionary snapshot_bounds(const Array &snapshot);
    static Vector3 frame(Camera3D *camera, const AABB &bounds, const Vector2 &viewport_size);
};
} // namespace godot
