#pragma once
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/typed_array.hpp>

namespace godot {
class NBNativeSourceProgram : public RefCounted {
    GDCLASS(NBNativeSourceProgram, RefCounted)
  protected:
    static void _bind_methods();

  public:
    static String system_fingerprint(const Ref<Resource> &effect);
    static String fingerprint(const Ref<Resource> &emitter);
    static Dictionary build(const Ref<Resource> &effect, const Dictionary &plan,
                            const Dictionary &parameters = Dictionary());
    static Variant invoke(const String &method, const Array &arguments);
};
namespace nbsource {
bool signed32(const Variant &value);
Dictionary instance_int_parameter(const Dictionary &contract, const String &expression);
void bind(const Array &calls, Array used, Dictionary bindings, const String &key, const String &target,
          int components, Array errors);
void bind_intrinsic(const Array &calls, Array used, Dictionary bindings, const String &key,
                    const String &alias, const String &helper, int components, int count, Array errors);
void bind_curl(const Array &calls, Array used, Dictionary bindings, const Ref<Resource> &operation,
               const String &alias, int64_t emitter_seed, Array errors);
void bind_orientation(const Array &calls, Array used, Dictionary bindings, const Ref<Resource> &operation,
                      const String &alias, Array errors);
void bind_color(const Array &calls, Array used, Dictionary bindings, const String &key, const String &alias,
                const String &component, Array errors);
int64_t random_count(const Variant &value);
Array random_leaves(const Variant &value, const String &path);
Array curve_leaves(const Variant &value, const String &path, const Array &arguments = Array(),
                   const Array &calls = Array());
String curve_identifier(const String &name);
String curve_owner(const Dictionary &sample);
Dictionary curve_sample(const Dictionary &contract, const String &target, const Array &arguments,
                        const Array &calls = Array());
Dictionary compiled_curve(const Dictionary &contract, const String &owner, const String &emitter_name,
                          Array errors);
Dictionary bind_curves(const Array &operations, const Dictionary &contract, const String &emitter_name,
                       Array errors);
void bind_emitter_spawn(const Dictionary &plan, const Dictionary &packet, Dictionary result);
void bind_lifecycle(const Ref<Resource> &effect, const Dictionary &plan, const Dictionary &packet,
                    Dictionary result);
Dictionary module_programs(const Dictionary &plan, const Dictionary &packet, Dictionary result);
} // namespace nbsource
} // namespace godot
