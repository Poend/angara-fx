#pragma once
#include "particle_view.h"
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/variant/typed_array.hpp>
namespace godot {
// Runtime-only authoritative GPU ABI state. Dictionary projection is explicit;
// compaction and copies retain packed numerical words without decoding fields.
class NBNativeGPUParticleBuffer : public RefCounted {
    GDCLASS(NBNativeGPUParticleBuffer, RefCounted)
    PackedByteArray bytes;
    PackedByteArray executed_rows, tick_rows;
    Array metadata;
    Dictionary layout;
    int64_t stride_bytes = 0;
    mutable Ref<NBNativeParticleView> cached_view;
  protected:
    static void _bind_methods();
  public:
    TypedArray<String> import_particles(const Dictionary &program, const Array &particles);
    TypedArray<String> append_particles(const Array &particles);
    TypedArray<String> commit_output(const PackedByteArray &output, bool compact = true);
    Dictionary snapshot() const;
    Array metadata_rows() const;
    Ref<NBNativeParticleView> get_view() const;
    TypedArray<String> project_into(const Array &particles) const;
    Ref<NBNativeGPUParticleBuffer> duplicate_buffer() const;
    void prepare_tick_metadata();
    bool matches_layout(const Dictionary &program) const;
    int64_t get_count() const { return metadata.size(); }
    int64_t get_stride_bytes() const { return stride_bytes; }
    PackedByteArray get_bytes() const { return bytes; }
    PackedByteArray dispatch_bytes() const;
    Dictionary get_statistics() const;
};
class NBNativeGPUBuffers : public RefCounted {
    GDCLASS(NBNativeGPUBuffers, RefCounted)
  protected:
    static void _bind_methods();

  public:
    static PackedByteArray pack(const Array &particles, const Dictionary &program);
    static bool unpack(const PackedByteArray &bytes, const Array &particles, const Dictionary &program);
    static PackedFloat32Array parameters(const Dictionary &program, double delta, double time,
                                         double duration, int64_t count, bool local,
                                         const Transform3D &source, const Dictionary &values);
};
} // namespace godot
