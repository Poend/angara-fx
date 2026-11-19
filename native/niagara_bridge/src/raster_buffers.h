#pragma once
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <unordered_map>
#include "effect_revision.h"
#include <vector>

namespace godot {
struct NBMaterialSignatureMemo {
    using ShaderNames = NBShaderUniforms;
    std::unordered_map<uint64_t,std::vector<uint32_t>> graphs;
    std::unordered_map<uint64_t,ShaderNames> shaders;
    void clear() {graphs.clear();shaders.clear();}
};
// Transient GPU upload data; authored effects remain ordinary text resources.
class NBNativeRasterBuffers : public RefCounted {
    GDCLASS(NBNativeRasterBuffers, RefCounted)
  protected:
    static void _bind_methods();

  public:
    static PackedByteArray geometry_bytes(const Array &arrays);
    static PackedByteArray quantized_geometry_bytes(const Array &arrays);
    static Array quantized_geometry_arrays(const Array &arrays);
    static PackedByteArray uniform_bytes(const Dictionary &program, const Dictionary &values);
    static Variant material_signature(const Ref<Material> &material);
    static Variant material_signature_memoized(const Ref<Material> &material, NBMaterialSignatureMemo &memo);
    static Array material_signatures(const Array &materials);
};
} // namespace godot
