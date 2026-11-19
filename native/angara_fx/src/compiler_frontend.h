#pragma once
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/typed_array.hpp>

namespace godot {
// Import/compiler checks are separate from executable VM capability validation.
class NBNativeCompilerAttributes : public RefCounted {
    GDCLASS(NBNativeCompilerAttributes, RefCounted)
  protected:
    static void _bind_methods();
  public:
    static String value_type(const Variant &value, const Dictionary &known,
                             const Dictionary &builtins, const Dictionary &dynamic_specs);
    static String assignment_error(const Variant &value, const String &type,
                                   const Dictionary &known, const Dictionary &builtins,
                                   const Dictionary &dynamic_specs);
    static TypedArray<String> references(const Variant &value);
    static Dictionary module_outputs(const Ref<Resource> &operation, const Dictionary &schemas);
    static Dictionary build(const Ref<Resource> &emitter, bool native_particles,
                              const Dictionary &builtins, const Dictionary &schemas,
                              const Dictionary &source_defaults = Dictionary());
};
class NBNativeProgramSchema : public RefCounted {
    GDCLASS(NBNativeProgramSchema, RefCounted)
  protected:
    static void _bind_methods();
  public:
    static bool unsigned_integer(const Variant &value);
    static bool binding(const Dictionary &row);
    static bool scalar(const Variant &value, const String &type);
    static bool same_constants(const Array &left, const Array &right);
    static TypedArray<String> validate(const Variant &value);
    static Dictionary extract_math(const String &hlsl, const String &assembly);
};
} // namespace godot
