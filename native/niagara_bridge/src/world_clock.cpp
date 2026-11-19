#include "world_clock.h"
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>
namespace godot {
void NBNativeWorldClock::_bind_methods() {
    ClassDB::bind_method(D_METHOD("snapshot"), &NBNativeWorldClock::snapshot);
    ClassDB::bind_static_method("NBNativeWorldClock", D_METHOD("values", "host"),
                                &NBNativeWorldClock::values);
}
NBNativeWorldClock::NBNativeWorldClock() {
    set_process_mode(PROCESS_MODE_ALWAYS);
    set_process_priority(-1000000);
    set_process(true);
    real_start = Time::get_singleton()->get_ticks_usec();
}
void NBNativeWorldClock::_process(double delta) {
    if (get_tree() && !get_tree()->is_paused())
        game_time += delta;
}
Dictionary NBNativeWorldClock::snapshot() const {
    Dictionary result;
    result["Engine.Time"] = float(game_time);
    result["Engine.RealTime"] =
        float(double(Time::get_singleton()->get_ticks_usec() - real_start) / 1000000.);
    return result;
}
Dictionary NBNativeWorldClock::values(Node *host) {
    if (!host || !host->is_inside_tree())
        return Dictionary();
    Window *root = host->get_tree()->get_root();
    const StringName key("_niagara_bridge_world_clock");
    if (root->has_meta(key)) {
        auto *clock = Object::cast_to<NBNativeWorldClock>(root->get_meta(key));
        if (clock)
            return clock->snapshot();
    }
    auto *clock = memnew(NBNativeWorldClock);
    clock->set_name("NiagaraBridgeWorldClock");
    // A player's first request may occur during the root's child-ready traversal.
    root->call_deferred("add_child", clock);
    root->set_meta(key, clock);
    return clock->snapshot();
}
} // namespace godot
