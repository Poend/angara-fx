#include "particle_forces.h"
#include "module_math.h"
#include "value_evaluator.h"
#include <godot_cpp/classes/random_number_generator.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
namespace godot {
namespace {
std::array<Ref<Resource>, 3> fields;
std::mutex fields_mutex;
Variant raw(const Ref<Resource> &op, const String &key, const Variant &fallback) {
    if (op.is_null())
        return fallback;
    return op->call("get_input", key, fallback);
}
} // namespace
void NBNativeVectorFields::_bind_methods() {
    ClassDB::bind_static_method("NBNativeVectorFields", D_METHOD("validation_error", "field"),
                                &NBNativeVectorFields::validation_error);
    ClassDB::bind_static_method("NBNativeVectorFields", D_METHOD("sample_cpu", "field", "position", "tiling"),
                                &NBNativeVectorFields::sample_cpu, DEFVAL(Vector3i(1, 1, 1)));
    ClassDB::bind_static_method("NBNativeVectorFields", D_METHOD("_index", "value", "dimension", "tiled"),
                                &NBNativeVectorFields::index);
    ClassDB::bind_static_method("NBNativeVectorFields", D_METHOD("_lerp", "a", "b", "weight"),
                                &NBNativeVectorFields::lerp);
    ClassDB::bind_static_method("NBNativeVectorFields",
                                D_METHOD("_value", "field", "x", "y", "z", "component"),
                                &NBNativeVectorFields::value);
}
String NBNativeVectorFields::validation_error(const Ref<Resource> &field) {
    if (field.is_null())
        return "Vector field resource is required.";
    Vector3i size = field->get("size");
    if (size.x <= 0 || size.y <= 0 || size.z <= 0 || size.x > 4194304 || size.y > 4194304 || size.z > 4194304)
        return "Vector field dimensions must be positive and bounded.";
    if (double(size.x) * size.y * size.z > 4194304.)
        return "Vector field exceeds the 4194304 voxel limit.";
    int64_t count = int64_t(size.x) * size.y * size.z;
    PackedFloat32Array data = field->get("vectors_xyz");
    if (count * 3 != data.size())
        return "Vector field dimensions do not match its XYZ lattice (maximum 4194304 voxels).";
    PackedFloat64Array lo = field->get("bounds_min"), hi = field->get("bounds_max");
    if (lo.size() != 3 || hi.size() != 3)
        return "Vector field bounds must contain three coordinates.";
    for (int n = 0; n < 3; ++n)
        if (!std::isfinite(lo[n]) || !std::isfinite(hi[n]) || hi[n] - lo[n] <= 1e-8)
            return "Vector field bounds must be finite with a positive extent.";
    for (int64_t n = 0; n < data.size(); ++n) {
        if (!std::isfinite(data[n]))
            return "Vector field contains a non-finite vector.";
        if (std::abs(data[n]) > 65504.)
            return "Vector field component exceeds the native float16 range.";
    }
    return String();
}
int64_t NBNativeVectorFields::index(double v, int64_t dimension, bool tiled) {
    if (dimension < 1 || !std::isfinite(v))
        return 0;
    if (tiled)
        v -= std::floor(v / double(dimension)) * dimension;
    return int64_t(std::clamp(v, 0., double(dimension - 1)));
}
double NBNativeVectorFields::lerp(double a, double b, double w) {
    return a + w * (b - a);
}
double NBNativeVectorFields::value(const Ref<Resource> &field, int64_t x, int64_t y, int64_t z,
                                   int64_t component) {
    if (field.is_null())
        return 0.;
    Vector3i size = field->get("size");
    PackedFloat32Array data = field->get("vectors_xyz");
    if (x < 0 || y < 0 || z < 0 || x >= size.x || y >= size.y || z >= size.z || component < 0 ||
        component >= 3)
        return 0.;
    int64_t offset = (x + int64_t(size.x) * (y + int64_t(size.y) * z)) * 3 + component;
    return offset >= 0 && offset < data.size() ? data[offset] : 0.;
}
Vector3 NBNativeVectorFields::sample_cpu(const Ref<Resource> &field, const Vector3 &p,
                                         const Vector3i &tiling) {
    if (field.is_null() || !bool(field->get("allow_cpu_access")) || !p.is_finite())
        return Vector3();
    Vector3i size = field->get("size");
    PackedFloat64Array lo = field->get("bounds_min"), hi = field->get("bounds_max");
    PackedFloat32Array data = field->get("vectors_xyz");
    if (size.x <= 0 || size.y <= 0 || size.z <= 0 || double(size.x) * size.y * size.z > 4194304. ||
        lo.size() != 3 || hi.size() != 3 || int64_t(size.x) * size.y * size.z * 3 != data.size())
        return Vector3();
    int64_t a[3], b[3];
    double f[3];
    for (int axis = 0; axis < 3; ++axis) {
        double extent = hi[axis] - lo[axis];
        if (!(extent > 1e-8))
            return Vector3();
        double cell = ((double(p[axis]) - lo[axis]) * (1. / extent)) * size[axis] - .5,
               base = std::floor(cell);
        f[axis] = cell - base;
        a[axis] = index(base, size[axis], tiling[axis] != 0);
        b[axis] = index(base + 1., size[axis], tiling[axis] != 0);
    }
    auto read = [&](int64_t x, int64_t y, int64_t z, int c) {
        return double(data[(x + int64_t(size.x) * (y + int64_t(size.y) * z)) * 3 + c]);
    };
    Vector3 result;
    for (int c = 0; c < 3; ++c) {
        double v00 = lerp(read(a[0], a[1], a[2], c), read(b[0], a[1], a[2], c), f[0]),
               v10 = lerp(read(a[0], b[1], a[2], c), read(b[0], b[1], a[2], c), f[0]);
        double v01 = lerp(read(a[0], a[1], b[2], c), read(b[0], a[1], b[2], c), f[0]),
               v11 = lerp(read(a[0], b[1], b[2], c), read(b[0], b[1], b[2], c), f[0]);
        result[c] = lerp(lerp(v00, v10, f[1]), lerp(v01, v11, f[1]), f[2]);
    }
    return result;
}
void NBNativeAttractionForces::_bind_methods() {
    ClassDB::bind_static_method("NBNativeAttractionForces",
                                D_METHOD("input", "operation", "key", "fallback", "context"),
                                &NBNativeAttractionForces::input);
    ClassDB::bind_static_method("NBNativeAttractionForces",
                                D_METHOD("vector", "operation", "key", "fallback", "context"),
                                &NBNativeAttractionForces::vector);
    ClassDB::bind_static_method("NBNativeAttractionForces", D_METHOD("_reference", "name"),
                                &NBNativeAttractionForces::reference);
    ClassDB::bind_static_method("NBNativeAttractionForces",
                                D_METHOD("normalized_range", "distance", "radius"),
                                &NBNativeAttractionForces::normalized_range);
    ClassDB::bind_static_method("NBNativeAttractionForces", D_METHOD("vortex", "operation", "context"),
                                &NBNativeAttractionForces::vortex);
    ClassDB::bind_static_method("NBNativeAttractionForces", D_METHOD("line", "operation", "context"),
                                &NBNativeAttractionForces::line);
}
Variant NBNativeAttractionForces::input(const Ref<Resource> &op, const String &key, const Variant &fallback,
                                        const Dictionary &ctx) {
    if (op.is_null())
        return fallback;
    return NBNativeValues::evaluate(raw(op, key, fallback), ctx,
                                    String(op->get("stable_id")) + String("/") + key + String("/0"));
}
Vector3 NBNativeAttractionForces::vector(const Ref<Resource> &op, const String &key, const Variant &fallback,
                                         const Dictionary &ctx) {
    return NBNativeValues::vector3(input(op, key, fallback, ctx));
}
Dictionary NBNativeAttractionForces::reference(const String &name) {
    Dictionary r;
    r["kind"] = "reference";
    r["name"] = name;
    return r;
}
double NBNativeAttractionForces::normalized_range(double distance, double radius) {
    return std::min(distance, radius) / std::max(radius, .0001);
}
Vector3 NBNativeAttractionForces::vortex(const Ref<Resource> &op, const Dictionary &ctx) {
    if (op.is_null())
        return Vector3();
    Vector3 origin = vector(op, "VortexOrigin", reference("Engine.Emitter.SimulationPosition"), ctx);
    if (bool(raw(op, "UseOriginOffset", false)))
        origin += NBNativeValues::transform_between(
            vector(op, "VortexOriginOffset", Vector3(), ctx),
            NBNativeValues::enum_name(raw(op, "VortexOriginOffsetCoordinateSpace", "Simulation")),
            "Simulation", ctx, false);
    Vector3 position = vector(op, "VortexInfluencePosition", reference("Particles.Position"), ctx),
            delta = origin - position;
    double distance = delta.length();
    Vector3 direction = delta.length_squared() < 1e-10 ? Vector3(0, 0, 1) : delta.normalized();
    if (delta.length_squared() < 1e-10)
        distance = 0.;
    Vector3 axis = vector(op, "VortexAxis", Vector3(0, 0, 1), ctx);
    axis = axis.length_squared() < 1e-10 ? Vector3(0, 0, 1) : axis.normalized();
    axis = NBNativeValues::transform_between(
        axis, NBNativeValues::enum_name(raw(op, "VortexAxisCoordinateSpace", "Simulation")), "Simulation",
        ctx, false);
    double influence = 1.;
    if (bool(raw(op, "UseInfluenceFalloff", false))) {
        double range = normalized_range(distance, input(op, "InfluenceFalloffRadius", 100., ctx));
        if (bool(input(op, "InvertInfluenceFalloff", true, ctx)))
            range = 1. - range;
        influence = std::pow(range, double(input(op, "InfluenceFalloffExponent", 1., ctx)));
    }
    Vector3 pull = direction;
    if (bool(raw(op, "OriginPullOnlyOnVortexAxis", true)))
        pull -= axis * direction.dot(axis);
    double amount = input(op, "VortexForceAmount", 200., ctx),
           origin_pull = input(op, "OriginPullAmount", 0., ctx);
    return (direction.cross(axis) * amount + pull * origin_pull) * influence;
}
Vector3 NBNativeAttractionForces::line(const Ref<Resource> &op, const Dictionary &ctx) {
    if (op.is_null())
        return Vector3();
    Vector3 start = vector(op, "LineStart", Vector3(100, 0, 0), ctx),
            end = vector(op, "LineEnd", Vector3(-100, 0, 0), ctx);
    if (bool(raw(op, "LineSegmentinLocalspace", false))) {
        start = NBNativeValues::transform_between(start, "Local", "Simulation", ctx, false, true);
        end = NBNativeValues::transform_between(end, "Local", "Simulation", ctx, false, true);
    }
    Vector3 position = ctx.get("Particles.Position", Vector3()), segment = start - end;
    double along = (position - end).dot(segment);
    Vector3 closest = along >= segment.length_squared() ? start : end;
    if (along > 0 && along < segment.length_squared())
        closest = end + segment * (along / segment.length_squared());
    Vector3 delta = position - closest;
    double strength = input(op, "AttractionStrength", 20., ctx);
    if (bool(raw(op, "UseAttractionFalloff", false)))
        strength = normalized_range(delta.length_squared() < 1e-10 ? 0. : delta.length(),
                                    input(op, "AttractionFalloff", 100., ctx));
    Vector3 biased = delta + Vector3(0, 0, .0001);
    return -(biased.length_squared() > 1e-8 ? biased.normalized() : Vector3()) * strength;
}
void NBNativeCurlNoise::_bind_methods() {
    ClassDB::bind_static_method("NBNativeCurlNoise", D_METHOD("quality", "operation"),
                                &NBNativeCurlNoise::quality);
    ClassDB::bind_static_method("NBNativeCurlNoise",
                                D_METHOD("input", "operation", "key", "default_value", "context"),
                                &NBNativeCurlNoise::input);
    ClassDB::bind_static_method("NBNativeCurlNoise", D_METHOD("ordinary_phase", "operation", "emitter_seed"),
                                &NBNativeCurlNoise::ordinary_phase);
    ClassDB::bind_static_method("NBNativeCurlNoise", D_METHOD("valid_seed", "value"),
                                &NBNativeCurlNoise::valid_seed);
    ClassDB::bind_static_method("NBNativeCurlNoise", D_METHOD("force", "operation", "context"),
                                &NBNativeCurlNoise::force);
}
void NBNativeCurlNoise::clear_cache() {
    std::lock_guard<std::mutex> guard(fields_mutex);
    for (auto &field : fields)
        field.unref();
}
int64_t NBNativeCurlNoise::quality(const Ref<Resource> &op) {
    if (op.is_null())
        return -1;
    String name = NBNativeValues::enum_name(raw(op, "NoiseQuality/Cost", "NewEnumerator1"));
    return name == "Baked" || name == "NewEnumerator0" ? 0
           : name == "NewEnumerator1"                  ? 1
           : name == "NewEnumerator2"                  ? 2
                                                       : -1;
}
Variant NBNativeCurlNoise::input(const Ref<Resource> &op, const String &key, const Variant &fallback,
                                 const Dictionary &ctx) {
    return NBNativeAttractionForces::input(op, key, fallback, ctx);
}
double NBNativeCurlNoise::ordinary_phase(const Ref<Resource> &op, int64_t emitter_seed) {
    int64_t seed = raw(op, "RandomSeed", 11),
            variation = bool(raw(op, "RandomizeNoiseSample", true)) ? emitter_seed : 0;
    Ref<RandomNumberGenerator> rng;
    rng.instantiate();
    rng->set_seed((uint64_t(uint32_t(seed)) << 32) | uint32_t(variation));
    return NBNativeCPUMath::f32(rng->randf() * 4096.);
}
bool NBNativeCurlNoise::valid_seed(const Variant &v) {
    return (v.get_type() == Variant::FLOAT || v.get_type() == Variant::INT) && std::isfinite(double(v)) &&
           double(v) >= -2147483648. && double(v) <= 2147483647. && double(v) == std::floor(double(v));
}
Vector3 NBNativeCurlNoise::force(const Ref<Resource> &op, Dictionary ctx) {
    if (op.is_null())
        return Vector3();
    String id = op->get("stable_id");
    double random;
    if (ctx.has("source_rng")) {
        Dictionary bindings = ctx["source_rng"];
        random =
            NBNativeSourceRandom::sample(bindings.get(id + String("/$CurlOffset"), Dictionary()), 0., 4096.,
                                         ctx.get("source_unique_id", 0), ctx.get("source_seed", 0));
    } else {
        Dictionary phases = ctx.get("_curl_phases", Dictionary());
        if (!phases.has(id))
            phases[id] = ordinary_phase(op, ctx.get("Emitter.RandomSeed", 0));
        random = phases[id];
    }
    Vector3 offset =
        NBNativeValues::vector3(input(op, "RandomizationVector", Vector3(.65, .125, .37), ctx)) * random;
    Vector3 position = NBNativeValues::vector3(
        input(op, "SamplePosition", NBNativeAttractionForces::reference("Particles.Position"), ctx));
    if (String(op->get("module_name")) == "V2/CurlNoiseForce@1.2") {
        position = NBNativeValues::transform_between(
            position, "World", NBNativeValues::enum_name(raw(op, "CoordinateSpace", "World")), ctx, false,
            true);
        if (bool(ctx.get("local_space", false)))
            position = NBNativeValues::transform_between(position, "Local", "World", ctx, true, true);
    }
    if (bool(raw(op, "ScaleCurlLookupByEngineOwnerScale", true))) {
        Vector3 scale = ctx.get("Engine.Owner.Scale", Vector3(1, 1, 1));
        for (int n = 0; n < 3; ++n)
            position[n] = std::abs(scale[n]) > 1e-8 ? position[n] / scale[n] : 0.;
    }
    double frequency =
        NBNativeCPUMath::f32(NBNativeCPUMath::f32(input(op, "NoiseFrequency", 10., ctx)) / 1000.);
    Vector3 point = (offset + position) * frequency;
    if (bool(raw(op, "PanNoise", false)))
        point -= NBNativeValues::vector3(input(op, "PanNoiseField", Vector3(.2, .5, .9), ctx)) *
                 double(ctx.get("Emitter.Age", 0.));
    point *= 125.;
    int q = int(quality(op));
    if (q < 0 || q > 2)
        return Vector3();
    Ref<Resource> field;
    {
        std::lock_guard<std::mutex> guard(fields_mutex);
        if (fields[q].is_null())
            fields[q] = ResourceLoader::get_singleton()->load(
                String("res://addons/angara_fx/data/vector_fields/TilingCurl") +
                String::num_int64(q == 0   ? 16
                                  : q == 1 ? 32
                                           : 64) +
                String(".tres"));
        field = fields[q];
    }
    return NBNativeVectorFields::sample_cpu(field, point) *
           NBNativeCPUMath::f32(input(op, "NoiseStrength", 10., ctx));
}
} // namespace godot
