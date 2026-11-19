#pragma once
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/script.hpp>
namespace godot {
class NBNativeRuntimeOperations : public RefCounted {
    GDCLASS(NBNativeRuntimeOperations, RefCounted)
    static void references_depth(const Variant &value, Dictionary output, int depth);
    static Variant prepare_depth(const Variant &value, const String &type, int depth);

  protected:
    static void _bind_methods();

  public:
    static void refresh_emitters(const Array &emitters, const Ref<Script> &runtime_script,
                                 const Ref<Script> &input_script, const Dictionary &writes);
    static void references(const Variant &value, Dictionary output);
    static int64_t signature(const Ref<Resource> &operation);
    static Ref<Resource> create(const Ref<Resource> &operation, const Ref<Script> &runtime_script,
                                const Ref<Script> &input_script);
    static Variant prepare_value(const Variant &value, const String &type = String());
    static Variant input_value(const Dictionary &indexed, const String &key,
                               const Variant &fallback = Variant(), int64_t occurrence = 0);
};
} // namespace godot
