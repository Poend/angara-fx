#pragma once
#include "particle_view.h"
#include "vm_program.h"
#include <godot_cpp/variant/transform3d.hpp>

namespace godot {
class NBNativeParticleBuffer;
class NBNativeVMDataset : public RefCounted {
    GDCLASS(NBNativeVMDataset, RefCounted)
    Dictionary variables, columns;
    TypedArray<String> errors;

  protected:
    static void _bind_methods();

  public:
    TypedArray<String> configure(const Variant &layout);
    Dictionary pack(const Dictionary &values, const Array &bindings, int dataset_index) const;
    Dictionary unpack(const Dictionary &dataset, int lane = 0) const;
    static Dictionary components(const Variant &value, const Dictionary &row);
    static Variant value(const Dictionary &raw);
    Dictionary get_variables() const {
        return variables;
    }
    Dictionary get_columns() const {
        return columns;
    }
    TypedArray<String> get_errors() const {
        return errors;
    }
};
class NBNativeVMParticles : public RefCounted {
    GDCLASS(NBNativeVMParticles, RefCounted)
    Ref<NBNativeVMExecutor> executor;
    Array variables;
    Dictionary columns, event_layouts, external_defaults, last_execution;
    bool ready = false, capture_execution = false;
    uint64_t layout_revision = 0;
    Dictionary execute_dataset(const Dictionary &dataset, const Array &templates, const Dictionary &current,
                               const Dictionary &previous, const Ref<NBNativeVMRandom> &random,
                               const Dictionary &event_inputs, const Ref<NBNativeVMIDs> &ids, int offset,
                               const Ref<NBNativeParticleBuffer> &resident);

  protected:
    static void _bind_methods();

  public:
    static Ref<NBNativeParticleView> create_view(const Dictionary &dataset, const Array &metadata,
                                                 const Array &layout, const PackedInt32Array &indices,
                                                 const PackedInt32Array &counts);
    NBNativeVMParticles();
    uint64_t get_layout_revision() const {
        return layout_revision;
    }
    const Array &source_layout() const {
        return variables;
    }
    TypedArray<String> configure(const Dictionary &program, const Array &interfaces = Array(),
                                 const Dictionary &parameters = Dictionary(),
                                 const Dictionary &contract = Dictionary());
    Dictionary pack(const Array &particles, bool packed_columns = false) const;
    Dictionary import_buffer(const Array &particles);
    Dictionary run_buffer(const Ref<NBNativeParticleBuffer> &buffer, const Dictionary &current,
                          const Dictionary &previous,
                          const Ref<NBNativeVMRandom> &random = Ref<NBNativeVMRandom>(),
                          const Dictionary &event_inputs = Dictionary(),
                          const Ref<NBNativeVMIDs> &ids = Ref<NBNativeVMIDs>(), int offset = 0);
    Dictionary unpack(const Dictionary &dataset, const Array &templates, const Array &kept,
                      bool include_raw_fields = true) const;
    static Dictionary project_dataset(const Dictionary &dataset, const Array &templates, const Array &kept,
                                      const Array &layout, bool include_raw_fields,
                                      bool deep_metadata = false,
                                      const PackedInt32Array &update_indices = PackedInt32Array(),
                                      const PackedInt32Array &update_counts = PackedInt32Array());
    Dictionary run(const Array &particles, const Dictionary &current, const Dictionary &previous,
                   const Ref<NBNativeVMRandom> &random = Ref<NBNativeVMRandom>(),
                   const Dictionary &event_inputs = Dictionary(),
                   const Ref<NBNativeVMIDs> &ids = Ref<NBNativeVMIDs>(), int offset = 0);
    static Dictionary parameter_values(const Dictionary &values);
    static Dictionary type_table();
    static Array existing_particles(const Array &particles);
    static void clear_spawn_flags(const Array &particles);
    static Dictionary append_spawn_templates(Dictionary emitter, int count, double time, double delta,
                                             double start_dt, double interval,
                                             const Transform3D &source_start, const Transform3D &source_end);
    static void project_field(Dictionary particle, const Dictionary &row, const Dictionary &raw);
    Ref<NBNativeVMExecutor> get_executor() const {
        return executor;
    }
    Array get_variables() const {
        return variables;
    }
    Dictionary get_columns() const {
        return columns;
    }
    Dictionary get_event_layouts() const {
        return event_layouts;
    }
    bool get_ready() const {
        return ready;
    }
    void set_capture_execution(bool v) {
        capture_execution = v;
    }
    bool get_capture_execution() const {
        return capture_execution;
    }
    Dictionary get_last_execution() const {
        return last_execution;
    }
};
class NBNativeVMSystem : public RefCounted {
    GDCLASS(NBNativeVMSystem, RefCounted)
    Dictionary stages, state, previous_parameters, last_execution, interface_state;
    mutable Dictionary packed_state;
    bool ready = false, capture_execution = false;

  protected:
    static void _bind_methods();

  public:
    TypedArray<String> configure(const Dictionary &contracts, const Dictionary &parameters = Dictionary());
    Dictionary run(const String &stage, const Dictionary &parameters, const Dictionary &instance_inputs);
    Dictionary run_for_step(const String &stage, const Dictionary &parameters, const Dictionary &instance_inputs,
                            bool copy_state = false);
    Dictionary flat_values() const;
    Dictionary spawn_events(const Array &names) const;
    Dictionary get_stages() const {
        return stages;
    }
    Dictionary get_state() const {
        // Scripts can edit the returned dictionary before the next tick.
        packed_state = Dictionary();
        return state;
    }
    Dictionary get_interface_state() const { return interface_state.duplicate(true); }
    Variant interface_value(const String &key) const { return interface_state.get(key, Variant()); }
    bool get_ready() const {
        return ready;
    }
    void set_capture_execution(bool v) {
        capture_execution = v;
    }
    bool get_capture_execution() const {
        return capture_execution;
    }
    Dictionary get_last_execution() const {
        return last_execution;
    }
};
class NBNativeVMParameters : public RefCounted {
    GDCLASS(NBNativeVMParameters, RefCounted)
  protected:
    static void _bind_methods();

  public:
    static Array matrix(const Transform3D &value);
    static Dictionary frame(const Dictionary &context, const Transform3D &owner,
                            const Transform3D &previous_owner, double delta, int total_spawned);
    static Dictionary spawn(const Dictionary &frame, const String &emitter_name, int total, int count,
                            double interval, double start, int group);
};
} // namespace godot
