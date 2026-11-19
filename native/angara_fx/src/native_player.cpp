#include "native_player.h"
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>

using namespace godot;
using Clock = std::chrono::steady_clock;
static double elapsed_us(Clock::time_point start) { return std::chrono::duration<double, std::micro>(Clock::now() - start).count(); }

void NBNativeParticlePlayer::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_effect", "value"), &NBNativeParticlePlayer::set_effect);
    ClassDB::bind_method(D_METHOD("get_effect"), &NBNativeParticlePlayer::get_effect);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "effect", PROPERTY_HINT_RESOURCE_TYPE, "NBNativeParticleEffect"), "set_effect", "get_effect");
    ClassDB::bind_method(D_METHOD("set_autoplay", "value"), &NBNativeParticlePlayer::set_autoplay);
    ClassDB::bind_method(D_METHOD("get_autoplay"), &NBNativeParticlePlayer::get_autoplay);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "autoplay"), "set_autoplay", "get_autoplay");
    ClassDB::bind_method(D_METHOD("set_playing", "value"), &NBNativeParticlePlayer::set_playing);
    ClassDB::bind_method(D_METHOD("get_playing"), &NBNativeParticlePlayer::get_playing);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "playing"), "set_playing", "get_playing");
    ClassDB::bind_method(D_METHOD("restart"), &NBNativeParticlePlayer::restart);
    ClassDB::bind_method(D_METHOD("advance", "delta"), &NBNativeParticlePlayer::advance);
    ClassDB::bind_method(D_METHOD("refresh_render_data"), &NBNativeParticlePlayer::refresh_render_data);
    ClassDB::bind_method(D_METHOD("get_statistics"), &NBNativeParticlePlayer::get_statistics);
    ClassDB::bind_method(D_METHOD("get_particle_state", "index"), &NBNativeParticlePlayer::get_particle_state);
    ClassDB::bind_method(D_METHOD("_effect_changed"), &NBNativeParticlePlayer::effect_changed);
}

NBNativeParticlePlayer::~NBNativeParticlePlayer() {
    if (effect.is_valid() && effect->is_connected("changed", Callable(this, "_effect_changed"))) effect->disconnect("changed", Callable(this, "_effect_changed"));
}

void NBNativeParticlePlayer::set_effect(const Ref<NBNativeParticleEffect> &value) {
    if (effect == value) return;
    if (effect.is_valid() && effect->is_connected("changed", Callable(this, "_effect_changed"))) effect->disconnect("changed", Callable(this, "_effect_changed"));
    effect = value;
    if (effect.is_valid()) effect->connect("changed", Callable(this, "_effect_changed"));
    effect_changed();
}

void NBNativeParticlePlayer::effect_changed() {
    if (ready && !Engine::get_singleton()->is_editor_hint()) restart();
}

void NBNativeParticlePlayer::_ready() {
    ready = true;
    if (Engine::get_singleton()->is_editor_hint()) { set_process(false); return; }
    authored_multimesh = get_multimesh();
    restart();
    set_process(true);
}

bool NBNativeParticlePlayer::prepare_renderer() {
    if (!ready) return true;
    if (authored_multimesh.is_null() || authored_multimesh->get_mesh().is_null()) { error = "Assign a saved MultiMesh with an authored mesh/material before playback."; return false; }
    runtime_multimesh = authored_multimesh->duplicate(false);
    runtime_multimesh->set_instance_count(0);
    runtime_multimesh->set_transform_format(MultiMesh::TRANSFORM_3D);
    runtime_multimesh->set_use_colors(true);
    runtime_multimesh->set_use_custom_data(false);
    runtime_multimesh->set_instance_count(config.capacity);
    runtime_multimesh->set_visible_instance_count(0);
    set_multimesh(runtime_multimesh);
    instance_buffer.resize(int64_t(config.capacity) * 16);
    return true;
}

