#pragma once
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/resource.hpp>
namespace godot {
class NBNativeSpawnScheduler : public RefCounted {
    GDCLASS(NBNativeSpawnScheduler, RefCounted)
  protected:
    static void _bind_methods();

  public:
    static Dictionary burst_10(const Ref<Resource> &operation, double age, double delta);
    static void append_legacy(Dictionary emitter, const Array &births, double delta, const Dictionary &frame,
                              const Dictionary &schema, const Transform3D &start, const Transform3D &end);
    static Dictionary append_events(Dictionary emitter, const Array &events, double delta, int64_t maximum,
                                    const Dictionary &frame, const Dictionary &schema,
                                    const Transform3D &start, const Transform3D &end);
    static Dictionary source(Dictionary emitter, const Dictionary &system, double time, double delta,
                             double lifecycle_delta, const Callable &context, const Callable &assignment);
    static Dictionary legacy(Dictionary emitter, double time, double delta, int64_t maximum,
                             bool include_initial, const Callable &context, const Callable &assignment);
};
} // namespace godot
