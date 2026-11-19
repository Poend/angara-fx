#include "raster_pass.h"
#include <godot_cpp/classes/project_settings.hpp>
#include "lighting_volume.h"
#include "raster_buffers.h"
#include <godot_cpp/classes/render_scene_buffers_rd.hpp>
#include <godot_cpp/classes/render_scene_data.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/rd_shader_source.hpp>
#include <godot_cpp/classes/rd_shader_spirv.hpp>
#include <godot_cpp/classes/rd_texture_format.hpp>
#include <godot_cpp/classes/rd_pipeline_rasterization_state.hpp>
#include <godot_cpp/classes/rd_pipeline_multisample_state.hpp>
#include <godot_cpp/classes/rd_pipeline_depth_stencil_state.hpp>
#include <godot_cpp/classes/rd_pipeline_color_blend_state.hpp>
#include <godot_cpp/classes/rd_pipeline_color_blend_state_attachment.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
namespace godot {
Dictionary NBNativeRasterPass::gpu_program(RenderingDevice *rd, const Dictionary &draw,
                                           const RID &framebuffer, bool precision) {
    String key = String(draw["key"]) + (surface_mode ? String() : UtilityFunctions::str(precision));
    if (gpu_programs.has(key))
        return gpu_programs[key];
    Dictionary program = draw["program"];
    Ref<RDShaderSource> source;
    source.instantiate();
    source->set_stage_source(RenderingDevice::SHADER_STAGE_VERTEX, program["vertex"]);
    source->set_stage_source(RenderingDevice::SHADER_STAGE_FRAGMENT, program["fragment"]);
    Ref<RDShaderSPIRV> spirv = rd->shader_compile_spirv_from_source(source);
    String error = spirv.is_null()
                       ? String("No raster SPIR-V returned.")
                       : spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_VERTEX) +
                             spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_FRAGMENT);
    if (!error.is_empty()) {
        fail(error);
        return Dictionary();
    }
    RID shader = rd->shader_create_from_spirv(spirv);
    Ref<RDPipelineRasterizationState> raster;
    raster.instantiate();
    raster->set_cull_mode(bool(program["cull_disabled"]) ? RenderingDevice::POLYGON_CULL_DISABLED
                                                         : RenderingDevice::POLYGON_CULL_BACK);
    Ref<RDPipelineColorBlendStateAttachment> attachment;
    attachment.instantiate();
    attachment->set_enable_blend(true);
    attachment->set_src_color_blend_factor(RenderingDevice::BLEND_FACTOR_ONE);
    attachment->set_dst_color_blend_factor(surface_mode ? RenderingDevice::BLEND_FACTOR_ONE_MINUS_SRC_ALPHA
                                                        : RenderingDevice::BLEND_FACTOR_ONE);
    attachment->set_src_alpha_blend_factor(surface_mode ? RenderingDevice::BLEND_FACTOR_ZERO
                                                        : RenderingDevice::BLEND_FACTOR_ONE);
    attachment->set_dst_alpha_blend_factor(RenderingDevice::BLEND_FACTOR_ONE);
    Ref<RDPipelineColorBlendState> blend;
    blend.instantiate();
    TypedArray<RDPipelineColorBlendStateAttachment> attachments;
    attachments.append(attachment);
    blend->set_attachments(attachments);
    Ref<RDPipelineMultisampleState> multisample;
    multisample.instantiate();
    Ref<RDPipelineDepthStencilState> depth;
    depth.instantiate();
    RID pipeline = rd->render_pipeline_create(shader, rd->framebuffer_get_format(framebuffer), -1,
                                              RenderingDevice::RENDER_PRIMITIVE_TRIANGLES, raster,
                                              multisample, depth, blend);
    if (!pipeline.is_valid()) {
        fail(surface_mode ? "Native surface pipeline creation failed."
                          : "Distortion raster pipeline creation failed.");
        rd->free_rid(shader);
        return Dictionary();
    }
    Dictionary result;
    result["shader"] = shader;
    result["pipeline"] = pipeline;
    gpu_programs[key] = result;
    return result;
}
// Holding the packed array keeps its COW allocation alive. Editing even one
// element detaches it, so the next draw uploads the new contents immediately.
RID NBNativeRasterPass::geometry_set(RenderingDevice *rd, const PackedByteArray &bytes, const RID &shader) {
    auto &entry = geometries[bytes.ptr()];
    entry.used = draw_generation;
    if (!entry.buffer.is_valid()) {
        entry.data = bytes;
        entry.buffer = rd->storage_buffer_create(bytes.size(), bytes);
        if (!entry.buffer.is_valid()) return RID();
    }
    RID &set = entry.sets[shader.get_id()];
    if (!set.is_valid() || !rd->uniform_set_is_valid(set)) {
        TypedArray<RDUniform> uniforms;
        uniforms.append(uniform(RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER, 0, Array::make(entry.buffer)));
        set = rd->uniform_set_create(uniforms, shader, 1);
    }
    return set;
}
RID NBNativeRasterPass::index_array(RenderingDevice *rd, const PackedInt32Array &indices) {
    auto &entry = index_buffers[indices.ptr()];
    entry.used = draw_generation;
    if (!entry.buffer.is_valid()) {
        entry.data = indices;
        entry.buffer = rd->index_buffer_create(indices.size(), RenderingDevice::INDEX_BUFFER_FORMAT_UINT32,
                                               indices.to_byte_array());
    }
    if (entry.buffer.is_valid() && !entry.array.is_valid())
        entry.array = rd->index_array_create(entry.buffer, 0, indices.size());
    return entry.array;
}
void NBNativeRasterPass::retire_geometry(Array &resources, bool all) {
    for (auto it = geometries.begin(); it != geometries.end();) {
        auto &entry = it->second;
        if (!all && entry.used == draw_generation) { ++it; continue; }
        for (const auto &pair : entry.sets) {
            Dictionary owner;
            owner["uniform_set"] = pair.second;
            resources.push_front(owner);
        }
        resources.append(entry.buffer);
        it = geometries.erase(it);
    }
    for (auto it = index_buffers.begin(); it != index_buffers.end();) {
        auto &entry = it->second;
        if (!all && entry.used == draw_generation) { ++it; continue; }
        resources.append(entry.array);
        resources.append(entry.buffer);
        it = index_buffers.erase(it);
    }
}
RID NBNativeRasterPass::draw_sampler(RenderingDevice *rd, const String &hints) {
    // Canonicalize only the four flags consumed by sampler().
    int key = int(hints.contains("filter_linear")) | (int(hints.contains("mipmap")) << 1) |
              (int(hints.contains("repeat_enable")) << 2) | (int(hints.contains("anisotropic")) << 3);
    RID state = draw_samplers.get(key, RID());
    if (!state.is_valid()) {
        state = sampler(rd, hints);
        if (state.is_valid()) draw_samplers[key] = state;
    }
    return state;
}
Array NBNativeRasterPass::prepare_draws(RenderingDevice *rd, const Array &draws, const RID &framebuffer,
                                        const RID &depth, const Vector2i &size, const Projection &projection,
                                        const Transform3D &camera, const Dictionary &frame_values,
                                        const Dictionary &lighting_textures, Array resources,
                                        bool precision) {
    Array prepared;
    Transform3D view_transform = camera.affine_inverse();
    for (int n = 0; n < draws.size(); ++n) {
        Dictionary draw = draws[n], gpu = gpu_program(rd, draw, framebuffer, precision);
        if (gpu.is_empty())
            continue;
        Dictionary program = draw["program"], values = Dictionary(draw["values"]).duplicate();
        values.merge(frame_values, true);
        Transform3D model = draw["model"], modelview = view_transform * model;
        values["PROJECTION_MATRIX"] = projection;
        values["INV_PROJECTION_MATRIX"] = projection.inverse();
        values["VIEW_MATRIX"] = view_transform;
        values["INV_VIEW_MATRIX"] = camera;
        values["MODEL_MATRIX"] = model;
        values["MODELVIEW_MATRIX"] = modelview;
        values["MODEL_NORMAL_MATRIX"] = model.basis.inverse().transposed();
        values["MODELVIEW_NORMAL_MATRIX"] = modelview.basis.inverse().transposed();
        values["VIEWPORT_SIZE"] = Vector2(size);
        PackedByteArray bytes = NBNativeRasterBuffers::uniform_bytes(program, values);
        RID ubo = rd->uniform_buffer_create(bytes.size(), bytes);
        resources.append(ubo);
        TypedArray<RDUniform> uniforms;
        uniforms.append(uniform(RenderingDevice::UNIFORM_TYPE_UNIFORM_BUFFER, 0, Array::make(ubo)));
        Array samplers = program["samplers"];
        Dictionary textures = draw["textures"];
        for (int slot = 0; slot < samplers.size(); ++slot) {
            Dictionary description = samplers[slot];
            String name = description["name"], hints = description["hints"];
            RID texture = name == "nb_depth_texture" ? depth : fallback;
            if (lighting_textures.has(name))
                texture = lighting_textures[name];
            if (textures.has(name)) {
                Ref<Texture> live = textures[name];
                texture = RenderingServer::get_singleton()->texture_get_rd_texture(
                    live->get_rid(), hints.contains("source_color"));
            }
            RID state = draw_sampler(rd, hints);
            uniforms.append(uniform(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE,
                                    description["binding"], Array::make(state, texture)));
        }
        RID shader = gpu["shader"], material_set = rd->uniform_set_create(uniforms, shader, 0);
        RID geometry_binding = geometry_set(rd, draw["geometry"], shader);
        RID indices_binding = index_array(rd, draw["indices"]);
        Dictionary material_owner;
        material_owner["uniform_set"] = material_set;
        resources.push_front(material_owner);
        if (!material_set.is_valid() || !geometry_binding.is_valid() || !indices_binding.is_valid()) {
            fail(surface_mode ? "Native surface uniform binding failed."
                              : "Distortion uniform binding failed.");
            continue;
        }
        Dictionary packet;
        packet["pipeline"] = gpu["pipeline"];
        packet["material"] = material_set;
        packet["geometry"] = geometry_binding;
        packet["indices"] = indices_binding;
        packet["lit"] = program.get("requires_lighting_volume", false);
        prepared.append(packet);
    }
    return prepared;
}
bool NBNativeRasterPass::compute_shader(RenderingDevice *rd, const String &code, RID &shader, RID &pipeline) {
    if (shader.is_valid())
        return true;
    Ref<RDShaderSource> source;
    source.instantiate();
    source->set_stage_source(RenderingDevice::SHADER_STAGE_COMPUTE, code);
    Ref<RDShaderSPIRV> spirv = rd->shader_compile_spirv_from_source(source);
    String error = spirv.is_null() ? String("No compute SPIR-V returned.")
                                   : spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_COMPUTE);
    if (!error.is_empty()) {
        fail(error);
        return false;
    }
    shader = rd->shader_create_from_spirv(spirv);
    pipeline = rd->compute_pipeline_create(shader);
    return true;
}
void NBNativeRasterPass::render_frame(RenderData *data) {
    Array groups, draws, lights;
    Dictionary policies, frame_values, configuration;
    {
        std::lock_guard<std::mutex> guard(lock);
        if (surface_mode) {
            frame_values = view.duplicate();
            frame_values.merge(height_fog, true);
            configuration = lighting_config.duplicate(true);
            if (!disposed) {
                lights = collect_lights();
                if (configuration.has("lights")) {
                    Array configured_lights = configuration["lights"];
                    configured_lights.append_array(lights);
                }
                Array keys = submissions.keys();
                for (int n = 0; n < keys.size(); ++n) {
                    Dictionary group = submissions[keys[n]];
                    Ref<RefCounted> owner = group["owner"];
                    if (Variant(owner->call("get_ref")).get_validated_object() == nullptr)
                        submissions.erase(keys[n]);
                    else
                        groups.append(group.duplicate());
                }
            }
            report["draws"] = 0;
            report["lighting_draws"] = 0;
            report["lighting_volume_active"] = false;
        } else if (!disposed) {
            Array keys = players.keys();
            for (int n = 0; n < keys.size(); ++n) {
                Dictionary group = players[keys[n]];
                Ref<RefCounted> owner = group["owner"];
                if (Variant(owner->call("get_ref")).get_validated_object() == nullptr) {
                    players.erase(keys[n]);
                    continue;
                }
                draws.append_array(group["draws"]);
                Dictionary policy = group["policy"];
                if (int64_t(group["refractive_surfaces"]) > 0 && bool(policy["supported"]))
                    policies[policy["key"]] = policy;
            }
        }
    }
    bool precision = false;
    Dictionary policy;
    if (surface_mode) {
        if (groups.is_empty())
            return;
    } else {
        if (policies.size() > 1) {
            fail("One camera cannot combine conflicting imported refraction policies; use a consistent "
                 "source project or an explicit manual camera policy.");
            return;
        }
        if (!policies.is_empty()) {
            policy = policies.values()[0];
            std::lock_guard<std::mutex> guard(lock);
            report["distortion_enabled"] = policy["enabled"];
            report["source_policy_verified"] = policy["verified"];
        }
        if (draws.is_empty()) {
            std::lock_guard<std::mutex> guard(lock);
            report["draws"] = 0;
            return;
        }
        if (policy.is_empty()) {
            fail("Refraction draws have no source policy.");
            return;
        }
        precision = policy["high_precision_offsets"];
    }
    auto *rd = RenderingServer::get_singleton()->get_rendering_device();
    Ref<RenderSceneBuffersRD> buffers = data->get_render_scene_buffers();
    auto *scene = data->get_render_scene_data();
    if (!rd || buffers.is_null() || buffers->get_view_count() != 1 || !scene) {
        fail(surface_mode ? "Native surface raster currently requires one Forward+ view."
                          : "Accumulated refraction currently requires one Forward+ view.");
        return;
    }
    Vector2i size = buffers->get_internal_size();
    RID color = buffers->get_color_layer(0), depth = buffers->get_depth_layer(0);
    Ref<RDTextureFormat> format = rd->texture_get_format(color);
    if (format.is_null() || format->get_format() != RenderingDevice::DATA_FORMAT_R16G16B16A16_SFLOAT ||
        (surface_mode && !(format->get_usage_bits() & RenderingDevice::TEXTURE_USAGE_COLOR_ATTACHMENT_BIT))) {
        fail(surface_mode ? "Native surface raster requires a resolved RGBA16F color attachment."
                          : "Accumulated refraction requires RGBA16F scene color.");
        return;
    }
    if (retired.size() >= 3)
        release(rd, retired.pop_front());
    Array resources;
    const int64_t level = ProjectSettings::get_singleton()->get_setting("rendering/textures/default_filters/anisotropic_filtering_level", 2);
    if (level != sampler_level) {
        resources.append_array(draw_samplers.values());
        draw_samplers.clear();
        sampler_level = level;
    }
    RID offsets, source_color, framebuffer;
    if (surface_mode) {
        TypedArray<RID> attachments;
        attachments.append(color);
        framebuffer = rd->framebuffer_create(attachments);
        resources.append(framebuffer);
    } else {
        if (!distortion_framebuffer.is_valid() || distortion_size != size || distortion_precision != precision) {
            resources.append_array(Array::make(distortion_framebuffer, distortion_offsets, distortion_color));
            distortion_size = size;
            distortion_precision = precision;
            distortion_offsets = texture(rd, size, precision ? RenderingDevice::DATA_FORMAT_R16G16B16A16_SFLOAT
                                                             : RenderingDevice::DATA_FORMAT_R8G8B8A8_UNORM,
                                         RenderingDevice::TEXTURE_USAGE_COLOR_ATTACHMENT_BIT | RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT);
            distortion_color = texture(rd, size, RenderingDevice::DATA_FORMAT_R16G16B16A16_SFLOAT,
                                       RenderingDevice::TEXTURE_USAGE_STORAGE_BIT | RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT);
            TypedArray<RID> attachments;
            attachments.append(distortion_offsets);
            distortion_framebuffer = distortion_offsets.is_valid() && distortion_color.is_valid() ? rd->framebuffer_create(attachments) : RID();
        }
        offsets = distortion_offsets;
        source_color = distortion_color;
        framebuffer = distortion_framebuffer;
    }
    if (!framebuffer.is_valid()) {
        fail(surface_mode ? "Native surface framebuffer creation failed."
                          : "Distortion framebuffer creation failed.");
        retired.append(resources);
        return;
    }
    if (!fallback.is_valid()) {
        PackedFloat32Array white;
        for (int n = 0; n < 4; ++n)
            white.append(1.f);
        TypedArray<PackedByteArray> bytes;
        bytes.append(white.to_byte_array());
        fallback = texture(rd, Vector2i(1, 1), RenderingDevice::DATA_FORMAT_R32G32B32A32_SFLOAT,
                           RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT, bytes);
    }
    Transform3D camera = scene->get_cam_transform();
    Projection projection = scene->get_cam_projection();
    if (surface_mode) {
        groups = ordered_groups(groups, camera);
        for (int n = 0; n < groups.size(); ++n)
            draws.append_array(Dictionary(groups[n])["draws"]);
    }
    Dictionary lighting_textures;
    bool needs_lighting = false;
    for (int n = 0; n < draws.size(); ++n)
        needs_lighting |=
            bool(Dictionary(Dictionary(draws[n])["program"]).get("requires_lighting_volume", false));
    if (surface_mode && needs_lighting) {
        if (!Array(configuration["errors"]).is_empty()) {
            fail(UtilityFunctions::str(configuration["errors"]));
            retired.append(resources);
            return;
        }

        TypedArray<AABB> bounds =
            NBNativeLightingVolume::cascade_bounds(configuration["settings"], camera, projection);
        String key =
            UtilityFunctions::var_to_bytes(Array::make(configuration, bounds)).hex_encode().sha256_text();
        if (key != lighting_key) {
            if (!lighting_gpu.is_empty())
                resources.append_array(lighting_gpu["resources"]);
            Ref<NBNativeLightingVolume> engine = dependencies["lighting_engine"];
            lighting_gpu = engine->build(rd, configuration, bounds);
            lighting_key = Array(lighting_gpu["errors"]).is_empty() ? key : String();
        }
        if (!Array(lighting_gpu["errors"]).is_empty()) {
            fail(UtilityFunctions::str(lighting_gpu["errors"]));
            retired.append(resources);
            return;
        }
        frame_values.merge(lighting_gpu["values"], true);
        lighting_textures = lighting_gpu["textures"];
    }
    ++draw_generation;
    Array prepared = prepare_draws(rd, draws, framebuffer, depth, size, projection, camera, frame_values,
                                   lighting_textures, resources, precision);
    retire_geometry(resources, false);
    PackedColorArray clears;
    clears.append(Color(0, 0, 0, 0));
    int64_t list = surface_mode
                       ? rd->draw_list_begin(framebuffer)
                       : rd->draw_list_begin(framebuffer, RenderingDevice::DRAW_CLEAR_COLOR_ALL, clears);
    int64_t lit_count = 0;
    for (int n = 0; n < prepared.size(); ++n) {
        Dictionary draw = prepared[n];
        rd->draw_list_bind_render_pipeline(list, draw["pipeline"]);
        rd->draw_list_bind_uniform_set(list, draw["material"], 0);
        rd->draw_list_bind_uniform_set(list, draw["geometry"], 1);
        rd->draw_list_bind_index_array(list, draw["indices"]);
        rd->draw_list_draw(list, true, 1);
        if (bool(draw["lit"]))
            ++lit_count;
    }
    rd->draw_list_end();
    if (!surface_mode) {
        if (!compute_shader(rd, apply_source, apply_shader, apply_pipeline)) {
            retired.append(resources);
            return;
        }
        RID nearest = draw_sampler(rd, "filter_nearest, repeat_disable");
        if (!compute_shader(rd, copy_source, copy_shader, copy_pipeline)) {
            retired.append(resources);
            return;
        }
        TypedArray<RDUniform> uniforms;
        uniforms.append(
            uniform(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 0, Array::make(nearest, color)));
        uniforms.append(uniform(RenderingDevice::UNIFORM_TYPE_IMAGE, 1, Array::make(source_color)));
        RID copy_set = rd->uniform_set_create(uniforms, copy_shader, 0);
        Dictionary copy_owner;
        copy_owner["uniform_set"] = copy_set;
        resources.push_front(copy_owner);
        list = rd->compute_list_begin();
        rd->compute_list_bind_compute_pipeline(list, copy_pipeline);
        rd->compute_list_bind_uniform_set(list, copy_set, 0);
        rd->compute_list_dispatch(list, (size.x + 7) / 8, (size.y + 7) / 8, 1);
        rd->compute_list_end();
        uniforms.clear();
        uniforms.append(uniform(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 0,
                                Array::make(nearest, source_color)));
        uniforms.append(
            uniform(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 1, Array::make(nearest, offsets)));
        uniforms.append(uniform(RenderingDevice::UNIFORM_TYPE_IMAGE, 2, Array::make(color)));
        RID apply_set = rd->uniform_set_create(uniforms, apply_shader, 0);
        Dictionary apply_owner;
        apply_owner["uniform_set"] = apply_set;
        resources.push_front(apply_owner);
        if (apply_set.is_valid()) {
            list = rd->compute_list_begin();
            rd->compute_list_bind_compute_pipeline(list, apply_pipeline);
            rd->compute_list_bind_uniform_set(list, apply_set, 0);
            rd->compute_list_dispatch(list, (size.x + 7) / 8, (size.y + 7) / 8, 1);
            rd->compute_list_end();
        } else
            fail("Refraction apply bindings failed.");
    }
    retired.append(resources);
    std::lock_guard<std::mutex> guard(lock);
    report["draws"] = prepared.size();
    report["frames"] = int64_t(report["frames"]) + 1;
    if (surface_mode) {
        report["lighting_draws"] = lit_count;
        report["lighting_volume_active"] = lit_count > 0 && !lighting_textures.is_empty();
    } else {
        report["offset_format"] = precision ? "RGBA16F" : "RGBA8_UNORM";
        report["scene_color_filter"] = "point";
        report["source_policy_verified"] = policy["verified"];
    }
}
} // namespace godot
