#pragma once
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/rendering_device.hpp>
namespace godot {
class NBNativeLightingVolume : public RefCounted {
    GDCLASS(NBNativeLightingVolume, RefCounted)
    Dictionary pipelines;
    Array owned;
    RID linear_sampler;
    uint64_t device_id = 0;
    static RID volume_texture(RenderingDevice *rd, int64_t dimension);

  protected:
    static void _bind_methods();

  public:
    ~NBNativeLightingVolume();
    static Dictionary configuration(const Ref<Resource> &context);
    static Vector3 ue(const Vector3 &value);
    static TypedArray<AABB> cascade_bounds(const Dictionary &settings, const Transform3D &camera,
                                           const Projection &projection);
    static PackedByteArray light_bytes(const Array &lights);
    TypedArray<String> initialize(RenderingDevice *rd);
    Dictionary build(RenderingDevice *rd, const Dictionary &configuration, const TypedArray<AABB> &bounds);
    void dispose(RenderingDevice *rd);
};
} // namespace godot
