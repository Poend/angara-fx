#include "raster_pass.h"
#include "lighting_volume.h"
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/rd_texture_format.hpp>
#include <godot_cpp/classes/rd_texture_view.hpp>
#include <godot_cpp/classes/rd_sampler_state.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/templates/sort_array.hpp>
#include <cmath>
#include <vector>
namespace godot {
void NBNativeRasterPass::_bind_methods() {
    ClassDB::bind_method(D_METHOD("initialize_pass", "surface", "types", "copy", "apply"),
                         &NBNativeRasterPass::initialize_pass, DEFVAL(String()), DEFVAL(String()));
    ClassDB::bind_method(D_METHOD("set_option", "key", "value"), &NBNativeRasterPass::set_option);
    ClassDB::bind_method(D_METHOD("get_option", "key"), &NBNativeRasterPass::get_option);
    ClassDB::bind_method(D_METHOD("statistics"), &NBNativeRasterPass::statistics);
    ClassDB::bind_method(D_METHOD("get_players"), &NBNativeRasterPass::get_players);
    ClassDB::bind_method(D_METHOD("get_retired"), &NBNativeRasterPass::get_retired);
    ClassDB::bind_method(D_METHOD("get_lighting_key"), &NBNativeRasterPass::get_lighting_key);
    ClassDB::bind_method(D_METHOD("get_height_fog"), &NBNativeRasterPass::get_height_fog);
    ClassDB::bind_method(D_METHOD("get_sort_override"), &NBNativeRasterPass::get_sort_override);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "_lighting_key", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE),
                 "", "get_lighting_key");
    ADD_PROPERTY(
        PropertyInfo(Variant::DICTIONARY, "_height_fog", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "",
        "get_height_fog");
    ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "_scene_sort_override", PROPERTY_HINT_NONE, "",
                              PROPERTY_USAGE_NONE),
                 "", "get_sort_override");
    ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "_players", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE),
                 "", "get_players");
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "_retired", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "",
                 "get_retired");
    ClassDB::bind_method(D_METHOD("dispose"), &NBNativeRasterPass::dispose);
    ClassDB::bind_method(D_METHOD("_dispose_gpu"), &NBNativeRasterPass::dispose_gpu);
    ClassDB::bind_method(D_METHOD("set_view_parameters", "pre_exposure", "color_limit", "integrated_fog"),
                         &NBNativeRasterPass::set_view_parameters);
    ClassDB::bind_method(D_METHOD("set_height_fog", "settings"), &NBNativeRasterPass::set_height_fog);
    ClassDB::bind_method(D_METHOD("prepare_player_view", "camera"), &NBNativeRasterPass::prepare_player_view);
    ClassDB::bind_method(D_METHOD("submit_surface", "owner", "instance", "sort_context"),
                         &NBNativeRasterPass::submit_surface, DEFVAL(Dictionary()));
    ClassDB::bind_method(D_METHOD("remove_surface", "owner"), &NBNativeRasterPass::remove_surface);
    ClassDB::bind_method(D_METHOD("submit_particle_lights", "owner", "records", "exposure"),
                         &NBNativeRasterPass::submit_particle_lights);
    ClassDB::bind_method(D_METHOD("submit_player_particle_lights", "owner", "records", "camera"),
                         &NBNativeRasterPass::submit_player_particle_lights);
    ClassDB::bind_method(D_METHOD("remove_particle_lights", "owner"),
                         &NBNativeRasterPass::remove_particle_lights);
    ClassDB::bind_method(D_METHOD("_sort_distance", "group", "camera_transform"),
                         &NBNativeRasterPass::sort_distance);
    ClassDB::bind_method(D_METHOD("_ordered_groups", "groups", "camera_transform"),
                         &NBNativeRasterPass::ordered_groups);
    ClassDB::bind_method(D_METHOD("submit_player", "player", "instances"),
                         &NBNativeRasterPass::submit_player);
    ClassDB::bind_method(D_METHOD("remove_player", "player"), &NBNativeRasterPass::remove_player);
    ClassDB::bind_static_method("NBNativeRasterPass", D_METHOD("uniform", "type", "binding", "ids"),
                                &NBNativeRasterPass::uniform);
    ClassDB::bind_static_method("NBNativeRasterPass",
                                D_METHOD("texture", "rd", "size", "format", "usage", "data"),
                                &NBNativeRasterPass::texture, DEFVAL(TypedArray<PackedByteArray>()));
    ClassDB::bind_static_method("NBNativeRasterPass", D_METHOD("sampler", "rd", "hints"),
                                &NBNativeRasterPass::sampler);
    ClassDB::bind_static_method("NBNativeRasterPass", D_METHOD("release", "rd", "resources"),
                                &NBNativeRasterPass::release);
    ClassDB::bind_static_method("NBNativeRasterPass",
                                D_METHOD("release_all", "rd", "retired", "programs", "persistent"),
                                &NBNativeRasterPass::release_all);
}
NBNativeRasterPass::NBNativeRasterPass() {
    set_effect_callback_type(EFFECT_CALLBACK_TYPE_POST_TRANSPARENT);
    set_access_resolved_color(true);
    set_access_resolved_depth(true);
    options["source_scene_context"] = Variant();
    options["source_pre_exposure"] = 1.;
    options["source_eye_adaptation_exposure"] = 1.;
    options["source_eye_adaptation_exposure_observed"] = false;
    options["source_color_limit"] = 32256.;
    options["source_height_fog"] = Variant();
    options["component_sort_policy"] = 0;
    options["component_sort_axis"] = Vector3(0, 0, -1);
    options["high_precision_offsets"] = false;
    options["use_imported_policy"] = false;
    view["nb_view_pre_exposure"] = 1.;
    view["nb_view_color_limit"] = 32256.;
    view["nb_integrated_fog"] = Vector4(0, 0, 0, 1);
    height_fog["nb_height_fog_enabled"] = false;
    lighting_config["errors"] =
        Array::make("Native lit surfaces require an exported scene lighting context.");
}
void NBNativeRasterPass::initialize_pass(bool surface, const Dictionary &types, const String &copy,
                                         const String &apply) {
    std::lock_guard<std::mutex> guard(lock);
    surface_mode = surface;
    dependencies = types;
    copy_source = copy;
    apply_source = apply;
    report["errors"] = Array();
    report["draws"] = 0;
    report["frames"] = 0;
    if (surface) {
        report["lighting_draws"] = 0;
        report["lighting_volume_active"] = false;
        report["native_visual_equivalence_verified"] = false;
    } else
        report["visual_equivalence_verified"] = false;
}
NBNativeRasterPass::~NBNativeRasterPass() {
    std::lock_guard<std::mutex> guard(gpu_lock);
    auto *server = RenderingServer::get_singleton();
    if (server) {
        auto *rd = server->get_rendering_device();
        if (rd)
            release_gpu(rd);
    }
}
void NBNativeRasterPass::set_option(const String &key, const Variant &value) {
    std::lock_guard<std::mutex> guard(lock);
    options[key] = value;
}
Variant NBNativeRasterPass::get_option(const String &key) const {
    std::lock_guard<std::mutex> guard(lock);
    return options.get(key, Variant());
}
Dictionary NBNativeRasterPass::get_players() const {
    std::lock_guard<std::mutex> guard(lock);
    return players.duplicate();
}
Array NBNativeRasterPass::get_retired() const {
    std::lock_guard<std::mutex> guard(gpu_lock);
    return retired.duplicate();
}
String NBNativeRasterPass::get_lighting_key() const {
    std::lock_guard<std::mutex> guard(gpu_lock);
    return lighting_key;
}
Dictionary NBNativeRasterPass::get_height_fog() const {
    std::lock_guard<std::mutex> guard(lock);
    return height_fog.duplicate();
}
Dictionary NBNativeRasterPass::get_sort_override() const {
    std::lock_guard<std::mutex> guard(lock);
    return sort_override.duplicate();
}
void NBNativeRasterPass::fail(const String &message) {
    std::lock_guard<std::mutex> guard(lock);
    Array errors = report["errors"];
    if (!errors.has(message))
        errors.append(message);
}
Array NBNativeRasterPass::collect_lights() {
    Array result, keys = light_submissions.keys();
    for (int n = 0; n < keys.size(); ++n) {
        Dictionary group = light_submissions[keys[n]];
        Ref<RefCounted> owner = group["owner"];
        if (Variant(owner->call("get_ref")).get_validated_object() == nullptr)
            light_submissions.erase(keys[n]);
        else
            result.append_array(group["lights"]);
    }
    return result;
}
Dictionary NBNativeRasterPass::statistics() {
    std::lock_guard<std::mutex> guard(lock);
    Dictionary result = report.duplicate(true);
    if (surface_mode) {
        result["submissions"] = submissions.size();
        result["particle_lights"] = collect_lights().size();
    }
    return result;
}
void NBNativeRasterPass::dispose() {
    {
        std::lock_guard<std::mutex> guard(lock);
        disposed = true;
        players.clear();
        submissions.clear();
        light_submissions.clear();
    }
    RenderingServer::get_singleton()->call_on_render_thread(
        callable_mp(this, &NBNativeRasterPass::dispose_gpu));
}
void NBNativeRasterPass::release_gpu(RenderingDevice *rd) {
    Array persistent;
    retire_geometry(persistent, true);
    persistent.append_array(draw_samplers.values());
    draw_samplers.clear();
    persistent.append_array(Array::make(distortion_framebuffer, distortion_offsets, distortion_color, apply_pipeline,
                                       apply_shader, copy_pipeline, copy_shader, fallback));
    distortion_framebuffer = distortion_offsets = distortion_color = RID();
    // Dispose cached geometry bindings before their shaders.
    retired.append(persistent);
    release_all(rd, retired, gpu_programs, Array());
    if (surface_mode) {
        release(rd, lighting_gpu.get("resources", Array()));
        Ref<NBNativeLightingVolume> engine = dependencies.get("lighting_engine", Variant());
        if (engine.is_valid())
            engine->dispose(rd);
    }
    lighting_gpu.clear();
    lighting_key = String();
    fallback = RID();
    apply_shader = RID();
    apply_pipeline = RID();
    copy_shader = RID();
    copy_pipeline = RID();
}
void NBNativeRasterPass::dispose_gpu() {
    std::lock_guard<std::mutex> guard(gpu_lock);
    auto *rd = RenderingServer::get_singleton()->get_rendering_device();
    if (rd)
        release_gpu(rd);
}
void NBNativeRasterPass::_render_callback(int32_t type, RenderData *data) {
    if (type == EFFECT_CALLBACK_TYPE_POST_TRANSPARENT && data) {
        std::lock_guard<std::mutex> guard(gpu_lock);
        render_frame(data);
    }
}
Ref<RDUniform> NBNativeRasterPass::uniform(int64_t type, int64_t binding, const Array &ids) {
    Ref<RDUniform> value;
    value.instantiate();
    value->set_uniform_type(RenderingDevice::UniformType(type));
    value->set_binding(binding);
    for (int n = 0; n < ids.size(); ++n)
        value->add_id(ids[n]);
    return value;
}
RID NBNativeRasterPass::texture(RenderingDevice *rd, const Vector2i &size, int64_t format, int64_t usage,
                                const TypedArray<PackedByteArray> &data) {
    Ref<RDTextureFormat> descriptor;
    descriptor.instantiate();
    descriptor->set_width(size.x);
    descriptor->set_height(size.y);
    descriptor->set_format(RenderingDevice::DataFormat(format));
    descriptor->set_usage_bits(usage);
    Ref<RDTextureView> view;
    view.instantiate();
    return rd->texture_create(descriptor, view, data);
}
RID NBNativeRasterPass::sampler(RenderingDevice *rd, const String &hints) {
    Ref<RDSamplerState> value;
    value.instantiate();
    auto filter = hints.contains("filter_linear") ? RenderingDevice::SAMPLER_FILTER_LINEAR
                                                  : RenderingDevice::SAMPLER_FILTER_NEAREST;
    value->set_min_filter(filter);
    value->set_mag_filter(filter);
    value->set_mip_filter(hints.contains("mipmap") ? RenderingDevice::SAMPLER_FILTER_LINEAR
                                                   : RenderingDevice::SAMPLER_FILTER_NEAREST);
    if (!hints.contains("mipmap"))
        value->set_max_lod(0.);
    auto repeat = hints.contains("repeat_enable") ? RenderingDevice::SAMPLER_REPEAT_MODE_REPEAT
                                                  : RenderingDevice::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE;
    value->set_repeat_u(repeat);
    value->set_repeat_v(repeat);
    value->set_repeat_w(repeat);
    value->set_use_anisotropy(hints.contains("anisotropic"));
    int64_t level = ProjectSettings::get_singleton()->get_setting(
        "rendering/textures/default_filters/anisotropic_filtering_level", 2);
    value->set_anisotropy_max(double(int64_t(1) << level));
    return rd->sampler_create(value);
}
void NBNativeRasterPass::release(RenderingDevice *rd, const Array &resources) {
    for (int n = 0; n < resources.size(); ++n) {
        Variant entry = resources[n];
        if (entry.get_type() == Variant::DICTIONARY) {
            RID rid = Dictionary(entry)["uniform_set"];
            if (rd->uniform_set_is_valid(rid))
                rd->free_rid(rid);
        } else {
            RID rid = entry;
            if (rid.is_valid())
                rd->free_rid(rid);
        }
    }
}
void NBNativeRasterPass::release_all(RenderingDevice *rd, Array retired, Dictionary programs,
                                     const Array &persistent) {
    for (int n = 0; n < retired.size(); ++n)
        release(rd, retired[n]);
    retired.clear();
    Array values = programs.values();
    for (int n = 0; n < values.size(); ++n) {
        Dictionary value = values[n];
        release(rd, Array::make(value["pipeline"], value["shader"]));
    }
    programs.clear();
    release(rd, persistent);
}
double NBNativeRasterPass::sort_distance(const Dictionary &group, const Transform3D &camera) {
    Dictionary settings, scene;
    {
        std::lock_guard<std::mutex> guard(lock);
        settings = options.duplicate();
        scene = sort_override.duplicate();
    }
    Dictionary sort = group["sort"];
    Vector3 delta = Vector3(sort.get("origin", camera.origin)) - camera.origin;
    int64_t policy = scene.get("policy", settings["component_sort_policy"]);
    double distance = delta.length();
    if (policy == 1)
        distance = -camera.basis.get_column(2).dot(delta);
    else if (policy == 2)
        distance = Vector3(scene.get("axis", settings["component_sort_axis"])).dot(delta);
    return distance + double(sort.get("distance_offset_cm", 0.)) * .01;
}
Array NBNativeRasterPass::ordered_groups(Array groups, const Transform3D &camera) {
    for (int n = 0; n < groups.size(); ++n) {
        Dictionary group = groups[n];
        group["distance"] = sort_distance(group, camera);
    }
    struct Compare {
        bool operator()(const Dictionary &a, const Dictionary &b) const {
            int64_t pa = Dictionary(a["sort"]).get("priority", 0),
                    pb = Dictionary(b["sort"]).get("priority", 0);
            if (pa != pb)
                return pa < pb;
            if (double(a["distance"]) != double(b["distance"]))
                return double(a["distance"]) > double(b["distance"]);
            return int64_t(a["identity"]) < int64_t(b["identity"]);
        }
    };
    // Keep Godot's same in-place sort and tie policy for the shared draw order.
    std::vector<Dictionary> sorted;
    for (int n = 0; n < groups.size(); ++n)
        sorted.push_back(groups[n]);
    if (!sorted.empty()) {
        SortArray<Dictionary, Compare> sorter;
        sorter.sort(sorted.data(), sorted.size());
    }
    for (int n = 0; n < groups.size(); ++n)
        groups[n] = sorted[n];
    return groups;
}
} // namespace godot
