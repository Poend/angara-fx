#include "gpu_backend.h"
#include "gpu_buffers.h"
#include "grid_scatter.h"
#include <godot_cpp/classes/rd_sampler_state.hpp>
#include <godot_cpp/classes/rd_shader_source.hpp>
#include <godot_cpp/classes/rd_shader_spirv.hpp>
#include <godot_cpp/classes/rd_texture_format.hpp>
#include <godot_cpp/classes/rd_texture_view.hpp>
#include <godot_cpp/classes/rd_uniform.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <algorithm>
#include <atomic>
#include <cstring>
namespace godot {
namespace {
std::atomic<uint32_t> source_counter{0};
}
void NBNativeVectorFieldGPU::_bind_methods() {
    ClassDB::bind_static_method("NBNativeVectorFieldGPU", D_METHOD("_float32_to_half", "bits"),
                                &NBNativeVectorFieldGPU::float32_to_half);
    ClassDB::bind_static_method("NBNativeVectorFieldGPU", D_METHOD("texture_data", "field"),
                                &NBNativeVectorFieldGPU::texture_data);
    ClassDB::bind_static_method("NBNativeVectorFieldGPU", D_METHOD("create_texture", "device", "field"),
                                &NBNativeVectorFieldGPU::create_texture);
    ClassDB::bind_static_method("NBNativeVectorFieldGPU", D_METHOD("create_sampler", "device", "tiling"),
                                &NBNativeVectorFieldGPU::create_sampler, DEFVAL(Vector3i(1, 1, 1)));
}
int64_t NBNativeVectorFieldGPU::float32_to_half(int64_t bits) {
    uint32_t raw = uint32_t(bits), sign = (raw >> 16) & 0x8000, mantissa = raw & 0x7fffff;
    int exponent = int((raw >> 23) & 0xff) - 127 + 15;
    if (exponent < -10)
        return sign;
    if (exponent <= 0) {
        mantissa |= 0x800000;
        int shift = 14 - exponent;
        return sign | ((mantissa + (1u << (shift - 1)) - 1 + ((mantissa >> shift) & 1)) >> shift);
    }
    if (exponent >= 31)
        return sign | 0x7c00;
    return sign | ((uint32_t(exponent) << 10) + ((mantissa + 0xfff + ((mantissa >> 13) & 1)) >> 13));
}
PackedByteArray NBNativeVectorFieldGPU::texture_data(const Ref<Resource> &field) {
    PackedByteArray bytes;
    if (field.is_null())
        return bytes;
    Vector3i size = field->get("size");
    PackedFloat32Array source = field->get("vectors_xyz");
    if (size.x <= 0 || size.y <= 0 || size.z <= 0 || size.x > 4096 || size.y > 4096 || size.z > 4096)
        return bytes;
    int64_t count = int64_t(size.x) * size.y * size.z;
    if (count > INT32_MAX / 8 || source.size() != count * 3)
        return bytes;
    bytes.resize(count * 8);
    bytes.fill(0);
    uint8_t *out = bytes.ptrw();
    for (int64_t n = 0; n < count; ++n)
        for (int axis = 0; axis < 3; ++axis) {
            uint32_t bits;
            float value = source[n * 3 + axis];
            std::memcpy(&bits, &value, 4);
            uint16_t half = uint16_t(float32_to_half(bits));
            std::memcpy(out + n * 8 + axis * 2, &half, 2);
        }
    return bytes;
}
RID NBNativeVectorFieldGPU::create_texture(RenderingDevice *device, const Ref<Resource> &field) {
    if (!device || field.is_null() || !field->has_method("validation_error") ||
        !String(field->call("validation_error")).is_empty())
        return RID();
    PackedByteArray bytes = texture_data(field);
    if (bytes.is_empty())
        return RID();
    Vector3i size = field->get("size");
    Ref<RDTextureFormat> format;
    format.instantiate();
    format->set_texture_type(RenderingDevice::TEXTURE_TYPE_3D);
    format->set_format(RenderingDevice::DATA_FORMAT_R16G16B16A16_SFLOAT);
    format->set_width(size.x);
    format->set_height(size.y);
    format->set_depth(size.z);
    format->set_usage_bits(RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT);
    Ref<RDTextureView> view;
    view.instantiate();
    TypedArray<PackedByteArray> data;
    data.append(bytes);
    return device->texture_create(format, view, data);
}
RID NBNativeVectorFieldGPU::create_sampler(RenderingDevice *device, const Vector3i &tiling) {
    if (!device)
        return RID();
    Ref<RDSamplerState> state;
    state.instantiate();
    state->set_min_filter(RenderingDevice::SAMPLER_FILTER_LINEAR);
    state->set_mag_filter(RenderingDevice::SAMPLER_FILTER_LINEAR);
    state->set_mip_filter(RenderingDevice::SAMPLER_FILTER_NEAREST);
    state->set_max_lod(0.);
    state->set_repeat_u(tiling.x ? RenderingDevice::SAMPLER_REPEAT_MODE_REPEAT
                                 : RenderingDevice::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE);
    state->set_repeat_v(tiling.y ? RenderingDevice::SAMPLER_REPEAT_MODE_REPEAT
                                 : RenderingDevice::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE);
    state->set_repeat_w(tiling.z ? RenderingDevice::SAMPLER_REPEAT_MODE_REPEAT
                                 : RenderingDevice::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE);
    return device->sampler_create(state);
}
void NBNativeGPUBackend::_bind_methods() {
    ClassDB::bind_method(D_METHOD("grid_snapshot"), &NBNativeGPUBackend::grid_snapshot);
    ClassDB::bind_method(D_METHOD("get_volume_textures"), &NBNativeGPUBackend::get_volume_textures);
    ClassDB::bind_static_method("NBNativeGPUBackend", D_METHOD("device_available"),
                                &NBNativeGPUBackend::device_available);
    ClassDB::bind_static_method("NBNativeGPUBackend", D_METHOD("get_source_dispatch_counter"),
                                &NBNativeGPUBackend::get_source_dispatch_counter);
    ClassDB::bind_static_method("NBNativeGPUBackend", D_METHOD("set_source_dispatch_counter", "value"),
                                &NBNativeGPUBackend::set_source_dispatch_counter);
    ClassDB::bind_method(D_METHOD("initialize", "compiled_program"), &NBNativeGPUBackend::initialize);
    ClassDB::bind_method(D_METHOD("run", "particles", "delta", "time", "duration", "local_space",
                                  "source_transform", "user_parameters"),
                         &NBNativeGPUBackend::run);
    ClassDB::bind_method(D_METHOD("run_buffer", "buffer", "delta", "time", "duration", "local_space", "source_transform", "user_parameters", "compact"), &NBNativeGPUBackend::run_buffer, DEFVAL(true));
    ClassDB::bind_method(D_METHOD("dispose"), &NBNativeGPUBackend::dispose);
    ClassDB::bind_method(D_METHOD("set_profile_phases", "value"), &NBNativeGPUBackend::set_profile_phases);
    ClassDB::bind_method(D_METHOD("get_profile_phases"), &NBNativeGPUBackend::get_profile_phases);
    ClassDB::bind_method(D_METHOD("get_phase_timings"), &NBNativeGPUBackend::get_phase_timings);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "profile_phases", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE),
                 "set_profile_phases", "get_profile_phases");
    ClassDB::bind_method(D_METHOD("_pack", "particles"), &NBNativeGPUBackend::pack);
    ClassDB::bind_method(D_METHOD("_unpack", "bytes", "particles"), &NBNativeGPUBackend::unpack);
    ClassDB::bind_method(D_METHOD("_parameter_data", "delta", "time", "duration", "count", "local_space",
                                  "source", "user_parameters"),
                         &NBNativeGPUBackend::parameter_data);
    ClassDB::bind_method(D_METHOD("get_program"), &NBNativeGPUBackend::get_program);
    ClassDB::bind_method(D_METHOD("set_program", "value"), &NBNativeGPUBackend::set_program);
    ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "program", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE),
                 "set_program", "get_program");
    ClassDB::bind_method(D_METHOD("get_error_message"), &NBNativeGPUBackend::get_error_message);
    ClassDB::bind_method(D_METHOD("set_error_message", "value"), &NBNativeGPUBackend::set_error_message);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "error_message", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE),
                 "set_error_message", "get_error_message");
    ClassDB::bind_method(D_METHOD("get_last_source_dispatch_counter"),
                         &NBNativeGPUBackend::get_last_source_dispatch_counter);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "last_source_dispatch_counter", PROPERTY_HINT_NONE, "",
                              PROPERTY_USAGE_NONE),
                 "", "get_last_source_dispatch_counter");
    ClassDB::bind_method(D_METHOD("get_last_dispatch_usec"), &NBNativeGPUBackend::get_last_dispatch_usec);
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "last_dispatch_usec", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "",
        "get_last_dispatch_usec");
    ClassDB::bind_method(D_METHOD("get_dispatch_count"), &NBNativeGPUBackend::get_dispatch_count);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "dispatch_count", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE),
                 "", "get_dispatch_count");
    ClassDB::bind_method(D_METHOD("get_bytes_transferred"), &NBNativeGPUBackend::get_bytes_transferred);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "bytes_transferred", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE),
                 "", "get_bytes_transferred");
}
NBNativeGPUBackend::~NBNativeGPUBackend() {
    dispose();
}
bool NBNativeGPUBackend::device_available() {
    return RenderingServer::get_singleton() && RenderingServer::get_singleton()->get_rendering_device();
}
int64_t NBNativeGPUBackend::get_source_dispatch_counter() {
    return source_counter.load();
}
void NBNativeGPUBackend::set_source_dispatch_counter(int64_t value) {
    source_counter.store(uint32_t(value));
}
bool NBNativeGPUBackend::initialize(const Dictionary &compiled) {
    dispose();
    error_message = String();
    program = compiled;
    int64_t stride = program.get("stride_vec4", 48);
    if (stride < 48 || stride > 65536) {
        error_message = "Invalid GPU particle stride.";
        return false;
    }
    width = stride * 4;
    if (!device_available()) {
        error_message = "Native GPU particles require a RenderingDevice (Forward+ or Mobile); Compatibility "
                        "and headless have no compute device.";
        return false;
    }
    device = RenderingServer::get_singleton()->create_local_rendering_device();
    if (!device) {
        error_message = "The renderer could not create a local compute device.";
        return false;
    }
    Dictionary grid_program = program.get("grid_stages", Dictionary());
    particle_reader_columns = program.get("reader_columns", Array());
    grid_parameter_layouts.clear();
    positive_grid_parameters.clear();
    Array grid_passes = grid_program.get("stages", Array());
    for (int i = 0; i < grid_passes.size(); ++i) {
        Dictionary pass=grid_passes[i];
        if(pass.has("scatter_pins")) {
            grid_parameter_layouts.resize(grid_passes.size());
            grid_parameter_layouts[i]=pass;
            continue;
        }
        Array keys = Dictionary(grid_passes[i]).get("parameters", Array());
        if (!keys.is_empty()) {
            grid_parameter_layouts.resize(grid_passes.size());
            grid_parameter_layouts[i] = keys;
            positive_grid_parameters.resize(grid_passes.size());
            positive_grid_parameters[i] = uint32_t(int64_t(Dictionary(grid_passes[i]).get("positive_parameters", 0)));
        }
    }
    Array layouts = Dictionary(grid_program.get("fields", Dictionary())).values();
    for (int i = 0; i < layouts.size(); ++i) {
        if (Dictionary(layouts[i]).has("target")) {
            published_grids.instantiate();
            break;
        }
    }
    error_message = published_grids.is_valid() ? published_grids->initialize(grid_program, volume_textures)
                                               : grids.initialize(device, grid_program);
    if (!error_message.is_empty()) {
        dispose();
        return false;
    }
    Ref<RDShaderSource> source;
    source.instantiate();
    source->set_language(RenderingDevice::SHADER_LANGUAGE_GLSL);
    source->set_stage_source(RenderingDevice::SHADER_STAGE_COMPUTE, program.get("source", ""));
    Ref<RDShaderSPIRV> spirv = device->shader_compile_spirv_from_source(source);
    String failure = spirv.is_null() ? String("No SPIR-V returned.")
                                     : spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_COMPUTE);
    if (!failure.is_empty()) {
        error_message = "GPU shader compilation failed: " + failure;
        dispose();
        return false;
    }
    shader = device->shader_create_from_spirv(spirv);
    if (!shader.is_valid()) {
        error_message = "GPU shader creation failed.";
        dispose();
        return false;
    }
    pipeline = device->compute_pipeline_create(shader);
    if (!pipeline.is_valid()) {
        error_message = "GPU compute pipeline creation failed.";
        dispose();
        return false;
    }
    Array parameters = program.get("parameters", Array());
    PackedByteArray bytes;
    bytes.resize((8 + parameters.size()) * 16);
    bytes.fill(0);
    parameter_buffer = device->storage_buffer_create(bytes.size(), bytes);
    parameter_capacity = bytes.size();
    bytes.resize(16);
    bytes.fill(0);
    output_count = device->storage_buffer_create(16, bytes);
    Array fields = program.get("vector_fields", Array());
    for (int n = 0; n < fields.size(); ++n) {
        Dictionary field = fields[n];
        RID texture = NBNativeVectorFieldGPU::create_texture(device, field.get("resource", Variant())),
            sampler = NBNativeVectorFieldGPU::create_sampler(device, field.get("tiling", Vector3i(1, 1, 1)));
        field_textures.push_back(texture);
        field_samplers.push_back(sampler);
        if (!texture.is_valid() || !sampler.is_valid()) {
            error_message = "Cannot create native vector-field volume/sampler.";
            dispose();
            return false;
        }
    }
    if (!parameter_buffer.is_valid() || !output_count.is_valid() || !resize(16)) {
        if (error_message.is_empty())
            error_message = "GPU parameter-buffer allocation failed.";
        dispose();
        return false;
    }
    return true;
}
bool NBNativeGPUBackend::resize(int64_t count) {
    if (!device || count < 1 || count > INT32_MAX / (width * 4)) {
        error_message = "Invalid GPU buffer capacity.";
        return false;
    }
    if (uniforms.is_valid())
        device->free_rid(uniforms);
    if (particles.is_valid())
        device->free_rid(particles);
    uniforms = RID();
    particles = RID();
    capacity = count;
    PackedByteArray bytes;
    bytes.resize(capacity * width * 4);
    bytes.fill(0);
    particles = device->storage_buffer_create(bytes.size(), bytes);
    return bind_buffers();
}
bool NBNativeGPUBackend::bind_buffers() {
    TypedArray<RDUniform> bindings;
    for (int n = 0; n < 3; ++n) {
        Ref<RDUniform> binding;
        binding.instantiate();
        binding->set_uniform_type(RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER);
        binding->set_binding(n);
        binding->add_id(n == 0 ? particles : n == 1 ? parameter_buffer : output_count);
        bindings.append(binding);
    }
    Array fields = program.get("vector_fields", Array());
    for (int n = 0; n < int(field_textures.size()); ++n) {
        Ref<RDUniform> binding;
        binding.instantiate();
        binding->set_uniform_type(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE);
        Dictionary field = fields[n];
        binding->set_binding(field.get("binding", 0));
        binding->add_id(field_samplers[n]);
        binding->add_id(field_textures[n]);
        bindings.append(binding);
    }
    uniforms = device->uniform_set_create(bindings, shader, 0);
    if (!particles.is_valid() || !uniforms.is_valid()) {
        error_message = "GPU storage-buffer allocation failed.";
        return false;
    }
    return true;
}
bool NBNativeGPUBackend::run(const Array &values, double delta, double time, double duration, bool local,
                             const Transform3D &source, const Dictionary &user_values) {
    int64_t phase_start = 0;
    if (profile_phases) {
        phase_timings = Dictionary();
        phase_start = Time::get_singleton()->get_ticks_usec();
    }
    auto mark = [&](const char *name) {
        if (profile_phases) {
            int64_t now = Time::get_singleton()->get_ticks_usec();
            phase_timings[name] = now - phase_start;
            phase_start = now;
        }
    };
    if (values.is_empty())
        return true;
    if (!device) {
        error_message = "GPU backend is not initialized.";
        return false;
    }
    int64_t started = Time::get_singleton()->get_ticks_usec();
    if (values.size() > capacity && !resize(values.size() * 2))
        return false;
    PackedByteArray upload = pack(values);
    if (upload.is_empty()) {
        error_message = "GPU particle layout or capacity is invalid.";
        return false;
    }
    mark("pack");
    PackedByteArray output = dispatch_bytes(upload, values.size(), delta, time, duration, local, source, user_values);
    if (output.size() != upload.size())
        return false;
    if (profile_phases)
        phase_start = Time::get_singleton()->get_ticks_usec();
    if (!unpack(output, values)) {
        error_message = "GPU snapshot layout or byte count is invalid.";
        return false;
    }
    mark("unpack");
    std::vector<int64_t> indices;
    for (int n = 0; n < values.size(); ++n) {
        Dictionary p = values[n];
        if (bool(p.get("alive", false)))
            indices.push_back(p.get("gpu_output_index", -1));
    }
    std::sort(indices.begin(), indices.end());
    for (int64_t n = 0; n < int64_t(indices.size()); ++n)
        if (indices[n] != n) {
            error_message = "GPU output allocation contains a missing or repeated particle index.";
            return false;
        }
    last_dispatch_usec = Time::get_singleton()->get_ticks_usec() - started;
    mark("allocation_validation");
    ++dispatch_count;
    bytes_transferred += upload.size() + output.size() + (8 + Array(program.get("parameters", Array())).size()) * 16;
    return true;
}
PackedByteArray NBNativeGPUBackend::dispatch_bytes(const PackedByteArray &upload, int64_t count,
                                                    double delta, double time, double duration, bool local,
                                                    const Transform3D &source, const Dictionary &user_values) {
    int64_t phase_start = profile_phases ? Time::get_singleton()->get_ticks_usec() : 0;
    auto mark = [&](const char *name) {
        if (profile_phases) {
            int64_t now = Time::get_singleton()->get_ticks_usec();
            phase_timings[name] = now - phase_start;
            phase_start = now;
        }
    };
    uint32_t counter = source_counter.fetch_add(1) + 1;
    last_source_dispatch_counter = counter;
    Dictionary parameters = user_values.duplicate();
    int32_t signed_counter;
    std::memcpy(&signed_counter, &counter, 4);
    parameters["_NB.GPU.DispatchCounter"] = signed_counter;
    PackedFloat32Array floats =
        parameter_data(delta, time, duration, count, local, source, parameters);
    PackedByteArray bytes = floats.to_byte_array(), zero;
    zero.resize(16);
    zero.fill(0);
    if (device->buffer_update(output_count, 0, 16, zero) != OK) {
        error_message = "Cannot reset GPU output allocation counter.";
        return PackedByteArray();
    }
    Array definitions = program.get("parameters", Array());
    for (int n = 0; n < definitions.size(); ++n) {
        Dictionary definition = definitions[n];
        if (definition.get("type", "") == Variant("int")) {
            uint32_t value = uint32_t(int64_t(parameters.get(definition.get("name", ""), 0)));
            std::memcpy(bytes.ptrw() + (8 + n) * 16, &value, 4);
        }
    }
    int64_t reader_upload_bytes = 0;
    if (!particle_reader_columns.is_empty()) {
        PackedByteArray packet = parameters.get("_NB.ReaderColumns", PackedByteArray());
        if (packet.size() < particle_reader_columns.size()*16 || packet.size()%16 != 0 ||
            bytes.size() > INT32_MAX - packet.size()) {
            error_message = "Invalid indexed particle reader packet.";
            return PackedByteArray();
        }
        bytes.append_array(packet);
        reader_upload_bytes = packet.size();
        if (bytes.size() > parameter_capacity) {
            int64_t next = std::max<int64_t>(bytes.size(), std::min<int64_t>(INT32_MAX, parameter_capacity * 2));
            RID replacement = device->storage_buffer_create(next);
            if (!replacement.is_valid()) {
                error_message = "Cannot grow indexed particle reader buffer.";
                return PackedByteArray();
            }
            RID old_buffer = parameter_buffer, old_set = uniforms;
            parameter_buffer = replacement;
            if (!bind_buffers()) {
                device->free_rid(replacement);
                uniforms = old_set;
                parameter_buffer = old_buffer;
                return PackedByteArray();
            }
            device->free_rid(old_set);
            device->free_rid(old_buffer);
            parameter_capacity = next;
        }
    }
    if (device->buffer_update(particles, 0, upload.size(), upload) != OK ||
        device->buffer_update(parameter_buffer, 0, bytes.size(), bytes) != OK) {
        error_message = "GPU storage-buffer upload failed.";
        return PackedByteArray();
    }
    mark("parameters_and_upload");
    int64_t list = device->compute_list_begin();
    device->compute_list_bind_compute_pipeline(list, pipeline);
    device->compute_list_bind_uniform_set(list, uniforms, 0);
    device->compute_list_dispatch(list, uint32_t((count + 63) / 64), 1, 1);
    device->compute_list_end();
    // Synchronous readback submits the pending compute and copy together and
    // waits for both. A separate submit/sync would stall this device twice.
    mark("dispatch_record");
    PackedByteArray output = device->buffer_get_data(particles, 0, upload.size());
    mark("dispatch_and_readback");
    if (output.size() != upload.size()) {
        error_message = "GPU snapshot readback returned an invalid byte count.";
        return PackedByteArray();
    }
    bytes_transferred += reader_upload_bytes;
    return output;
}
bool NBNativeGPUBackend::run_buffer(const Ref<NBNativeGPUParticleBuffer> &buffer, double delta, double time,
                                    double duration, bool local, const Transform3D &source,
                                    const Dictionary &user_values, bool compact) {
    if (buffer.is_null() || !buffer->matches_layout(program)) {
        error_message = "Resident GPU buffer has an incompatible schema.";
        return false;
    }
    if (!buffer->get_count())
        return true;
    if (!device) {
        error_message = "GPU backend is not initialized.";
        return false;
    }
    int64_t started = Time::get_singleton()->get_ticks_usec();
    if (profile_phases)
        phase_timings = Dictionary();
    if (buffer->get_count() > capacity && !resize(buffer->get_count() * 2))
        return false;
    PackedByteArray upload = buffer->dispatch_bytes();
    if (profile_phases)
        phase_timings["resident_prepare"] = Time::get_singleton()->get_ticks_usec() - started;
    PackedByteArray output = dispatch_bytes(upload, buffer->get_count(), delta, time, duration, local, source, user_values);
    if (output.size() != upload.size())
        return false;
    int64_t commit_started = profile_phases ? Time::get_singleton()->get_ticks_usec() : 0;
    TypedArray<String> errors = buffer->commit_output(output, compact);
    if (!errors.is_empty()) {
        error_message = errors[0];
        return false;
    }
    if (profile_phases)
        phase_timings["resident_commit"] = Time::get_singleton()->get_ticks_usec() - commit_started;
    last_dispatch_usec = Time::get_singleton()->get_ticks_usec() - started;
    ++dispatch_count;
    bytes_transferred += upload.size() + output.size() + (8 + Array(program.get("parameters", Array())).size()) * 16;
    return true;
}
bool NBNativeGPUBackend::step_grids(const Dictionary &parameters, const Array &emitter_states) {
    Array stages;
    stages.resize(grid_parameter_layouts.size());
    for (int i = 0; i < stages.size(); ++i) {
        if(grid_parameter_layouts[i].get_type()==Variant::DICTIONARY) {
            PackedByteArray packet;
            error_message=nbgrid::scatter_particles(grid_parameter_layouts[i],emitter_states,parameters,packet);
            if(!error_message.is_empty()) return false;
            stages[i]=packet;
            continue;
        }
        if (grid_parameter_layouts[i].get_type() != Variant::ARRAY)
            continue;
        Array keys = grid_parameter_layouts[i];
        PackedFloat32Array values;
        values.resize(((keys.size() + 3) / 4) * 4);
        values.fill(0.0f);
        for (int j = 0; j < keys.size(); ++j) {
            if (keys[j] == Variant("@iteration")) continue;
            Variant v = parameters.get(keys[j], Variant());
            if ((v.get_type() != Variant::FLOAT && v.get_type() != Variant::INT) ||
                !std::isfinite(float(double(v))) ||
                ((positive_grid_parameters[i] & (uint32_t(1) << j)) && float(double(v)) <= 0.0f)) {
                error_message = "Missing or invalid live grid scalar: " + String(keys[j]);
                return false;
            }
            values.set(j, float(double(v)));
        }
        stages[i] = values.to_byte_array();
    }
    if (published_grids.is_valid())
        published_grids->step(stages);
    else if (device) {
        error_message=grids.step(device, stages);
        if(!error_message.is_empty()) return false;
    }
    return true;
}
void NBNativeGPUBackend::dispose() {
    if (published_grids.is_valid()) {
        published_grids->dispose();
        published_grids.unref();
        volume_textures.clear();
    }
    if (device) {
        grids.clear(device);
        for (RID handle : {uniforms, pipeline, particles, parameter_buffer, shader, output_count})
            if (handle.is_valid())
                device->free_rid(handle);
        for (RID handle : field_textures)
            if (handle.is_valid())
                device->free_rid(handle);
        for (RID handle : field_samplers)
            if (handle.is_valid())
                device->free_rid(handle);
        memdelete(device);
        device = nullptr;
    }
    field_textures.clear();
    field_samplers.clear();
    uniforms = RID();
    pipeline = RID();
    particles = RID();
    parameter_buffer = RID();
    shader = RID();
    output_count = RID();
    capacity = 0;
}
PackedByteArray NBNativeGPUBackend::pack(const Array &values) const {
    return NBNativeGPUBuffers::pack(values, program);
}
bool NBNativeGPUBackend::unpack(const PackedByteArray &bytes, const Array &values) const {
    return NBNativeGPUBuffers::unpack(bytes, values, program);
}
PackedFloat32Array NBNativeGPUBackend::parameter_data(double delta, double time, double duration,
                                                      int64_t count, bool local, const Transform3D &source,
                                                      const Dictionary &values) const {
    return NBNativeGPUBuffers::parameters(program, delta, time, duration, count, local, source, values);
}
} // namespace godot
