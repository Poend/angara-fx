#include "lighting_volume.h"
#include "raster_pass.h"
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/rd_shader_source.hpp>
#include <godot_cpp/classes/rd_shader_spirv.hpp>
#include <godot_cpp/classes/rd_sampler_state.hpp>
#include <godot_cpp/classes/rd_texture_format.hpp>
#include <godot_cpp/classes/rd_texture_view.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <algorithm>
#include <cmath>
namespace godot {
namespace {
bool number(const Variant &v) {
    return (v.get_type() == Variant::INT || v.get_type() == Variant::FLOAT) && std::isfinite(double(v));
}
double radians(double v) {
    return v * (3.14159265358979323846264338327950288 / 180.);
}
} // namespace
void NBNativeLightingVolume::_bind_methods() {
    ClassDB::bind_static_method("NBNativeLightingVolume", D_METHOD("configuration", "context"),
                                &NBNativeLightingVolume::configuration);
    ClassDB::bind_static_method("NBNativeLightingVolume", D_METHOD("_ue", "value"),
                                &NBNativeLightingVolume::ue);
    ClassDB::bind_static_method("NBNativeLightingVolume",
                                D_METHOD("cascade_bounds", "settings", "camera", "projection"),
                                &NBNativeLightingVolume::cascade_bounds);
    ClassDB::bind_static_method("NBNativeLightingVolume", D_METHOD("light_bytes", "lights"),
                                &NBNativeLightingVolume::light_bytes);
    ClassDB::bind_method(D_METHOD("initialize", "rd"), &NBNativeLightingVolume::initialize);
    ClassDB::bind_method(D_METHOD("build", "rd", "configuration", "bounds"), &NBNativeLightingVolume::build);
    ClassDB::bind_method(D_METHOD("dispose", "rd"), &NBNativeLightingVolume::dispose);
}
Dictionary NBNativeLightingVolume::configuration(const Ref<Resource> &context) {
    Dictionary result, settings;
    TypedArray<String> errors;
    TypedArray<Dictionary> lights;
    result["errors"] = errors;
    result["settings"] = settings;
    result["lights"] = lights;
    if (context.is_null()) {
        errors.append("Native lit surfaces require an exported scene lighting context.");
        return result;
    }
    const char *suffixes[] = {
        "",          ".Dim",    ".InnerDistance", ".OuterDistance",      ".Blur",
        ".Temporal", ".MinFOV", ".FOVSnapFactor", ".AdaptToPerspective", ".PositionOffsetRadius"};
    const char *fields[] = {"enabled",  "dimension", "inner_cm", "outer_cm", "blur",
                            "temporal", "min_fov",   "fov_snap", "adapt",    "random_radius"};
    Dictionary cvars = context->get("source_cvars");
    for (int n = 0; n < 10; ++n) {
        String name = String("r.TranslucencyLightingVolume") + suffixes[n];
        Variant raw = cvars.get(name, Variant());
        if (raw.get_type() == Variant::STRING) {
            String text = raw;
            if (text == "true" || text == "false")
                raw = text == "true" ? "1" : "0";
        }
        if (raw.get_type() != Variant::STRING || !String(raw).is_valid_float() ||
            !std::isfinite(String(raw).to_float())) {
            errors.append("Missing/nonfinite native lighting setting: " + name);
            continue;
        }
        settings[fields[n]] = String(raw).to_float();
    }
    if (!errors.is_empty())
        return result;
    double dimension = settings["dimension"];
    if (dimension != std::floor(dimension) || dimension < 4. || dimension > 256.)
        errors.append("Native TLV dimension must be an integer in [4,256]; larger GPU allocations are not "
                      "implemented.");
    if (double(settings["inner_cm"]) <= 0. || double(settings["outer_cm"]) <= double(settings["inner_cm"]) ||
        double(settings["fov_snap"]) <= 0. || double(settings["min_fov"]) <= 0. ||
        double(settings["min_fov"]) >= 90.)
        errors.append("Invalid native TLV bounds/FOV settings.");
    if (double(settings["temporal"]) != 0.)
        errors.append("Native TLV temporal history is not implemented.");
    if (double(settings["random_radius"]) != 0.)
        errors.append("Native TLV per-pixel blue-noise position offset is not implemented.");
    Array components = context->get("additional_scene_components");
    for (int n = 0; n < components.size(); ++n) {
        String name = components[n];
        if (name == "SkyLightComponent" || name == "SkyAtmosphereComponent")
            errors.append("Native lighting requires an additional injection stage for " + name);
    }
    Array source_lights = context->get("lights");
    for (int n = 0; n < source_lights.size(); ++n) {
        Dictionary light = source_lights[n];
        Variant participation = light.get("affects_translucency", Variant());
        if (participation.get_type() != Variant::BOOL) {
            errors.append("Native TLV requires an explicit translucency participation flag.");
            continue;
        }
        if (!bool(participation))
            continue;
        String kind = UtilityFunctions::str(light.get("class", Variant()));
        if (kind != "DirectionalLightComponent" && kind != "PointLightComponent" &&
            kind != "SpotLightComponent") {
            errors.append("Native TLV light shape is not implemented: " + kind);
            continue;
        }
        for (const char *flag : {"cast_shadows", "light_function", "ies_profile"}) {
            Variant value = light.get(flag, Variant());
            if (value.get_type() != Variant::BOOL || bool(value))
                errors.append("Native TLV requires a separate implementation for " + String(flag));
        }
        bool valid = true;
        for (const char *name : {"position_ue_cm", "direction_ue", "renderer_color_linear"}) {
            Variant raw = light.get(name, Variant());
            if (raw.get_type() != Variant::ARRAY || Array(raw).size() != 3) {
                valid = false;
                continue;
            }
            Array values = raw;
            for (int slot = 0; slot < values.size(); ++slot)
                if (!number(values[slot]))
                    valid = false;
        }
        if (!valid) {
            errors.append("Native TLV requires finite light vectors.");
            continue;
        }
        Array direction = light["direction_ue"], colors = light["renderer_color_linear"];
        if (!Vector3(direction[0], direction[1], direction[2]).is_normalized())
            errors.append("Native TLV light direction must be normalized.");
        for (int slot = 0; slot < colors.size(); ++slot)
            if (double(colors[slot]) < 0.)
                errors.append("Native TLV light color must be nonnegative.");
        if (kind != "DirectionalLightComponent") {
            for (const char *field : {"attenuation_radius_cm", "falloff_exponent"}) {
                Variant value = light.get(field, Variant());
                if (!number(value) || double(value) < 0. ||
                    (String(field) == "attenuation_radius_cm" && double(value) == 0.))
                    errors.append("Invalid native TLV local-light " + String(field));
            }
            if (light.get("inverse_squared", Variant()).get_type() != Variant::BOOL)
                errors.append("Missing native TLV light attenuation mode.");
        }
        if (kind == "SpotLightComponent")
            for (const char *field : {"inner_cone_degrees", "outer_cone_degrees"})
                if (!number(light.get(field, Variant())))
                    errors.append("Invalid native TLV cone angle.");
        lights.append(light.duplicate(true));
    }
    return result;
}
Vector3 NBNativeLightingVolume::ue(const Vector3 &v) {
    return Vector3(-v.z, v.x, v.y);
}
TypedArray<AABB> NBNativeLightingVolume::cascade_bounds(const Dictionary &settings, const Transform3D &camera,
                                                        const Projection &projection) {
    TypedArray<AABB> result;
    Vector3 origin = ue(camera.origin) * 100.f, forward = ue(-camera.basis.get_column(2)),
            right = ue(camera.basis.get_column(0)), up = ue(camera.basis.get_column(1));
    double fov = 3.14159265358979323846264338327950288 / 4., aspect = 1.;
    if (projection.columns[3].w == 0. && double(settings["adapt"]) != 0.) {
        fov =
            std::max(std::atan(1. / std::abs(double(projection.columns[0].x))), radians(settings["min_fov"]));
        double snap = radians(settings["fov_snap"]);
        fov += snap - std::fmod(fov, snap);
        aspect = std::abs(double(projection.columns[1].y) / double(projection.columns[0].x));
    }
    double outer = settings["outer_cm"], maximum = std::sqrt(std::max(1., 3. * outer * outer));
    for (int cascade = 0; cascade < 2; ++cascade) {
        double first = cascade == 0 ? 0. : double(settings["inner_cm"]),
               last = cascade == 0 ? double(settings["inner_cm"]) : outer;
        Vector3 vertices[8], center;
        int index = 0;
        for (double distance : {first, last}) {
            double horizontal = std::min(maximum, distance * std::tan(fov));
            for (double x : {-1., 1.})
                for (double y : {-1., 1.})
                    vertices[index++] = origin + forward * distance + right * (x * horizontal) +
                                        up * (y * horizontal / aspect);
        }
        for (int n = 0; n < 8; ++n)
            center += vertices[n] * (n >= 4 ? 1. / 4.4 : 1. / 44.);
        double radius = 0.;
        for (const Vector3 &vertex : vertices)
            radius = std::max(radius, double(center.distance_to(vertex)));
        double voxel = radius * 2. / double(settings["dimension"]);
        for (int axis = 0; axis < 3; ++axis)
            center[axis] -= std::fmod(double(center[axis]), voxel);
        result.append(AABB(center - Vector3(1, 1, 1) * radius, Vector3(1, 1, 1) * radius * 2.));
    }
    return result;
}
PackedByteArray NBNativeLightingVolume::light_bytes(const Array &lights) {
    PackedFloat32Array data;
    for (int n = 0; n < lights.size(); ++n) {
        Dictionary light = lights[n];
        String kind = light["class"];
        auto vector = [&](const char *name) {
            Array values = light[name];
            for (int axis = 0; axis < values.size(); ++axis)
                data.append(values[axis]);
        };
        vector("position_ue_cm");
        data.append(kind == "DirectionalLightComponent" ? 0
                    : kind == "NiagaraSimpleLight"      ? 3
                    : kind == "SpotLightComponent"      ? 2
                                                        : 1);
        vector("direction_ue");
        data.append(light.get("attenuation_radius_cm", 0));
        vector("renderer_color_linear");
        data.append(bool(light.get("inverse_squared", false)) ? 1.f : 0.f);
        double inner = radians(std::clamp(double(light.get("inner_cone_degrees", 0)), 0., 88.9));
        double outer =
            std::clamp(radians(light.get("outer_cone_degrees", 0)), inner + .001, radians(88.9) + .001);
        data.append(light.get("falloff_exponent", 0));
        data.append(std::cos(outer));
        data.append(1. / (std::cos(inner) - std::cos(outer)));
        data.append(0.f);
    }
    if (data.is_empty())
        data.resize(16);
    return data.to_byte_array();
}
NBNativeLightingVolume::~NBNativeLightingVolume() {
    if (device_id) {
        auto *rd = Object::cast_to<RenderingDevice>(ObjectDB::get_instance(device_id));
        if (rd)
            dispose(rd);
    }
}
TypedArray<String> NBNativeLightingVolume::initialize(RenderingDevice *rd) {
    TypedArray<String> errors;
    if (!pipelines.is_empty())
        return errors;
    if (!rd) {
        errors.append("Native TLV requires a live rendering device.");
        return errors;
    }
    device_id = rd->get_instance_id();
    for (const char *stage : {"inject", "filter"}) {
        String code =
            FileAccess::get_file_as_string(
                String("res://addons/angara_fx/materials/native_lighting_volume_") + stage + ".glsl")
                .trim_prefix("#[compute]\n");
        Ref<RDShaderSource> source;
        source.instantiate();
        source->set_stage_source(RenderingDevice::SHADER_STAGE_COMPUTE, code);
        Ref<RDShaderSPIRV> spirv = rd->shader_compile_spirv_from_source(source);
        String error = spirv.is_null()
                           ? String("No native TLV SPIR-V returned.")
                           : spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_COMPUTE);
        if (!error.is_empty()) {
            errors.append(error);
            continue;
        }
        RID shader = rd->shader_create_from_spirv(spirv), pipeline = rd->compute_pipeline_create(shader);
        owned.append(pipeline);
        owned.append(shader);
        Dictionary value;
        value["shader"] = shader;
        value["pipeline"] = pipeline;
        pipelines[stage] = value;
    }
    Ref<RDSamplerState> sampler;
    sampler.instantiate();
    sampler->set_min_filter(RenderingDevice::SAMPLER_FILTER_LINEAR);
    sampler->set_mag_filter(RenderingDevice::SAMPLER_FILTER_LINEAR);
    sampler->set_repeat_u(RenderingDevice::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE);
    sampler->set_repeat_v(RenderingDevice::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE);
    sampler->set_repeat_w(RenderingDevice::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE);
    linear_sampler = rd->sampler_create(sampler);
    owned.append(linear_sampler);
    if (!errors.is_empty())
        dispose(rd);
    return errors;
}
RID NBNativeLightingVolume::volume_texture(RenderingDevice *rd, int64_t dimension) {
    Ref<RDTextureFormat> format;
    format.instantiate();
    format->set_texture_type(RenderingDevice::TEXTURE_TYPE_3D);
    format->set_format(RenderingDevice::DATA_FORMAT_R16G16B16A16_SFLOAT);
    format->set_width(dimension);
    format->set_height(dimension);
    format->set_depth(dimension);
    format->set_usage_bits(RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT |
                           RenderingDevice::TEXTURE_USAGE_STORAGE_BIT |
                           RenderingDevice::TEXTURE_USAGE_CAN_COPY_FROM_BIT);
    Ref<RDTextureView> view;
    view.instantiate();
    return rd->texture_create(format, view);
}
Dictionary NBNativeLightingVolume::build(RenderingDevice *rd, const Dictionary &config,
                                         const TypedArray<AABB> &bounds) {
    Dictionary result, textures, values;
    Array resources;
    TypedArray<String> errors;
    errors.assign(Array(config["errors"]).duplicate());
    result["errors"] = errors;
    result["textures"] = textures;
    result["values"] = values;
    result["resources"] = resources;
    if (!errors.is_empty())
        return result;
    errors.append_array(initialize(rd));
    if (!errors.is_empty())
        return result;
    Dictionary settings = config["settings"];
    Array lights = config["lights"];
    int64_t dimension = settings["dimension"];
    PackedByteArray bytes = light_bytes(lights);
    RID light_data = rd->storage_buffer_create(bytes.size(), bytes);
    resources.append(light_data);
    for (int cascade = 0; cascade < 2; ++cascade) {
        AABB box = bounds[cascade];
        double voxel = double(box.size.x) / dimension;
        PackedFloat32Array floats;
        for (double v : {double(box.position.x), double(box.position.y), double(box.position.z), voxel})
            floats.append(v);
        PackedInt32Array integers;
        integers.append(dimension);
        integers.append(cascade);
        integers.append(double(settings["enabled"]) != 0. ? lights.size() : 0);
        integers.append(0);
        PackedByteArray parameters = floats.to_byte_array();
        parameters.append_array(integers.to_byte_array());
        RID ubo = rd->uniform_buffer_create(parameters.size(), parameters);
        resources.append(ubo);
        RID ambient = volume_texture(rd, dimension), directional = volume_texture(rd, dimension);
        resources.append(ambient);
        resources.append(directional);
        TypedArray<RDUniform> uniforms;
        uniforms.append(
            NBNativeRasterPass::uniform(RenderingDevice::UNIFORM_TYPE_UNIFORM_BUFFER, 0, Array::make(ubo)));
        uniforms.append(NBNativeRasterPass::uniform(RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER, 1,
                                                    Array::make(light_data)));
        uniforms.append(
            NBNativeRasterPass::uniform(RenderingDevice::UNIFORM_TYPE_IMAGE, 2, Array::make(ambient)));
        uniforms.append(
            NBNativeRasterPass::uniform(RenderingDevice::UNIFORM_TYPE_IMAGE, 3, Array::make(directional)));
        Dictionary inject = pipelines["inject"];
        RID binding = rd->uniform_set_create(uniforms, inject["shader"], 0);
        Dictionary owner;
        owner["uniform_set"] = binding;
        resources.push_front(owner);
        if (!binding.is_valid()) {
            errors.append("Native TLV injection binding failed.");
            return result;
        }
        int64_t commands = rd->compute_list_begin();
        rd->compute_list_bind_compute_pipeline(commands, inject["pipeline"]);
        rd->compute_list_bind_uniform_set(commands, binding, 0);
        rd->compute_list_dispatch(commands, (dimension + 3) / 4, (dimension + 3) / 4, (dimension + 3) / 4);
        rd->compute_list_end();
        if (double(settings["blur"]) != 0.) {
            RID filtered_a = volume_texture(rd, dimension), filtered_d = volume_texture(rd, dimension);
            resources.append(filtered_a);
            resources.append(filtered_d);
            uniforms.clear();
            uniforms.append(NBNativeRasterPass::uniform(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 0,
                                                        Array::make(linear_sampler, ambient)));
            uniforms.append(NBNativeRasterPass::uniform(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 1,
                                                        Array::make(linear_sampler, directional)));
            uniforms.append(
                NBNativeRasterPass::uniform(RenderingDevice::UNIFORM_TYPE_IMAGE, 2, Array::make(filtered_a)));
            uniforms.append(
                NBNativeRasterPass::uniform(RenderingDevice::UNIFORM_TYPE_IMAGE, 3, Array::make(filtered_d)));
            Dictionary filter = pipelines["filter"];
            binding = rd->uniform_set_create(uniforms, filter["shader"], 0);
            owner = Dictionary();
            owner["uniform_set"] = binding;
            resources.push_front(owner);
            if (!binding.is_valid()) {
                errors.append("Native TLV filter binding failed.");
                return result;
            }
            commands = rd->compute_list_begin();
            rd->compute_list_bind_compute_pipeline(commands, filter["pipeline"]);
            rd->compute_list_bind_uniform_set(commands, binding, 0);
            rd->compute_list_dispatch(commands, (dimension + 3) / 4, (dimension + 3) / 4,
                                      (dimension + 3) / 4);
            rd->compute_list_end();
            ambient = filtered_a;
            directional = filtered_d;
        }
        String suffix = cascade == 0 ? "inner" : "outer";
        textures["nb_tlv_ambient_" + suffix] = ambient;
        textures["nb_tlv_directional_" + suffix] = directional;
        values["nb_tlv_min_" + suffix] =
            Vector4(box.position.x, box.position.y, box.position.z, 1. / dimension);
        values["nb_tlv_inverse_" + suffix] =
            Vector4(1. / double(box.size.x), 1. / double(box.size.y), 1. / double(box.size.z), voxel);
    }
    return result;
}
void NBNativeLightingVolume::dispose(RenderingDevice *rd) {
    for (int n = 0; n < owned.size(); ++n) {
        RID rid = owned[n];
        if (rid.is_valid())
            rd->free_rid(rid);
    }
    owned.clear();
    pipelines.clear();
    linear_sampler = RID();
    device_id = 0;
}
} // namespace godot
