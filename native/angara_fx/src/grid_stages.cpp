#include "grid_stages.h"
#include "grid_pressure_shader.h"
#include "grid_scatter.h"
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/rd_shader_source.hpp>
#include <godot_cpp/classes/rd_sampler_state.hpp>
#include <godot_cpp/classes/rd_shader_spirv.hpp>
#include <godot_cpp/classes/rd_uniform.hpp>
#include <godot_cpp/classes/rd_texture_format.hpp>
#include <godot_cpp/classes/rd_texture_view.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <cmath>

namespace godot::nbgrid {
bool reader_declaration(const Ref<Resource> &op) {
    return String(op->get("operation_kind")) == "assignment" && String(op->get("target_type")) == "DI<ParticleRead>";
}
bool declaration(const Ref<Resource> &op) {
    return String(op->get("operation_kind")) == "assignment" &&
           String(op->get("target_type")) == "DI<Grid3DCollection>";
}
namespace {
Variant input(const Ref<Resource> &op) {
    Array inputs = op->get("inputs");
    return inputs.size() == 1 ? Ref<Resource>(inputs[0])->get("value") : Variant();
}
String word(const Variant &v) {
    if (v.get_type() == Variant::STRING)
        return v;
    if (v.get_type() == Variant::DICTIONARY && String(Dictionary(v).get("kind", "")) == "reference")
        return Dictionary(v).get("name", "");
    return "";
}
bool scalar(const Variant &v) {
    return (v.get_type() == Variant::FLOAT || v.get_type() == Variant::INT) &&
           std::isfinite(float(double(v)));
}
bool gradient_module(const Ref<Resource> &op) {
    return String(op->get("operation_kind")) == "call" &&
           String(op->get("module_name")) == "/NiagaraFluids/Modules/Grid3D/Grid3D_ComputeGradient";
}
bool advection_module(const Ref<Resource> &op) {
    return String(op->get("operation_kind")) == "call" &&
           String(op->get("module_name")) == "/NiagaraFluids/Modules/Grid3D/Grid3D_AdvectScalar@1.0";
}
bool pressure_module(const Ref<Resource> &op) {
    return String(op->get("operation_kind")) == "call" &&
           String(op->get("module_name")) == "/NiagaraFluids/Modules/Grid3D/Grid3D_PressureIteration@2.0";
}
Dictionary module_inputs(const Ref<Resource> &op) {
    Dictionary pins;
    Array inputs = op->get("inputs");
    for (int i = 0; i < inputs.size(); ++i) {
        Ref<Resource> pin = inputs[i];
        pins[pin->get("input_name")] = pin->get("value");
    }
    return pins;
}
String transport_scalar(const Variant &value, Array &parameters, uint32_t *positive = nullptr) {
    if (scalar(value))
        return String::num_real(double(value), true);
    String name = word(value);
    if (value.get_type() != Variant::DICTIONARY ||
        !(name.begins_with("User.") || name.begins_with("Emitter.")))
        return "";
    int index = parameters.find(name);
    if (index < 0) {
        index = parameters.size();
        parameters.append(name);
    }
    if (positive && index < 32)
        *positive |= uint32_t(1) << index;
    return "frame.values[" + String::num_int64(index / 4) + "][" + String::num_int64(index % 4) + "]";
}
RID sample_image(RenderingDevice *device, Vector3i size, RenderingDevice::DataFormat storage_format) {
    Ref<RDTextureFormat> format;
    format.instantiate();
    format->set_texture_type(RenderingDevice::TEXTURE_TYPE_3D);
    format->set_format(storage_format);
    format->set_width(size.x);
    format->set_height(size.y);
    format->set_depth(size.z);
    format->set_usage_bits(RenderingDevice::TEXTURE_USAGE_STORAGE_BIT |
                           RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT |
                           RenderingDevice::TEXTURE_USAGE_CAN_COPY_TO_BIT);
    Ref<RDTextureView> view;
    view.instantiate();
    RID texture = device->texture_create(format, view);
    if (texture.is_valid() && device->texture_clear(texture, Color(0, 0, 0, 0), 0, 1, 0, 1) != OK) {
        device->free_rid(texture);
        return RID();
    }
    return texture;
}
String grid_vector(const Variant &value, const Dictionary &outputs) {
    if (value.get_type() == Variant::DICTIONARY)
        return outputs.get(word(value), "");
    if (value.get_type() != Variant::ARRAY)
        return "";
    Array values = value;
    if (values.size() != 3)
        return "";
    PackedStringArray parts;
    for (int i = 0; i < 3; ++i) {
        if (!scalar(values[i]))
            return "";
        parts.append(String::num_real(double(values[i]), true));
    }
    return "vec3(" + String(",").join(parts) + ")";
}
String expression(const Variant &v, const Array &channels, const String &grid, const Dictionary &outputs) {
    if (v.get_type() == Variant::FLOAT || v.get_type() == Variant::INT) {
        if (!std::isfinite(float(double(v))))
            return "";
        return "float(" + String::num_real(double(v), true) + ")";
    }
    if (v.get_type() != Variant::DICTIONARY)
        return "";
    Dictionary d = v;
    String name = d.get("name", "");
    if (String(d.get("kind", "")) == "reference") {
        if (outputs.has(name) && String(outputs[name]).begins_with("scalar_"))
            return outputs[name];
        if (!name.begins_with(grid + String(".")))
            return "";
        int i = channels.find(name.substr(grid.length() + 1));
        return i < 0 ? String() : "c" + String::num_int64(i);
    }
    // ExecutionIndex on a three-dimensional Niagara grid is the x-fastest linear cell index.
    Array args = d.get("arguments", Array());
    if (String(d.get("kind", "")) != "call")
        return "";
    if (name == "DotProduct" || name == "/Niagara/DynamicInputs/Vectors/DotProduct") {
        Dictionary parts;
        for (int i = 0; i < args.size(); ++i) {
            Dictionary arg = args[i];
            String vector = grid_vector(arg.get("value", Variant()), outputs);
            if (vector.is_empty())
                return "";
            parts[arg["name"]] = vector;
        }
        if (args.size() != 2 || !parts.has("VectorA") || !parts.has("VectorB"))
            return "";
        return "dot(" + String(parts["VectorA"]) + "," + String(parts["VectorB"]) + ")";
    }
    if (name == "MakeFloatFromInt" || name == "/Niagara/DynamicInputs/TypeConversions/MakeFloatFromInt") {
        if (args.size() != 1)
            return "";
        Dictionary arg = args[0];
        Variant value = arg.get("value", Variant());
        if (String(arg.get("name", "")) != "Int" || value.get_type() != Variant::DICTIONARY)
            return "";
        Dictionary index = value;
        String function = index.get("name", "");
        if (String(index.get("kind", "")) == "call" && Array(index.get("arguments", Array())).is_empty() &&
            (function == "ReturnExecIndex" || function == "/Niagara/DynamicInputs/Execution/ReturnExecIndex"))
            return "float(cell)";
        return "";
    }
    String sign;
    if (name == "Add_Float" || name == "/Niagara/DynamicInputs/Add/Add_Float")
        sign = "+";
    else if (name == "Subtract_Float" || name == "/Niagara/DynamicInputs/Subtract/Subtract_Float")
        sign = "-";
    else if (name == "Multiply_Float" || name == "/Niagara/DynamicInputs/Multiply/Multiply_Float")
        sign = "*";
    else
        return "";
    if (args.size() != 2)
        return "";
    Dictionary parts;
    for (int i = 0; i < args.size(); ++i) {
        Dictionary arg = args[i];
        String value = expression(arg.get("value", Variant()), channels, grid, outputs);
        if (value.is_empty())
            return "";
        parts[arg["name"]] = value;
    }
    if (!parts.has("A") || !parts.has("B"))
        return "";
    return "(" + String(parts["A"]) + sign + String(parts["B"]) + ")";
}
String definition(const Ref<Resource> &op, const Dictionary &environment, Dictionary &info) {
    Variant v = input(op);
    if (v.get_type() != Variant::STRING)
        return "Grid3D requires an exported interface definition.";
    Variant parsed = JSON::parse_string(v);
    if (parsed.get_type() != Variant::DICTIONARY)
        return "Invalid Grid3D interface definition.";
    Dictionary cfg = parsed;
    if (String(cfg.get("SetResolutionMethod", "")) != "Independent" ||
        int64_t(cfg.get("NumAttributes", 0)) != 0)
        return "Grid3D requires independent resolution and named channels.";
    Dictionary binding =
        Dictionary(cfg.get("RenderTargetUserParameter", Dictionary())).get("parameter", Dictionary());
    String target = binding.get("name", "None");
    if (target != "None") {
        if (!target.begins_with("User.") || target.length() <= 5)
            return "Grid3D render target requires a named User object parameter.";
        info["target"] = target;
    }
    String format = bool(cfg.get("bOverrideFormat", false)) ? cfg.get("OverrideBufferFormat", "")
                                                            : environment.get("default_format", "");
    format = environment.get("forced_format", format);
    if (format != "Float" && format != "HalfFloat")
        return "Grid3D requires an exported float or half-float storage format.";
    Variant scale = environment.get("resolution_multiplier", 1.0);
    if (scale.get_type() != Variant::INT && scale.get_type() != Variant::FLOAT)
        return "Invalid Grid3D resolution multiplier.";
    float multiplier = double(scale);
    if (!std::isfinite(multiplier))
        return "Invalid Grid3D resolution multiplier.";
    Dictionary axes = cfg.get("NumCells", Dictionary());
    Vector3i size;
    int axis = 0;
    for (const char *key : {"x", "y", "z"}) {
        Variant dimension = axes.get(key, Variant());
        if (dimension.get_type() != Variant::FLOAT && dimension.get_type() != Variant::INT)
            return "Missing Grid3D resolution.";
        double n = dimension;
        if (!(n >= 1 && n <= 65535 && std::floor(n) == n))
            return "Invalid Grid3D resolution.";
        float scaled = float(n) * multiplier;
        if (scaled > 65535)
            return "Scaled Grid3D axis exceeds the supported range.";
        size[axis++] = std::abs(multiplier - 1.f) <= 1.e-8f ? int(n) : int(std::max(1.f, scaled));
    }
    info["size"] = size;
    info["half"] = format == "HalfFloat";
    info["channels"] = Array();
    info["attributes"] = Dictionary();
    info["clear_before_scatter"] = cfg.get("ClearBeforeNonIterationStage", true);
    return "";
}
} // namespace
String compile(const Ref<Resource> &emitter, Dictionary &result) {
    Dictionary grids, targets, readers, indices;
    Dictionary contracts = emitter->get("source_rng_contracts");
    Dictionary environment = contracts.get("grid3d_settings", Dictionary());
    Array stages, compiled;
    Ref<Resource> spawn = emitter->call("get_stage", "EmitterSpawn");
    if (spawn.is_valid()) {
        Array ops = spawn->get("operations");
        for (int i = 0; i < ops.size(); ++i) {
            Ref<Resource> op = ops[i];
            if (!bool(op->get("enabled"))) continue;
            String name = op->get("module_name");
            if (reader_declaration(op)) {
                Variant raw = input(op);
                Variant parsed = raw.get_type()==Variant::STRING ? JSON::parse_string(raw) : Variant();
                if (parsed.get_type()==Variant::DICTIONARY) {
                    Dictionary binding = Dictionary(parsed).get("EmitterBinding",Dictionary());
                    if (String(binding.get("bindingMode",""))=="Other") readers[name] = binding.get("emitterName","");
                }
                continue;
            }
            if (!declaration(op)) {
                Variant raw=input(op);
                if(raw.get_type()==Variant::DICTIONARY && String(Dictionary(raw).get("name",""))=="/NiagaraFluids/DynamicInput/Grid3D_GetGridIndexFromName") {
                    Array args=Dictionary(raw).get("arguments",Array());
                    Dictionary index;
                    for(int k=0;k<args.size();++k) {
                        Dictionary arg=args[k];
                        if(String(arg["name"])=="GridCollection") index["grid"]=arg["value"];
                        if(String(arg["name"])=="GridName") index["name"]=word(arg["value"]);
                    }
                    if(index.has("grid") && index.has("name")) indices[name]=index;
                }
                continue;
            }
            Dictionary info;
            String error = definition(op, environment, info);
            if (!error.is_empty())
                return name + ": " + error;
            if (grids.has(name) || !name.begins_with("Emitter."))
                return "Invalid or duplicate emitter Grid3D declaration.";
            if (info.has("target")) {
                String target = info["target"];
                if (targets.has(target))
                    return "Multiple Grid3D fields publish to " + target;
                targets[target] = name;
            }
            grids[name] = info;
        }
    }
    Array all = emitter->get("stages");
    for (int i = 0; i < all.size(); ++i) {
        Ref<Resource> stage = all[i];
        if (!String(stage->get("stage_name")).begins_with("SimulationStage:"))
            continue;
        Dictionary settings = stage->get("simulation_settings");
        if (settings.is_empty())
            return "Simulation stage requires its source iteration settings. Reimport the effect.";
        if (!bool(settings.get("enabled", true)))
            continue;
        Dictionary args;
        Array pairs = settings.get("arguments", Array());
        for (int j = 0; j < pairs.size(); ++j) {
            Dictionary pair = pairs[j];
            String name = pair["name"];
            if (args.has(name) || (name != "DataInterface" && name != "ExecuteBehavior" &&
                                   name != "NumIterations" && name != "Iteration"))
                return "Unsupported simulation stage setting: " + name;
            args[name] = pair["value"];
        }
        String grid = word(args.get("DataInterface", Variant())), reader;
        if(readers.has(grid)) reader=readers[grid];
        if(!reader.is_empty()) {
            Array operations=stage->get("operations");
            for(int j=0;j<operations.size();++j) {
                Ref<Resource> op=operations[j];
                if(!bool(op->get("enabled"))) continue;
                Dictionary pins=module_inputs(op);
                if(String(op->get("module_name"))!=scatter_module || word(pins.get("ParticleReader",Variant()))!=word(args["DataInterface"]))
                    return "Unsupported particle-reader simulation stage operation.";
                grid=word(pins.get("Grid",Variant()));
            }
        }
        if (!grids.has(grid) || word(args.get("Iteration", "DataInterface")) != "DataInterface")
            return "Simulation stage requires a declared Grid3DCollection iteration source.";
        String behavior = word(args.get("ExecuteBehavior", "Always"));
        if (behavior != "Always" && behavior != "OnSimulationReset" && behavior != "NotOnSimulationReset")
            return "Unsupported Grid3D execution behavior.";
        Variant iterations = args.get("NumIterations", 1);
        if (iterations.get_type() != Variant::INT || int64_t(iterations) < 1 || int64_t(iterations) > 65535)
            return "Unsupported Grid3D iteration count.";
        Array ops = stage->get("operations"), active;
        Dictionary info = grids[grid];
        Array channels = info["channels"];
        Dictionary attributes = info["attributes"];
        for (int j = 0; j < ops.size(); ++j) {
            Ref<Resource> op = ops[j];
            if (!bool(op->get("enabled")))
                continue;
            String name = op->get("module_name");
            if(!reader.is_empty() && name==scatter_module) { active.append(op); continue; }
            if (name.begins_with("Transient.") && String(op->get("operation_kind")) == "assignment" &&
                (String(op->get("target_type")) == "float" || String(op->get("target_type")) == "Vector")) {
                active.append(op);
                continue;
            }
            if (gradient_module(op) || advection_module(op) || pressure_module(op)) {
                if (pressure_module(op)) {
                    String boundary = word(module_inputs(op).get("TransientGrid", Variant()));
                    if (!grids.has(boundary))
                        return "Pressure solve needs its declared boundary grid.";
                    Dictionary layout = grids[boundary];
                    layout["sampled"] = true;
                }
                if (advection_module(op)) {
                    String velocity = word(module_inputs(op).get("VelocityGrid", Variant()));
                    if (!grids.has(velocity))
                        return "Scalar advection requires a declared velocity grid.";
                    Dictionary velocity_layout = grids[velocity];
                    velocity_layout["sampled"] = true;
                    info["sampled"] = true;
                }
                info["neighbor_reads"] = true;
                active.append(op);
                continue;
            }
            String channel = name.substr(grid.length() + 1);
            if (String(op->get("operation_kind")) != "assignment" ||
                (String(op->get("target_type")) != "float" && String(op->get("target_type")) != "Vector") ||
                !name.begins_with(grid + ".") || !channel.is_valid_identifier())
                return "Unsupported Grid3D cell operation: " + name;
            int width = String(op->get("target_type")) == "Vector" ? 3 : 1;
            if (attributes.has(channel) && int(attributes[channel]) != width)
                return "Conflicting Grid3D attribute types: " + name;
            if (!attributes.has(channel)) {
                attributes[channel] = width;
                if (width == 1)
                    channels.append(channel);
                else
                    for (const char *suffix : {".x", ".y", ".z"})
                        channels.append(channel + suffix);
            }
            active.append(op);
        }
        if (active.is_empty())
            return "Empty Grid3D stages have no verified write contract.";
        Dictionary entry;
        entry["grid"] = grid;
        if(!reader.is_empty()) entry["reader"]=reader;
        entry["behavior"] = behavior;
        entry["iterations"] = iterations;
        entry["operations"] = active;
        stages.append(entry);
    }
    for (int i = 0; i < stages.size(); ++i) {
        Dictionary stage = stages[i];
        String grid = stage["grid"];
        Dictionary info = grids[grid];
        Array channels = info["channels"], ops = stage["operations"];
        Vector3i s = info["size"];
        int64_t count = int64_t(s.x) * s.y * s.z;
        bool published = info.has("target");
        if (stage.has("reader")) {
            if(ops.size()!=1 || int(stage["iterations"])!=1 || published || bool(info.get("sampled",false)) || bool(info.get("neighbor_reads",false)))
                return "Particle scatter requires one operation and a separately stored raster grid.";
            if(channels.is_empty() || count > INT32_MAX/(4*channels.size())) return "Invalid particle raster layout.";
            String error=scatter_source(info,module_inputs(ops[0]),indices,stage);
            if(!error.is_empty()) return error;
            compiled.append(stage);
            continue;
        }
        if (published && channels.size() != 1)
            return "Multi-attribute Grid3D render targets require the source channel tiling layout.";
        if (channels.is_empty())
            return "Grid3D stage has no declared output attribute.";
        bool neighbors = bool(info.get("neighbor_reads", false));
        if (neighbors && channels.size() != 1)
            return "Grid3D index-based gradient requires a verified scalar channel layout.";
        if (count > INT32_MAX / (4 * channels.size()))
            return "Grid3D storage exceeds the supported buffer range.";
        String body = "#version 450\nlayout(local_size_x=4,local_size_y=4,local_size_z=4) in;\n"
                      "layout(set=0,binding=0,std430) buffer Cells { float v[]; } buf;\n";
        if (neighbors) {
            body += "layout(set=0,binding=2,std430) writeonly buffer OutputCells { float v[]; } dst;\n";
            body += "float read_cell(ivec3 p,ivec3 "
                    "size){if(any(lessThan(p,ivec3(0)))||any(greaterThanEqual(p,size))) return 0.0;"
                    "return buf.v[p.x+size.x*(p.y+size.y*p.z)];}\n";
        }
        if (published)
            body += "layout(set=0,binding=1,r32f) uniform writeonly image3D target;\n";
        bool sampled = bool(info.get("sampled", false));
        if (sampled) {
            bool vector = channels.size() == 3;
            if (!vector && channels.size() != 1)
                return "3D sampling requires a scalar or RGB velocity layout.";
            String format = vector ? (bool(info["half"]) ? "rgba16f" : "rgba32f")
                                   : (bool(info["half"]) ? "r16f" : "r32f");
            body += "layout(set=0,binding=5," + format + ") uniform writeonly image3D sample_out;\n";
        }
        String external;
        Array frame_parameters;
        uint32_t positive_parameters = 0;
        for (int j = 0; j < ops.size(); ++j) {
            Ref<Resource> op = ops[j];
            if (pressure_module(op)) {
                Dictionary pins = module_inputs(op);
                String boundary = word(pins.get("TransientGrid", Variant())),
                       sim = word(pins.get("SimGrid", Variant()));
                if (boundary == grid || !grids.has(boundary) || !grids.has(sim) ||
                    Vector3i(Dictionary(grids[sim])["size"]) != s ||
                    Vector3i(Dictionary(grids[boundary])["size"]) != s ||
                    Array(Dictionary(grids[boundary])["channels"]).size() != 1)
                    return "Pressure solve requires matching topology and one boundary channel at index "
                           "zero.";
                if (!external.is_empty())
                    return "Multiple external field bindings in one pressure pass are unsupported.";
                external = boundary;
                stage["external"] = external;
                body += pressure_kernel;
                continue;
            }
            if (!advection_module(op))
                continue;
            Dictionary pins = module_inputs(op);
            String velocity = word(pins.get("VelocityGrid", Variant()));
            if (velocity == grid || !grids.has(velocity) || (!external.is_empty() && external != velocity))
                return "Scalar advection requires a separate, declared velocity grid for this stage.";
            if (external.is_empty()) {
                external = velocity;
                Dictionary layout = grids[velocity], attrs = layout["attributes"];
                if (attrs.size() != 1 || int(attrs.values()[0]) != 3)
                    return "Velocity-index advection requires a verified three-component velocity layout.";
                body += "layout(set=0,binding=3) uniform sampler3D flow_volume;\n"
                        "layout(set=0,binding=4) uniform sampler3D previous_density;\n";
                stage["external"] = external;
                stage["sample_previous_density"] = true;
            }
        }
        if (bool(info["half"])) {
            // D3D12 typed half-float volume stores truncate toward zero on the verified source GPU.
            body += "float store_half(float v){ uint h=packHalf2x16(vec2(v,0))&65535u;"
                    " if(abs(unpackHalf2x16(h).x)>abs(v)) h-=1u; return unpackHalf2x16(h).x; }\n";
        }
        body += "void main(){\n";
        body += "uvec3 size=uvec3(" + String::num_int64(s.x) + "," + String::num_int64(s.y) + "," +
                String::num_int64(s.z) + ");\n";
        body += "uvec3 p=gl_GlobalInvocationID; if(any(greaterThanEqual(p,size)))return; uint "
                "cell=p.x+size.x*(p.y+size.y*p.z);\n";
        for (int c = 0; c < channels.size(); ++c)
            body +=
                "float c" + String::num_int64(c) + "=buf.v[cell+" + String::num_int64(c * count) + "u];\n";
        Dictionary outputs;
        for (int j = 0; j < ops.size(); ++j) {
            Ref<Resource> op = ops[j];
            String name = op->get("module_name");
            if (name.begins_with("Transient.") && String(op->get("operation_kind")) == "assignment") {
                bool vector = String(op->get("target_type")) == "Vector";
                Variant v = input(op);
                String value = vector ? grid_vector(v, outputs) : expression(v, channels, grid, outputs);
                if (!vector && value.is_empty())
                    value = transport_scalar(v, frame_parameters);
                if (value.is_empty())
                    return "Unsupported Grid3D transient input: " + name;
                String local = (vector ? String("vector_") : String("scalar_")) + String::num_int64(j);
                body += (vector ? String("vec3 ") : String("float ")) + local + "=" + value + ";\n";
                outputs[name] = local;
                continue;
            }
            if (pressure_module(op)) {
                Dictionary pins = module_inputs(op);
                if (pins.size() != 10 || word(pins.get("PressureGrid", Variant())) != grid ||
                    pins.get("UseSolidVelocity", Variant()).get_type() != Variant::BOOL ||
                    bool(pins["UseSolidVelocity"]) ||
                    pins.get("PressureIndex", Variant()).get_type() != Variant::INT ||
                    int(pins["PressureIndex"]) != 0 ||
                    pins.get("BoundaryIndex", Variant()).get_type() != Variant::INT ||
                    int(pins["BoundaryIndex"]) != 0)
                    return "Pressure solve requires scalar pressure and stationary solid boundaries.";
                for (const char *v : {"Divergence", "Pressure", "Boundary", "Velocity"}) {
                    String key = "Transient." + String(v);
                    if (!outputs.has(key) ||
                        (String(outputs[key]).begins_with("vector_") != (String(v) == "Velocity")))
                        return "Missing or incorrectly typed pressure input: " + key;
                }
                String dx = transport_scalar(pins.get("dx", Variant()), frame_parameters, &positive_parameters),
                       dt = transport_scalar(pins.get("dt", Variant()), frame_parameters, &positive_parameters),
                       relax = transport_scalar(pins.get("Relaxation", Variant()), frame_parameters);
                if (dx.is_empty() || dt.is_empty() || relax.is_empty() ||
                    (scalar(pins["dt"]) && double(pins["dt"]) <= 0) ||
                    (scalar(pins["dx"]) && double(pins["dx"]) <= 0))
                    return "Pressure solve needs positive timestep and cell spacing.";
                int k = frame_parameters.size();
                frame_parameters.append("@iteration");
                stage["iteration_slot"] = k;
                String iter =
                    "int(frame.values[" + String::num_int64(k / 4) + "][" + String::num_int64(k % 4) + "])";
                String local = "scalar_pressure" + String::num_int64(j), alias = op->get("alias_name");
                if (alias.is_empty())
                    alias = "Grid3D_PressureIteration";
                outputs["Output." + alias + ".Pressure"] = local;
                body += "float " + local + "=solve_pressure(ivec3(p),ivec3(size)," + dx + "," + dt + "," +
                        String(outputs["Transient.Divergence"]) + "," + iter + "," +
                        String(outputs["Transient.Pressure"]) + "," + String(outputs["Transient.Boundary"]) +
                        "," + String(outputs["Transient.Velocity"]) + "," + relax + ");\n";
                continue;
            }
            if (advection_module(op)) {
                Dictionary pins = module_inputs(op);
                if (pins.size() != 10 || word(pins.get("AdvectionMethod", Variant())) != "SemiLagranian" ||
                    word(pins.get("InterpolationType", Variant())) != "Linear" ||
                    word(pins.get("AdvectedGrid", Variant())) != grid ||
                    pins.get("UseStackContextVelocity", Variant()).get_type() != Variant::BOOL ||
                    bool(pins["UseStackContextVelocity"]) ||
                    pins.get("VelocityIndex", Variant()).get_type() != Variant::INT ||
                    int(pins["VelocityIndex"]) != 0 ||
                    pins.get("ScalarIndex", Variant()).get_type() != Variant::INT ||
                    int(pins["ScalarIndex"]) != 0)
                    return "Unsupported scalar advection method, channel or velocity binding.";
                String dx = transport_scalar(pins.get("dx", Variant()), frame_parameters, &positive_parameters),
                       dt = transport_scalar(pins.get("dt", Variant()), frame_parameters),
                       mult =
                           transport_scalar(pins.get("AdvectionVelocityMult", Variant()), frame_parameters);
                if (dx.is_empty() || dt.is_empty() || mult.is_empty() ||
                    (scalar(pins["dx"]) && double(pins["dx"]) <= 0))
                    return "Advection requires a positive cell size and scalar step and velocity multiplier.";
                String suffix = String::num_int64(j), velocity_pos = "vp" + suffix, velocity = "vel" + suffix,
                       symbol = "scalar_" + suffix;
                String alias = op->get("alias_name");
                if (alias.is_empty())
                    alias = "Grid3D_AdvectScalar";
                outputs["Output." + alias + ".AdvectedScalar"] = symbol;
                body += "vec3 " + velocity_pos + "=(vec3(p)+0.5)/vec3(size);\n";
                body += "vec3 " + velocity + "=textureLod(flow_volume," + velocity_pos + ",0.0).rgb;\n";
                body += "float " + symbol + "=textureLod(previous_density,(vec3(p)-(" + dt + "/" + dx + "*" +
                        mult + ")*" + velocity + "+0.5)/vec3(size),0.0).r;\n";
                continue;
            }
            if (gradient_module(op)) {
                Dictionary pins = module_inputs(op);
                Variant dx = pins.get("dx", Variant()), index = pins.get("ScalarIndex", Variant());
                if (pins.size() != 4 || pins.get("VectorLength", Variant()).get_type() != Variant::BOOL ||
                    bool(pins["VectorLength"]) || word(pins.get("Grid", Variant())) != grid ||
                    index.get_type() != Variant::INT || int(index) != 0 || !scalar(dx) || double(dx) <= 0.0)
                    return "Scalar grid gradient requires this iteration grid, channel zero, and positive "
                           "cell spacing.";
                String alias = op->get("alias_name");
                if (alias.is_empty())
                    alias = "Grid3D_ComputeGradient";
                String symbol = "grad" + String::num_int64(j);
                outputs["Output." + alias + ".Grad"] = symbol;
                PackedStringArray components;
                for (const char *axis : {"1,0,0", "0,1,0", "0,0,1"}) {
                    String offset = "ivec3(" + String(axis) + ")";
                    components.append("read_cell(ivec3(p)+" + offset + ",ivec3(size))-read_cell(ivec3(p)-" +
                                      offset + ",ivec3(size))");
                }
                body += "vec3 " + symbol + "=vec3(" + String(",").join(components) + ")/(2.0*" +
                        String::num_real(double(dx), true) + ");\n";
                continue;
            }
            String channel = String(op->get("module_name")).substr(grid.length() + 1);
            if (String(op->get("target_type")) == "Vector") {
                String v = grid_vector(input(op), outputs);
                if (v.is_empty())
                    return "Unsupported Grid3D vector value for " + channel;
                int first = channels.find(channel + ".x");
                for (int axis = 0; axis < 3; ++axis)
                    body += "c" + String::num_int64(first + axis) + "=(" + v + ")[" +
                            String::num_int64(axis) + "];\n";
                continue;
            }
            String value = expression(input(op), channels, grid, outputs);
            if (value.is_empty())
                return "Unsupported Grid3D value for " + grid + "." + channel;
            body += "c" + String::num_int64(channels.find(channel)) + "=" + value + ";\n";
        }
        for (int c = 0; c < channels.size(); ++c) {
            String value = "c" + String::num_int64(c);
            if (bool(info["half"]))
                value = "store_half(" + value + ")";
            body += String(neighbors ? "dst.v[cell+" : "buf.v[cell+") + String::num_int64(c * count) +
                    "u]=" + value + ";\n";
            if (published)
                body += "imageStore(target,ivec3(p),vec4(" + value + ",0,0,1));\n";
        }
        if (sampled) {
            PackedStringArray samples;
            for (int c = 0; c < channels.size(); ++c) {
                String value = "c" + String::num_int64(c);
                samples.append(bool(info["half"]) ? "store_half(" + value + ")" : value);
            }
            while (samples.size() < 4)
                samples.append("0.0");
            body += "imageStore(sample_out,ivec3(p),vec4(" + String(",").join(samples) + "));\n";
        }
        stage.erase("operations");
        if (!frame_parameters.is_empty()) {
            if (frame_parameters.size() > 32)
                return "Grid3D stage exceeds its device-independent push constant budget.";
            int count = (frame_parameters.size() + 3) / 4;
            body = body.replace("void main(){", "layout(push_constant,std430) uniform Frame{vec4 values[" +
                                                    String::num_int64(count) + "]; } frame;\nvoid main(){");
            stage["parameters"] = frame_parameters;
            stage["positive_parameters"] = int64_t(positive_parameters);
        }
        stage["source"] = body + "}\n";
        compiled.append(stage);
    }
    Array keys = grids.keys();
    for (int i = 0; i < keys.size(); ++i) {
        Variant key = keys[i];
        if (Array(Dictionary(grids[key])["channels"]).is_empty())
            return "Grid3D interface has no supported consuming stage: " + String(key);
    }
    if (!compiled.is_empty()) {
        result["fields"] = grids;
        result["stages"] = compiled;
    }
    return "";
}

String Execution::initialize(RenderingDevice *device, const Dictionary &program) {
    if (program.is_empty())
        return "";
    Dictionary layouts = program["fields"];
    Array names = layouts.keys(), passes = program["stages"];
    for (int i = 0; i < names.size(); ++i) {
        Dictionary layout = layouts[names[i]];
        Field field{names[i], layout["size"], layout["channels"], RID(), RID(), layout.get("target", ""), {}};
        int64_t bytes = int64_t(field.size.x) * field.size.y * field.size.z * field.channels.size() * 4;
        field.buffer = device->storage_buffer_create(bytes);
        if (!field.buffer.is_valid())
            return "Could not allocate a Grid3D field.";
        field.attributes = layout["attributes"];
        fields.push_back(field);
        if (bool(layout.get("neighbor_reads", false))) {
            fields.back().other = device->storage_buffer_create(bytes);
            if (!fields.back().other.is_valid())
                return "Could not allocate the previous-iteration Grid3D storage.";
        }
        if (device->buffer_clear(field.buffer, 0, bytes) != OK)
            return "Could not initialize Grid3D cells.";
        auto &stored = fields.back();
        if (bool(layout.get("sampled", false))) {
            if (!sampler.is_valid()) {
                Ref<RDSamplerState> state;
                state.instantiate();
                state->set_min_filter(RenderingDevice::SAMPLER_FILTER_LINEAR);
                state->set_mag_filter(RenderingDevice::SAMPLER_FILTER_LINEAR);
                state->set_repeat_u(RenderingDevice::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE);
                state->set_repeat_v(RenderingDevice::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE);
                state->set_repeat_w(RenderingDevice::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE);
                state->set_max_lod(0.0);
                sampler = device->sampler_create(state);
                if (!sampler.is_valid())
                    return "Could not create the grid sampler.";
            }
            bool half = layout["half"], vector = field.channels.size() == 3;
            auto format = vector ? (half ? RenderingDevice::DATA_FORMAT_R16G16B16A16_SFLOAT
                                         : RenderingDevice::DATA_FORMAT_R32G32B32A32_SFLOAT)
                                 : (half ? RenderingDevice::DATA_FORMAT_R16_SFLOAT
                                         : RenderingDevice::DATA_FORMAT_R32_SFLOAT);
            for (int index = 0; index < (stored.other.is_valid() ? 2 : 1); ++index) {
                stored.samples[index] = sample_image(device, field.size, format);
                if (!stored.samples[index].is_valid())
                    return "Could not allocate the filtered iteration volume.";
            }
        }
        if (!field.target.is_empty()) {
            stored.image = sample_image(device, field.size, RenderingDevice::DATA_FORMAT_R32_SFLOAT);
            if (!stored.image.is_valid())
                return "Could not create a published Grid3D volume.";
            stored.texture.instantiate();
            stored.texture->set_texture_rd_rid(stored.image);
        }
    }
    for (int i = 0; i < passes.size(); ++i) {
        Dictionary pass = passes[i];
        Stage stage{};
        stage.field = names.find(pass["grid"]);
        stage.behavior = pass["behavior"];
        stage.external = pass.has("external") ? names.find(pass["external"]) : -1;
        stage.iteration_slot = pass.get("iteration_slot", -1);
        stage.has_parameters = !Array(pass.get("parameters", Array())).is_empty();
        stage.iterations = int64_t(pass["iterations"]);
        stage.scatter = pass.has("scatter_pins");
        stage.clear_grid = bool(pass.get("clear_grid",false));
        stages.push_back(stage);
        auto &s = stages.back();
        Ref<RDShaderSource> source;
        source.instantiate();
        source->set_stage_source(RenderingDevice::SHADER_STAGE_COMPUTE, pass["source"]);
        Ref<RDShaderSPIRV> code = device->shader_compile_spirv_from_source(source);
        if (code.is_null())
            return "Missing Grid3D compute program.";
        String error = code->get_stage_compile_error(RenderingDevice::SHADER_STAGE_COMPUTE);
        if (!error.is_empty())
            return "Grid3D compute compilation: " + error;
        s.shader = device->shader_create_from_spirv(code);
        if (!s.shader.is_valid())
            return "Could not create Grid3D shader.";
        s.pipeline = device->compute_pipeline_create(s.shader);
        if (!s.pipeline.is_valid())
            return "Could not create Grid3D pipeline.";
        const Field &field = fields[s.field];
        int variants = field.other.is_valid() ? 2 : 1;
        if (s.external >= 0 && fields[s.external].other.is_valid())
            variants = 4;
        for (int swap = 0; swap < variants; ++swap) {
            TypedArray<RDUniform> uniforms;
            auto bind = [&](int slot, RenderingDevice::UniformType type, RID rid) {
                Ref<RDUniform> binding;
                binding.instantiate();
                binding->set_uniform_type(type);
                binding->set_binding(slot);
                binding->add_id(rid);
                uniforms.append(binding);
            };
            bind(0, RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER, (swap & 1) ? field.other : field.buffer);
            if (field.image.is_valid())
                bind(1, RenderingDevice::UNIFORM_TYPE_IMAGE, field.image);
            if (field.other.is_valid())
                bind(2, RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER,
                     (swap & 1) ? field.buffer : field.other);
            if (s.external >= 0) {
                const auto &other = fields[s.external];
                auto bind_sampler = [&](int slot, RID image) {
                    Ref<RDUniform> binding;
                    binding.instantiate();
                    binding->set_uniform_type(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE);
                    binding->set_binding(slot);
                    binding->add_id(sampler);
                    binding->add_id(image);
                    uniforms.append(binding);
                };
                bind_sampler(3, other.samples[(swap & 2) ? 1 : 0]);
                if (bool(pass.get("sample_previous_density", false)))
                    bind_sampler(4, field.samples[swap & 1]);
            }
            if (field.samples[0].is_valid())
                bind(5, RenderingDevice::UNIFORM_TYPE_IMAGE,
                     field.samples[field.other.is_valid() ? 1 - (swap & 1) : 0]);
            s.uniforms[swap] = device->uniform_set_create(uniforms, s.shader, 0);
            if (!s.uniforms[swap].is_valid())
                return "Could not bind Grid3D storage.";
        }
    }
    return "";
}
String Execution::step(RenderingDevice *device, const Array &parameters, bool local) {
    if (stages.empty())
        return "";
    int64_t list = -1;
    bool work=false;
    for (int index = 0; index < int(stages.size()); ++index) {
        Stage &stage = stages[index];
        if ((first_tick && stage.behavior == "NotOnSimulationReset") ||
            (!first_tick && stage.behavior == "OnSimulationReset"))
            continue;
        Field &field = fields[stage.field];
        if (stage.scatter) {
            if (list != -1) {device->compute_list_end();list=-1;}
            PackedByteArray rows=parameters[index];
            if(stage.clear_grid) {
                int64_t bytes=int64_t(field.size.x)*field.size.y*field.size.z*field.channels.size()*4;
                if(device->buffer_clear(field.buffer,0,bytes)!=OK) return "Could not clear particle raster.";
                work=true;
            }
            if(rows.is_empty()) continue;
            if(rows.size()>stage.particle_bytes || !stage.particle_set.is_valid()) {
                if(stage.particle_set.is_valid()) device->free_rid(stage.particle_set);
                if(stage.particle_buffer.is_valid()) device->free_rid(stage.particle_buffer);
                stage.particle_set=RID();
                stage.particle_bytes=std::min<int64_t>(UINT32_MAX, std::max<int64_t>(rows.size(),stage.particle_bytes + stage.particle_bytes/2));
                stage.particle_buffer=device->storage_buffer_create(stage.particle_bytes);
                if(!stage.particle_buffer.is_valid()) return "Could not allocate particle scatter input.";
                Ref<RDUniform> input;input.instantiate();
                input->set_binding(0);input->set_uniform_type(RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER);
                input->add_id(stage.particle_buffer);
                TypedArray<RDUniform> inputs;inputs.append(input);
                stage.particle_set=device->uniform_set_create(inputs,stage.shader,1);
                if(!stage.particle_set.is_valid()) return "Could not bind particle scatter input.";
            }
            if(device->buffer_update(stage.particle_buffer,0,rows.size(),rows)!=OK) return "Could not upload particle scatter input.";
            list=device->compute_list_begin();
            device->compute_list_bind_compute_pipeline(list,stage.pipeline);
            device->compute_list_bind_uniform_set(list,stage.uniforms[0],0);
            device->compute_list_bind_uniform_set(list,stage.particle_set,1);
            const int64_t limit = device->limit_get(RenderingDevice::LIMIT_MAX_COMPUTE_WORKGROUP_COUNT_X);
            PackedByteArray offset;
            offset.resize(4);
            const int64_t particles = rows.size() / 64;
            for (int64_t first = 0; first < particles; first += limit) {
                offset.encode_u32(0, first);
                device->compute_list_set_push_constant(list,offset,offset.size());
                device->compute_list_dispatch(list,std::min<int64_t>(particles-first,limit),1,1);
            }
            device->compute_list_add_barrier(list);
            work=true;
            continue;
        }
        if (list == -1)
            list = device->compute_list_begin();
        work=true;
        device->compute_list_bind_compute_pipeline(list, stage.pipeline);
        PackedByteArray constants;
        if (stage.has_parameters) {
            constants = parameters[index];
            if (stage.iteration_slot < 0)
                device->compute_list_set_push_constant(list, constants, constants.size());
        }
        Vector3i n = field.size;
        for (int j = 0; j < stage.iterations; ++j) {
            if (stage.iteration_slot >= 0) {
                constants.encode_float(stage.iteration_slot * 4, float(j));
                device->compute_list_set_push_constant(list, constants, constants.size());
            }
            device->compute_list_bind_uniform_set(
                list,
                stage.uniforms[int(field.swapped) +
                               ((stage.external >= 0 && fields[stage.external].swapped) ? 2 : 0)],
                0);
            if (field.other.is_valid())
                field.swapped = !field.swapped;
            device->compute_list_dispatch(list, (n.x + 3) / 4, (n.y + 3) / 4, (n.z + 3) / 4);
            device->compute_list_add_barrier(list);
        }
    }
    if (list != -1) device->compute_list_end();
    if (local && work) {
        device->submit();
        device->sync();
    }
    first_tick = false;
    return "";
}
Dictionary Execution::textures() const {
    Dictionary result;
    for (const auto &field : fields) {
        if (field.texture.is_valid()) {
            result[field.name] = field.texture;
            result[field.target] = field.texture;
        }
    }
    return result;
}
Dictionary Execution::snapshot(RenderingDevice *device) const {
    Dictionary result;
    for (const auto &field : fields) {
        PackedFloat32Array values =
            device->buffer_get_data(field.swapped ? field.other : field.buffer).to_float32_array();
        int64_t count = int64_t(field.size.x) * field.size.y * field.size.z;
        Dictionary channels;
        if (values.size() != count * field.channels.size())
            return Dictionary();
        Array names = field.attributes.keys();
        int offset = 0;
        for (int n = 0; n < names.size(); ++n) {
            int width = field.attributes[names[n]];
            if (width == 1)
                channels[names[n]] = values.slice(offset * count, (offset + 1) * count);
            else {
                PackedFloat32Array data;
                data.resize(count * width);
                for (int i = 0; i < count; ++i)
                    for (int c = 0; c < width; ++c)
                        data.set(i * width + c, values[(offset + c) * count + i]);
                channels[names[n]] = data;
            }
            offset += width;
        }
        Dictionary row;
        row["cells"] = field.size;
        row["attributes"] = channels;
        result[field.name] = row;
    }
    return result;
}
void Execution::clear(RenderingDevice *device) {
    for (const auto &stage : stages) {
        if (stage.particle_set.is_valid()) device->free_rid(stage.particle_set);
        if (stage.particle_buffer.is_valid()) device->free_rid(stage.particle_buffer);
        for (RID handle : {stage.uniforms[0], stage.uniforms[1], stage.uniforms[2], stage.uniforms[3],
                           stage.pipeline, stage.shader})
            if (handle.is_valid())
                device->free_rid(handle);
    }
    for (auto &field : fields) {
        if (field.texture.is_valid())
            field.texture->set_texture_rd_rid(RID());
        if (field.image.is_valid())
            device->free_rid(field.image);
        for (RID image : field.samples)
            if (image.is_valid())
                device->free_rid(image);
        if (field.other.is_valid())
            device->free_rid(field.other);
        device->free_rid(field.buffer);
    }
    if (sampler.is_valid()) {
        device->free_rid(sampler);
        sampler = RID();
    }
    stages.clear();
    fields.clear();
    first_tick = true;
}
} // namespace godot::nbgrid

