#pragma once
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/vector4.hpp>
#include <array>
#include <vector>
#include <cstdint>
#include "vm_external.h"
#include "collision_world.h"
#include "vm_kernel.h"

namespace godot {
class NBNativeParticleBuffer;
class NBNativeVMRandom : public RefCounted {
    GDCLASS(NBNativeVMRandom, RefCounted)
    std::array<uint32_t, 20> words{};
    std::array<uint32_t, 4> counters{};
    bool ready = false;

  protected:
    static void _bind_methods();

  public:
    bool configure(const Array &initial, const Array &counts);
    bool initialize_from_entropy(const Array &cycles, int64_t second, int64_t nonce);
    bool initialize_runtime();
    PackedInt64Array next_uint4();
    Vector4 next_fraction4();
    bool next(std::array<uint32_t, 4> &result);
    Array get_words() const;
    Array get_counters() const;
};

class NBNativeVMIDs : public RefCounted {
    GDCLASS(NBNativeVMIDs, RefCounted)
    std::vector<int32_t> table, free_ids, spawned;
    int32_t acquire_tag = 0, max_used = -1;

  protected:
    static void _bind_methods();

  public:
    void allocate(int required, int tag, bool maintain = false);
    Dictionary acquire(int count);
    TypedArray<String> update(const Array &ids, const Array &kept, int output_start);
    Dictionary snapshot() const;
    TypedArray<String> restore(const Dictionary &snapshot);
    int get_acquire_tag() const {
        return acquire_tag;
    }
    Array get_free() const {
        return snapshot()["free"];
    }
    Array get_table() const {
        return snapshot()["table"];
    }
    Array get_spawned() const {
        return snapshot()["spawned"];
    }
    int get_max_used() const {
        return max_used;
    }
};

class NBNativeVMProgram : public Resource {
    GDCLASS(NBNativeVMProgram, Resource)
    Dictionary definition;

  protected:
    static void _bind_methods();

  public:
    void set_definition(const Dictionary &value) {
        definition = value.duplicate(true);
        emit_changed();
    }
    Dictionary get_definition() const {
        return definition.duplicate(true);
    }
    TypedArray<String> validate() const;
};

class NBNativeVMReaders : public RefCounted {
    GDCLASS(NBNativeVMReaders, RefCounted)
    Dictionary current, previous;
    String phase = "system";
    bool raw_fields_needed = false;

  protected:
    static void _bind_methods();

  public:
    void begin_frame();
    void require_raw_fields() {
        raw_fields_needed = true;
    }
    bool needs_raw_fields() const {
        return raw_fields_needed;
    }
    void publish(const String &name, const Array &particles, const Ref<NBNativeVMIDs> &ids,
                 int spawned_count);
    void publish_buffer(const String &name, const Ref<NBNativeParticleBuffer> &buffer,
                         const Ref<NBNativeVMIDs> &ids, int spawned_count);
    Dictionary source(const Dictionary &binding, const String &scope) const;
    Dictionary capture(const Dictionary &bindings, const String &scope) const;
    static Dictionary bind(const Dictionary &info, const Dictionary &function);
    static Array sample(const Dictionary &binding, const Array &inputs, const Dictionary &data);
    static int index_for_id(const Dictionary &data, int id, int tag);
    Dictionary get_current() const;
    Dictionary get_previous() const;
    void set_phase(const String &value) {
        phase = value;
    }
    String get_phase() const {
        return phase;
    }
};

class NBNativeVMExecutor : public RefCounted {
    GDCLASS(NBNativeVMExecutor, RefCounted)
    struct Instruction {
        int kind = 0, primitive = 0, dataset = 0, function = 0;
        nbvm::ScalarKernel kernel = nullptr;
        std::vector<int> inputs, outputs;
    };
    struct Binding {
        int reg = 0, base = 0, dataset = 0, component = 0;
        bool integer = false, broadcast = false;
        Variant name, type;
        bool literal = false, previous = false;
        uint32_t literal_word = 0;
    };
    Dictionary execute_frame(const Dictionary &parameters, const Array &datasets, int count,
                             const Ref<NBNativeVMRandom> &random, const Dictionary &id_states,
                             bool packed_output, const Dictionary *particle_defaults);
    friend class NBNativeVMParticles;
    Dictionary program, curves;
    Array interfaces;
    TypedArray<String> errors;
    std::vector<Instruction> code;
    std::vector<Binding> constants, inputs, outputs;
    std::vector<nbvm::ExternalBinding> external;
    Ref<RefCounted> particle_context;
    uint64_t player_reader_id = 0;
    Ref<NBNativeCollisionWorld> collision_context;
    String reader_scope;
    Dictionary system_interfaces;
    bool allow_system_interfaces = false, uses_system_interfaces = false;
    double last_execute_usec = 0;
    int64_t executions = 0;
    bool last_output_packed = false;
    bool bind_external(const Dictionary &parameters);

