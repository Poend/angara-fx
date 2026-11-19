#pragma once
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
namespace godot {
class NBNativeSourceClock : public RefCounted {
    GDCLASS(NBNativeSourceClock, RefCounted)
    double delta = 1.0 / 60.0, engine_age = 0, simulation_age = 0;
    int64_t max_substeps = 100, dropped_substeps = 0;

  protected:
    static void _bind_methods();

  public:
    static double f32(double value);
    static Dictionary configuration(const Ref<Resource> &effect, bool native_comparison = false);
    void configure(const Dictionary &settings);
    int64_t take_steps(double engine_delta);
    void finish_step();
    double get_delta() const {
        return delta;
    }
    void set_delta(double value) {
        delta = value;
    }
    double get_engine_age() const {
        return engine_age;
    }
    void set_engine_age(double value) {
        engine_age = value;
    }
    double get_simulation_age() const {
        return simulation_age;
    }
    void set_simulation_age(double value) {
        simulation_age = value;
    }
    int64_t get_max_substeps() const {
        return max_substeps;
    }
    void set_max_substeps(int64_t value) {
        max_substeps = value;
    }
    int64_t get_dropped_substeps() const {
        return dropped_substeps;
    }
    void set_dropped_substeps(int64_t value) {
        dropped_substeps = value;
    }
};
class NBNativeDocument : public RefCounted {
    GDCLASS(NBNativeDocument, RefCounted)
  protected:
    static void _bind_methods();

  public:
    static Ref<Resource> find(const Array &records, const StringName &field, const String &name,
                              int64_t occurrence = 0);
    static Variant input_value(const Array &records, const String &name, const Variant &fallback = Variant(),
                               int64_t occurrence = 0);
};
class NBNativeLifecycle : public RefCounted {
    GDCLASS(NBNativeLifecycle, RefCounted)
  protected:
    static void _bind_methods();

  public:
    static Dictionary initial();
    static Dictionary context(const Dictionary &state, const String &prefix);
    static Dictionary configuration(const Ref<Resource> &operation, bool system = false);
    static Variant evaluate(const Dictionary &config, const String &name, const Variant &fallback,
                            const Dictionary &values);
    static String tick(Dictionary state, const Dictionary &config, double delta, const Dictionary &values);
    static String emitter_tick(Dictionary state, const Dictionary &config, const Dictionary &system,
                               int64_t count, double delta, const Dictionary &values);
    static void change(Dictionary state, int64_t next, int64_t source, bool system = false);
    static String tick_evaluated(Dictionary state, const Dictionary &config, double delta,
                                 const Variant &duration, const Variant &delay, const Variant &count);
    static void system_response(Dictionary state, const Dictionary &config, int64_t owner_state);
    static String emitter_response(Dictionary state, const Dictionary &config, const Dictionary &system,
                                   int64_t count);
};
} // namespace godot
