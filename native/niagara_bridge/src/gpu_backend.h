#pragma once
#include "grid_stages.h"
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <vector>
namespace godot {
class NBNativeGPUParticleBuffer;
class NBNativeVectorFieldGPU : public RefCounted {
    GDCLASS(NBNativeVectorFieldGPU, RefCounted)
  protected:
    static void _bind_methods();

  public:
    static int64_t float32_to_half(int64_t bits);
    static PackedByteArray texture_data(const Ref<Resource> &field);
    static RID create_texture(RenderingDevice *device, const Ref<Resource> &field);
    static RID create_sampler(RenderingDevice *device, const Vector3i &tiling = Vector3i(1, 1, 1));
};
class NBNativeGPUBackend : public RefCounted {
    GDCLASS(NBNativeGPUBackend, RefCounted)
    RenderingDevice *device = nullptr;
    RID shader, pipeline, particles, parameter_buffer, uniforms, output_count;
    std::vector<RID> field_textures, field_samplers;
    int64_t capacity = 0, width = 192;
    Dictionary program;
    Array particle_reader_columns;
    nbgrid::Execution grids;
    Ref<NBNativeGridVolumes> published_grids;
    Array grid_parameter_layouts;
    std::vector<uint32_t> positive_grid_parameters;
    Dictionary volume_textures;
    Dictionary phase_timings;
    bool profile_phases = false;
    String error_message;
    int64_t last_source_dispatch_counter = 0, last_dispatch_usec = 0, dispatch_count = 0,
            bytes_transferred = 0;
    bool resize(int64_t count);
    bool bind_buffers();
    int64_t parameter_capacity = 0;
    PackedByteArray dispatch_bytes(const PackedByteArray &upload, int64_t count,
                                  double delta, double time, double duration, bool local,
                                  const Transform3D &source, const Dictionary &parameters);

  protected:
    static void _bind_methods();

  public:
    ~NBNativeGPUBackend();
    static bool device_available();
    static int64_t get_source_dispatch_counter();
    static void set_source_dispatch_counter(int64_t value);
    bool initialize(const Dictionary &compiled);
    bool step_grids(const Dictionary &parameters, const Array &emitter_states);
    Dictionary grid_snapshot() const {
        return published_grids.is_valid() ? published_grids->snapshot()
                                          : (device ? grids.snapshot(device) : Dictionary());
    }
    Dictionary get_volume_textures() const {
        return volume_textures.duplicate();
    }
    bool run(const Array &values, double delta, double time, double duration, bool local,
             const Transform3D &source, const Dictionary &parameters);
    bool run_buffer(const Ref<NBNativeGPUParticleBuffer> &buffer, double delta, double time,
                    double duration, bool local, const Transform3D &source,
                    const Dictionary &parameters, bool compact = true);
    void dispose();
    void set_profile_phases(bool value) { profile_phases = value; }
    bool get_profile_phases() const { return profile_phases; }
    Dictionary get_phase_timings() const { return phase_timings.duplicate(); }
    PackedByteArray pack(const Array &values) const;
    bool unpack(const PackedByteArray &bytes, const Array &values) const;
    PackedFloat32Array parameter_data(double delta, double time, double duration, int64_t count, bool local,
                                      const Transform3D &source, const Dictionary &values) const;
    Dictionary get_program() const {
        return program;
    }
    const Array &get_particle_reader_columns() const { return particle_reader_columns; }
    void set_program(const Dictionary &value) {
        program = value;
    }
    String get_error_message() const {
        return error_message;
    }
    void set_error_message(const String &value) {
        error_message = value;
    }
    int64_t get_last_source_dispatch_counter() const {
        return last_source_dispatch_counter;
    }
    int64_t get_last_dispatch_usec() const {
        return last_dispatch_usec;
    }
    int64_t get_dispatch_count() const {
        return dispatch_count;
    }
    int64_t get_bytes_transferred() const {
        return bytes_transferred;
    }
};
} // namespace godot
