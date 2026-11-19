#include "grid_scatter.h"
#include "particle_view.h"
#include "value_evaluator.h"
#include <godot_cpp/classes/resource.hpp>
#include <cmath>

namespace godot::nbgrid {
namespace {
String ref_name(const Variant &v) {
    if (v.get_type() != Variant::DICTIONARY)
        return "";
    Dictionary d = v;
    return String(d.get("kind", "")) == "reference" ? String(d.get("name", "")) : String();
}
bool read_number(const Variant &input, const Dictionary &ctx, float &out) {
    if (input.get_type() == Variant::DICTIONARY) {
        Dictionary node = input;
        String kind = node.get("kind", "");
        if (kind == "reference")
            return read_number(ctx.get(node["name"], Variant()), Dictionary(), out);
        if (kind != "call" || String(node.get("name", "")).get_file() != "Multiply_Float")
            return false;
        float a, b;
        Dictionary args;
        Array inputs = node["arguments"];
        for (int i = 0; i < inputs.size(); ++i) {
            Dictionary pin = inputs[i];
            args[pin["name"]] = pin["value"];
        }
        if (args.size() != 2 || !read_number(args.get("A", Variant()), ctx, a) ||
            !read_number(args.get("B", Variant()), ctx, b))
            return false;
        out = a * b;
    } else {
        if (input.get_type() != Variant::FLOAT && input.get_type() != Variant::INT)
            return false;
        out = float(double(input));
    }
    return std::isfinite(out);
}
bool matrix_input(const Variant &pin, const Dictionary &ctx, Transform3D &matrix) {
    String key = ref_name(pin);
    if (key == "Engine.Owner.SystemWorldToLocal" || key == "Engine.Owner.SystemLocalToWorld") {
        Transform3D source = ctx["source_transform"];
        Basis basis;
        for (int i = 0; i < 3; ++i) {
            Vector3 axis;
            axis[i] = 1.f;
            basis.set_column(i, NBNativeValues::godot_to_unreal(
                                    source.basis.xform(NBNativeValues::unreal_to_godot(axis))));
        }
        matrix = Transform3D(basis, NBNativeValues::godot_to_unreal(source.origin));
        if (key == "Engine.Owner.SystemWorldToLocal") {
            if (matrix.basis.determinant() == 0.f)
                return false;
            matrix = matrix.affine_inverse();
        }
    } else {
        Variant value = ctx.get(key, Variant());
        if (value.get_type() != Variant::TRANSFORM3D)
            return false;
        matrix = value;
    }
    return matrix.is_finite();
}
} // namespace
String scatter_source(const Dictionary &layout, const Dictionary &pins, const Dictionary &indices,
                      Dictionary &stage) {
    Dictionary slots, attributes = layout["attributes"];
    Array channels = layout["channels"];
    for (const char *field : {"Density", "Temperature", "Velocity"}) {
        String name = field, flag = "Scatter" + name;
        if (pins.get(flag, Variant()).get_type() != Variant::BOOL)
            return "Scatter needs an explicit " + flag;
        if (!bool(pins[flag])) {
            slots[name] = -1;
            continue;
        }
        Variant index = pins.get(name + "Index", Variant());
        const bool vector = name == "Velocity";
        int slot = -1;
        if (index.get_type() == Variant::INT) {
            int64_t n = index;
            if (n >= 0 && n < channels.size())
                slot = int(n);
        } else if (indices.has(ref_name(index))) {
            Dictionary selected = indices[ref_name(index)];
            if (selected["grid"] == pins["Grid"])
                slot = channels.find(String(selected["name"]) + (vector ? ".x" : ""));
        }
        if (slot < 0 || slot >= channels.size())
            return "Unresolved raster channel for " + name;
        String attr = String(channels[slot]).trim_suffix(".x");
        if (int(attributes.get(attr, 0)) != (vector ? 3 : 1) ||
            (vector && channels[slot] != Variant(attr + ".x")))
            return "Invalid raster channel width for " + name;
        slots[name] = slot;
    }
    if (int(slots["Density"]) == int(slots["Temperature"]) && int(slots["Density"]) >= 0)
        return "Overlapping density and temperature raster slots.";
    for (const char *key : {"UseRadius", "LocalSpaceParticles", "UseFalloff", "ScaleEmissionbydt"})
        if (pins.get(key, Variant()).get_type() != Variant::BOOL)
            return String("Missing scatter switch ") + key;
    if (!bool(pins["UseRadius"]))
        return "Particle scatter without source radius is unsupported.";
    Array keys = pins.keys();
    for (int i = 0; i < keys.size(); ++i) {
        String key = keys[i];
        if (key.begins_with("Scatterfluids_source_") && bool(pins[key]))
            return "Custom fluid scatter channels are unsupported.";
    }
    stage["scatter_pins"] = pins;
    stage["size"] = layout["size"];
    stage["clear_grid"] = layout.get("clear_before_scatter", true);
    const Vector3i n = layout["size"];
    String shader = "#version 450\nlayout(local_size_x=64) in;\n"
                    "layout(set=0,binding=0,std430) buffer Raster { uint v[]; } dst;\n"
                    "struct Particle {vec4 location;vec4 radii;vec4 emission;vec4 velocity;};\n"
                    "layout(set=1,binding=0,std430) readonly buffer Input {Particle particles[];} src;\n"
                    "layout(push_constant,std430) uniform Draw {uint first;} draw;\n";
    shader += "const ivec3 size=ivec3(" + String::num_int64(n.x) + "," + String::num_int64(n.y) + "," +
              String::num_int64(n.z) + ");\n";
    shader += R"GLSL(
void splat(vec3 center, float radius, vec3 value, int channel, int width) {
    if(channel<0 || radius<0.0) return;
    // Clip the integer offset range before looping, including truncation toward zero at the left face.
    vec3 lo=max(-vec3(ceil(radius)),floor(-center-1.0)+1.0);
    vec3 hi=min(vec3(ceil(radius)),ceil(vec3(size)-center)-1.0);
    if(any(greaterThan(lo,hi))) return;
    ivec3 first=ivec3(lo), extent=ivec3(hi)-first+1;
    int plane=extent.x*extent.y;
    for(int i=int(gl_LocalInvocationIndex);i<plane*extent.z;i+=64){
        ivec3 offset=first+ivec3(i%extent.x,(i/extent.x)%extent.y,i/plane);
        ivec3 p=ivec3(center+vec3(offset));
        if(length(vec3(offset))>radius || any(lessThan(p,ivec3(0))) || any(greaterThanEqual(p,size))) continue;
        float weight=1.0;
)GLSL";
    if (bool(pins["UseFalloff"]))
        shader +=
            "if(radius>0.0) weight=1.0-smoothstep(max(0.0,radius-2.41421),radius,length(center-vec3(p)));\n";
    shader += "for(int c=0;c<width;c++){float v=value[c]*weight;\n";
    if (bool(layout["half"]))
        shader += "uint "
                  "h=packHalf2x16(vec2(v,0))&65535u;if(abs(unpackHalf2x16(h).x)>abs(v))h--;v=unpackHalf2x16("
                  "h).x;\n";
    shader += R"GLSL(
            atomicExchange(dst.v[p.x+size.x*(p.y+size.y*(p.z+size.z*(channel+c)))],floatBitsToUint(v));
        }
    }
}
void main(){Particle p=src.particles[draw.first + gl_WorkGroupID.x];
)GLSL";
    shader +=
        "splat(p.location.xyz,p.radii.x,vec3(p.emission.x)," + String::num_int64(slots["Density"]) + ",1);\n";
    shader += "splat(p.location.xyz,p.radii.y,vec3(p.emission.y)," + String::num_int64(slots["Temperature"]) +
              ",1);\n";
    shader +=
        "splat(p.location.xyz,p.radii.z,p.velocity.xyz," + String::num_int64(slots["Velocity"]) + ",3);\n}";
    stage["source"] = shader;
    return "";
}

