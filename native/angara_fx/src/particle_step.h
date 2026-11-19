#pragma once
#include "particle_buffer.h"
#include "vm_events.h"
#include "vm_program.h"
namespace godot {
class NBNativeVMParticleStep : public RefCounted {
    GDCLASS(NBNativeVMParticleStep, RefCounted)
  protected:
    static void _bind_methods();

  public:
    static Dictionary run(const Dictionary &emitter, const Dictionary &current, const Dictionary &previous,
                          const Array &handlers, const Ref<NBNativeVMReaders> &readers,
                          const Ref<NBNativeVMEvents> &events, int64_t tick);
};
} // namespace godot