  protected:
    static void _bind_methods();

  public:
    void set_system_interfaces(const Dictionary &state) {
        allow_system_interfaces = true;
        system_interfaces = state;
    }
    bool has_system_interfaces() const { return uses_system_interfaces; }
    static TypedArray<String> validate_program(const Dictionary &source);
    static int chunk_capacity(const Dictionary &source, int count);
    static Variant parameter_component(const Dictionary &row, const Dictionary &parameters);
    static Dictionary curve_table(const Dictionary &source, const Array &interfaces,
                                  const Dictionary &parameters = Dictionary());
    static Array sample_curve(const Dictionary &curve, double time);
    static TypedArray<String> curve_parameter_names(const Dictionary &info, const String &owner = "");
    TypedArray<String> configure(const Dictionary &source, const Array &data_interfaces = Array(),
                                 const Dictionary &parameters = Dictionary());
    TypedArray<String> update_user_curves(const Dictionary &parameters);
    Dictionary execute(const Dictionary &parameters, const Array &datasets, int count,
                       const Ref<NBNativeVMRandom> &random = Ref<NBNativeVMRandom>(),
                       const Dictionary &id_states = Dictionary(), bool packed_output = false);
    Dictionary get_program() const {
        return program.duplicate(true);
    }
    const Dictionary &source_program() const {
        return program;
    }
    Dictionary get_curves() const {
        return curves.duplicate(true);
    }
    Array get_interfaces() const {
        return interfaces.duplicate(true);
    }
    TypedArray<String> get_errors() const {
        return errors.duplicate();
    }
    void register_reader_fields(const Ref<RefCounted> &value) const {
        Ref<NBNativeVMReaders> readers = value;
        if (readers.is_valid())
            for (const auto &binding : external)
                if (binding.kind == nbvm::ExternalBinding::Reader)
                    readers->require_raw_fields();
    }
    void set_particle_context(const Ref<RefCounted> &value) {
        player_reader_id = 0;
        particle_context = value;
        register_reader_fields(value);
    }
    void bind_player_readers(const Ref<NBNativeVMReaders> &readers) {
        // The player owns its readers. A published buffer retains its projector
        // and executor, so retaining the readers here would close a cycle.
        particle_context.unref();
        player_reader_id = readers.is_valid() ? readers->get_instance_id() : 0;
        register_reader_fields(readers);
    }
    Ref<RefCounted> get_particle_context() const {
        if (player_reader_id)
            return Ref<RefCounted>(Object::cast_to<NBNativeVMReaders>(ObjectDB::get_instance(player_reader_id)));
        return particle_context;
    }
    void set_collision_context(const Ref<NBNativeCollisionWorld> &value) {
        collision_context = value;
    }
    Ref<NBNativeCollisionWorld> get_collision_context() const {
        return collision_context;
    }
    void set_reader_scope(const String &value) {
        reader_scope = value;
    }
    String get_reader_scope() const {
        return reader_scope;
    }
    Dictionary get_statistics() const;
};
} // namespace godot