String scatter_particles(const Dictionary &stage, const Array &states, const Dictionary &ctx,
                         PackedByteArray &packet) {
    String source = stage["reader"];
    Dictionary state;
    for (int i = 0; i < states.size(); ++i) {
        Dictionary current = states[i];
        Ref<Resource> emitter = Dictionary(current["plan"])["source"];
        if (String(emitter->get("display_name")) == source) {
            state = current;
            break;
        }
    }
    if (state.is_empty())
        return "Missing particle reader source: " + source;
    auto view = NBNativeParticleView::for_emitter(state);
    if (view.is_null() || !view->get_errors().is_empty())
        return "Invalid particle reader source: " + source;
    if (view->get_count() == 0)
        return "";
    Dictionary pins = stage["scatter_pins"];
    float dx, dt = 1, spread[3]{}, scale[3]{};
    if (!read_number(pins.get("dx", Variant()), ctx, dx) || dx <= 0 ||
        (bool(pins["ScaleEmissionbydt"]) && !read_number(pins.get("dt", Variant()), ctx, dt)))
        return "Invalid scatter cell spacing or timestep.";
    Transform3D world_to_unit, world_to_local;
    if (!matrix_input(pins.get("WorldToUnit", Variant()), ctx, world_to_unit) ||
        (bool(pins["ScatterVelocity"]) &&
         !matrix_input(pins.get("WorldToLocal", Variant()), ctx, world_to_local)))
        return "Unresolved scatter grid transform.";
    const char *fields[] = {"Density", "Temperature", "Velocity"};
    for (int i = 0; i < 3; ++i) {
        String key = fields[i];
        if (bool(pins["Scatter" + key]) &&
            (!read_number(pins.get("SplatSize" + key, Variant()), ctx, spread[i]) ||
             !read_number(pins.get(key + "Mult", Variant()), ctx, scale[i])))
            return "Missing scatter scale for " + key;
    }
    int64_t pos = view->field_handle("position");
    const int64_t radius = view->attribute_handle("Particles.fluids_source_radius");
    const int64_t density = view->attribute_handle("Particles.fluids_source_density");
    const int64_t temperature = view->attribute_handle("Particles.fluids_source_temperature");
    const int64_t velocity = view->attribute_handle("Particles.fluids_source_velocity");
    Dictionary scope = ctx.duplicate();
    scope["local_space"] = Dictionary(state["plan"])["local_space"];
    PackedFloat32Array words;
    if (view->get_count() > UINT32_MAX / 64 ||
        words.resize(view->get_count() * 16) != OK)
        return "Could not allocate the particle raster input.";
    float *out = words.ptrw();
    Vector3 dims = Vector3(Vector3i(stage["size"]));
    for (int64_t i = 0; i < view->get_count(); ++i, out += 16) {
        Vector3 point = view->read(pos, i);
        if (bool(pins["LocalSpaceParticles"]))
            point = NBNativeValues::transform_between(point, "Simulation", "World", scope, false, true);
        Vector3 cell = world_to_unit.xform(point) * dims - Vector3(.5, .5, .5);
        float r = double(view->attribute_or(radius, i, 0.0)) / dx;
        Vector3 v =
            world_to_local.basis.xform(Vector3(view->attribute_or(velocity, i, Vector3())) * scale[2]);
        if (!cell.is_finite() || !std::isfinite(r) || !v.is_finite())
            return "Nonfinite fluid particle.";
        out[0] = cell.x;
        out[1] = cell.y;
        out[2] = cell.z;
        for (int j = 0; j < 3; ++j)
            out[4 + j] = r * spread[j];
        out[8] = float(double(view->attribute_or(density, i, 0.0))) * scale[0] * dt;
        out[9] = float(double(view->attribute_or(temperature, i, 0.0))) * scale[1] * dt;
        out[12] = v.x;
        out[13] = v.y;
        out[14] = v.z;
    }
    packet = words.to_byte_array();
    return "";
}
} // namespace godot::nbgrid
