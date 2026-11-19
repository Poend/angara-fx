#pragma once
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/resource.hpp>
namespace godot {
class NBNativeTextSaver : public RefCounted {
    GDCLASS(NBNativeTextSaver, RefCounted)
  protected:
    static void _bind_methods();

  public:
    static int64_t make_byte_arrays_readable(const String &path);
    static int64_t save(const Ref<Resource> &resource, const String &path);
};
} // namespace godot
