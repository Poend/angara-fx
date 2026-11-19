#pragma once
#include "particle_view.h"
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>

namespace godot {
class NBNativeVMParticles;
// Explicit simulation state. Snapshots are projections, never authoritative inputs.
// Stored columns use the exported Niagara layout, including custom attributes.
class NBNativeParticleBuffer : public RefCounted {
    GDCLASS(NBNativeParticleBuffer, RefCounted)
    Array variables, templates;
    Dictionary dataset;
    // Metadata dictionaries remain immutable between projections. Negative
    // sidecar entries retain the template's original keys during mixed appends.
    PackedInt32Array update_indices, update_counts;
    Ref<NBNativeVMParticles> projector;
    uint64_t layout_revision = 0;
    mutable Ref<NBNativeParticleView> cached_view;

  protected:
    static void _bind_methods();

  public:
    void initialize(const Ref<NBNativeVMParticles> &adapter, const Dictionary &data, const Array &metadata,
                    bool import_metadata = false);
    Dictionary input_for(const Ref<NBNativeVMParticles> &adapter, const Array &bindings) const;
    Dictionary snapshot(bool include_raw_fields = true) const;
    Array metadata_rows() const;
    Ref<NBNativeParticleView> get_view() const;
    TypedArray<String> append(const Ref<NBNativeParticleBuffer> &other);
    Ref<NBNativeParticleBuffer> duplicate_buffer() const;
    int64_t get_count() const {
        return templates.size();
    }
    const Array &metadata() const {
        return templates;
    }
    void prepare_tick_metadata();
    void inherit_tick_metadata(const Ref<NBNativeParticleBuffer> &source, const Array &kept);
    Dictionary get_statistics() const;
};
} // namespace godot
