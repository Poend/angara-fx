#pragma once
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <array>
#include <vector>
#include <cstdint>
namespace godot {
class NBNativeCollisionWorld;
}
namespace nbvm {
using ExternalInputs = std::array<uint32_t, 16>;
using ExternalOutputs = std::array<uint32_t, 16>;
struct RichKey {
    float time = 0, value = 0, arrive = 0, leave = 0;
    int interpolation = 0;
};
struct RichChannel {
    std::vector<RichKey> keys;
    float default_value = 0;
    godot::String pre, post;
    double sample(double time) const;
};
struct ExternalBinding {
    enum Kind { Curve, Field, Reader, Collision, SpriteRenderer, VolumeControl, MatrixQuaternion, ObjectReader, Bounds, DebugBox };
    Kind kind = Curve;
    int width = 0;
    godot::String function;
    godot::Dictionary metadata;
    std::vector<float> lut;
    std::vector<RichChannel> rich;
    double minimum = 0, inverse = 0;
    std::array<int, 3> size{}, tiling{};
    std::array<double, 3> bounds_min{}, bounds_max{};
    bool cpu_access = false;
    std::array<bool, 16> integer_outputs{};
    std::array<uint32_t, 3> renderer_values{};
    bool bind(const godot::Dictionary &info, const godot::Dictionary &signature,
              const godot::Dictionary &parameters, godot::TypedArray<godot::String> &errors);
    godot::String bind_volume(const godot::Dictionary &info, const godot::Dictionary &signature);
    void control_volume(const ExternalInputs &inputs, godot::Dictionary &state, ExternalOutputs &outputs) const;
    bool writes_system_state() const { return kind == VolumeControl || kind == Bounds || kind == DebugBox; }
    bool has_instance_pointer() const { return kind == Reader || kind == Collision || kind == ObjectReader || writes_system_state(); }
    void sample(const ExternalInputs &inputs, const godot::Dictionary &reader_data,
                ExternalOutputs &outputs) const;
    bool collide(const ExternalInputs &inputs, godot::NBNativeCollisionWorld &world,
                 const godot::String &scope, ExternalOutputs &outputs) const;
    static int reader_index(const godot::Dictionary &data, int id, int tag);
};
} // namespace nbvm
