#include "vm_external.h"
#include "vm_scalar.h"
#include "collision_world.h"
#include <algorithm>
#include <cmath>
using namespace godot;
namespace nbvm {
namespace {
float f32(double v) {
    return float(v);
}
float lerp32(double a, double b, double t) {
    return f32(a + f32(t * f32(b - a)));
}
int iv(const Dictionary &d, const char *key, int fallback = 0) {
    return int(int64_t(d.get(key, fallback)));
}
bool numeric(const Variant &v) {
    return (v.get_type() == Variant::INT || v.get_type() == Variant::FLOAT) && std::isfinite(double(v));
}
bool integer_value(const Variant &v) {
    return numeric(v) && double(v) >= 0 && double(v) <= 4294967295. && double(v) == std::floor(double(v));
}
bool triple(const Variant &v, bool positive = false, bool booleans = false) {
    if (v.get_type() != Variant::ARRAY)
        return false;
    Array a = v;
    if (a.size() != 3)
        return false;
    for (int n = 0; n < 3; n++)
        if (booleans ? a[n].get_type() != Variant::BOOL
                     : (!numeric(a[n]) || (positive && (!integer_value(a[n]) || double(a[n]) <= 0))))
            return false;
    return true;
}
int index_for_id(const Dictionary &data, int id, int tag) {
    Dictionary ids = data.get("ids", Dictionary());
    Array table = ids.get("table", Array()), particles = data.get("particles", Array());
    if (id < 0 || id >= table.size())
        return -1;
    int index = int(int64_t(table[id]));
    if (index < 0 || index >= particles.size())
        return -1;
    Dictionary p = particles[index], fields = p.get("_vm_fields", Dictionary()),
               field = fields.get("ID", Dictionary());
    Array actual = field.get("int32", Array());
    return actual.size() == 2 && int64_t(actual[0]) == id && int64_t(actual[1]) == tag ? index : -1;
}
} // namespace
double RichChannel::sample(double time) const {
    if (keys.empty())
        return default_value;
    if (keys.size() == 1)
        return keys[0].value;
    double first = keys.front().time, last = keys.back().time, offset = 0;
    String extrap = time < first ? pre : post;
    if ((time < first || time > last) && extrap == "Linear") {
        auto a = time < first ? keys[0] : keys[keys.size() - 2], b = time < first ? keys[1] : keys.back();
        float duration = f32(double(b.time) - a.time);
        if (std::abs(duration) < 1e-8f)
            return time < first ? a.value : b.value;
        return f32(double(a.value) +
                   f32(f32(double(b.value) - a.value) / duration) * double(f32(time - a.time)));
    }
    if ((time < first || time > last) && extrap != "Constant" && extrap != "Linear" && last > first) {
        double initial = time, cycles;
        if (time > last) {
            cycles = std::floor((last - time) / (last - first));
            time += (last - first) * cycles;
        } else {
            cycles = std::floor((time - first) / (last - first));
            time -= (last - first) * cycles;
        }
        if (time == last && initial < first)
            time = first;
        if (time == first && initial > last)
            time = last;
        cycles = std::abs(cycles);
        if (extrap == "CycleWithOffset")
            offset = (double(keys.front().value) - keys.back().value) * cycles * (initial < first ? 1. : -1.);
        if (extrap == "Oscillate" && std::fmod(cycles, 2.) != 0)
            time = first + last - time;
    }
    if (time <= first)
        return keys.front().value + offset;
    for (size_t n = 1; n < keys.size(); n++) {
        const auto &right = keys[n], &left = keys[n - 1];
        if (time > right.time)
            continue;
        float duration = f32(double(right.time) - left.time);
        if (duration <= 0)
            return left.value + offset;
        float alpha = f32(f32(time - left.time) / duration);
        if (left.interpolation == 1)
            return (time < right.time ? left.value : right.value) + offset;
        if (left.interpolation == 2) {
            float p1 = f32(double(left.value) +
                           f32(f32(double(left.leave) * duration) * double(f32(1. / 3.)))),
                  p2 = f32(double(right.value) -
                           f32(f32(double(right.arrive) * duration) * double(f32(1. / 3.))));
            return lerp32(lerp32(lerp32(left.value, p1, alpha), lerp32(p1, p2, alpha), alpha),
                          lerp32(lerp32(p1, p2, alpha), lerp32(p2, right.value, alpha), alpha), alpha) +
                   offset;
        }
        return lerp32(left.value, right.value, alpha) + offset;
    }
    return keys.back().value + offset;
}
int ExternalBinding::reader_index(const Dictionary &data, int id, int tag) {
    return index_for_id(data, id, tag);
}

bool ExternalBinding::bind(const Dictionary &info, const Dictionary &signature, const Dictionary &parameters,
                           TypedArray<String> &errors) {
    function = signature.get("name", "");
    String type = info.get("type", "");
    width = iv(signature, "num_outputs");
    int inputs = iv(signature, "num_inputs");
    auto fail = [&](const String &message) {
        errors.append(message);
        return false;
    };
    if (type == "NiagaraDataInterfaceDebugDraw" && function == "DrawBox" && inputs == 16 && width == 0) {
        if (info.get("debug_draw_enabled", Variant()).get_type() != Variant::BOOL) return fail("Missing native debug draw policy.");
        kind = DebugBox;
        metadata["kind"] = "debug_box";
        metadata["enabled"] = info["debug_draw_enabled"];
        return true;
    }
    if (type == "NiagaraDataInterfaceEmitterProperties" && function == "SetFixedBounds" && inputs == 9 && width == 0) {
        if (!info.has("bounds_emitter")) return fail("Missing source fixed bounds emitter binding.");
        kind = Bounds;
        metadata["kind"] = "fixed_bounds";
        metadata["emitter"] = info["bounds_emitter"];
        return true;
    }
    if (width < 1 || width > 16 || inputs < 0 || inputs > 16)
        return fail("Unsupported external output width.");
    if (type == "NiagaraDataInterfaceUObjectPropertyReader") {
        Dictionary reader = info.get("object_reader", Dictionary());
        String key = info.get("resolved_parameter", "");
        if (reader.get("SourceMode", Variant()) != Variant("Binding") ||
            reader.get("SourceActor", Variant()) != Variant("None") ||
            reader.get("binding", Variant()) != Variant("None") ||
            (parameters.has(key) && parameters[key].get_type() != Variant::NIL))
            return fail("Object property reader requires its bound scene object: " + key);
        if (inputs != 1 || !((function == "GetComponentTransform" && width == 11) ||
            (function == "GetColorProperty" && width == 5) ||
            ((function == "GetFloatProperty" || function == "GetBoolProperty") && width == 2)))
            return fail("Unsupported object property reader signature: " + function);
        kind = ObjectReader;
        metadata["kind"] = "unbound_object_reader";
        return true;
    }
    if (type == "NiagaraDataInterfaceGrid3DCollection" || type == "NiagaraDataInterfaceRenderTargetVolume") {
        String error = bind_volume(info, signature);
        return error.is_empty() || fail(error);
    }
    if (type == "FastPathLibrary" && function == "FastMatrixToQuaternion") {
        kind = MatrixQuaternion;
        metadata["kind"] = "matrix_to_quaternion";
        return (inputs == 16 && width == 4) || fail("FastMatrixToQuaternion requires a 4x4 matrix.");
    }
    if (type == "NiagaraDataInterfaceCollisionQuery") {
        kind = Collision;
        bool async = function == "PerformCollisionQueryAsyncCPU";
        if ((!async && function != "PerformCollisionQuerySyncCPU") || inputs != (async ? 11 : 10) ||
            width != (async ? 12 : 11))
            return fail("Unsupported native CPU collision signature.");
        metadata["async"] = async;
        metadata["owner"] = signature.get("owner_name", "");
        int base = async ? 1 : 0;
        integer_outputs[0] = true;
        integer_outputs[base] = integer_outputs[base + 1] = integer_outputs[base + 10] = true;
        Array types;
        for (int n = 0; n < width; n++)
            types.append(integer_outputs[n] ? "int32" : "float32");
        metadata["output_types"] = types;
        return true;
    }
    std::vector<String> user_names;
    for (const char *key :
         {"name", "compile_name", "registered_parameter_map_read", "registered_parameter_map_write"}) {
        String name = info.get(key, "");
        if (name.begins_with("User.") &&
            std::find(user_names.begin(), user_names.end(), name) == user_names.end())
            user_names.push_back(name);
    }
    String owner = signature.get("owner_name", "");
    if (owner.begins_with("User.") &&
        std::find(user_names.begin(), user_names.end(), owner) == user_names.end())
        user_names.push_back(owner);
    if (type == "NiagaraDataInterfaceSpriteRendererInfo") {
        if (!user_names.empty())
            return fail("Live User sprite renderer interface overrides are not integrated.");
        if (info.get("sprite_renderer", Variant()).get_type() != Variant::DICTIONARY)
            return fail("Missing exported sprite renderer properties. Re-export the Niagara system.");
        Dictionary sprite = info["sprite_renderer"];
        bool subuv = function == "GetSubUVDetails";
        if (inputs != 0 || width != (subuv ? 3 : 1))
            return fail("Sprite renderer VM signature mismatch.");
        if (subuv) {
            Variant blend = sprite.get("sub_image_blend", Variant());
            Variant x = sprite.get("sub_image_x", Variant()), y = sprite.get("sub_image_y", Variant());
            if (blend.get_type() != Variant::BOOL || !numeric(x) || !numeric(y) ||
                !std::isfinite(float(double(x))) || !std::isfinite(float(double(y))))
                return fail("Invalid sprite renderer SubUV properties.");
            renderer_values = {bool(blend) ? UINT32_MAX : 0u, bits(float(double(x))), bits(float(double(y)))};
        } else if (function == "IsValid") {
            Variant valid = sprite.get("valid", Variant());
            if (valid.get_type() != Variant::BOOL)
                return fail("Missing sprite renderer validity.");
            renderer_values[0] = bool(valid) ? UINT32_MAX : 0u;
        } else {
            const char *key = function == "GetSourceMode" ? "source_mode" : function == "GetAlignment" ?
                              "alignment" : function == "GetFacingMode" ? "facing_mode" : nullptr;
            if (!key || !integer_value(sprite.get(key, Variant())))
                return fail("Unknown sprite renderer function or missing exported enumeration: " + function);
            renderer_values[0] = uint32_t(int64_t(sprite[key]));
        }
        kind = SpriteRenderer;
        integer_outputs[0] = true;
        return true;
    }
    if (type == "NiagaraDataInterfaceVectorField") {
        kind = Field;
        if (!user_names.empty())
            return fail("Live User vector field overrides are not integrated.");
        int expected_in = function == "SampleField" || function == "LoadField" ? 3 : 0,
            expected_out = function == "FieldBounds" ? 6 : 3;
        if ((function != "SampleField" && function != "LoadField" && function != "FieldDimensions" &&
             function != "FieldTilingAxes" && function != "FieldBounds") ||
            inputs != expected_in || width != expected_out)
            return fail("Unsupported vector field VM signature.");
        Dictionary data = info.get("vector_field", Dictionary());
        if (data.get("producer", "") !=
                Variant("UVectorFieldStatic CPU lattice; original CPU access and DI tiling flags") ||
            data.get("static", Variant()) != Variant(true))
            return fail("Native vector field metadata is unavailable.");
        if (!triple(data.get("size", Variant()), true) ||
            !triple(data.get("tiling", Variant()), false, true) ||
            !triple(data.get("bounds_min", Variant())) || !triple(data.get("bounds_max", Variant())) ||
            data.get("allow_cpu_access", Variant()).get_type() != Variant::BOOL ||
            data.get("has_cpu_data", Variant()).get_type() != Variant::BOOL)
            return fail("Invalid vector field layout/access metadata.");
        Array dimensions = data["size"], tiles = data["tiling"], minimums = data["bounds_min"],
              maximums = data["bounds_max"];
        int64_t voxels = 1;
        for (int n = 0; n < 3; n++) {
            if (double(dimensions[n]) > 4194304)
                return fail("Vector field dimensions exceed supported lattice.");
            size[n] = int(int64_t(dimensions[n]));
            voxels *= size[n];
            if (voxels > 4194304)
                return fail("Vector field exceeds voxel limit.");
            tiling[n] = bool(tiles[n]);
            bounds_min[n] = minimums[n];
            bounds_max[n] = maximums[n];
            if (bounds_max[n] - bounds_min[n] <= 1e-8)
                return fail("Invalid vector field bounds extent.");
        }
        cpu_access = bool(data["allow_cpu_access"]) && bool(data["has_cpu_data"]);
        if (cpu_access) {
            if (data.get("vectors_xyz", Variant()).get_type() != Variant::ARRAY)
                return fail("Missing vector field CPU lattice.");
            Array values = data["vectors_xyz"];
            if (values.size() != voxels * 3)
                return fail("Vector field lattice length differs from dimensions.");
            for (int n = 0; n < values.size(); n++) {
                if (!numeric(values[n]) || std::abs(double(values[n])) > 65504.)
                    return fail("Invalid vector field value.");
                lut.push_back(f32(values[n]));
            }
        }
        metadata["kind"] = "vector_field";
        metadata["function"] = function;
        return true;
    }
    if (type == "NiagaraDataInterfaceParticleRead") {
        kind = Reader;
        if (!user_names.empty())
            return fail("Live User particle-reader overrides are not integrated.");
        Dictionary data = info.get("particle_reader", Dictionary());
        if (data.get("producer", "") !=
                Variant("FNiagaraDataInterfaceEmitterBinding.ResolveHandle rules; compiled source dataset") ||
            data.get("resolved", Variant()).get_type() != Variant::BOOL ||
            !integer_value(info.get("user_pointer_index", Variant())))
            return fail("Particle reader source/provenance metadata is unavailable.");
        metadata["kind"] = "particle_reader";
        metadata["function"] = function;
        metadata["source_emitter"] = data.get("source_emitter", "");
        metadata["resolved"] = data["resolved"];
        metadata["local_space"] = data.get("local_space", false);
        metadata["pointer_index"] = info["user_pointer_index"];
        bool resolved = data["resolved"];
        int expected_in = -1, expected_out = -1;
        String attribute_type;
        if (function == "GetLocalSpace" || function == "Get Num Spawned Particles" ||
            function == "Get Num Particles") {
            expected_in = 1;
            expected_out = 1;
        } else if (function == "Get ID At Spawn Index") {
            expected_in = 2;
            expected_out = 3;
        } else if (function == "Get Particle Index") {
            expected_in = 3;
            expected_out = 1;
        } else if (function == "Get Particle Index From ID Table") {
            expected_in = 2;
            expected_out = 2;
        } else if (function.begins_with("Get ") &&
                   (function.ends_with(" By Index") || function.ends_with(" By ID"))) {
            bool by_id = function.ends_with(" By ID");
            String name = function.substr(4, function.length() - 4 - (by_id ? 6 : 9));
            int count = 0;
            String component = "float32";
            if (name == "Int") {
                attribute_type = "NiagaraInt32";
                component = "int32";
                count = 1;
            } else if (name == "Bool") {
                attribute_type = "NiagaraBool";
                component = "int32";
                count = 1;
            } else if (name == "Float") {
                attribute_type = "NiagaraFloat";
                count = 1;
            } else if (name == "Vector 2D") {
                attribute_type = "Vector2f";
                count = 2;
            } else if (name == "Vector") {
                attribute_type = "Vector3f";
                count = 3;
            } else if (name == "Vector 4") {
                attribute_type = "Vector4f";
                count = 4;
            } else if (name == "Position") {
                attribute_type = "NiagaraPosition";
                count = 3;
            } else if (name == "Color") {
                attribute_type = "LinearColor";
                count = 4;
            } else if (name == "Quaternion") {
                attribute_type = "Quat4f";
                count = 4;
            } else if (name == "ID") {
                attribute_type = "NiagaraID";
                component = "int32";
                count = 2;
            } else
                return fail("Unsupported particle reader attribute type.");
            Dictionary specifiers = signature.get("specifiers", Dictionary());
            String attribute = specifiers.get("Attribute", "");
            if (attribute.is_empty())
                return fail("Missing particle reader Attribute specifier.");
            metadata["attribute"] = attribute;
            metadata["by_id"] = by_id;
            metadata["type"] = name;
            metadata["component_type"] = component;
            metadata["valid_attribute"] = false;
            expected_in = by_id ? 3 : 2;
            expected_out = count + 1;
            integer_outputs[0] = true;
            for (int n = 1; n <= count; n++)
                integer_outputs[n] = component == "int32";
            if (resolved) {
                Dictionary layout = data.get("layout", Dictionary());
                Array variables = layout.get("variables", Array());
                for (int n = 0; n < variables.size(); n++) {
                    Dictionary row = variables[n];
                    if (row.get("name", "") == Variant(attribute))
                        metadata["valid_attribute"] =
                            row.get("type", "") == Variant(attribute_type) ||
                            (name == "Vector" && row.get("type", "") == Variant("NiagaraPosition"));
                }
            }
        } else
            return fail("Unsupported native particle reader function: " + function);
        if (attribute_type.is_empty())
            for (int n = 0; n < width; n++)
                integer_outputs[n] = true;
        if (inputs != expected_in || width != expected_out)
            return fail("Particle reader VM signature mismatch.");
        if (resolved && (String(data.get("source_emitter", "")).is_empty() ||
                         data.get("local_space", Variant()).get_type() != Variant::BOOL ||
                         data.get("simulation_target", "") != Variant("CPUSim")))
            return fail("Particle reader requires a resolved CPU source.");
        Array types;
        for (int n = 0; n < width; n++)
            types.append(integer_outputs[n] ? "int32" : "float32");
        metadata["output_types"] = types;
        return true;
    }
    if (!bool(info.get("is_curve", false)))
        return fail("Unsupported native data interface function: " + type + "." + function);
    kind = Curve;
    int expected = function == "SampleCurve"           ? 1
                   : function == "SampleVector2DCurve" ? 2
                   : function == "SampleVectorCurve"   ? 3
                   : function == "SampleColorCurve"    ? 4
                                                       : 0;
    if (!expected || width != expected || inputs != 1 || !bool(info.get("is_curve", false)) ||
        iv(info, "elements_per_sample") != width)
        return fail("Unsupported native external curve signature.");
    if (user_names.empty()) {
        if (!bool(info.get("use_lut", false)) ||
            info.get("shader_lut", Variant()).get_type() != Variant::ARRAY)
            return fail("Native embedded curve requires its exported LUT.");
        Array values = info["shader_lut"];
        if (values.size() < width || values.size() % width)
            return fail("Invalid native curve LUT.");
        for (int n = 0; n < values.size(); n++) {
            if (!numeric(values[n]))
                return fail("Nonfinite native LUT value.");
            lut.push_back(f32(values[n]));
        }
        minimum = double(info.get("time_min", 0.));
        inverse = double(info.get("inv_time_range", 0.));
        metadata = info.duplicate(true);
    } else {
        if (user_names.size() != 1)
            return fail("Ambiguous User curve binding.");
        String parameter = user_names[0];
        if (parameters.get(parameter, Variant()).get_type() != Variant::DICTIONARY)
            return fail("Native curve requires a typed runtime instance binding: " + parameter);
        Dictionary value = parameters[parameter];
        String expected_kind = width == 1   ? "curve_di"
                               : width == 2 ? "vector2_curve_di"
                               : width == 3 ? "vector3_curve_di"
                                            : "color_curve_di";
        if (value.get("kind", "") != Variant(expected_kind) ||
            value.get("errors", Array()).get_type() != Variant::ARRAY ||
            !Array(value.get("errors", Array())).is_empty() ||
            value.get("uses_lut", Variant()).get_type() != Variant::BOOL ||
            value.get("channels", Variant()).get_type() != Variant::ARRAY ||
            Array(value["channels"]).size() != width)
            return fail("Invalid typed User curve: " + parameter);
        for (const char *key : {"min_time", "max_time", "inv_range"})
            if (!numeric(value.get(key, Variant())))
                return fail("Invalid User curve time range.");
        minimum = value["min_time"];
        inverse = value["inv_range"];
        if (bool(value["uses_lut"])) {
            if (value.get("lut", Variant()).get_type() != Variant::PACKED_FLOAT32_ARRAY)
                return fail("User curve requires a packed float LUT.");
            PackedFloat32Array values = value["lut"];
            if (values.size() < width || values.size() % width)
                return fail("Invalid User curve LUT.");
            for (int n = 0; n < values.size(); n++) {
                if (!std::isfinite(values[n]))
                    return fail("Nonfinite User curve sample.");
                lut.push_back(values[n]);
            }
        } else {
            Array channels = value["channels"];
            for (int n = 0; n < channels.size(); n++) {
                if (channels[n].get_type() != Variant::DICTIONARY)
                    return fail("Invalid rich curve channel.");
                Dictionary channel = channels[n];
                if (channel.get("keys", Variant()).get_type() != Variant::ARRAY)
                    return fail("Missing rich curve keys.");
                RichChannel bound;
                bound.default_value = f32(channel.get("default", 0.));
                bound.pre = channel.get("pre", "Constant");
                bound.post = channel.get("post", "Constant");
                Array keys = channel["keys"];
                for (int k = 0; k < keys.size(); k++) {
                    if (keys[k].get_type() != Variant::DICTIONARY)
                        return fail("Invalid rich curve key.");
                    Dictionary key = keys[k];
                    for (const char *field : {"time", "value", "arrive", "leave"})
                        if (!numeric(key.get(field, 0.)))
                            return fail("Nonfinite rich curve key.");
                    RichKey point;
                    point.time = f32(key.get("time", 0.));
                    point.value = f32(key.get("value", 0.));
                    point.arrive = f32(key.get("arrive", 0.));
                    point.leave = f32(key.get("leave", 0.));
                    String mode = String(key.get("interp", "Linear")).to_lower();
                    if (mode != "linear" && mode != "constant" && mode != "cubic")
                        return fail("Unsupported rich curve interpolation.");
                    point.interpolation = mode == "constant" ? 1 : mode == "cubic" ? 2 : 0;
                    if (!bound.keys.empty() && point.time < bound.keys.back().time)
                        return fail("Unordered rich curve keys.");
                    bound.keys.push_back(point);
                }
                rich.push_back(std::move(bound));
            }
        }
        metadata = info.duplicate(true);
        metadata["runtime_curve"] = value.duplicate(true);
        metadata["runtime_parameter"] = parameter;
    }
    return std::isfinite(minimum) && std::isfinite(inverse) ? true : fail("Nonfinite curve range.");
}

bool ExternalBinding::collide(const ExternalInputs &inputs, NBNativeCollisionWorld &world,
                              const String &scope, ExternalOutputs &outputs) const {
    bool async = metadata.get("async", false);
    int offset = async ? 1 : 0, start = offset + 1;
    Vector3 a(number(inputs[start]), number(inputs[start + 1]), number(inputs[start + 2]));
    Vector3 b(number(inputs[start + 3]), number(inputs[start + 4]), number(inputs[start + 5]));
    int channel = signed_word(inputs[start + 6]);
    bool skip = inputs[start + 7] != 0, complex = inputs[start + 8] != 0;
    NBNativeCollisionWorld::Hit hit;
    int next = 0;
    bool ok = async
                  ? world.query_async_native(scope + String("|") + String(metadata.get("owner", "")),
                                             signed_word(inputs[1]), a, b, channel, skip, complex, next, hit)
                  : world.trace(a, b, channel, skip, complex, hit);
    if (!ok)
        return false;
    if (async)
        outputs[0] = uint32_t(next);
    outputs[offset] = hit.valid ? 0xffffffffu : 0;
    outputs[offset + 1] = hit.inside ? 0xffffffffu : 0;
    for (int n = 0; n < 3; n++) {
        outputs[offset + 2 + n] = bits(float(hit.position[n]));
        outputs[offset + 5 + n] = bits(float(hit.normal[n]));
    }
    outputs[offset + 8] = bits(hit.friction);
    outputs[offset + 9] = bits(hit.restitution);
    outputs[offset + 10] = uint32_t(hit.surface);
    return true;
}
void ExternalBinding::sample(const ExternalInputs &inputs, const Dictionary &reader_data,
                             ExternalOutputs &outputs) const {
    if (kind == ObjectReader) {
        outputs.fill(0);
        if (function == "GetColorProperty") outputs[4] = bits(1.f);
        if (function == "GetComponentTransform") {
            outputs[7] = bits(1.f);
            outputs[8] = outputs[9] = outputs[10] = bits(1.f);
        }
        return;
    }
    if (kind == MatrixQuaternion) {
        auto m = [&inputs](int row, int column) { return number(inputs[4 * row + column]); };
        const float trace = m(0, 0) + m(1, 1) + m(2, 2);
        if (trace > 0.f) {
            const float inv = 1.f / std::sqrt(trace + 1.f);
            for (int i = 0; i < 3; ++i) {
                int j = (i + 1) % 3, k = (i + 2) % 3;
                outputs[i] = bits((m(j,k) - m(k,j)) * (.5f * inv));
            }
            outputs[3] = bits(.5f * (1.f / inv));
        } else {
            int i = m(1,1) > m(0,0) ? 1 : 0;
            if (m(2,2) > m(i,i)) i = 2;
            const int j = (i+1)%3, k = (j+1)%3;
            const float inv = 1.f / std::sqrt(m(i,i) - m(j,j) - m(k,k) + 1.f), s = .5f * inv;
            outputs[i] = bits(.5f * (1.f / inv));
            outputs[j] = bits((m(i,j) + m(j,i)) * s);
            outputs[k] = bits((m(i,k) + m(k,i)) * s);
            outputs[3] = bits((m(j,k) - m(k,j)) * s);
        }
        return;
    }
    if (kind == Collision)
        return; // Collision needs the attached shared world, never a synthetic sample.
    if (kind == SpriteRenderer) {
        std::copy_n(renderer_values.begin(), width, outputs.begin());
        return;
    }
    if (kind == Curve) {
        double time = number(inputs[0]);
        if (!rich.empty()) {
            for (int n = 0; n < width; n++)
                outputs[n] = bits(float(rich[n].sample(time)));
            return;
        }
        int last = int(lut.size()) / width - 1;
        float normalized = f32(f32(time - minimum) * double(inverse)),
              position = std::clamp(f32(double(normalized) * last), 0.f, float(last));
        if (!std::isfinite(position)) {
            for (int n = 0; n < width; n++)
                outputs[n] = bits(NAN);
            return;
        }
        int low = int(position), high = std::min(low + 1, last);
        float alpha = f32(double(position) - low);
        for (int n = 0; n < width; n++)
            outputs[n] = bits(lerp32(lut[low * width + n], lut[high * width + n], alpha));
        return;
    }
    if (kind == Field) {
        if (function == "FieldDimensions" || function == "FieldTilingAxes") {
            for (int n = 0; n < 3; n++)
                outputs[n] = bits(float(function == "FieldDimensions" ? size[n] : tiling[n]));
            return;
        }
        if (function == "FieldBounds") {
            for (int n = 0; n < 3; n++) {
                outputs[n] = bits(float(bounds_min[n]));
                outputs[n + 3] = bits(float(bounds_max[n]));
            }
            return;
        }
        if (!cpu_access)
            return;
        auto at = [&](int x, int y, int z, int c) {
            return double(lut[(x + size[0] * (y + size[1] * z)) * 3 + c]);
        };
        if (function == "LoadField") {
            int x = signed_word(inputs[0]), y = signed_word(inputs[1]), z = signed_word(inputs[2]);
            if (x < 0 || y < 0 || z < 0 || x >= size[0] || y >= size[1] || z >= size[2])
                return;
            for (int n = 0; n < 3; n++)
                outputs[n] = bits(float(at(x, y, z, n)));
            return;
        }
        std::array<int, 3> a{}, b{};
        std::array<double, 3> weight{};
        for (int n = 0; n < 3; n++) {
            double p = number(inputs[n]);
            if (!std::isfinite(p))
                return;
            double cell = ((p - bounds_min[n]) * (1. / (bounds_max[n] - bounds_min[n]))) * size[n] - .5,
                   base = std::floor(cell);
            weight[n] = cell - base;
            auto index = [&](double value) {
                if (tiling[n])
                    value -= std::floor(value / size[n]) * size[n];
                return int(std::clamp(value, 0., double(size[n] - 1)));
            };
            a[n] = index(base);
            b[n] = index(base + 1.);
        }
        auto lerp = [](double x, double y, double t) { return x + t * (y - x); };
        for (int n = 0; n < 3; n++) {
            double v00 = lerp(at(a[0], a[1], a[2], n), at(b[0], a[1], a[2], n), weight[0]),
                   v10 = lerp(at(a[0], b[1], a[2], n), at(b[0], b[1], a[2], n), weight[0]),
                   v01 = lerp(at(a[0], a[1], b[2], n), at(b[0], a[1], b[2], n), weight[0]),
                   v11 = lerp(at(a[0], b[1], b[2], n), at(b[0], b[1], b[2], n), weight[0]);
            outputs[n] = bits(float(lerp(lerp(v00, v10, weight[1]), lerp(v01, v11, weight[1]), weight[2])));
        }
        return;
    }
    bool resolved = bool(metadata.get("resolved", false));
    Array particles = resolved ? Array(reader_data.get("particles", Array())) : Array();
    Dictionary ids = resolved ? Dictionary(reader_data.get("ids", Dictionary())) : Dictionary();
    if (function == "GetLocalSpace") {
        outputs[0] = truth(resolved && bool(metadata.get("local_space", false)));
        return;
    }
    if (function == "Get Num Particles") {
        outputs[0] = uint32_t(particles.size());
        return;
    }
    if (function == "Get Num Spawned Particles") {
        outputs[0] = resolved ? uint32_t(iv(reader_data, "spawned_count")) : 0;
        return;
    }
    int index = signed_word(inputs[1]);
    if (function == "Get ID At Spawn Index") {
        Array spawned = ids.get("spawned", Array());
        if (index >= 0 && index < spawned.size()) {
            outputs[0] = UINT32_MAX;
            outputs[1] = uint32_t(int64_t(spawned[index]));
            outputs[2] = uint32_t(iv(ids, "acquire_tag"));
        }
        return;
    }
    if (function == "Get Particle Index") {
        outputs[0] = uint32_t(resolved ? index_for_id(reader_data, index, signed_word(inputs[2])) : -1);
        return;
    }
    if (function == "Get Particle Index From ID Table") {
        Array table = ids.get("table", Array());
        int value = index >= 0 && index < table.size() ? int(int64_t(table[index])) : -1;
        outputs[0] = truth(value != -1);
        outputs[1] = uint32_t(value);
        return;
    }
    String type = metadata.get("type", "");
    for (int n = 1; n < width; n++)
        outputs[n] = integer_outputs[n] ? 0 : bits(0.f);
    if (type == "Color")
        for (int n = 1; n < width; n++)
            outputs[n] = bits(1.f);
    else if (type == "Quaternion")
        outputs[4] = bits(1.f);
    else if (type == "ID")
        outputs[1] = outputs[2] = UINT32_MAX;
    if (bool(metadata.get("by_id", false)) && resolved)
        index = index_for_id(reader_data, index, signed_word(inputs[2]));
    if (!bool(metadata.get("valid_attribute", false)) || !resolved || index < 0 || index >= particles.size())
        return;
    Dictionary particle = particles[index], fields = particle.get("_vm_fields", Dictionary()),
               raw = fields.get(metadata.get("attribute", ""), Dictionary());
    Array values = raw.get(metadata.get("component_type", "float32"), Array());
    if (values.size() != width - 1)
        return;
    outputs[0] = UINT32_MAX;
    for (int n = 1; n < width; n++)
        outputs[n] = integer_outputs[n] ? uint32_t(int64_t(values[n - 1])) : bits(f32(values[n - 1]));
}
} // namespace nbvm
