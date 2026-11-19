#pragma once
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/resource.hpp>
namespace godot {
class NBNativeParticleModules : public RefCounted {
    GDCLASS(NBNativeParticleModules, RefCounted)
    static void spawn_initialized(Dictionary emitter, double birth_time, const Transform3D &birth_transform,
                                  int64_t execution_index, int64_t execution_count, const Dictionary &frame,
                                  const Dictionary &schema, Dictionary particle);
  protected:
    static void _bind_methods();

  public:
    static void spawn(Dictionary emitter, double birth_time, const Transform3D &birth_transform,
                      int64_t execution_index, int64_t execution_count, const Dictionary &frame,
                      const Dictionary &schema);
    static void spawn_with_defaults(Dictionary emitter, double birth_time, const Transform3D &birth_transform,
                                    int64_t execution_index, int64_t execution_count, const Dictionary &frame,
                                    const Dictionary &schema, Dictionary &batch_defaults);
    static void update(Dictionary emitter, double delta, double time, const Dictionary &frame,
                       const Dictionary &schema);
    static bool execute_advanced(const Ref<Resource> &operation, Dictionary particle, Dictionary context,
                                 double delta);
    static bool execute(const Ref<Resource> &operation, Dictionary particle, Dictionary context, double delta,
                        const Dictionary &builtins);
};
} // namespace godot
