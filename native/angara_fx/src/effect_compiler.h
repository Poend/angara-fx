#pragma once
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <initializer_list>

namespace godot {
// Script-facing compatibility entry point; all plan and validation logic is native.
class NBNativeEffectCompiler : public RefCounted {
    GDCLASS(NBNativeEffectCompiler, RefCounted)
  protected:
    static void _bind_methods();

  public:
    static Variant invoke(const String &method, const Array &arguments, const Dictionary &dependencies,
                          const Dictionary &contracts);
};
namespace nbcompiler {
class Compiler {
  public:
    Dictionary dependencies, contracts, builtins, specs, fields;
    explicit Compiler(const Dictionary &dependencies, const Dictionary &contracts);
    Variant external(const String &dependency, const StringName &method, const Array &args) const;
    String type(const Variant &value, const Dictionary &known) const;
    static Variant input(const Ref<Resource> &object, const String &key, const Variant &fallback = Variant());
    static String mode(const Ref<Resource> &object, const String &key, const Variant &fallback = Variant());
    static void issue(Dictionary result, const Ref<Resource> &emitter, const Ref<Resource> &operation,
                      const String &severity, const String &message);
    Dictionary parameters(const Ref<Resource> &effect, const Dictionary &overrides) const;
    Dictionary compile(const Ref<Resource> &effect, Dictionary parameters, Dictionary options) const;
    int64_t dynamic_mask(const Dictionary &plan, const Ref<Resource> &renderer) const;
    bool check_value(const Variant &value, const Dictionary &parameters) const;
    bool math_arguments(const Dictionary &value, const Dictionary &parameters) const;
    bool index_arguments(const Dictionary &value, const Dictionary &parameters) const;
    bool uses_index(const Variant &value) const;
    String lifecycle_expression(const Variant &value, bool system) const;
    TypedArray<String> lifecycle_errors(const Ref<Resource> &operation, const Dictionary &parameters,
                                        const Dictionary &types, bool system) const;
    TypedArray<String> curve_errors(const Variant &value, const Dictionary &parameters) const;
    TypedArray<String> missing_parameters(const Variant &value, const Dictionary &parameters) const;
    static bool rate_supported(Variant value, const Dictionary &parameters);
    TypedArray<String> orientation_errors(const Ref<Resource> &operation, const Dictionary &types) const;
    TypedArray<String> torus_errors(const Ref<Resource> &operation, const Dictionary &types) const;
    static TypedArray<String> shape_errors(const Ref<Resource> &operation, const Dictionary &parameters);
    TypedArray<String> curl_errors(const Ref<Resource> &operation, const Dictionary &types) const;
    Dictionary attribute_types(const Ref<Resource> &effect, const Dictionary &layout) const;
    static String camera_offset(const Ref<Resource> &renderer);
    TypedArray<String> camera_errors(const Ref<Resource> &renderer, const Dictionary &types,
                                     const Dictionary &parameters, const Dictionary &layout) const;
    TypedArray<String> written_errors(const Ref<Resource> &renderer, const Dictionary &layout) const;
    static Dictionary spawn_order(const Ref<Resource> &emitter);
    static Vector3 mesh_scale(const Ref<Resource> &emitter);
    Dictionary mesh_renderer(const Ref<Resource> &renderer, const Dictionary &parameters, Dictionary result,
                             const Ref<Resource> &emitter) const;
    void validate_operation(const Ref<Resource> &emitter, const Ref<Resource> &operation, Dictionary plan,
                            Dictionary result, const Dictionary &parameters, const Dictionary &scoped,
                            const Dictionary &options) const;
    void renderer(const Ref<Resource> &effect, const Ref<Resource> &emitter, const Ref<Resource> &source,
                  Dictionary plan, Dictionary result, const Dictionary &parameters,
                  const String &backend) const;
};
bool number(const Variant &value);
bool equal(const Variant &a, const Variant &b);
bool named(const Variant &value, std::initializer_list<const char *> names);
Dictionary reference(const String &name);
void append(Array target, const Array &values);
} // namespace nbcompiler
} // namespace godot
