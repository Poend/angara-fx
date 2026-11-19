#include "composite_renderer.h"
#include "geometry_cache.h"
#include "material_exposure.h"
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/quad_mesh.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
using namespace godot;
namespace {
struct Geometry {
    // Hold the packed arrays for the entire upload. Their COW data pointers
    // remain valid even if a texture notification changes the source arrays.
    Array channels;
    const Vector3 *vertices = nullptr, *normals = nullptr;
    const Vector2 *uv = nullptr, *uv2 = nullptr;
    const Color *colors = nullptr;
    const float *tangents = nullptr;
    const int32_t *indices = nullptr;
    int64_t vertex_count = 0, index_count = 0;
    bool indexed = false;
    std::array<const float *, 4> custom_data{};
    std::array<int64_t, 4> custom_counts{};
};
template<class Packed>
auto channel(const Variant &slot, int64_t required) -> decltype(Packed().ptr()) {
    if (slot.get_type() != Variant::Type(GetTypeInfo<Packed>::VARIANT_TYPE)) return nullptr;
    Packed values = slot;
    return values.size() == required ? values.ptr() : nullptr;
}
String prepare_geometry(const Array &source, Geometry &g) {
    if (source.size() != Mesh::ARRAY_MAX || source[Mesh::ARRAY_VERTEX].get_type() != Variant::PACKED_VECTOR3_ARRAY)
        return "Missing geometry arrays or transform.";
    g.channels = source.duplicate();
    const Array &channels = g.channels;
    PackedVector3Array vertices = channels[Mesh::ARRAY_VERTEX];
    g.vertices = vertices.ptr();
    g.vertex_count = vertices.size();
    g.normals = channel<PackedVector3Array>(channels[Mesh::ARRAY_NORMAL], g.vertex_count);
    g.tangents = channel<PackedFloat32Array>(channels[Mesh::ARRAY_TANGENT], g.vertex_count * 4);
    g.colors = channel<PackedColorArray>(channels[Mesh::ARRAY_COLOR], g.vertex_count);
    g.uv = channel<PackedVector2Array>(channels[Mesh::ARRAY_TEX_UV], g.vertex_count);
    g.uv2 = channel<PackedVector2Array>(channels[Mesh::ARRAY_TEX_UV2], g.vertex_count);
    g.indexed = channels[Mesh::ARRAY_INDEX].get_type() == Variant::PACKED_INT32_ARRAY;
    g.index_count = g.vertex_count;
    if (g.indexed) {
        PackedInt32Array indices = channels[Mesh::ARRAY_INDEX];
        g.index_count = indices.size();
        g.indices = indices.ptr();
        for (int64_t i = 0; i < g.index_count; ++i)
            if (g.indices[i] < 0 || g.indices[i] >= g.vertex_count) return "Geometry index exceeds vertices.";
    }
    for (int c = 0; c < 4; ++c) {
        const Variant &slot = channels[Mesh::ARRAY_CUSTOM0 + c];
        if (slot.get_type() != Variant::PACKED_FLOAT32_ARRAY) continue;
        PackedFloat32Array values = slot;
        g.custom_data[c] = values.ptr();
        g.custom_counts[c] = values.size();
    }
    return String();
}
Color custom_color(const PackedFloat32Array &data, int index) {
    if (data.size() < (int64_t(index) + 1) * 4)
        return Color(0, 0, 0, 0);
    const float *v = data.ptr() + index * 4;
    return Color(v[0], v[1], v[2], v[3]);
}
Color geometry_custom(const Geometry &geometry, int slot, int index) {
    if (geometry.custom_counts[slot] < (int64_t(index) + 1) * 4)
        return Color(0, 0, 0, 0);
    const float *v = geometry.custom_data[slot] + index * 4;
    return Color(v[0], v[1], v[2], v[3]);
}
} // namespace
void NBNativeCompositeRenderer::_bind_methods() {
    ClassDB::bind_method(D_METHOD("material_view", "camera", "physical_light_units"), &NBNativeCompositeRenderer::material_view);
    BIND_CONSTANT(ATTRIBUTE_STRIDE);
    BIND_CONSTANT(TEXTURE_WIDTH);
    ClassDB::bind_method(D_METHOD("configure", "parent", "material", "component_priority", "distance_offset"),
                         &NBNativeCompositeRenderer::configure);
    ClassDB::bind_method(D_METHOD("dispose"), &NBNativeCompositeRenderer::dispose);
    ClassDB::bind_method(D_METHOD("update", "records", "time", "bounds_override"),
                         &NBNativeCompositeRenderer::update, DEFVAL(Variant()));
    ClassDB::bind_method(D_METHOD("_write_record", "index", "record", "transform"),
                         &NBNativeCompositeRenderer::write_record);
    ClassDB::bind_static_method("NBNativeCompositeRenderer", D_METHOD("_custom", "arrays", "slot", "index"),
                                &NBNativeCompositeRenderer::custom);
    ClassDB::bind_method(D_METHOD("get_instance"), &NBNativeCompositeRenderer::get_instance);
    ClassDB::bind_method(D_METHOD("get_material"), &NBNativeCompositeRenderer::get_material);
    ClassDB::bind_method(D_METHOD("get_attributes_image"), &NBNativeCompositeRenderer::get_attributes_image);
    ClassDB::bind_method(D_METHOD("set_attributes_image", "value"),
                         &NBNativeCompositeRenderer::set_attributes_image);
    ClassDB::bind_method(D_METHOD("get_attributes_texture"),
                         &NBNativeCompositeRenderer::get_attributes_texture);
    ClassDB::bind_method(D_METHOD("get_statistics"), &NBNativeCompositeRenderer::get_statistics);
    ClassDB::bind_method(D_METHOD("get_bounds_context"), &NBNativeCompositeRenderer::get_bounds_context);
    ClassDB::bind_method(D_METHOD("get_capacity"), &NBNativeCompositeRenderer::get_capacity);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "instance", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "",
                 "get_instance");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "material", PROPERTY_HINT_RESOURCE_TYPE, "ShaderMaterial",
                              PROPERTY_USAGE_NONE),
                 "", "get_material");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "attributes_image", PROPERTY_HINT_RESOURCE_TYPE, "Image",
                              PROPERTY_USAGE_NONE),
                 "set_attributes_image", "get_attributes_image");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "attributes_texture", PROPERTY_HINT_RESOURCE_TYPE,
                              "ImageTexture", PROPERTY_USAGE_NONE),
                 "", "get_attributes_texture");
    ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "statistics", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE),
                 "", "get_statistics");
    ADD_PROPERTY(
        PropertyInfo(Variant::DICTIONARY, "bounds_context", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "",
        "get_bounds_context");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "capacity", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "",
                 "get_capacity");
}
Dictionary NBNativeCompositeRenderer::material_view(Camera3D *camera, bool physical_light_units) {
    if (!bool(Dictionary(material->get_meta("angara_fx_system_material", Dictionary())).get("mixed_lighting", false))) {
        Dictionary result; result["supported"] = true; return result;
    }
    Dictionary result = NBNativeMaterialExposure::for_camera(camera, physical_light_units);
    if (bool(result["supported"])) material->set_shader_parameter("nb_unlit_emission_scale", result["emission_scale"]);
    return result;
}
MeshInstance3D *NBNativeCompositeRenderer::get_instance() const {
    return Object::cast_to<MeshInstance3D>(ObjectDB::get_instance(instance_id));
}
void NBNativeCompositeRenderer::fail(const String &message) {
    statistics["error"] = message;
    if (auto *node = get_instance()) {
        node->set_mesh(Ref<Mesh>());
        node->set_visible(false);
    }
    UtilityFunctions::push_error("Native particle geometry: " + message);
}
void NBNativeCompositeRenderer::configure(Node3D *parent, const Ref<ShaderMaterial> &value, int priority,
                                          double distance_offset) {
    dispose();
    if (!parent || value.is_null()) {
        fail("Missing renderer parent or authored shader material.");
        return;
    }
    material = value;
    material->set_render_priority(priority);
    bounds_context["materials"] = Array::make(material);
    Ref<QuadMesh> quad;
    quad.instantiate();
    quad->set_size(Vector2(1, 1));
    quad_arrays = quad->surface_get_arrays(0);
    set_attributes_image(Image::create_empty(ATTRIBUTE_STRIDE, 1, false, Image::FORMAT_RGBAF));
    attributes_texture = ImageTexture::create_from_image(attributes_image);
    material->set_shader_parameter("nb_particle_attributes", attributes_texture);
    material->set_shader_parameter("nb_particle_attributes_enabled", true);
    material->set_shader_parameter("nb_particle_attribute_stride", ATTRIBUTE_STRIDE);
    auto *node = memnew(MeshInstance3D);
    instance_id = node->get_instance_id();
    node->set_name("ScopedParticleSystem");
    node->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
    node->set_material_override(material);
    node->set_sorting_use_aabb_center(true);
    node->set_sorting_offset(distance_offset);
    parent->add_child(node);
    node->set_as_top_level(true);
    node->set_global_transform(Transform3D());
}
void NBNativeCompositeRenderer::dispose() {
    if (auto *node = get_instance()) {
        if (node->get_parent())
            node->get_parent()->remove_child(node);
        node->queue_free();
    }
    instance_id = 0;
    material.unref();
    if (draw_mesh.is_valid())
        draw_mesh->clear_surfaces();
    draw_mesh.unref();
    attributes_image.unref();
    attributes_texture.unref();
    capacity = 0;
    attributes.clear();
    bounds_context.clear();
}
void NBNativeCompositeRenderer::set_attributes_image(const Ref<Image> &value) {
    attributes_image = value;
    attributes.clear();
    if (value.is_null())
        return;
    if (value->get_format() != Image::FORMAT_RGBAF) {
        fail("Particle attribute image must be RGBAF.");
        return;
    }
    PackedByteArray data = value->get_data();
    attributes.resize(data.size() / sizeof(float));
    if (!data.is_empty())
        std::memcpy(attributes.data(), data.ptr(), data.size());
}
void NBNativeCompositeRenderer::commit_image() {
    if (attributes_image.is_null())
        return;
    PackedByteArray data;
    data.resize(attributes.size() * sizeof(float));
    if (!attributes.empty())
        std::memcpy(data.ptrw(), attributes.data(), attributes.size() * sizeof(float));
    attributes_image->set_data(attributes_image->get_width(), attributes_image->get_height(), false,
                               Image::FORMAT_RGBAF, data);
}
bool NBNativeCompositeRenderer::write(int index, const Dictionary &record, const Transform3D &transform) {
    return write_values(index, record.get("attributes", Color(0, 0, 1, 1)),
                        record.get("color", Color(1, 1, 1, 1)),
                        record.get("dynamic1", Color(0, 0, 0, 0)),
                        record.get("dynamic2", Color(0, 0, 0, 0)),
                        record.get("dynamic3", Color(0, 0, 0, 0)),
                        double(record.get("relative_time", 0.0)), transform);
}
bool NBNativeCompositeRenderer::write_values(int index, const Color &parameters, const Color &source_color,
                                             const Color &dynamic1, const Color &dynamic2,
                                             const Color &dynamic3, double relative_time,
                                             const Transform3D &transform) {
    if (index < 0 || (size_t(index) + 1) * ATTRIBUTE_STRIDE * 4 > attributes.size())
        return false;
    float *target = attributes.data() + size_t(index) * ATTRIBUTE_STRIDE * 4;
    auto color = [&](int slot, const Color &value) {
        float *p = target + slot * 4;
        p[0] = value.r;
        p[1] = value.g;
        p[2] = value.b;
        p[3] = value.a;
    };
    color(0, parameters);
    color(1, dynamic1);
    color(2, dynamic2);
    color(3, dynamic3);
    color(4, source_color);
    for (int column = 0; column < 3; column++) {
        Vector3 axis = transform.basis.get_column(column);
        color(5 + column, Color(axis.x, axis.y, axis.z, 0));
    }
    color(8, Color(transform.origin.x, transform.origin.y, transform.origin.z, 1));
    color(9, Color(float(relative_time), 0, 0, 0));
    return true;
}
void NBNativeCompositeRenderer::write_record(int index, const Dictionary &record,
                                             const Transform3D &transform) {
    if (!write(index, record, transform)) {
        fail("Particle attribute record exceeds image capacity.");
        return;
    }
    commit_image();
}
Color NBNativeCompositeRenderer::custom(const Array &arrays, int slot, int index) {
    return slot >= 0 && slot < arrays.size() && arrays[slot].get_type() == Variant::PACKED_FLOAT32_ARRAY
               ? custom_color(arrays[slot], index)
               : Color(0, 0, 0, 0);
}
void NBNativeCompositeRenderer::update(const Array &records, double time, const Variant &bounds_override) {
    auto begin = std::chrono::steady_clock::now();
    std::vector<NBGeometryRecord> packet;
    packet.reserve(records.size());
    for (int64_t n = 0; n < records.size(); ++n) {
        NBGeometryRecord record;
        Variant value = records[n];
        if (value.get_type() != Variant::DICTIONARY)
            record.error = "Invalid geometry record.";
        else {
            Dictionary row = value;
            if (row.get("transform", Variant()).get_type() != Variant::TRANSFORM3D)
                record.error = "Missing geometry arrays or transform.";
            else {
                record.transform = row["transform"];
                if (row.has("arrays")) record.arrays = Array(row["arrays"]);
                record.per_vertex = row.get("per_vertex_attributes", false);
                record.geometry_colors = row.get("geometry_colors", false);
                record.dynamic = row.get("dynamic", Color(0, 0, 0, 0));
                record.dynamic1 = row.get("dynamic1", Color(0, 0, 0, 0));
                record.dynamic2 = row.get("dynamic2", Color(0, 0, 0, 0));
                record.dynamic3 = row.get("dynamic3", Color(0, 0, 0, 0));
                record.color = row.get("color", Color(1, 1, 1, 1));
                record.attributes = row.get("attributes", Color(0, 0, 1, 1));
                record.context_slot = row.get("context_slot", 0);
                record.relative_time = row.get("relative_time", 0.0);
                record.relative_times = row.get("relative_times", PackedFloat32Array());
            }
        }
        packet.push_back(std::move(record));
    }
    update_packet(packet, time, bounds_override);
    if (statistics.has("upload_usec"))
        statistics["upload_usec"] = std::chrono::duration<double, std::micro>(
            std::chrono::steady_clock::now() - begin).count();
}
void NBNativeCompositeRenderer::update_packet(const std::vector<NBGeometryRecord> &records, double time, const Variant &bounds_override) {
    auto begin = std::chrono::steady_clock::now();
    auto *node = get_instance();
    statistics = Dictionary();
    statistics["particles"] = records.size();
    statistics["surfaces"] = 0;
    statistics["vertices"] = records.size() * 4;
    statistics["attribute_stride"] = ATTRIBUTE_STRIDE;
    statistics["native_execution"] = true;
    if (!node || material.is_null()) {
        fail("Configure the native geometry renderer before update.");
        return;
    }
    material->set_shader_parameter("simulation_time", time);
    auto elapsed = [&]() {
        return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - begin).count();
    };
    if (records.empty()) {
        node->set_mesh(Ref<Mesh>());
        node->set_visible(false);
        statistics["upload_usec"] = elapsed();
        return;
    }
    std::vector<Geometry> shapes;
    std::vector<size_t> shape_indices;
    shape_indices.reserve(records.size());
    std::optional<size_t> quad, previous_mesh;
    const Array *previous_source = nullptr;
    int64_t vertex_count = 0, index_count = 0, attribute_count = 0;
    for (const NBGeometryRecord &record : records) {
        if (!record.error.is_empty()) {
            fail(record.error);
            return;
        }
        auto &index = record.arrays ? previous_mesh : quad;
        if (record.arrays && (!previous_source || !UtilityFunctions::is_same(*previous_source, *record.arrays))) {
            index.reset();
            previous_source = &*record.arrays;
        }
        if (!index) {
            index = shapes.size();
            auto &shape = shapes.emplace_back();
            String error = prepare_geometry(record.arrays ? *record.arrays : quad_arrays, shape);
            if (!error.is_empty()) {
                fail(error);
                return;
            }
        }
        shape_indices.push_back(*index);
        const Geometry &shape = shapes[*index];
        vertex_count += shape.vertex_count;
        index_count += shape.index_count;
        attribute_count += record.per_vertex ? shape.vertex_count : 1;
        if (vertex_count > 16000000 || index_count > 48000000 || attribute_count > 16000000) {
            fail("Geometry packet exceeds native capacity limit.");
            return;
        }
    }
    if (attribute_count > capacity) {
        capacity = int(std::max<int64_t>(16, attribute_count * 2));
        int width = std::min(TEXTURE_WIDTH, capacity * ATTRIBUTE_STRIDE);
        int height = (capacity * ATTRIBUTE_STRIDE + width - 1) / width;
        set_attributes_image(Image::create_empty(width, height, false, Image::FORMAT_RGBAF));
        attributes_texture->set_image(attributes_image);
    }
    PackedVector3Array vertices, normals;
    PackedFloat32Array tangents, custom0, custom1;
    PackedColorArray colors;
    PackedVector2Array uv, uv2;
    PackedInt32Array indices;
    vertices.resize(vertex_count);
    normals.resize(vertex_count);
    tangents.resize(vertex_count * 4);
    colors.resize(vertex_count);
    uv.resize(vertex_count);
    uv2.resize(vertex_count);
    custom0.resize(vertex_count * 4);
    custom1.resize(vertex_count * 4);
    indices.resize(index_count);
    Vector3 *v_out = vertices.ptrw(), *n_out = normals.ptrw();
    float *t_out = tangents.ptrw(), *c0_out = custom0.ptrw(), *c1_out = custom1.ptrw();
    Color *color_out = colors.ptrw();
    Vector2 *uv_out = uv.ptrw(), *uv2_out = uv2.ptrw();
    int32_t *i_out = indices.ptrw();
    int vertex_base = 0, index_base = 0, attribute_index = 0;
    for (size_t i = 0; i < records.size(); ++i) {
        const NBGeometryRecord &r = records[i];
        const Geometry &g = shapes[shape_indices[i]];
        if (!r.per_vertex && !write_values(attribute_index, r.attributes, r.color,
                                           r.dynamic1, r.dynamic2, r.dynamic3, r.relative_time, r.transform)) {
            fail("Attribute packet extent mismatch.");
            return;
        }
        const auto determinant = r.transform.basis.determinant();
        const Basis normal_basis = std::abs(determinant) > 1e-20 ? r.transform.basis.inverse().transposed() : Basis();
        const int64_t count_vertices = g.vertex_count;
        const int64_t relative_count = r.per_vertex ? r.relative_times.size() : 0;
        const float *relative_data = relative_count ? r.relative_times.ptr() : nullptr;
        for (int index = 0; index < count_vertices; index++) {
            int dst = vertex_base + index;
            Color dynamic = r.per_vertex ? geometry_custom(g, 0, index) : r.dynamic;
            Color source_color = g.colors ? g.colors[index] : Color(1, 1, 1, 1);
            if (r.per_vertex) {
                if (!write_values(attribute_index, r.attributes, source_color,
                                  geometry_custom(g, 1, index), geometry_custom(g, 2, index),
                                  geometry_custom(g, 3, index),
                                  index < relative_count ? relative_data[index] : 0.f,
                                  r.transform)) {
                    fail("Per-vertex attribute extent mismatch.");
                    return;
                }
            }
            v_out[dst] = r.transform.xform(g.vertices[index]);
            n_out[dst] =
                normal_basis.xform(g.normals ? g.normals[index] : Vector3(0, 0, 1))
                    .normalized();
            Vector3 tangent = r.transform.basis.xform(
                g.tangents ? Vector3(g.tangents[index * 4], g.tangents[index * 4 + 1], g.tangents[index * 4 + 2]) : Vector3(1, 0, 0));
            tangent.normalize();
            t_out[dst * 4] = tangent.x;
            t_out[dst * 4 + 1] = tangent.y;
            t_out[dst * 4 + 2] = tangent.z;
            t_out[dst * 4 + 3] = (g.tangents ? g.tangents[index * 4 + 3] : 1.f) * (determinant < 0 ? -1.f : 1.f);
            color_out[dst] = r.geometry_colors ? source_color : r.color;
            uv_out[dst] = g.uv ? g.uv[index] : Vector2();
            uv2_out[dst] = g.uv2 ? g.uv2[index] : Vector2();
            c0_out[dst * 4] = dynamic.r;
            c0_out[dst * 4 + 1] = dynamic.g;
            c0_out[dst * 4 + 2] = dynamic.b;
            c0_out[dst * 4 + 3] = dynamic.a;
            c1_out[dst * 4] = float(r.context_slot);
            c1_out[dst * 4 + 1] = float(attribute_index);
            c1_out[dst * 4 + 2] = c1_out[dst * 4 + 3] = 0;
            if (r.per_vertex)
                attribute_index++;
        }
        int count = int(g.index_count);
        const int32_t *source_indices = g.indices;
        for (int index = 0; index < count; index++)
            i_out[index_base + index] = vertex_base + (g.indexed ? source_indices[index] : index);
        index_base += count;
        vertex_base += int(g.vertex_count);
        if (!r.per_vertex)
            attribute_index++;
    }
    Array arrays;
    arrays.resize(Mesh::ARRAY_MAX);
    arrays[Mesh::ARRAY_VERTEX] = vertices;
    arrays[Mesh::ARRAY_NORMAL] = normals;
    arrays[Mesh::ARRAY_TANGENT] = tangents;
    arrays[Mesh::ARRAY_COLOR] = colors;
    arrays[Mesh::ARRAY_TEX_UV] = uv;
    arrays[Mesh::ARRAY_TEX_UV2] = uv2;
    arrays[Mesh::ARRAY_CUSTOM0] = custom0;
    arrays[Mesh::ARRAY_CUSTOM1] = custom1;
    arrays[Mesh::ARRAY_INDEX] = indices;
    if (draw_mesh.is_null())
        draw_mesh.instantiate();
    Ref<ArrayMesh> mesh = draw_mesh;
    mesh->clear_surfaces();
    mesh->set_meta("angara_fx_immutable_geometry", true);
    uint64_t flags = (uint64_t(Mesh::ARRAY_CUSTOM_RGBA_FLOAT) << Mesh::ARRAY_FORMAT_CUSTOM0_SHIFT) |
                     (uint64_t(Mesh::ARRAY_CUSTOM_RGBA_FLOAT) << Mesh::ARRAY_FORMAT_CUSTOM1_SHIFT);
    mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays, Array(), Dictionary(),
                                  BitField<Mesh::ArrayFormat>(flags));
    mesh->set_custom_aabb(bounds_override.get_type() == Variant::AABB ? AABB(bounds_override) : AABB());
    NBNativeGeometryCache::seed_composite(mesh,arrays);
    node->set_mesh(mesh);
    node->set_visible(true);
    commit_image();
    attributes_texture->update(attributes_image);
    statistics["surfaces"] = mesh->get_surface_count();
    statistics["vertices"] = vertices.size();
    statistics["attribute_records"] = attribute_count;
    statistics["bounds"] = mesh->get_aabb();
    statistics["attribute_texture_bytes"] = attributes_image->get_data_size();
    statistics["upload_usec"] = elapsed();
}
