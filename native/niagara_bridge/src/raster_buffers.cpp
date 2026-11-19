#include "material_programs.h"
#include "raster_buffers.h"
#include "material_graph_hash.h"
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/worker_thread_pool.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/templates/hashfuncs.hpp>
#include <godot_cpp/variant/basis.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/projection.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <algorithm>
#include <array>
#include <cstring>
#include <cmath>
#include <limits>

namespace godot {
void NBNativeRasterBuffers::_bind_methods() {
    ClassDB::bind_static_method("NBNativeRasterBuffers",D_METHOD("material_signatures","materials"),&NBNativeRasterBuffers::material_signatures);
    ClassDB::bind_static_method("NBNativeRasterBuffers", D_METHOD("quantized_geometry_bytes", "arrays"), &NBNativeRasterBuffers::quantized_geometry_bytes);
    ClassDB::bind_static_method("NBNativeRasterBuffers", D_METHOD("geometry_bytes", "arrays"),
                                &NBNativeRasterBuffers::geometry_bytes);
    ClassDB::bind_static_method("NBNativeRasterBuffers", D_METHOD("uniform_bytes", "program", "values"),
                                &NBNativeRasterBuffers::uniform_bytes);
    ClassDB::bind_static_method("NBNativeRasterBuffers", D_METHOD("material_signature", "material"),
                                &NBNativeRasterBuffers::material_signature);
}
Variant NBNativeRasterBuffers::material_signature(const Ref<Material> &material) {
    NBMaterialSignatureMemo memo;
    return material_signature_memoized(material,memo);
}
Array NBNativeRasterBuffers::material_signatures(const Array &materials) {
    NBMaterialSignatureMemo memo;Array result;
    for(int64_t i=0;i<materials.size();++i) {
        Ref<Material> material=materials[i];
        result.append(material.is_null() ? Variant() : material_signature_memoized(material,memo));
    }
    return result;
}
Variant NBNativeRasterBuffers::material_signature_memoized(const Ref<Material> &material, NBMaterialSignatureMemo &memo) {
    auto *current_revision = NBNativeEffectRevision::current_revision;
    Ref<ShaderMaterial> shader_material = material;
    if (shader_material.is_null())
        return material.is_null() ? Variant() : Variant(material);
    Ref<Shader> shader = shader_material->get_shader();
    Array values = Array::make(material, shader, material->get_render_priority(), material->get_next_pass());
    std::vector<uint32_t> graph_hashes;
    int64_t graph_begin=-1;
    if (shader.is_valid()) {
        values.append(NBNativeMaterialPrograms::shader_code_hash(shader));
        Dictionary program = material->get_meta("niagara_bridge_material_program", Dictionary());
        values.append(Array::make(program.get("schema_version", Variant()),
                                  program.get("compiler_abi", Variant()),
                                  program.get("shader_sha256", Variant()),
                                  program.get("graph_sha256", Variant()), program.get("graph", Variant())));
        values.append(Array::make(program.get("defaults", Variant()), program.get("bindings", Variant()),
                                  program.get("native_texture_formats", Variant())));
        Variant graph_value = program.get("graph", Variant());
        Ref<Resource> graph =
            graph_value.get_type() == Variant::OBJECT ? Ref<Resource>(graph_value) : Ref<Resource>();
        if (graph.is_valid()) {
            Ref<Script> script=graph->get_script();
            bool plain=script.is_valid() && script->get_path()=="res://addons/niagara_bridge/materials/conversion/material_graph.gd";
            if(!plain) memo.clear();
            auto &graphs = plain && current_revision ? current_revision->saved_graph_fields : memo.graphs;
            auto found=graphs.find(graph->get_instance_id());
            if(plain && found!=graphs.end()) graph_hashes=found->second;
            else {
                Array properties=graph->call("get_property_list");
                Array pairs;
                int64_t functions_index = -1;
                bool large_nodes = false;
                for(int64_t n=0;n<properties.size();++n) {
                    Dictionary property=properties[n];String name=property.get("name","");
                    if((int64_t(property.get("usage",0)) & PROPERTY_USAGE_STORAGE) && name!="script") {
                        Variant value = graph->get(name);
                        Variant pair = Array::make(property["name"], value);
                        if (plain) {
                            if (name == "nodes" && value.get_type() == Variant::ARRAY)
                                large_nodes = Array(value).size() >= 16;
                            if (name == "functions" && value.get_type() == Variant::DICTIONARY && !Dictionary(value).is_empty())
                                functions_index = pairs.size();
                            pairs.append(pair);
                        } else {
                            // A subsequent custom getter can edit an earlier value.
                            graph_hashes.push_back(pair.recursive_hash(1));
                        }
                    }
                }
                if (plain) graph_hashes = graph_element_hashes(pairs, large_nodes ? functions_index : -1);
                if(plain) graphs.emplace(graph->get_instance_id(),graph_hashes);
                else memo.clear();
            }
            graph_begin=values.size();
        }
        auto &shaders = current_revision ? current_revision->saved_uniform_names : memo.shaders;
        auto found=shaders.find(shader->get_instance_id());
        String code=shader->get_code();
        if(found==shaders.end() || found->second.code!=code) {
            NBMaterialSignatureMemo::ShaderNames entry;entry.code=code;
            Array uniforms=shader->get_shader_uniform_list();entry.names.reserve(uniforms.size());
            for(int64_t n=0;n<uniforms.size();++n) entry.names.push_back(Dictionary(uniforms[n])["name"]);
            found=shaders.insert_or_assign(shader->get_instance_id(),std::move(entry)).first;
        }
        for(const Variant &name:found->second.names)
            values.append(Array::make(name,shader_material->get_shader_parameter(name)));
    }
    // Godot Array::hash combines element recursive hashes with Murmur3, then
    // fmix32. Keep the exact public value while reusing shared graph pairs.
    uint32_t hash=hash_murmur3_one_32(Variant::ARRAY);
    for(int64_t i=0;i<=values.size();++i) {
        if(i==graph_begin) for(uint32_t value:graph_hashes) hash=hash_murmur3_one_32(value,hash);
        if(i<values.size()) hash=hash_murmur3_one_32(values[i].recursive_hash(1),hash);
    }
    return int64_t(hash_fmix32(hash));
}
PackedByteArray NBNativeRasterBuffers::quantized_geometry_bytes(const Array &arrays) {
    return geometry_bytes(quantized_geometry_arrays(arrays));
}
Array NBNativeRasterBuffers::quantized_geometry_arrays(const Array &arrays) {
    // Match ArrayMesh's uncompressed-attribute storage roundtrip for the
    // complete native composite format. Float UV/custom data remains intact.
    // Standalone adapter is checked against actual surface_get_arrays output.
    Array decoded=arrays.duplicate();
    if(decoded.size()!=Mesh::ARRAY_MAX) return Array();
    PackedVector3Array normals=decoded[Mesh::ARRAY_NORMAL];
    PackedFloat32Array tangents=decoded[Mesh::ARRAY_TANGENT];
    PackedColorArray colors=decoded[Mesh::ARRAY_COLOR];
    const int64_t normal_count = normals.size(), tangent_count = tangents.size(), color_count = colors.size();
    auto oct_words=[](Vector2 oct,bool tangent) {
        uint16_t x=uint16_t(std::clamp(double(oct.x*65535),0.,65535.));
        uint16_t y=uint16_t(std::clamp(double(oct.y*65535),0.,65535.));
        if(tangent && x==0 && y==65535) x=65535;
        return Vector2(x/65535.0,y/65535.0);
    };
    // Detach packed arrays before workers see the pointers. Each range owns
    // different vertices. The GPU receives the original ArrayMesh roundtrip.
    Vector3 *n = normals.ptrw();
    float *t = tangents.ptrw();
    Color *c = colors.ptrw();
    const int64_t vertex_count = std::max({normal_count, tangent_count / 4, color_count});
    for (int64_t i = tangent_count - tangent_count % 4; i < tangent_count; ++i)
        if (!std::isfinite(t[i])) return Array();
    auto decode_range = [&](int64_t first, int64_t end) {
        for (int64_t i = first; i < std::min(end, normal_count); ++i) {
            if (!n[i].is_finite()) return false;
            n[i] = Vector3::octahedron_decode(oct_words(n[i].octahedron_encode(), false));
        }
        for (int64_t i = first; i < std::min(end, tangent_count / 4); ++i) {
            float *components = t + 4 * i;
            Vector3 tangent(components[0], components[1], components[2]);
            if (!tangent.is_finite() || !std::isfinite(components[3])) return false;
            Vector2 oct = tangent.octahedron_tangent_encode(components[3]);
            float sign;
            tangent = Vector3::octahedron_tangent_decode(oct_words(oct, true), &sign);
            components[0] = tangent.x;
            components[1] = tangent.y;
            components[2] = tangent.z;
            components[3] = sign;
        }
        for (int64_t i = first; i < std::min(end, color_count); ++i) {
            for (int channel = 0; channel < 4; ++channel) {
                if (!std::isfinite(c[i][channel])) return false;
                c[i][channel] = uint8_t(std::clamp(double(c[i][channel]) * 255.0, 0., 255.)) / 255.0;
            }
        }
        return true;
    };
    auto *pool = vertex_count >= 4096 ? WorkerThreadPool::get_singleton() : nullptr;
    if (pool && pool->get_caller_task_id() < 0 && pool->get_caller_group_id() < 0) {
        std::array<bool, 4> valid{};
        auto chunk = [&](uint32_t i) {
            valid[i] = decode_range(vertex_count * i / 4, vertex_count * (i + 1) / 4);
        };
        auto task = pool->add_native_group_task([](void *work, uint32_t i) {
            (*static_cast<decltype(chunk) *>(work))(i + 1);
        }, &chunk, 3, 3, true);
        chunk(0);
        pool->wait_for_group_task_completion(task);
        if (std::find(valid.begin(), valid.end(), false) != valid.end()) return Array();
    } else if (!decode_range(0, vertex_count)) {
        return Array();
    }
    decoded[Mesh::ARRAY_NORMAL]=normals;decoded[Mesh::ARRAY_TANGENT]=tangents;decoded[Mesh::ARRAY_COLOR]=colors;
    return decoded;
}
PackedByteArray NBNativeRasterBuffers::geometry_bytes(const Array &arrays) {
    PackedByteArray result;
    if (arrays.size() != Mesh::ARRAY_MAX ||
        arrays[Mesh::ARRAY_VERTEX].get_type() != Variant::PACKED_VECTOR3_ARRAY)
        return result;
    PackedVector3Array vertices = arrays[Mesh::ARRAY_VERTEX];
    if (vertices.size() > std::numeric_limits<int32_t>::max() / 160)
        return result;
    result.resize(vertices.size() * 160);
    if (vertices.is_empty())
        return result;
    PackedVector3Array normals = arrays[Mesh::ARRAY_NORMAL];
    PackedFloat32Array tangents = arrays[Mesh::ARRAY_TANGENT];
    PackedColorArray colors = arrays[Mesh::ARRAY_COLOR];
    PackedVector2Array uv0 = arrays[Mesh::ARRAY_TEX_UV], uv1 = arrays[Mesh::ARRAY_TEX_UV2];
    std::array<PackedFloat32Array, 4> custom;
    for (int c = 0; c < 4; ++c) {
        Variant channel = arrays[Mesh::ARRAY_CUSTOM0 + c];
        if (channel.get_type() == Variant::PACKED_BYTE_ARRAY)
            custom[c] = PackedByteArray(channel).to_float32_array();
        else if (channel.get_type() == Variant::PACKED_FLOAT32_ARRAY)
            custom[c] = channel;
    }
    uint8_t *target = result.ptrw();
    const Vector3 *points = vertices.ptr();
    const int64_t vertex_count = vertices.size(), normal_count = normals.size(),
                  tangent_count = tangents.size(), color_count = colors.size(),
                  uv0_count = uv0.size(), uv1_count = uv1.size();
    const Vector3 *normal_data = normals.ptr();
    const float *tangent_data = tangents.ptr();
    const Color *color_data = colors.ptr();
    const Vector2 *uv0_data = uv0.ptr(), *uv1_data = uv1.ptr();
    std::array<int64_t, 4> custom_counts{};
    std::array<const float *, 4> custom_data{};
    for (int c = 0; c < 4; ++c) {
        custom_counts[c] = custom[c].size();
        custom_data[c] = custom[c].ptr();
    }
    for (int64_t i = 0; i < vertex_count; ++i) {
        // Ten std430 vec4 slots, including one reserved zero slot.
        // memcpy avoids alignment assumptions and always writes float32,
        // including when the extension is built with double real_t.
        std::array<float, 40> row{};
        row[0] = float(points[i].x);
        row[1] = float(points[i].y);
        row[2] = float(points[i].z);
        row[3] = 1.f;
        Vector3 normal = i < normal_count ? normal_data[i] : Vector3(0, 0, 1);
        row[4] = float(normal.x);
        row[5] = float(normal.y);
        row[6] = float(normal.z);
        row[8] = 1.f;
        row[11] = 1.f;
        if (tangent_count >= i * 4 + 4)
            std::copy_n(tangent_data + i * 4, 4, row.data() + 8);
        Vector2 a = i < uv0_count ? uv0_data[i] : Vector2(), b = i < uv1_count ? uv1_data[i] : Vector2();
        row[12] = float(a.x);
        row[13] = float(a.y);
        row[14] = float(b.x);
        row[15] = float(b.y);
        Color color = i < color_count ? color_data[i] : Color(1, 1, 1, 1);
        row[16] = color.r;
        row[17] = color.g;
        row[18] = color.b;
        row[19] = color.a;
        for (int c = 0; c < 4; ++c)
            if (custom_counts[c] >= i * 4 + 4)
                std::copy_n(custom_data[c] + i * 4, 4, row.data() + 20 + c * 4);
        std::memcpy(target + i * 160, row.data(), 160);
    }
    return result;
}
PackedByteArray NBNativeRasterBuffers::uniform_bytes(const Dictionary &program, const Dictionary &values) {
    PackedByteArray result;
    int64_t slots = program.get("slots", 0);
    if (slots < 0 || slots > std::numeric_limits<int32_t>::max() / 16)
        return result;
    result.resize(slots * 16);
    result.fill(0);
    Array fields = program.get("fields", Array());
    Dictionary defaults = program.get("defaults", Dictionary());
    for (int n = 0; n < fields.size(); ++n) {
        Dictionary field = fields[n];
        String name = field.get("name", ""), type = field.get("type", "");
        int64_t offset = int64_t(field.get("slot", -1)) * 16;
        int count = type == "mat4" ? 4 : type == "mat3" ? 3 : 1;
        if (offset < 0 || offset + count * 16 > result.size())
            return PackedByteArray();
        Variant value = values.get(name, defaults.get(name, Variant()));
        if (value.get_type() == Variant::NIL)
            continue;
        if (type == "int" || type == "bool")
            result.encode_s32(offset, int64_t(value));
        else if (type == "float")
            result.encode_float(offset, double(value));
        else if (type == "mat4") {
            Projection matrix =
                value.get_type() == Variant::TRANSFORM3D ? Projection(Transform3D(value)) : Projection(value);
            for (int column = 0; column < 4; ++column)
                for (int row = 0; row < 4; ++row)
                    result.encode_float(offset + column * 16 + row * 4, matrix.columns[column][row]);
        } else if (type == "mat3") {
            if (value.get_type() == Variant::BASIS) {
                Basis matrix = value;
                for (int column = 0; column < 3; ++column)
                    for (int row = 0; row < 3; ++row)
                        result.encode_float(offset + column * 16 + row * 4, matrix.get_column(column)[row]);
            } else {
                Projection matrix = value.get_type() == Variant::TRANSFORM3D ? Projection(Transform3D(value))
                                                                             : Projection(value);
                for (int column = 0; column < 3; ++column)
                    for (int row = 0; row < 3; ++row)
                        result.encode_float(offset + column * 16 + row * 4, matrix.columns[column][row]);
            }
        } else {
            int components = type == "vec2" ? 2 : type == "vec3" ? 3 : type == "vec4" ? 4 : 0;
            for (int c = 0; c < components; ++c) {
                bool valid = false, out_of_bounds = false;
                Variant component = value.get_indexed(c, valid, out_of_bounds);
                if (!valid || out_of_bounds)
                    return PackedByteArray();
                result.encode_float(offset + c * 4, double(component));
            }
            if (!components)
                return PackedByteArray();
        }
    }
    return result;
}
} // namespace godot
