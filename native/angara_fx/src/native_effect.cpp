#include "native_effect.h"
#include <godot_cpp/core/class_db.hpp>
#include <cmath>

using namespace godot;

void NBNativeParticleEffect::_bind_methods() {
#define NB_BIND(type, name) \
    ClassDB::bind_method(D_METHOD("set_" #name, "value"), &NBNativeParticleEffect::set_##name); \
    ClassDB::bind_method(D_METHOD("get_" #name), &NBNativeParticleEffect::get_##name); \
    ADD_PROPERTY(PropertyInfo(type, #name), "set_" #name, "get_" #name);
    NB_BIND(Variant::INT, schema_version)
    NB_BIND(Variant::INT, capacity)
    NB_BIND(Variant::INT, burst_count)
    NB_BIND(Variant::FLOAT, spawn_rate)
    NB_BIND(Variant::FLOAT, lifetime)
    NB_BIND(Variant::FLOAT, emission_duration)
    NB_BIND(Variant::VECTOR3, spawn_extents)
    NB_BIND(Variant::VECTOR3, initial_velocity)
    NB_BIND(Variant::VECTOR3, velocity_spread)
    NB_BIND(Variant::VECTOR3, acceleration)
    NB_BIND(Variant::FLOAT, drag)
    NB_BIND(Variant::FLOAT, particle_size)
    NB_BIND(Variant::COLOR, start_color)
    NB_BIND(Variant::COLOR, end_color)
    NB_BIND(Variant::INT, seed)
#undef NB_BIND
    ClassDB::bind_method(D_METHOD("validate"), &NBNativeParticleEffect::validate);
}

PackedStringArray NBNativeParticleEffect::validate() const {
    PackedStringArray errors;
    const auto &d = definition;
    if (d.schema_version != 1) errors.append("Unsupported native prototype schema_version.");
    if (d.capacity < 1 || d.capacity > 1000000) errors.append("capacity must be between1 and1000000.");
    if (d.burst_count < 0) errors.append("burst_count must be nonnegative.");
    if (!std::isfinite(d.spawn_rate) || d.spawn_rate < 0 || d.spawn_rate > 10000000) errors.append("Invalid spawn_rate.");
    if (!std::isfinite(d.lifetime) || d.lifetime <= 0) errors.append("lifetime must be finite and positive.");
    if (!std::isfinite(d.emission_duration) || d.emission_duration < 0) errors.append("Invalid emission_duration.");
    if (!std::isfinite(d.drag) || d.drag < 0) errors.append("drag must be finite and nonnegative.");
    if (!std::isfinite(d.particle_size) || d.particle_size <= 0) errors.append("particle_size must be finite and positive.");
    if (!d.spawn_extents.is_finite() || d.spawn_extents.x < 0 || d.spawn_extents.y < 0 || d.spawn_extents.z < 0) errors.append("Invalid spawn_extents.");
    if (!d.initial_velocity.is_finite() || !d.velocity_spread.is_finite() || !d.acceleration.is_finite()) errors.append("Nonfinite motion parameters.");
    for (const Color c : {d.start_color, d.end_color}) {
        if (!std::isfinite(c.r) || !std::isfinite(c.g) || !std::isfinite(c.b) || !std::isfinite(c.a)) errors.append("Nonfinite particle color.");
    }
    return errors;
}
