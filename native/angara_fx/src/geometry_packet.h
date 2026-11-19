#pragma once
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <optional>
#include <vector>
namespace godot {
// Runtime transport shared by CPU/GPU renderer consumers. No Resource state or
// device handles are serialized; the public Dictionary format remains an adapter.
struct NBGeometryRecord {
    Transform3D transform;
    std::optional<Array> arrays;
    bool per_vertex = false, geometry_colors = false;
    Color color = Color(1, 1, 1, 1), dynamic = Color(0, 0, 0, 0),
          dynamic1 = Color(0, 0, 0, 0), dynamic2 = Color(0, 0, 0, 0),
          dynamic3 = Color(0, 0, 0, 0), attributes = Color(0, 0, 1, 1);
    double relative_time = 0.;
    int64_t context_slot = 0;
    PackedFloat32Array relative_times;
    String error;
};
struct NBGeometryPacket {
    std::vector<NBGeometryRecord> records;
    Variant bounds;
    TypedArray<Dictionary> diagnostics;
};
} // namespace godot
