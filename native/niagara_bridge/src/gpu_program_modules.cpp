#include "gpu_program.h"
#include "fluid_source_attributes.h"
#include "effect_compiler.h"
namespace godot::nbgpu {
using nbcompiler::named;
using nbcompiler::reference;
String Builder::module() {
    if (nbcompiler::equal(operation->get("operation_kind"), String("assignment")))
        return assignment();
    String name = operation->get("module_name");
    if (reader_modules.has(operation->get("stable_id"))) {
        Dictionary reader = reader_modules[operation->get("stable_id")], column;
        column["source"] = reader["source"];
        column["attribute"] = reader["attribute"];
        column["has_attribute"] = reader["has_attribute"];
        if (reader.has("fixed_index")) column["fixed_index"] = reader["fixed_index"];
        if (reader_columns.is_empty())
            helpers.append("void nb_read_vector(int col,int index,out bool valid,out vec3 value){\n"
                "  ivec3 h=floatBitsToInt(params.data[NB_READER_BASE+col].xyz);\n"
                "  valid=index>=h.z && index-h.z<h.y; value=vec3(0.0);\n"
                "  if(valid) value=params.data[NB_READER_BASE+h.x+(index-h.z)].xyz;\n}");
        int slot = reader_columns.find(column);
        if (slot < 0) { slot = reader_columns.size(); reader_columns.append(column); }
        Dictionary pins = reader["pins"];
        Array keys = pins.keys();
        String code = String(reader["code"]).replace("NB_COLUMN",integer(slot));
        for (int i = 0; i < keys.size(); ++i) {
            String key = keys[i];
            code = code.replace("NB_INPUT_" + key + " ", "(" + input(key,Variant(),pins[key]) + ") ");
        }
        return code;
    }
    if (name == nbfluid::source_module) {
        Variant channel = operation->call("get_input", "WritetoDataChannel(Experimental)", false, 0);
        if (channel.get_type() != Variant::BOOL || bool(channel))
            return unsupported("fluid source data channel publication");
        String code;
        // Snapshot the inputs before writing any attributes so links to the old map remain valid.
        for (const auto &field : nbfluid::source_fields) {
            if (!attribute_rows.has(field.name))
                return unsupported(String("missing fluid attribute ") + field.name);
            if (!field.pin) continue;
            if (Variant(operation->call("get_input", field.pin, Variant(), 0)).get_type() == Variant::NIL)
                return unsupported(String("missing fluid source input ") + field.pin);
            String value = input(field.pin, Variant(), field.type);
            if (String(field.pin) == "Velocity")
                value = "(" + value + ")*" + input("VelocityScale", 1.0);
            code += cat({field.type, " fluid_", field.pin, "=", value, ";\n"});
        }
        for (const auto &field : nbfluid::source_fields) {
            String slot = Dictionary(attribute_rows[field.name])["slot"].stringify();
            String target = "p.attributes[" + slot + "]";
            if (!field.pin) {
                String radius = Dictionary(attribute_rows["Particles.fluids_source_radius"])["slot"].stringify();
                code += target + "=p.attributes[" + radius + "];\n";
            } else {
                String mask = String(field.type) == "float" ? ".x" : (String(field.type) == "vec3" ? ".xyz" : "");
                code += target + mask + "=fluid_" + field.pin + ";\n";
            }
        }
        return code;
    }
    if (name == "V2/InitializeParticle")
        return initialize();
    if (name == "TorusLocation")
        return torus();
    if (name == "BoxLocation") {
        String alias = operation->get("alias_name");
        String prefix = "Output." + (alias.is_empty() ? name : alias) + ".";
        String s = space("CoordinateSpace", "Local");
        String code = box_point() + cat({"vec3 a=abs(point),n=point;",
            "if(a.x>a.y&&a.x>a.z)n.yz=vec2(0);if(a.y>a.x&&a.y>a.z)n.xz=vec2(0);if(a.z>a.x&&a.z>a.y)n.xy=vec2(0);",
            "if(dot(n,n)>0.0)n=normalize(n);"});
        if (flag("RotateBox", false))
            code += cat({"vec3 axis=", input("RotateBoxOnAxis", Array::make(0,0,1), "vec3"),
                ";float angle=", input("NormalizedBoxRotationAngle", 0.0), "*6.28318530718;",
                "if(dot(axis,axis)>0.0){axis=normalize(axis);float c=cos(angle),s=sin(angle);",
                "point=point*c+cross(axis,point)*s+axis*dot(axis,point)*(1.0-c);",
                "n=n*c+cross(axis,n)*s+axis*dot(axis,n)*(1.0-c);}"});
        String mask = flag("MaskSpawnGroup", false)
                          ? cat({"if(p.spawn_group==", input("SpawnGroupMask", 0, "int"), ")"}) : String();
        return code + cat({"vec3 v=nb_transform(point+", input("BoxOffset", Array::make(0,0,0), "vec3"), ",",
            s, ",true,p,spawning);", mask, "p.position=", input("BoxOrigin", reference("Particles.Position"), "vec3"), "+v;",
            module_outputs[prefix+"BoxVector"], "=v;", module_outputs[prefix+"BoxNormal"], "=nb_transform(n,", s, ",false,p,spawning);"});
    }
    if (name == "KillParticlesInVolume")
        return kill_volume();
    if (name == "WindForce@1.0") {
        String alias = operation->get("alias_name");
        String output = module_outputs["Output." + (alias.is_empty() ? String("WindForce") : alias) + ".IsAffectedByWind"];
        return cat({"vec3 w=", input("WindSpeed", Array::make(100,0,0), "vec3"), "*",input("WindSpeedScale", 1.0),
            ";float wl=length(w),vl=length(p.velocity);vec3 wd=wl<1e-5?vec3(0,0,1e-6):w/wl;",
            "vec3 vd=vl<1e-5?vec3(0,0,1e-6):p.velocity/vl;",output,"=(wl<1e-5?1e-6:wl)>dot(wd,vd)*(vl<1e-5?1e-6:vl);",
            "if(",output,")p.force+=w*clamp(1.0-",input("AirResistance", 0.2),",0.0,1.0);"});
    }
    if (name == "ShapeLocation")
        return shape();
    if (name == "V2/SphereLocation")
        return sphere();
    if (name == "AddVelocity@1.0")
        return cat({"vec3 v=", input("Velocity", Array::make(0, 0, 0), "vec3"), "*",
                    input("ScaleAddedVelocity", Array::make(1, 1, 1), "vec3"),
                    "; p.velocity+=nb_transform(v,", space("CoordinateSpace", "Local"),
                    ",false,p,spawning);"});
    if (name == "AddVelocityFromPoint")
        return point_velocity();
    if (name == "AddVelocity")
        return velocity();
    if (name == "ParticleState@1.0" || name == "ParticleState@1.1")
        return particle_state();
    if (name == "GravityForce")
        return cat({"p.force+=nb_transform(", input("Gravity", Array::make(0, 0, -980), "vec3"), ",",
                    space("CoordinateSpace", "World"), ",false,p,spawning)*p.mass;"});
    if (named(name, {"V2/CurlNoiseForce@1.0", "V2/CurlNoiseForce@1.1", "V2/CurlNoiseForce@1.2"}))
        return curl();
    if (name == "VortexForce" || name == "VortexForce@1.1")
        return vortex();
    if (name == "LineAttractionForce" || name == "LineAttractionForce@1.0")
        return line_force();
    if (name == "CameraOffset")
        return cat({"p.camera_offset=", input("CameraOffsetAmount", 0.0), ";"});
    if (name == "KillParticles")
        return cat({"if(", input("KillParticles", true, "bool"), ") p.alive=0.0;"});
    if (name == "SpriteRotationRate")
        return cat({"p.rotation=radians(degrees(p.rotation)+",
                    input("DeltaTime", reference("Engine.DeltaTime")), "*", input("RotationRate", 1.0),
                    ");"});
    if (named(name, {"Drag@1.0", "Drag", "Drag@1.1"})) {
        String code = flag("UseLinearDrag", true) ? cat({"p.drag+=", input("Drag", 0.0), ";\n"}) : String();
        if (name != "Drag@1.0")
            code += cat({"p.ignore_mass=", flag("IgnoreMass", true) ? "true" : "false", ";\n"});
        if (flag("UseRotationalDrag", false))
            code += cat({"p.rotational_drag+=", input("RotationalDrag", 1.0), "*100.0;\n"});
        if (name != "Drag@1.0")
            code += "p.rotational_drag*=0.01;";
        return code;
    }
    if (name == "Color") {
        String alpha =
            attribute_types.has("Emitter.AlphaScale") &&
                    Dictionary(attribute_layout.get("declarations", Dictionary())).has("Emitter.AlphaScale")
                ? expression(reference("Emitter.AlphaScale"), "float", "")
                : String("1.0");
        return cat({"p.color=", input("Color", Array::make(1, 1, 1, 1), "vec4"), "*vec4(",
                    input("ScaleColor", Array::make(1, 1, 1), "vec3"), ",", input("ScaleAlpha", 1.0), "*",
                    alpha, ");"});
    }
    if (name == "ScaleColor@1.0") {
        if (mode("ScaleMode", "RGBandAlphaSeparately") != "RGBandAlphaSeparately")
            return unsupported("ScaleColor mode");
        String code = cat(
            {"vec4 color=", input("ColorValueToScale", reference("Particles.Initial.Color"), "vec4"), ";\n"});
        if (flag("ScaleRGB", true))
            code += cat({"color.rgb*=", input("ScaleRGB", Array::make(1, 1, 1), "vec3", 1), ";\n"});
        if (flag("ScaleA", true))
            code += cat({"color.a*=", input("ScaleAlpha", 1.0), ";\n"});
        return code + "p.color=color;";
    }
    if (name == "ScaleSpriteSize") {
        String fn =
            curve(source_curve(cat({operation->get("stable_id"), "/UniformCurveSpriteScale/0"}),
                               operation->call("get_input", "UniformCurveSpriteScale", Dictionary())));
        return cat(
            {"p.size_factor*=", fn, "(", input("UniformCurveIndex", reference("Particles.NormalizedAge")),
             ")*", input("UniformCurveScale", 1.0),
             "; p.size=", input("InitialSpriteSize", reference("Particles.Initial.SpriteSize"), "vec2"),
             "*p.size_factor;"});
    }
    if (name == "ScaleSpriteSize@1.0")
        return cat({"p.size_factor*=", input("ScaleFactor", Array::make(1, 1), "vec2"), "; p.size=",
                    input("InitialSpriteSize", reference("Particles.Initial.SpriteSize"), "vec2"),
                    "*p.size_factor;"});
    if (name == "ScaleMeshSize")
        return cat(
            {"p.mesh_scale_factor*=", input("ScaleFactor", Array::make(1, 1, 1), "vec3"),
             "; p.mesh_scale=", input("InitialMeshScale", reference("Particles.Initial.Scale"), "vec3"),
             "*p.mesh_scale_factor;"});
    if (named(name, {"V3/InitialMeshOrientation@1.0", "V3/InitialMeshOrientation", "V3/UpdateMeshOrientation",
                     "V3/UpdateMeshOrientation@1.0"}))
        return orientation();
    if (name == "ScaleSpriteSizeBySpeed") {
        String code =
            cat({"float threshold=max(", input("VelocityThreshold", 1.0), ",0.0001); float speed=length(",
                 input("SourceVelocity", reference("Particles.Velocity"), "vec3"),
                 "); if(speed<=0.00001) speed=0.0; float a=clamp(speed,0.0001,threshold)/threshold;\n"});
        if (flag("SampleScaleFactorByCurve", false))
            code += cat({"a=",
                         curve(source_curve(cat({operation->get("stable_id"), "/ScaleFactorCurve/0"}),
                                            operation->call("get_input", "ScaleFactorCurve", Dictionary()))),
                         "(a);\n"});
        return code + cat({"p.size_factor*=mix(", input("MinScaleFactor", Array::make(1, 1), "vec2"), ",",
                           input("MaxScaleFactor", Array::make(1, 1), "vec2"), ",clamp(a,0.0,1.0)); p.size=",
                           input("InitialSpriteSize", reference("Particles.Initial.SpriteSize"), "vec2"),
                           "*p.size_factor;"});
    }
    if (name == "SolveForcesAndVelocity") {
        String code = cat({"float solver_dt=", input("DeltaTime", reference("Engine.DeltaTime")),
                           "; vec3 position=", input("Position", reference("Particles.Position"), "vec3"),
                           "; vec3 velocity=", input("Velocity", reference("Particles.Velocity"), "vec3"),
                           "; vec3 force=", input("Force", reference("Transient.PhysicsForce"), "vec3"),
                           "; float inverse_mass=1.0/max(", input("Mass", reference("Particles.Mass")),
                           ",0.000001);\n"});
        if (flag("WritetoPresolveProperties", true))
            code += "p.presolve_position=position; p.presolve_velocity=velocity; p.presolve_force=force;\n";
        return code + "p.previous_velocity=velocity; vec3 forced=velocity+force*inverse_mass*solver_dt; "
                      "float d=max(p.ignore_mass?p.drag:p.drag*inverse_mass,0.0); "
                      "p.velocity=(forced-p.advection)/(1.0+d*solver_dt)+p.advection; "
                      "p.position=position+p.velocity*solver_dt; p.force=vec3(0); p.drag=0.0;";
    }
    if (name == "V2/SubUVAnimation@1.0")
        return cat({"float phase=", input("SubUVLookupIndex", reference("Particles.NormalizedAge")), "*",
                    input("SubUVLoopCount", 1.0), "; phase=phase-trunc(phase); p.subimage=mix(",
                    input("StartFrame", 0.0), ",", input("EndFrame", 63.0), ",phase);"});
    if (name == "DynamicMaterialParameters@1.0" || name == "DynamicMaterialParameters@1.1") {
        String code;
        for (int index = 0; index < 4; ++index) {
            if (!flag("WriteParameterIndex" + integer(index), index == 0))
                continue;
            String suffix = index == 0 ? String() : integer(index);
            if (index == 1 && name.ends_with("@1.0"))
                code += "p.dynamic_value1=p.dynamic_value;\n";
            for (int c = 0; c < 4; ++c)
                if (flag(cat({"Param", integer(c), "WriteEnabled", suffix}), true))
                    code += cat({"p.dynamic_value", suffix, "[", integer(c), "]=",
                                 input(cat({"Index", integer(index), "Param", integer(c + 1)}), 0.0), ";\n"});
        }
        return code;
    }
    return unsupported("module");
}
} // namespace godot::nbgpu
