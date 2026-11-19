#pragma once
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/reg_ex.hpp>
#include <map>
#include <mutex>

namespace godot {
class NBNativeLegacyRNGShape : public RefCounted {
    GDCLASS(NBNativeLegacyRNGShape, RefCounted)
  protected:
    static void _bind_methods();

  public:
    static String signature(const String &body);
    static Dictionary inspect(const String &name, const Dictionary &functions, const Dictionary &environment);
    static bool zero_spawn_id_on_update(const String &source, const Dictionary &functions);
};
// Conservative emitted-source dependency analysis, not an HLSL interpreter.
class NBNativeRNGContract : public RefCounted {
    GDCLASS(NBNativeRNGContract, RefCounted)
    Dictionary functions, globals;
    Array calls, samples, diagnostics;
    int64_t counter = 0, unseeded_counter = 0;
    String source, entry;
    bool zero_spawn_id = false;
    std::map<String, Ref<RegEx>> patterns;
    mutable std::mutex lock;
    Ref<RegEx> regex(const String &pattern);
    void read_functions();
    Dictionary walk(const String &name, const Array &arguments, const Dictionary &environment,
                    const Array &stack, bool under_control);
    void record_rng(const String &primitive, const Array &arguments, const String &type,
                    const Dictionary &environment, const Array &stack, int64_t line, bool under_control,
                    bool tick_seed = false);
    void store(const String &target, Dictionary value, Dictionary environment);
    Array tags(const String &expression, const Dictionary &environment);
    Dictionary component_tags(const String &expression, const Dictionary &environment);
    static String component(const String &value);
    static Array statements(const String &body, int64_t first_line);
    static String resolve(const String &expression, const Dictionary &environment);
    String constant_literal(const String &expression, const Dictionary &environment);
    Variant constant_condition(const String &line, const Dictionary &environment);
    bool contains_rng_call(const String &text, const Array &seen = Array());
    Array tokens(const String &expression);
    static Array arguments(const String &expression);
    static Array unite(const Array &left, const Array &right);
    static int components(const String &type);
    static int64_t close_brace(const String &text, int64_t begin);
    int64_t line(int64_t offset) const;
    void error(const String &message, int64_t line);

  protected:
    static void _bind_methods();

  public:
    Dictionary extract(const String &hlsl, const String &entry_function);
    Dictionary get_functions() const;
};
} // namespace godot
