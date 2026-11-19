#pragma once
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/resource.hpp>
namespace godot {
class NBNativeParticleContext : public RefCounted {
    GDCLASS(NBNativeParticleContext, RefCounted)
  protected:
    static void _bind_methods();

  public:
    static Dictionary for_particle(Dictionary particle, Dictionary emitter, double delta, double time,
                                   const Transform3D &source_transform, const Dictionary &frame,
                                   const Dictionary &builtins, const Dictionary &extra);
    static Dictionary for_emitter(Dictionary emitter, double delta, double time, const Dictionary &frame);
    static void assign_emitter(const Ref<Resource> &operation, Dictionary emitter, double delta, double time,
                               const String &stage, const Dictionary &frame);
    static void refresh(Dictionary context, const Dictionary &particle, const Dictionary &builtins,
                        const Dictionary &extra);
    static void refresh_operation(const Ref<Resource> &operation, Dictionary context,
                                  const Dictionary &particle, const Dictionary &writes,
                                  const Dictionary &builtins, const Dictionary &extra);
};
} // namespace godot