namespace godot {
namespace {
void render_call(const Callable &call, bool wait) {
    RenderingServer *rs = RenderingServer::get_singleton();
    if (rs->is_on_render_thread()) {
        call.call();
        return;
    }
    rs->call_on_render_thread(call);
    if (wait)
        rs->force_sync();
}
} // namespace
void NBNativeGridVolumes::prepare(Ref<NBNativeGridVolumes> job, Dictionary program, Dictionary receipt) {
    auto *rd = RenderingServer::get_singleton()->get_rendering_device();
    receipt["error"] = job->execution.initialize(rd, program);
    receipt["textures"] = job->execution.textures();
}
String NBNativeGridVolumes::initialize(const Dictionary &program, Dictionary &textures) {
    Dictionary receipt;
    render_call(callable_mp_static(&NBNativeGridVolumes::prepare)
                    .bind(Ref<NBNativeGridVolumes>(this), program, receipt),
                true);
    textures = receipt.get("textures", Dictionary());
    return receipt.get("error", "Render thread did not initialize the Grid3D textures.");
}
void NBNativeGridVolumes::advance(Ref<NBNativeGridVolumes> job, Array parameters) {
    String error=job->execution.step(RenderingServer::get_singleton()->get_rendering_device(), parameters, false);
    ERR_FAIL_COND_MSG(!error.is_empty(),error);
}
void NBNativeGridVolumes::step(const Array &parameters) {
    render_call(
        callable_mp_static(&NBNativeGridVolumes::advance).bind(Ref<NBNativeGridVolumes>(this), parameters),
        false);
}
void NBNativeGridVolumes::read(Ref<NBNativeGridVolumes> job, Dictionary receipt) {
    receipt["values"] = job->execution.snapshot(RenderingServer::get_singleton()->get_rendering_device());
}
Dictionary NBNativeGridVolumes::snapshot() {
    Dictionary receipt;
    render_call(callable_mp_static(&NBNativeGridVolumes::read).bind(Ref<NBNativeGridVolumes>(this), receipt),
                true);
    return receipt.get("values", Dictionary());
}
void NBNativeGridVolumes::release(Ref<NBNativeGridVolumes> job) {
    job->execution.clear(RenderingServer::get_singleton()->get_rendering_device());
}
void NBNativeGridVolumes::dispose() {
    render_call(callable_mp_static(&NBNativeGridVolumes::release).bind(Ref<NBNativeGridVolumes>(this)),
                false);
}
} // namespace godot
