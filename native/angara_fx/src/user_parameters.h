#pragma once
#include <godot_cpp/classes/resource.hpp>

namespace godot {
class NBNativeUserParameters : public RefCounted {
    GDCLASS(NBNativeUserParameters, RefCounted)

  protected:
    static void _bind_methods();

  public:
    static String qualified_name(const String &name);
    static Ref<Resource> declaration(const Ref<Resource> &effect, const String &name);
    static Variant typed_value(const String &declared_type, const Variant &value);
};
}
