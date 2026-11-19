#include "gpu_buffers.h"
#include "value_evaluator.h"
#include <godot_cpp/classes/worker_thread_pool.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>
namespace godot {
namespace {
constexpr int BASE = 192, CACHE_START = 84, CACHE_SIZE = 64;
struct Field {
    const char *name;
    int offset;
};
constexpr Field scalars[] = {{"age", 3},
                             {"lifetime", 7},
                             {"rotation", 11},
                             {"mass", 28},
                             {"uv_flip", 29},
                             {"subimage_index", 30},
                             {"material_random", 31},
                             {"drag", 35},
                             {"birth_time", 43},
                             {"id", 47},
                             {"rotational_drag", 51},
                             {"normalized_age", 63},
                             {"camera_offset", 67},
                             {"mesh_index", 151},
                             {"uv_flip_y", 155}};
constexpr Field vectors[] = {{"position", 0},
                             {"velocity", 4},
                             {"alignment", 8},
                             {"force", 32},
                             {"previous_velocity", 36},
                             {"presolve_position", 40},
                             {"presolve_velocity", 44},
                             {"presolve_force", 48},
                             {"advection_velocity", 52},
                             {"mesh_scale", 148},
                             {"initial_mesh_scale", 152},
                             {"initial_position", 168}};
constexpr Field colors[] = {{"color", 16},     {"initial_color", 20}, {"dynamic", 24},
                            {"dynamic1", 172}, {"dynamic2", 176},     {"dynamic3", 180}};
constexpr Field rotations[] = {
    {"mesh_orientation", 156}, {"initial_mesh_orientation", 160}, {"initial_rotation", 164}};
constexpr Field pairs[] = {{"size", 12}, {"initial_size", 14}, {"size_scale_factor", 56}};
struct NamedField {
    Variant name;
    int offset;
};
template <size_t Count> std::array<NamedField, Count> named_fields(const Field (&fields)[Count]) {
    std::array<NamedField, Count> result;
    for (size_t i = 0; i < Count; ++i)
        result[i] = {String(fields[i].name), fields[i].offset};
    return result;
}
void word(uint8_t *bytes, int index, uint32_t value) {
    std::memcpy(bytes + index * 4, &value, 4);
}
uint32_t word(const uint8_t *bytes, int index) {
    uint32_t v;
    std::memcpy(&v, bytes + index * 4, 4);
    return v;
}
void number(uint8_t *bytes, int index, double value) {
    float v = float(value);
    std::memcpy(bytes + index * 4, &v, 4);
}
float number(const uint8_t *bytes, int index) {
    float v;
    std::memcpy(&v, bytes + index * 4, 4);
    return v;
}
void vector(uint8_t *p, int o, const Vector3 &v) {
    number(p, o, v.x);
    number(p, o + 1, v.y);
    number(p, o + 2, v.z);
}
Vector3 vector(const uint8_t *p, int o) {
    return Vector3(number(p, o), number(p, o + 1), number(p, o + 2));
}
void color(uint8_t *p, int o, const Color &v) {
    number(p, o, v.r);
    number(p, o + 1, v.g);
    number(p, o + 2, v.b);
    number(p, o + 3, v.a);
}
Color color(const uint8_t *p, int o) {
    return Color(number(p, o), number(p, o + 1), number(p, o + 2), number(p, o + 3));
}
Basis unreal_basis(const Basis &b) {
    return Basis(NBNativeValues::godot_to_unreal(b.xform(NBNativeValues::unreal_to_godot(Vector3(1, 0, 0)))),
                 NBNativeValues::godot_to_unreal(b.xform(NBNativeValues::unreal_to_godot(Vector3(0, 1, 0)))),
                 NBNativeValues::godot_to_unreal(b.xform(NBNativeValues::unreal_to_godot(Vector3(0, 0, 1)))));
}
void basis(uint8_t *p, int offset, const Basis &b) {
    for (int n = 0; n < 3; ++n)
        vector(p, offset + n * 4, b.get_column(n));
}
int stride(const Dictionary &program) {
    int64_t count = program.get("stride_vec4", 48);
    Array fields = program.get("attribute_layout", Array());
    if (count < 48 || count > 65536 || count != 48 + fields.size())
        return 0;
    for (int n = 0; n < fields.size(); ++n) {
        if (fields[n].get_type() != Variant::DICTIONARY)
            return 0;
        Dictionary field = fields[n];
        if (int64_t(field.get("slot", -1)) != n || !field.has("name") || !field.has("type"))
            return 0;
    }
    return int(count * 4);
}
enum class AttributeType { Other, Int, Bool, Float, Vec2, Vec3, Color, Quat, Vec4 };
AttributeType attribute_type(const String &type) {
    if (type == "int") return AttributeType::Int;
    if (type == "bool") return AttributeType::Bool;
    if (type == "float") return AttributeType::Float;
    if (type == "vec2") return AttributeType::Vec2;
    if (type == "vec3" || type == "position") return AttributeType::Vec3;
    if (type == "color") return AttributeType::Color;
    if (type == "quat") return AttributeType::Quat;
    if (type == "vec4") return AttributeType::Vec4;
    return AttributeType::Other;
}
struct AttributeField {
    Variant name;
    AttributeType type;
};
std::vector<AttributeField> attribute_fields(const Dictionary &program) {
    Array fields = program.get("attribute_layout", Array());
    std::vector<AttributeField> result;
    result.reserve(fields.size());
    for (int64_t c = 0; c < fields.size(); ++c) {
        Dictionary field = fields[c];
        result.push_back({field["name"], attribute_type(field["type"])});
    }
    return result;
}
void components(uint8_t *p, int offset, const Variant &v, AttributeType type) {
    if (type == AttributeType::Int) {
        word(p, offset, uint32_t(int64_t(v)));
        return;
    }
    if (type == AttributeType::Bool || type == AttributeType::Float) {
        number(p, offset, double(v));
        return;
    }
    if (v.get_type() == Variant::VECTOR2) {
        Vector2 a = v;
        number(p, offset, a.x);
        number(p, offset + 1, a.y);
    } else if (v.get_type() == Variant::VECTOR3)
        vector(p, offset, v);
    else if (v.get_type() == Variant::COLOR)
        color(p, offset, v);
    else if (v.get_type() == Variant::QUATERNION) {
        Quaternion q = v;
        for (int c = 0; c < 4; ++c)
            number(p, offset + c, q[c]);
    } else if (v.get_type() == Variant::VECTOR4) {
        Vector4 a = v;
        for (int c = 0; c < 4; ++c)
            number(p, offset + c, a[c]);
    }
}
Variant attribute(const uint8_t *p, int offset, AttributeType type) {
    if (type == AttributeType::Int) {
        uint32_t bits = word(p, offset);
        int32_t v;
        std::memcpy(&v, &bits, 4);
        return v;
    }
    if (type == AttributeType::Bool)
        return number(p, offset) != 0;
    if (type == AttributeType::Float)
        return number(p, offset);
    if (type == AttributeType::Vec2)
        return Vector2(number(p, offset), number(p, offset + 1));
    if (type == AttributeType::Vec3)
        return vector(p, offset);
    if (type == AttributeType::Color)
        return color(p, offset);
    if (type == AttributeType::Quat)
        return Quaternion(number(p, offset), number(p, offset + 1), number(p, offset + 2),
                          number(p, offset + 3));
    if (type == AttributeType::Vec4)
        return Vector4(number(p, offset), number(p, offset + 1), number(p, offset + 2),
                       number(p, offset + 3));
    return Variant();
}
} // namespace
void NBNativeGPUParticleBuffer::_bind_methods() {
    ClassDB::bind_method(D_METHOD("import_particles", "program", "particles"), &NBNativeGPUParticleBuffer::import_particles);
    ClassDB::bind_method(D_METHOD("append_particles", "particles"), &NBNativeGPUParticleBuffer::append_particles);
    ClassDB::bind_method(D_METHOD("commit_output", "bytes", "compact"), &NBNativeGPUParticleBuffer::commit_output, DEFVAL(true));
    ClassDB::bind_method(D_METHOD("get_view"), &NBNativeGPUParticleBuffer::get_view);
    ClassDB::bind_method(D_METHOD("snapshot"), &NBNativeGPUParticleBuffer::snapshot);
    ClassDB::bind_method(D_METHOD("project_into", "particles"), &NBNativeGPUParticleBuffer::project_into);
    ClassDB::bind_method(D_METHOD("duplicate_buffer"), &NBNativeGPUParticleBuffer::duplicate_buffer);
    ClassDB::bind_method(D_METHOD("prepare_tick_metadata"), &NBNativeGPUParticleBuffer::prepare_tick_metadata);
    ClassDB::bind_method(D_METHOD("matches_layout", "program"), &NBNativeGPUParticleBuffer::matches_layout);
    ClassDB::bind_method(D_METHOD("get_count"), &NBNativeGPUParticleBuffer::get_count);
    ClassDB::bind_method(D_METHOD("get_stride_bytes"), &NBNativeGPUParticleBuffer::get_stride_bytes);
    ClassDB::bind_method(D_METHOD("get_bytes"), &NBNativeGPUParticleBuffer::get_bytes);
    ClassDB::bind_method(D_METHOD("dispatch_bytes"), &NBNativeGPUParticleBuffer::dispatch_bytes);
    ClassDB::bind_method(D_METHOD("get_statistics"), &NBNativeGPUParticleBuffer::get_statistics);
}
TypedArray<String> NBNativeGPUParticleBuffer::import_particles(const Dictionary &program, const Array &particles) {
    TypedArray<String> errors;
    int width = stride(program);
    PackedByteArray packed = NBNativeGPUBuffers::pack(particles, program);
    if (!width || particles.size() > INT32_MAX / (width * 4) || packed.size() != particles.size() * width * 4) {
        errors.append("Invalid resident GPU particle layout or input.");
        return errors;
    }
    Dictionary schema;
    schema["stride_vec4"] = width / 4;
    schema["attribute_layout"] = Array(program.get("attribute_layout", Array())).duplicate(true);
    cached_view.unref();
    bytes = packed;
    metadata = particles.duplicate(true);
    layout = schema;
    stride_bytes = width * 4;
    executed_rows.resize(particles.size());
    executed_rows.fill(0);
    tick_rows = executed_rows;
    return errors;
}
bool NBNativeGPUParticleBuffer::matches_layout(const Dictionary &program) const {
    if (!stride_bytes || stride(program) * 4 != stride_bytes)
        return false;
    return Array(program.get("attribute_layout", Array())) == Array(layout["attribute_layout"]);
}
TypedArray<String> NBNativeGPUParticleBuffer::append_particles(const Array &particles) {
    TypedArray<String> errors;
    if (!stride_bytes || particles.size() > INT32_MAX / stride_bytes - metadata.size()) {
        errors.append("Uninitialized or oversized resident GPU particle append.");
        return errors;
    }
    PackedByteArray packed = NBNativeGPUBuffers::pack(particles, layout);
    if (packed.size() != particles.size() * stride_bytes) {
        errors.append("Invalid resident GPU birth packet.");
        return errors;
    }
    // Array has shared mutable storage, so detach before appending to a copy.
    Array joined = metadata.duplicate();
    joined.append_array(particles.duplicate(true));
    PackedByteArray joined_bytes = bytes;
    joined_bytes.append_array(packed);
    cached_view.unref();
    metadata = joined;
    bytes = joined_bytes;
    const int64_t old_count = executed_rows.size();
    executed_rows.resize(metadata.size());
    tick_rows.resize(metadata.size());
    uint8_t *executed = executed_rows.ptrw(), *ticks = tick_rows.ptrw();
    for (int64_t n = old_count; n < metadata.size(); ++n)
        executed[n] = ticks[n] = 0;
    return errors;
}
TypedArray<String> NBNativeGPUParticleBuffer::commit_output(const PackedByteArray &output, bool compact) {
    TypedArray<String> errors;
    const int64_t count = metadata.size();
    if (!stride_bytes || output.size() != count * stride_bytes) {
        errors.append("Resident GPU output byte count differs from its input.");
        return errors;
    }
    const uint8_t *source = output.ptr();
    std::vector<int64_t> order(size_t(count), -1);
    int64_t survivors = 0;
    for (int64_t n = 0; n < count; ++n) {
        const uint8_t *row = source + n * stride_bytes;
        for (int offset : {151, 188, 189, 190, 191}) {
            double value = number(row, offset);
            if (!std::isfinite(value) || value < double(INT64_MIN) || value >= double(INT64_MAX)) {
                errors.append("Invalid resident GPU allocation word.");
                return errors;
            }
        }
        if (!(number(row, 39) > .5f))
            continue;
        int64_t index = int64_t(number(row, 188));
        if (index < 0 || index >= count || order[size_t(index)] != -1) {
            errors.append("Resident GPU output allocation is missing or repeated.");
            return errors;
        }
        order[size_t(index)] = n;
        ++survivors;
    }
    for (int64_t n = 0; n < survivors; ++n)
        if (order[size_t(n)] < 0) {
            errors.append("Resident GPU output allocation has a gap.");
            return errors;
        }
    // unpack() leaves these CPU-owned values untouched; the next pack() writes
    // them from metadata again. Preserve the exact original ABI words directly.
    const uint8_t *before = bytes.ptr();
    auto preserve_cpu_words=[](uint8_t *row,const uint8_t *prior) {
        for(int offset:{43,47}) std::memcpy(row+offset*4,prior+offset*4,4);
        for(int offset=60;offset<84;++offset)
            if(offset!=63 && offset!=67) std::memcpy(row+offset*4,prior+offset*4,4);
        std::memcpy(row+184*4,prior+184*4,16);
    };
    if (!compact) {
        PackedByteArray normalized=output;
        uint8_t *target=normalized.ptrw();
        for(int64_t n=0;n<count;++n) preserve_cpu_words(target+n*stride_bytes,before+n*stride_bytes);
        cached_view.unref();bytes=normalized;executed_rows.fill(1);
        return errors;
    }
    // Compact directly from the GPU output. Patch CPU-owned words only in the
    // final surviving row, avoiding a full intermediate COW copy/normalization.
    PackedByteArray packed;
    packed.resize(survivors * stride_bytes);
    uint8_t *target = packed.ptrw();
    Array retained;
    retained.resize(survivors);
    PackedByteArray retained_ticks;
    retained_ticks.resize(survivors);
    uint8_t *ticks = retained_ticks.ptrw();
    const uint8_t *old_ticks = tick_rows.ptr();
    for (int64_t n = 0; n < survivors; ++n) {
        int64_t original = order[size_t(n)];
        std::memcpy(target + n * stride_bytes, source + original * stride_bytes, stride_bytes);
        preserve_cpu_words(target+n*stride_bytes,before+original*stride_bytes);
        retained[n] = metadata[original];
        ticks[n] = old_ticks[original];
    }
    cached_view.unref();
    bytes = packed;
    metadata = retained;
    tick_rows = retained_ticks;
    executed_rows.resize(survivors);
    executed_rows.fill(1);
    return errors;
}
PackedByteArray NBNativeGPUParticleBuffer::dispatch_bytes() const {
    PackedByteArray result = bytes;
    if (!stride_bytes || metadata.is_empty())
        return result;
    const auto fields = attribute_fields(layout);
    uint8_t *data = result.ptrw();
    const uint8_t *executed = executed_rows.ptr();
    for (int64_t n = 0; n < metadata.size(); ++n) {
        if (!executed[n])
            continue;
        uint8_t *row = data + n * stride_bytes;
        auto scalar_roundtrip = [&](int offset) {
            uint32_t bits = word(row, offset);
            if ((bits & 0x7f800000u) == 0x7f800000u && (bits & 0x007fffffu)) {
                // Scalar unpack values pass through Variant's double storage;
                // unlike vectors/cache, signalling NaNs are quieted there.
                volatile double expanded = double(number(row, offset));
                number(row, offset, expanded);
            }
        };
        for (const auto &field : scalars)
            if (field.offset != 43 && field.offset != 47 && field.offset != 151)
                scalar_roundtrip(field.offset);
        for (int offset : {39, 55, 171})
            number(row, offset, number(row, offset) > .5f ? 1. : 0.);
        number(row, 151, double(int64_t(number(row, 151))));
        std::memset(row + 188 * 4, 0, 16);
        for (size_t c = 0; c < fields.size(); ++c) {
            int offset = BASE + int(c) * 4;
            AttributeType type = fields[c].type;
            int width = type == AttributeType::Int || type == AttributeType::Bool || type == AttributeType::Float ? 1
                        : type == AttributeType::Vec2 ? 2 : type == AttributeType::Vec3 ? 3
                        : type == AttributeType::Other ? 0 : 4;
            if (type == AttributeType::Bool)
                number(row, offset, number(row, offset) != 0.f ? 1. : 0.);
            else if (type == AttributeType::Float)
                scalar_roundtrip(offset);
            if (width < 4)
                std::memset(row + (offset + width) * 4, 0, (4 - width) * 4);
        }
    }
    return result;
}
void NBNativeGPUParticleBuffer::prepare_tick_metadata() {
    cached_view.unref();
    if (!stride_bytes)
        return;
    uint8_t *data = bytes.ptrw();
    const int64_t count = metadata.size();
    for (int64_t n = 0; n < count; ++n) {
        number(data + n * stride_bytes, 186, double(n));
        number(data + n * stride_bytes, 187, double(count));
    }
    tick_rows.resize(count);
    tick_rows.fill(1);
}
Ref<NBNativeParticleView> NBNativeGPUParticleBuffer::get_view() const {
    if (cached_view.is_valid()) return cached_view;
    cached_view.instantiate();
    cached_view->initialize(metadata);
    if (!stride_bytes || bytes.size() != metadata.size() * stride_bytes) {
        cached_view->add_error("Invalid resident GPU snapshot.");
        return cached_view;
    }
    const uint8_t *data = bytes.ptr();
    for (int64_t lane = 0; lane < metadata.size(); ++lane)
        for (int offset : {151, 188, 189, 190, 191}) {
            double value = number(data + lane * stride_bytes, offset);
            if (!std::isfinite(value) || value < double(INT64_MIN) || value >= double(INT64_MAX)) {
                cached_view->add_error("Invalid resident GPU snapshot.");
                return cached_view;
            }
        }
    const PackedByteArray retained = bytes;
    const int64_t width = stride_bytes;
    cached_view->add_field("spawn_group", [retained, data, width](int64_t lane) -> Variant {
        return int64_t(int32_t(word(data + lane * width, 71)));
    });
    for (auto field : scalars) {
        if (field.offset == 43 || field.offset == 47) continue;
        cached_view->add_field(field.name, [retained, data, width, offset = field.offset](int64_t lane) -> Variant {
            return offset == 151 ? Variant(int64_t(number(data + lane * width, offset))) : Variant(number(data + lane * width, offset));
        });
    }
    for(auto field:scalars) {
        if(field.offset==43 || field.offset==47 || field.offset==151) continue;
        cached_view->add_native_field<double>(field.name,[retained,data,width,offset=field.offset](int64_t lane){return double(number(data+lane*width,offset));});
    }
    for (auto field : vectors)
        cached_view->add_native_field<Vector3>(field.name, [retained, data, width, offset = field.offset](int64_t lane) { return vector(data + lane * width, offset); });
    for (auto field : colors)
        cached_view->add_native_field<Color>(field.name, [retained, data, width, offset = field.offset](int64_t lane) { return color(data + lane * width, offset); });
    for (auto field : rotations)
        cached_view->add_native_field<Quaternion>(field.name, [retained, data, width, offset = field.offset](int64_t lane) {
            const uint8_t *p = data + lane * width;
            return Quaternion(number(p, offset), number(p, offset + 1), number(p, offset + 2), number(p, offset + 3));
        });
    for (auto field : pairs)
        cached_view->add_native_field<Vector2>(field.name, [retained, data, width, offset = field.offset](int64_t lane) {
            const uint8_t *p = data + lane * width; return Vector2(number(p, offset), number(p, offset + 1));
        });
    for (const auto &field : std::array<Field, 3>{{{"alive",39},{"drag_ignore_mass",55},{"first_frame",171}}})
        cached_view->add_field(field.name, [retained, data, width, offset = field.offset](int64_t lane) { return number(data + lane * width, offset) > .5f; });
    for (const auto &field : std::array<Field, 2>{{{"gpu_seed",58},{"gpu_cache_mask",59}}})
        cached_view->add_field(field.name, [retained, data, width, offset = field.offset](int64_t lane) { return int64_t(word(data + lane * width, offset)); });
    for (const auto &field : std::array<Field, 4>{{{"gpu_output_index",188},{"gpu_last_execution_index",189},{"gpu_last_execution_count",190},{"gpu_subgroup_size",191}}})
        cached_view->add_field(field.name, [retained, data, width, offset = field.offset](int64_t lane) { return int64_t(number(data + lane * width, offset)); });
    cached_view->add_field("gpu_cache", [retained, data, width](int64_t lane) {
        PackedFloat32Array result; result.resize(CACHE_SIZE);
        std::memcpy(result.ptrw(), data + lane * width + CACHE_START * 4, CACHE_SIZE * 4); return result;
    });
    cached_view->add_field("gpu_needs_spawn", [retained, data, width, executed = executed_rows](int64_t lane) { return !executed[lane] && number(data + lane * width, 39) < 0.f; });
    for (int c = 0; c < 2; ++c) {
        String name = c == 0 ? "update_execution_index" : "update_execution_count";
        cached_view->add_field(name, [retained, data, width, ticks = tick_rows, metadata = metadata, name, c](int64_t lane) -> Variant {
            return ticks[lane] ? Variant(int64_t(number(data + lane * width, 186 + c))) : Dictionary(metadata[lane]).get(name, Variant());
        });
    }
    const auto fields = attribute_fields(layout);
    for (size_t c = 0; c < fields.size(); ++c)
        cached_view->add_attribute(fields[c].name, [retained, data, width, type = fields[c].type, offset = BASE + int(c) * 4](int64_t lane) { return attribute(data + lane * width, offset, type); });
    return cached_view;
}
Dictionary NBNativeGPUParticleBuffer::snapshot() const {
    Dictionary result;
    TypedArray<String> errors;
    Array particles;
    if (!stride_bytes)
        errors.append("Uninitialized resident GPU particle buffer.");
    else {
        particles = metadata.duplicate(true);
        if (!NBNativeGPUBuffers::unpack(bytes, particles, layout)) {
            errors.append("Invalid resident GPU snapshot.");
            particles.clear();
        } else {
            const uint8_t *data = bytes.ptr();
            for (int64_t n = 0; n < particles.size(); ++n) {
                Dictionary particle = particles[n];
                const uint8_t *row = data + n * stride_bytes;
                particle["gpu_needs_spawn"] = !executed_rows[n] && number(row, 39) < 0.f;
                if (tick_rows[n]) {
                    particle["update_execution_index"] = int64_t(number(row, 186));
                    particle["update_execution_count"] = int64_t(number(row, 187));
                }
            }
        }
    }
    result["errors"] = errors;
    result["particles"] = particles;
    return result;
}
Array NBNativeGPUParticleBuffer::metadata_rows() const {
    Array rows;rows.resize(metadata.size());
    const Variant id_key=String("id"),zero=int64_t(0);
    for(int64_t i=0;i<metadata.size();++i) {
        Dictionary row;row[id_key]=Dictionary(metadata[i]).get(id_key,zero);
        rows[i]=row;
    }
    return rows;
}
TypedArray<String> NBNativeGPUParticleBuffer::project_into(const Array &particles) const {
    TypedArray<String> errors;
    if (!stride_bytes || particles.size() != metadata.size() ||
        !NBNativeGPUBuffers::unpack(bytes, particles, layout))
        errors.append("Resident GPU projection has incompatible target rows.");
    return errors;
}
Ref<NBNativeGPUParticleBuffer> NBNativeGPUParticleBuffer::duplicate_buffer() const {
    Ref<NBNativeGPUParticleBuffer> result;
    result.instantiate();
    result->bytes = bytes;
    result->metadata = metadata;
    result->layout = layout;
    result->stride_bytes = stride_bytes;
    result->executed_rows = executed_rows;
    result->tick_rows = tick_rows;
    return result;
}
Dictionary NBNativeGPUParticleBuffer::get_statistics() const {
    Dictionary result;
    result["particles"] = metadata.size();
    result["byte_count"] = bytes.size();
    result["stride_bytes"] = stride_bytes;
    result["native_resident"] = true;
    return result;
}
void NBNativeGPUBuffers::_bind_methods() {
    ClassDB::bind_static_method("NBNativeGPUBuffers", D_METHOD("pack", "particles", "program"),
                                &NBNativeGPUBuffers::pack);
    ClassDB::bind_static_method("NBNativeGPUBuffers", D_METHOD("unpack", "bytes", "particles", "program"),
                                &NBNativeGPUBuffers::unpack);
    ClassDB::bind_static_method("NBNativeGPUBuffers",
                                D_METHOD("parameters", "program", "delta", "time", "duration", "count",
                                         "local_space", "source", "values"),
                                &NBNativeGPUBuffers::parameters);
}
PackedByteArray NBNativeGPUBuffers::pack(const Array &particles, const Dictionary &program) {
    PackedByteArray bytes;
    int width = stride(program);
    if (!width || particles.size() > INT32_MAX / (width * 4))
        return bytes;
    bytes.resize(particles.size() * width * 4);
    bytes.fill(0);
    uint8_t *data = bytes.ptrw();
    const auto fields = attribute_fields(program);
    const auto scalar_fields = named_fields(scalars);
    const auto vector_fields = named_fields(vectors);
    const auto color_fields = named_fields(colors);
    const auto rotation_fields = named_fields(rotations);
    const auto pair_fields = named_fields(pairs);
    const int64_t particle_count = particles.size();
    for (int64_t n = 0; n < particle_count; ++n) {
        if (particles[n].get_type() != Variant::DICTIONARY)
            return PackedByteArray();
    }
    const PackedFloat32Array empty_cache;
    const Dictionary empty_attributes;
    auto pack_row = [&](int64_t n) {
        Dictionary p = particles[n];
        uint8_t *out = data + n * width * 4;
        for (const auto &entry : scalar_fields)
            number(out, entry.offset, p.get(entry.name, entry.offset == 155 ? 1. : 0.));
        for (const auto &entry : vector_fields)
            vector(out, entry.offset, Vector3(p.get(entry.name, Vector3())));
        for (const auto &entry : color_fields)
            color(out, entry.offset, Color(p.get(entry.name, Color(0, 0, 0, 0))));
        for (const auto &entry : rotation_fields) {
            Quaternion q = p.get(entry.name, Quaternion());
            for (int c = 0; c < 4; ++c)
                number(out, entry.offset + c, q[c]);
        }
        for (const auto &entry : pair_fields) {
            Vector2 a = p.get(entry.name, Vector2());
            number(out, entry.offset, a.x);
            number(out, entry.offset + 1, a.y);
        }
        number(out, 39, bool(p.get("gpu_needs_spawn", false)) ? -1. : bool(p.get("alive", false)) ? 1. : 0.);
        number(out, 55, bool(p.get("drag_ignore_mass", false)) ? 1. : 0.);
        number(out, 171, bool(p.get("first_frame", false)) ? 1. : 0.);
        Transform3D birth = p.get("gpu_birth_transform", Transform3D());
        basis(out, 60, unreal_basis(birth.basis));
        basis(out, 72, unreal_basis(birth.basis.orthonormalized()));
        Vector3 origin = NBNativeValues::godot_to_unreal(birth.origin);
        number(out, 75, origin.x);
        number(out, 79, origin.y);
        number(out, 83, origin.z);
        // The shader ABI overlays padding words with live particle fields.
        number(out, 63, p.get("normalized_age", 0.));
        number(out, 67, p.get("camera_offset", 0.));
        word(out, 71, uint32_t(int64_t(p.get("spawn_group", 0))));
        PackedFloat32Array cache = p.get("gpu_cache", empty_cache);
        const int64_t cache_count = std::min(int64_t(CACHE_SIZE), cache.size());
        const float *cache_data = cache.ptr();
        for (int c = 0; c < cache_count; ++c)
            number(out, CACHE_START + c, cache_data[c]);
        word(out, 58, uint32_t(int64_t(p.get("gpu_seed", 1))));
        word(out, 59, uint32_t(int64_t(p.get("gpu_cache_mask", 0))));
        number(out, 184, p.get("spawn_execution_index", 0));
        number(out, 185, p.get("spawn_execution_count", 1));
        number(out, 186, p.get("update_execution_index", 0));
        number(out, 187, p.get("update_execution_count", 1));
        Dictionary attributes = p.get("attributes", empty_attributes);
        for (size_t c = 0; c < fields.size(); ++c) {
            const auto &field = fields[c];
            Variant v = attributes.get(field.name, Variant());
            if (v.get_type() != Variant::NIL)
                components(out, BASE + c * 4, v, field.type);
        }
    };
    constexpr int64_t block_size = 64;
    auto *pool = WorkerThreadPool::get_singleton();
    if (particle_count >= 2 * block_size && pool && pool->get_caller_task_id() < 0 && pool->get_caller_group_id() < 0) {
        const uint32_t blocks = uint32_t((particle_count + block_size - 1) / block_size);
        auto pack_block = [&](uint32_t block) {
            int64_t first = int64_t(block) * block_size;
            for (int64_t n = first; n < std::min(first + block_size, particle_count); ++n)
                pack_row(n);
        };
        auto task = pool->add_native_group_task([](void *data, uint32_t index) {
            (*static_cast<decltype(pack_block) *>(data))(index + 1);
        }, &pack_block, blocks - 1, std::min(3u, blocks - 1), true);
        pack_block(0);
        pool->wait_for_group_task_completion(task);
    } else {
        for (int64_t n = 0; n < particle_count; ++n)
            pack_row(n);
    }
    return bytes;
}
bool NBNativeGPUBuffers::unpack(const PackedByteArray &bytes, const Array &particles,
                                const Dictionary &program) {
    int width = stride(program);
    if (!width || particles.size() > INT32_MAX / (width * 4) || bytes.size() != particles.size() * width * 4)
        return false;
    const uint8_t *data = bytes.ptr();
    for (int n = 0; n < particles.size(); ++n) {
        if (particles[n].get_type() != Variant::DICTIONARY)
            return false;
        for (int offset : {151, 188, 189, 190, 191}) {
            const double value = number(data + int64_t(n) * width * 4, offset);
            if (!std::isfinite(value) || value < double(INT64_MIN) || value >= double(INT64_MAX))
                return false;
        }
    }
    const auto fields = attribute_fields(program);
    const auto scalar_fields = named_fields(scalars);
    const auto vector_fields = named_fields(vectors);
    const auto color_fields = named_fields(colors);
    const auto rotation_fields = named_fields(rotations);
    const auto pair_fields = named_fields(pairs);
    const int64_t particle_count = particles.size();
    for (int64_t n = 0; n < particle_count; ++n) {
        Dictionary p = particles[n];
        const uint8_t *in = data + n * width * 4;
        for (const auto &entry : scalar_fields) {
            if (entry.offset == 43 || entry.offset == 47)
                continue; // Source birth time and ID are CPU-owned.
            p[entry.name] = entry.offset == 151 ? Variant(int64_t(number(in, entry.offset)))
                                                : Variant(number(in, entry.offset));
        }
        for (const auto &entry : vector_fields)
            p[entry.name] = vector(in, entry.offset);
        for (const auto &entry : color_fields)
            p[entry.name] = color(in, entry.offset);
        for (const auto &entry : rotation_fields)
            p[entry.name] = Quaternion(number(in, entry.offset), number(in, entry.offset + 1),
                                       number(in, entry.offset + 2), number(in, entry.offset + 3));
        for (const auto &entry : pair_fields)
            p[entry.name] = Vector2(number(in, entry.offset), number(in, entry.offset + 1));
        p["alive"] = number(in, 39) > .5;
        p["drag_ignore_mass"] = number(in, 55) > .5;
        p["first_frame"] = number(in, 171) > .5;
        p["spawn_group"] = int64_t(int32_t(word(in, 71)));
        p["gpu_seed"] = int64_t(word(in, 58));
        p["gpu_cache_mask"] = int64_t(word(in, 59));
        PackedFloat32Array cache;
        cache.resize(CACHE_SIZE);
        std::memcpy(cache.ptrw(), in + CACHE_START * 4, CACHE_SIZE * 4);
        p["gpu_cache"] = cache;
        p["gpu_output_index"] = int64_t(number(in, 188));
        p["gpu_last_execution_index"] = int64_t(number(in, 189));
        p["gpu_last_execution_count"] = int64_t(number(in, 190));
        p["gpu_subgroup_size"] = int64_t(number(in, 191));
        Dictionary attributes = p.get("attributes", Dictionary());
        for (size_t c = 0; c < fields.size(); ++c) {
            const auto &field = fields[c];
            attributes[field.name] = attribute(in, BASE + c * 4, field.type);
        }
        p["attributes"] = attributes;
        p["gpu_needs_spawn"] = false;
    }
    return true;
}
PackedFloat32Array NBNativeGPUBuffers::parameters(const Dictionary &program, double delta, double time,
                                                  double duration, int64_t count, bool local,
                                                  const Transform3D &source, const Dictionary &values) {
    Array parameters = program.get("parameters", Array());
    PackedByteArray bytes;
    bytes.resize((8 + parameters.size()) * 16);
    bytes.fill(0);
    uint8_t *out = bytes.ptrw();
    number(out, 0, delta);
    number(out, 1, time);
    number(out, 2, duration);
    number(out, 3, double(count));
    basis(out, 4, unreal_basis(source.basis));
    basis(out, 16, unreal_basis(source.basis.orthonormalized()));
    Vector3 origin = NBNativeValues::godot_to_unreal(source.origin);
    number(out, 19, origin.x);
    number(out, 23, origin.y);
    number(out, 27, origin.z);
    number(out, 28, local ? 1. : 0.);
    for (int n = 0; n < parameters.size(); ++n) {
        Dictionary parameter = parameters[n];
        String type = parameter.get("type", "");
        Variant v = values.get(parameter.get("name", ""), 0.);
        int offset = 32 + n * 4;
        if (type == "int")
            continue; // Integer words are patched into the upload, preserving the public float API.
        if (type == "vec2") {
            Vector2 a = NBNativeValues::vector2(v);
            number(out, offset, a.x);
            number(out, offset + 1, a.y);
        } else if (type == "vec3")
            vector(out, offset, NBNativeValues::vector3(v));
        else if (type == "vec4") {
            if (v.get_type() == Variant::QUATERNION || v.get_type() == Variant::VECTOR4)
                components(out, offset, v, attribute_type(type));
            else
                color(out, offset, NBNativeValues::color(v));
        } else
            number(out, offset, v);
    }
    return bytes.to_float32_array();
}
} // namespace godot
