#pragma once
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <initializer_list>

namespace godot {
class NBNativeGPUProgram : public RefCounted {
    GDCLASS(NBNativeGPUProgram, RefCounted)
  protected:
    static void _bind_methods();

  public:
    static Dictionary build(const Dictionary &plan, const Dictionary &parameters,
                            const Dictionary &contracts);
};
namespace nbgpu {
String cat(std::initializer_list<String> parts);
String integer(int64_t value);
String joined(const Array &values, const String &delimiter);
class Builder {
  public:
    Dictionary contracts, builtins, specs, fields, references;
    Array errors, helpers, parameters, vector_fields;
    Dictionary cache_slots, user_parameters, static_parameters, source_rng, attribute_layout, attribute_rows,
        attribute_types, module_outputs, module_output_types, literal_outputs;
    Ref<Resource> operation;
    String stage_name;
    bool interpolated_spawn = false;
    bool emitter_deterministic = false;
    int64_t emitter_seed = 0, cache_limit = 16, stride = 48;
    explicit Builder(const Dictionary &contracts);
    Dictionary build(const Dictionary &plan, const Dictionary &parameters);
    String stage(const Array &operations);
    String module();
    Dictionary reader_modules;
    Array reader_columns;
    String assignment();
    String initialize();
    String shape();
    String box_point();
    String kill_volume();
    String sphere();
    String velocity();
    String point_velocity();
    String particle_state();
    String orientation();
    String typed_read(const String &expression, const String &actual, const String &expected);
    String input(const String &name, const Variant &fallback, const String &type = "float",
                 int occurrence = 0);
    Variant source_curve(const String &path, const Variant &fallback);
    String intrinsic(const String &target, std::initializer_list<String> bounds, const String &type, int module_draw = 0);
    String native_range(const String &path, std::initializer_list<String> bounds, const String &type,
                        bool as_integer = false);
    String int_parameter(const String &name);
    String torus();
    String vortex();
    String line_force();
    String curl();
    String expression(const Variant &value, const String &type, const String &path);
    String curve(Variant value, int components = 1);
    String literal(const Variant &value, const String &type);
    String number(const Variant &value);
    String space(const String &key, const String &fallback);
    String orientation_space();
    String mesh_facing();
    String mode(const String &key, const String &fallback);
    bool flag(const String &key, bool fallback);
    String unsupported(const String &branch);
};
} // namespace nbgpu
} // namespace godot
