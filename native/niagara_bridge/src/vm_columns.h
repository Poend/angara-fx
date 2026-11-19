#pragma once
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
namespace nbvm {
// A retained column view performs no per-element PackedArray conversions.
struct ColumnView {
    godot::Array values;
    godot::PackedFloat32Array floats;
    godot::PackedInt32Array ints;
    const float *float_data = nullptr;
    const int32_t *int_data = nullptr;
    int64_t size = -1;
    ColumnView() = default;
    explicit ColumnView(const godot::Variant &column) {
        switch (column.get_type()) {
        case godot::Variant::ARRAY:
            values = column;
            size = values.size();
            break;
        case godot::Variant::PACKED_FLOAT32_ARRAY:
            floats = column;
            size = floats.size();
            float_data = floats.ptr();
            break;
        case godot::Variant::PACKED_INT32_ARRAY:
            ints = column;
            size = ints.size();
            int_data = ints.ptr();
            break;
        default:
            break;
        }
    }
    godot::Variant at(int64_t index) const {
        if (float_data)
            return float_data[index];
        if (int_data)
            return int_data[index];
        return values[index];
    }
    double number_at(int64_t index) const {
        if (float_data)
            return float_data[index];
        if (int_data)
            return int_data[index];
        return double(values[index]);
    }
};
} // namespace nbvm
