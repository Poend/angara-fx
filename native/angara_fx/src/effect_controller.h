#pragma once
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/script.hpp>
namespace godot {
class NBNativeVMExecutor;
class NBNativeEffectController : public RefCounted {
    GDCLASS(NBNativeEffectController, RefCounted)
  protected:
    static void _bind_methods();

  public:
    static void restart(Node3D *player, const Ref<Script> &compiler, const Ref<Script> &lifecycle_module,
                        const Dictionary &prepared = Dictionary());
    static void release(Node *player);
    static TypedArray<String> bind_world(Node3D *player, const Ref<NBNativeVMExecutor> &executor,
                                         const String &scope);
    static void collision_failure(Node *player, const Array &errors);
    static void ribbon_history(Dictionary emitter);
    static void collect_curve_parameters(Node *player, const Variant &value, Dictionary result);
    static Dictionary curve_parameters(Node *player, const Dictionary &plan);
    static void refresh_curves(Node *player, const Ref<Script> &gpu_program);
    static void schedule_spawn(Node *player, Dictionary emitter, double delta, bool include_initial);
    static void schedule_source(Node *player, Dictionary emitter, double delta);
    static void spawn_events(Node *player, Dictionary emitter, const Array &events, double delta);
    static int64_t bounds_signature(Node *player);
    static void prepare_bounds(Node3D *player);
    static void check_bounds(Node *player);
    static void advance_bounds(Node *player, double delta);
    static void mark_partial_bounds(Node *player);
    static void sync_primitive_bounds(Node *player);
    static void advance(Node3D *player, double seconds, const Ref<Script> &compiler);
    static void seek(Node *player, double time);
    static void step(Node3D *player, double delta);
    static void simulate(Node *player, double delta);
    static Dictionary frame(Node *player, Dictionary emitter, double delta, double time);
    static bool run_system(Node *player, const String &stage, double delta);
    static void run_vm(Node *player, Dictionary emitter, double delta, double time);
    static void run_gpu(Node *player, Dictionary emitter, double delta, double time);
    static void vm_failure(Node *player, Dictionary emitter, const Array &errors);
    static bool publish_events(Node *player, Dictionary emitter, const Dictionary &events);
    static Array handlers(const Dictionary &plan);
};
} // namespace godot
