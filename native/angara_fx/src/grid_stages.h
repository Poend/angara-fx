#pragma once
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/texture3drd.hpp>
#include <vector>

namespace godot {
namespace nbgrid {
bool declaration(const Ref<Resource> &op);
bool reader_declaration(const Ref<Resource> &op);
// Returns the first unsupported source operation. No partial stage can be scheduled.
String compile(const Ref<Resource> &emitter, Dictionary &result);
class Execution {
    struct Field {
        String name;
        Vector3i size;
        Array channels;
        RID buffer;
        RID image;
        String target;
        Ref<Texture3DRD> texture;
        RID other;
        bool swapped = false;
        Dictionary attributes;
        RID samples[2];
    };
    struct Stage {
        int field;
        int iterations;
        String behavior;
        int external = -1;
        bool has_parameters = false;
        int iteration_slot = -1;
        bool scatter = false, clear_grid = false;
        RID particle_buffer, particle_set;
        int64_t particle_bytes = 0;
        RID shader, pipeline, uniforms[4];
    };
    std::vector<Field> fields;
    std::vector<Stage> stages;
    bool first_tick = true;
    RID sampler;

  public:
    String initialize(RenderingDevice *device, const Dictionary &program);
    String step(RenderingDevice *device, const Array &parameters, bool local = true);
    Dictionary textures() const;
    Dictionary snapshot(RenderingDevice *device) const;
    void clear(RenderingDevice *device);
};
} // namespace nbgrid

// Published volumes belong to the viewport's device. Queued work retains its owner
// until the render thread has consumed it, including when a player restarts mid-frame.
class NBNativeGridVolumes : public RefCounted {
    GDCLASS(NBNativeGridVolumes, RefCounted)
    nbgrid::Execution execution;
    static void prepare(Ref<NBNativeGridVolumes> job, Dictionary program, Dictionary receipt);
    static void advance(Ref<NBNativeGridVolumes> job, Array parameters);
    static void read(Ref<NBNativeGridVolumes> job, Dictionary receipt);
    static void release(Ref<NBNativeGridVolumes> job);

  protected:
    static void _bind_methods() {}

  public:
    String initialize(const Dictionary &program, Dictionary &textures);
    void step(const Array &parameters);
    Dictionary snapshot();
    void dispose();
};
} // namespace godot
