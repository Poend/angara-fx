#pragma once
#include "native_effect.h"
#include <godot_cpp/classes/multi_mesh_instance3d.hpp>
#include <godot_cpp/classes/multi_mesh.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <vector>
#include <cstdint>

namespace godot {
class NBNativeParticlePlayer : public MultiMeshInstance3D {
    GDCLASS(NBNativeParticlePlayer, MultiMeshInstance3D)
    Ref<NBNativeParticleEffect> effect;
    Ref<MultiMesh> authored_multimesh;
    Ref<MultiMesh> runtime_multimesh;
    NBParticleDefinition config;
    std::vector<Vector3> positions, velocities;
    std::vector<float> ages;
    std::vector<uint64_t> ids;
    PackedFloat32Array instance_buffer;
    uint64_t rng_state = 1, emitted = 0, dropped_spawns = 0, simulation_steps = 0;
    double simulation_time = 0, spawn_fraction = 0;
    double simulation_usec = 0, upload_usec = 0;
    bool autoplay = true, playing = true, ready = false, valid = false;
    String error;
    float random_signed();
    void spawn_one(float remaining_delta);
    void move_particle(size_t index, float delta);
    void upload_instances();
    bool prepare_renderer();
    void effect_changed();
protected:
    static void _bind_methods();
public:
    ~NBNativeParticlePlayer() override;
    void _ready() override;
    void _process(double delta) override;
    void set_effect(const Ref<NBNativeParticleEffect> &value);
    Ref<NBNativeParticleEffect> get_effect() const { return effect; }
    void set_autoplay(bool value) { autoplay = value; }
    bool get_autoplay() const { return autoplay; }
    void set_playing(bool value) { playing = value; }
    bool get_playing() const { return playing; }
    void restart();
    void advance(double delta);
    void refresh_render_data();
    Dictionary get_statistics() const;
    Dictionary get_particle_state(int index) const;
};
}
