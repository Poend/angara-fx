#pragma once
#include "particle_view.h"
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <vector>
namespace godot {
struct NBRendererRow {
    int64_t lane = 0;
    size_t overlay_offset = 0;
};
// Invocation-local binding writes share one schema and one flat allocation.
// Rows retain source lanes through filtering/sorting; simulation is untouched.
struct NBRendererRows {
    struct Write { Variant key; bool attribute = false; };
    Ref<NBNativeParticleView> source;
    std::vector<NBRendererRow> rows;
    std::vector<Write> writes;
    std::vector<int64_t> slots;
    std::vector<Variant> values;
    int64_t velocity_slot = -1, facing_slot = -1, alignment_slot = -1;
    TypedArray<String> errors;
    Variant field(const NBRendererRow &row, int64_t handle, const Variant &fallback = Variant()) const;
    template<class T> T field_native(const NBRendererRow &row,int64_t handle,const T &fallback=T()) const {
        if(handle>=0 && size_t(handle)<slots.size() && slots[size_t(handle)]>=0)
            return T(values[row.overlay_offset+size_t(slots[size_t(handle)])]);
        return source->has_field(handle,row.lane) ? source->read_native<T>(handle,row.lane) : fallback;
    }
    Variant attribute(const NBRendererRow &row, int64_t handle, const Variant &fallback) const;
    Variant special(const NBRendererRow &row, int64_t slot, const Variant &fallback) const;
    Dictionary overrides(const NBRendererRow &row) const;
    void sort(const Ref<Resource> &renderer, Camera3D *camera, const Transform3D &space);
};
} // namespace godot
