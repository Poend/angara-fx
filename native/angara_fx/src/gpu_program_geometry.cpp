#include "gpu_program.h"
#include "effect_compiler.h"

namespace godot::nbgpu {
using nbcompiler::reference;
String Builder::torus() {
    String shader = contracts["torus_source"];
    if (!helpers.has(shader))
        helpers.append(shader);
    String radius, u, v;
    if (mode("TorusDistributionMode", "Random") == "Direct") {
        radius = input("RadiusPosition", 1.0);
        u = input("UPosition", 0.0);
        v = input("VPosition", 0.0);
    } else {
        Array values;
        for (Array row :
             {Array::make("TorusRadius", "SurfacePosition", "999.0"),
              Array::make("TorusU", "UDistribution", "1.0"), Array::make("TorusV", "VDistribution", "1.0")}) {
            String minimum = cat({"clamp(", input(row[1], 0.0), ",0.0,", row[2], ")"});
            values.append(source_rng.is_empty()
                              ? cat({"nb_range(p,", minimum, ",1.0)"})
                              : native_range(cat({operation->get("stable_id"), "/$", row[0]}),
                                             {minimum, "1.0"}, "float"));
        }
        radius = values[0];
        u = values[1];
        v = values[2];
    }
    String shape = mode("TorusMode", "Torus");
    int m = shape == "Ring" ? 1 : shape == "TorusKnot" ? 2 : 0;
    String code =
        cat({"float radial=", radius, ";float u=", u, ";float v=", v, ";vec3 position,tangent,normal;\n"});
    code += cat({"nb_torus_geometry(", integer(m), ",radial,u,v,", input("LargeRadius", 100.0), ",",
                 input("HandleRadius", 25.0), ",", input("KnotHelixLoops", 2), ",", input("KnotRingLoops", 5),
                 ",", flag("EnableTorusKnotRadius", false) ? "true" : "false", ",",
                 input("TorusKnotTubeRadius", 10.0), ",position,tangent,normal);\n"});
    if (flag("EnableScale", false))
        code += cat({"vec3 scale=", input("NonUniformScale", Array::make(1, 1, 1), "vec3"),
                     ";position*=scale;tangent=normalize(tangent*scale);normal=normalize(normal*scale);\n"});
    if (flag("AlignTorusWithAxis", false))
        code += cat({"nb_torus_orient(", input("TorusAxis", Array::make(0, 0, 1), "vec3"), ",",
                     input("NormalizedAngleAroundTorusAxis", 0.0), ",position,tangent,normal);\n"});
    String s = space("CoordinateSpace", "Local");
    code += cat({"position=nb_transform(position,", s, ",true,p,spawning);tangent=nb_transform(tangent,", s,
                 ",false,p,spawning);normal=nb_transform(normal,", s, ",false,p,spawning);\n"});
    if (flag("MaskSpawnGroup", false)) {

        Variant fallback =
            attribute_types.has("Emitter.SpawnGroup") ? Variant(reference("Emitter.SpawnGroup")) : Variant(0);
        code += cat({"if(", input("SpawnGroupMask", fallback, "int"), "==p.spawn_group) "});
    }
    code += cat({"p.position=position+(", input("Offset", Array::make(0, 0, 0), "vec3"), "+",
                 input("TorusOrigin", reference("Particles.Position"), "vec3"), ");\n"});
    String alias = operation->get("alias_name");
    if (alias.is_empty())
        alias = String(operation->get("module_name")).get_file();
    for (Array row :
         {Array::make("TorusVector", "position"), Array::make("TorusTangent", "tangent"),
          Array::make("TorusNormal", "normal"), Array::make("TorusBitangent", "cross(normal,tangent)")})
        code += cat({module_outputs[cat({"Output.", alias, ".", row[0]})], "=", row[1], ";\n"});
    return code;
}
String Builder::vortex() {
    String code =
        cat({"vec3 origin=", input("VortexOrigin", reference("Engine.Emitter.SimulationPosition"), "vec3"),
             ";\n"});
    if (flag("UseOriginOffset", false))
        code += cat({"origin+=nb_transform(", input("VortexOriginOffset", Array::make(0, 0, 0), "vec3"), ",",
                     space("VortexOriginOffsetCoordinateSpace", "Simulation"), ",false,p,spawning);\n"});
    code +=
        cat({"vec3 delta=origin-", input("VortexInfluencePosition", reference("Particles.Position"), "vec3"),
             "; float distance=length(delta); vec3 direction=dot(delta,delta)<1e-10 ? vec3(0,0,1) : "
             "normalize(delta); if(dot(delta,delta)<1e-10) distance=0.0;\n"});
    code +=
        cat({"vec3 axis=", input("VortexAxis", Array::make(0, 0, 1), "vec3"),
             "; axis=dot(axis,axis)<1e-10 ? vec3(0,0,1) : normalize(axis);axis=nb_transform(axis,",
             space("VortexAxisCoordinateSpace", "Simulation"), ",false,p,spawning);float influence=1.0;\n"});
    if (flag("UseInfluenceFalloff", false))
        code += cat({"float radius=", input("InfluenceFalloffRadius", 100.0),
                     ";float range_value=min(distance,radius)/max(radius,0.0001); if(",
                     input("InvertInfluenceFalloff", true, "bool"),
                     ") range_value=1.0-range_value;influence=pow(range_value,",
                     input("InfluenceFalloffExponent", 1.0), ");\n"});
    code += "vec3 pull=direction;\n";
    if (flag("OriginPullOnlyOnVortexAxis", true))
        code += "pull-=axis*dot(direction,axis);\n";
    code += cat({"vec3 force=(cross(direction,axis)*", input("VortexForceAmount", 200.0), "+pull*",
                 input("OriginPullAmount", 0.0), ")*influence;\n"});
    if (flag("WritetoIntrinsicParameters", true))
        code += "p.force+=force;\n";
    String alias = operation->get("alias_name");
    if (alias.is_empty())
        alias = "VortexForce";
    return code + cat({module_outputs[cat({"Output.", alias, ".VortexForce"})], "=force;\n"});
}
String Builder::line_force() {
    String code = cat({"vec3 start=", input("LineStart", Array::make(100, 0, 0), "vec3"),
                       ";vec3 end=", input("LineEnd", Array::make(-100, 0, 0), "vec3"), ";\n"});
    if (flag("LineSegmentinLocalspace", false))
        code += "start=nb_transform_between(start,1,0,false,true,p,spawning);end=nb_transform_between(end,1,"
                "0,false,true,p,spawning);\n";
    code += "vec3 segment=start-end;float along=dot(p.position-end,segment);vec3 "
            "closest=along>=dot(segment,segment) ? start : end;if(along>0.0 && along<dot(segment,segment)) "
            "closest=end+segment*(along/dot(segment,segment));vec3 delta=p.position-closest;\n";
    if (flag("UseAttractionFalloff", false))
        code += cat({"float distance=dot(delta,delta)<1e-10 ? 0.0 : length(delta);float radius=",
                     input("AttractionFalloff", 100.0),
                     ";float strength=min(distance,radius)/max(radius,0.0001);\n"});
    else
        code += cat({"float strength=", input("AttractionStrength", 20.0), ";\n"});
    return code + "vec3 biased=delta+vec3(0,0,0.0001);p.force-=normalize(biased)*strength;\n";
}
String Builder::mesh_facing() {
    Array slots;
    for (const char *name : {"previous", "average"}) {
        String path = cat({operation->get("stable_id"), "/$mesh_facing/", name});
        if (!cache_slots.has(path)) {
            Dictionary slot;
            slot["index"] = cache_slots.size();
            slot["type"] = "vec4";
            slot["purpose"] = "orientation_state";
            cache_slots[path] = slot;
        }
        slots.append(Dictionary(cache_slots[path])["index"]);
    }
    if (int64_t(slots[1]) >= cache_limit)
        return unsupported("persistent state exceeds 16 shared cache slots");
    String prev = cat({"p.cache[", slots[0].stringify(), "]"}),
           average = cat({"p.cache[", slots[1].stringify(), "].xyz"}),
           mask = cat({"(1u<<", slots[0].stringify(), "u)"});
    String code = cat({"vec3 origin=", input("FacingOrigin", Array::make(0, 0, 0), "vec3"),
                       "; vec3 target=", input("FacingPosition", Array::make(0, 0, 0), "vec3"), ";\n"});
    if (flag("TransformFacingOrigin", false))
        code += cat({"origin=nb_transform_between(origin,",
                     space("FacingOriginCoordinateSpace", "Simulation"), ",0,false,true,p,spawning);\n"});
    if (flag("TransformFacingPosition", false))
        code += cat({"target=nb_transform_between(target,",
                     space("FacingPositionCoordinateSpace", "Simulation"), ",0,false,true,p,spawning);\n"});
    code += cat({"vec3 difference=target-origin; vec3 facing=nb_safe_direction(difference); vec4 "
                 "previous=(p.cache_mask&",
                 mask, ")!=0u?", prev, ":vec4(0,0,0,1);\n"});
    if (flag("FacingInterpolation", false))
        code += cat({"if(", input("FirstFrame", reference("Transient.FirstFrame"), "bool"), ") ", average,
                     "=facing;\n", average, "=mix(", average, ",facing,clamp(",
                     input("FacingInterpolationRate", 1.0), "*dt,0.0,1.0)); facing=", average, ";\n"});
    code += cat({"vec3 side=", input("SideDirection", Array::make(0, 1, 0), "vec3"),
                 "; vec3 up=", input("UpDirection", Array::make(0, 0, 1), "vec3"), ";\n"});
    if (flag("TransformSide", false))
        code += cat(
            {"side=nb_transform(side,", space("SideCoordinateSpace", "Simulation"), ",false,p,spawning);\n"});
    if (flag("TransformUp", false))
        code +=
            cat({"up=nb_transform(up,", space("UpCoordinateSpace", "Simulation"), ",false,p,spawning);\n"});
    Array modes = Array::make("Facing", "Facing/Side", "Facing/Up", "Facing/Side/Up");
    int m = modes.find(mode("StabilizationMode", "Facing"));
    code +=
        cat({"vec4 q_pre=dot(difference,difference)<0.0000000001?vec4(0,0,0,1):nb_stabilize(facing,side,up,",
             integer(m), ",", input("ReferenceAxisVector", Array::make(1, 0, 0), "vec3"),
             "); vec4 incoming=p.mesh_orientation;\n"});
    if (flag("OverrideQuat", false))
        code += cat({"vec4 override_value=", input("OverrideQuaternion", Array::make(0, 0, 0, 1), "vec4"),
                     "; float alpha=", input("OverrideIncomingOrientation", 0.0),
                     "; "
                     "incoming=normalize((1.0-alpha)*incoming+(dot(incoming,override_value)>0.0?alpha:-alpha)"
                     "*override_value);\n"});
    return code + cat({"p.mesh_orientation=normalize(nb_quat_mul(q_pre,nb_quat_mul(vec4(previous.xyz,-"
                       "previous.w),incoming))); ",
                       prev, "=q_pre; p.cache_mask|=", mask, ";"});
}
} // namespace godot::nbgpu
