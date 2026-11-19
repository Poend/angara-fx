#pragma once
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

namespace godot {

// Stage1 data contract: native constant-parameter emitter prototype, in Godot
// metres/Y-up. It does NOT claim to implement Niagara VM/import/collisions yet.
struct NBParticleDefinition {
    int schema_version = 1;
    int capacity = 100000;
    int burst_count = 2000;
    double spawn_rate = 1000.0;
    double lifetime = 4.0;
    double emission_duration = 0.0;
    Vector3 spawn_extents = Vector3(0.15, 0.05, 0.15);
    Vector3 initial_velocity = Vector3(0, 2.0, 0);
    Vector3 velocity_spread = Vector3(0.6, 0.5, 0.6);
    Vector3 acceleration = Vector3(0, -1.5, 0);
    double drag = 0.1;
    double particle_size = 0.035;
    Color start_color = Color(1.0, 0.55, 0.1, 1.0);
    Color end_color = Color(0.1, 0.35, 1.0, 0.0);
    int64_t seed = 42;
};

class NBNativeParticleEffect : public Resource {
    GDCLASS(NBNativeParticleEffect, Resource)
    NBParticleDefinition definition;
protected:
    static void _bind_methods();
public:
    const NBParticleDefinition &get_definition() const { return definition; }
    PackedStringArray validate() const;
#define NB_PROPERTY(type, name) \
    void set_##name(type value) { definition.name = value; emit_changed(); } \
    type get_##name() const { return definition.name; }
    NB_PROPERTY(int, schema_version)
    NB_PROPERTY(int, capacity)
    NB_PROPERTY(int, burst_count)
    NB_PROPERTY(double, spawn_rate)
    NB_PROPERTY(double, lifetime)
    NB_PROPERTY(double, emission_duration)
    NB_PROPERTY(Vector3, spawn_extents)
    NB_PROPERTY(Vector3, initial_velocity)
    NB_PROPERTY(Vector3, velocity_spread)
    NB_PROPERTY(Vector3, acceleration)
    NB_PROPERTY(double, drag)
    NB_PROPERTY(double, particle_size)
    NB_PROPERTY(Color, start_color)
    NB_PROPERTY(Color, end_color)
    NB_PROPERTY(int64_t, seed)
#undef NB_PROPERTY
};
}