void NBNativeParticlePlayer::restart() {
    valid = false; error = "";
    positions.clear(); velocities.clear(); ages.clear(); ids.clear();
    simulation_time = 0; spawn_fraction = 0; emitted = 0; dropped_spawns = 0; simulation_steps = 0; simulation_usec = 0; upload_usec = 0;
    // An invalid initial resource must not leave the authored editor marker drawn.
    // Keep the saved MultiMesh untouched; only the runtime copy is hidden.
    if (ready && runtime_multimesh.is_null() && authored_multimesh.is_valid()) {
        runtime_multimesh = authored_multimesh->duplicate(false);
        set_multimesh(runtime_multimesh);
    }
    if (runtime_multimesh.is_valid()) runtime_multimesh->set_visible_instance_count(0);
    if (effect.is_null()) { error = "No native effect resource assigned."; return; }
    const PackedStringArray problems = effect->validate();
    if (!problems.is_empty()) { error = problems[0]; return; }
    config = effect->get_definition();
    positions.reserve(config.capacity); velocities.reserve(config.capacity); ages.reserve(config.capacity); ids.reserve(config.capacity);
    rng_state = uint64_t(config.seed) ^ UINT64_C(0x9e3779b97f4a7c15);
    if (rng_state == 0) rng_state = 1;
    if (!prepare_renderer()) return;
    valid = true;
    const int count = std::min(config.burst_count, config.capacity);
    for (int i = 0; i < count; ++i) spawn_one(0);
    dropped_spawns += uint64_t(config.burst_count - count);
    if (ready) upload_instances();
}

float NBNativeParticlePlayer::random_signed() {
    uint64_t x = rng_state; x ^= x >> 12; x ^= x << 25; x ^= x >> 27; rng_state = x;
    const uint64_t word = x * UINT64_C(2685821657736338717);
    return float((word >> 40) & UINT64_C(0xffffff)) * (2.0f / 16777216.0f) - 1.0f;
}

void NBNativeParticlePlayer::move_particle(size_t index, float delta) {
    velocities[index] += config.acceleration * delta;
    velocities[index] /= 1.0f + float(config.drag) * delta;
    positions[index] += velocities[index] * delta;
    ages[index] += delta;
}

void NBNativeParticlePlayer::spawn_one(float remaining_delta) {
    // Explicit sequencing keeps the sample order independent of argument evaluation order.
    const float ox = random_signed(), oy = random_signed(), oz = random_signed();
    const float sx = random_signed(), sy = random_signed(), sz = random_signed();
    const Vector3 offset(ox, oy, oz);
    const Vector3 spread(sx, sy, sz);
    positions.push_back(offset * config.spawn_extents);
    velocities.push_back(config.initial_velocity + spread * config.velocity_spread);
    ages.push_back(0); ids.push_back(emitted++);
    if (remaining_delta > 0) move_particle(positions.size() - 1, remaining_delta);
    if (ages.back() >= float(config.lifetime)) { positions.pop_back(); velocities.pop_back(); ages.pop_back(); ids.pop_back(); }
}

void NBNativeParticlePlayer::advance(double delta) {
    if (!valid || !std::isfinite(delta) || delta <= 0) return;
    const auto started = Clock::now();
    const float dt = float(delta);
    size_t write = 0;
    for (size_t index = 0; index < positions.size(); ++index) {
        move_particle(index, dt);
        if (ages[index] >= float(config.lifetime)) continue;
        if (write != index) { positions[write] = positions[index]; velocities[write] = velocities[index]; ages[write] = ages[index]; ids[write] = ids[index]; }
        ++write;
    }
    positions.resize(write); velocities.resize(write); ages.resize(write); ids.resize(write);
    const double emission_delta = config.emission_duration > 0 ? std::max(0.0, std::min(delta, config.emission_duration - simulation_time)) : delta;
    const double budget = spawn_fraction + config.spawn_rate * emission_delta;
    const uint64_t births = uint64_t(std::floor(budget));
    const uint64_t available = uint64_t(config.capacity) - positions.size();
    const uint64_t count = std::min(births, available);
    for (uint64_t index = 0; index < count; ++index) {
        const double birth_offset = (double(index) + 1.0 - spawn_fraction) / config.spawn_rate;
        spawn_one(float(std::max(0.0, delta - birth_offset)));
    }
    dropped_spawns += births - count; spawn_fraction = budget - double(births);
    simulation_time += delta; ++simulation_steps;
    simulation_usec = elapsed_us(started);
    if (ready) upload_instances();
}

void NBNativeParticlePlayer::_process(double delta) {
    if (autoplay && playing) advance(delta);
    else refresh_render_data();
}

void NBNativeParticlePlayer::refresh_render_data() { if (valid && ready) upload_instances(); }

void NBNativeParticlePlayer::upload_instances() {
    if (runtime_multimesh.is_null()) return;
    const auto started = Clock::now();
    Basis facing;
    if (get_viewport() != nullptr) {
        Camera3D *camera = get_viewport()->get_camera_3d();
        if (camera != nullptr) facing = get_global_transform().basis.inverse() * camera->get_global_transform().basis;
    }
    const Vector3 right = facing.get_column(0) * float(config.particle_size);
    const Vector3 up = facing.get_column(1) * float(config.particle_size);
    const Vector3 normal = facing.get_column(2) * float(config.particle_size);
    float *buffer = instance_buffer.ptrw();
    Vector3 minimum, maximum;
    for (size_t index = 0; index < positions.size(); ++index) {
        const Vector3 p = positions[index];
        if (index == 0) minimum = maximum = p;
        else { minimum = minimum.min(p); maximum = maximum.max(p); }
        const float phase = std::clamp(ages[index] / float(config.lifetime), 0.0f, 1.0f);
        const Color color = config.start_color.lerp(config.end_color, phase);
        float *row = buffer + index * 16;
        row[0] = right.x; row[1] = up.x; row[2] = normal.x; row[3] = p.x;
        row[4] = right.y; row[5] = up.y; row[6] = normal.y; row[7] = p.y;
        row[8] = right.z; row[9] = up.z; row[10] = normal.z; row[11] = p.z;
        row[12] = color.r; row[13] = color.g; row[14] = color.b; row[15] = color.a;
    }
    runtime_multimesh->set_buffer(instance_buffer);
    runtime_multimesh->set_visible_instance_count(int(positions.size()));
    if (!positions.empty()) runtime_multimesh->set_custom_aabb(AABB(minimum, maximum - minimum).grow(float(config.particle_size)));
    upload_usec = elapsed_us(started);
}

Dictionary NBNativeParticlePlayer::get_statistics() const {
    Dictionary result;
    result["native_execution"] = true; result["prototype_schema"] = 1; result["valid"] = valid; result["error"] = error;
    result["particle_count"] = int64_t(positions.size()); result["capacity"] = config.capacity;
    result["emitted"] = int64_t(emitted); result["dropped_spawns"] = int64_t(dropped_spawns); result["simulation_steps"] = int64_t(simulation_steps);
    result["simulation_time"] = simulation_time; result["simulation_usec"] = simulation_usec; result["upload_usec"] = upload_usec;
    result["physics_backend"] = ProjectSettings::get_singleton()->get_setting("physics/3d/physics_engine", "GodotPhysics3D");
    result["collision_implemented"] = false;
    result["niagara_vm_implemented"] = false;
    return result;
}

Dictionary NBNativeParticlePlayer::get_particle_state(int index) const {
    Dictionary state;
    if (index < 0 || size_t(index) >= positions.size()) return state;
    state["id"] = int64_t(ids[index]); state["position"] = positions[index]; state["velocity"] = velocities[index]; state["age"] = ages[index];
    state["color"] = config.start_color.lerp(config.end_color, std::clamp(ages[index] / float(config.lifetime), 0.0f, 1.0f));
    return state;
}
