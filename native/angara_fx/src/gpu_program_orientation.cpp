#include "gpu_program.h"
#include "effect_compiler.h"
namespace godot::nbgpu {
using nbcompiler::reference;
String Builder::particle_state() {
    String code = cat({"p.first_frame=p.age<=dt; bool first_frame=p.first_frame; float lifetime=",
                       input("Lifetime", reference("Particles.Lifetime")),
                       "; float safe_lifetime=max(lifetime,0.00001); p.age+=",
                       input("DeltaTime", reference("Engine.DeltaTime")), ";\n"});
    if (flag("KillParticlesWhenLifetimeHasElapsed", true)) {
        if (nbcompiler::equal(operation->get("module_name"), String("ParticleState@1.0")))
            code += "if(!(p.age<safe_lifetime-0.0001)) p.alive=0.0; p.normalized_age=p.age/safe_lifetime;";
        else
            code += "if(!((first_frame&&lifetime>0.000001)||p.age<safe_lifetime)) p.alive=0.0; "
                    "p.normalized_age=clamp(p.age/safe_lifetime,0.0,1.0);";
    } else {
        String inactive = "false";
        if (flag("LetInfinitelyLivedParticlesDieWhenEmitterDeactivates", false))
            inactive = cat({"(", expression(reference("Emitter.ExecutionState"), "int", ""), "!=0 || ",
                            expression(reference("System.ExecutionState"), "int", ""), "!=0)"});
        code += cat({"bool die_on_inactive=", inactive, ";\n"});
        if (flag("LoopParticlesLifetime", false))
            code += "if(!die_on_inactive) { precise float "
                    "loop_age=p.age-safe_lifetime*trunc(p.age/safe_lifetime); p.age=loop_age; }\n";
        code += "if(die_on_inactive && p.age>safe_lifetime-0.0001) p.alive=0.0;\n";
        code += "p.normalized_age=p.age/safe_lifetime;";
    }
    return code;
}
String Builder::orientation() {
    String module = operation->get("module_name");
    if (module == "V3/InitialMeshOrientation@1.0" || module == "V3/InitialMeshOrientation") {
        bool mesh = mode("RotationCoordinateSpace", "Mesh") == "Mesh";
        String code = "vec4 rotation=p.mesh_orientation;\n";
        if (flag("UseRotationVector", false))
            code += cat({"rotation=nb_rotation_vector(nb_transform(",
                         input("Rotation", Array::make(0, 0, 0), "vec3"), ",", orientation_space(),
                         ",false,p,spawning),1.0,", mesh ? "p.mesh_orientation" : "vec4(0,0,0,1)", ");\n"});
        if (module.ends_with("@1.0")) {
            if (flag("UseOrientationVector", true)) {
                String s = space("OrientationCoordinateSpace", "Simulation");
                code += cat({"vec3 axis=nb_transform(nb_safe_direction(",
                             input("OrientationAxis", Array::make(1, 0, 0), "vec3"), "),", s,
                             ",false,p,spawning);vec3 direction=nb_transform(",
                             input("OrientationVector", Array::make(1, 0, 0), "vec3"), ",", s,
                             ",false,p,spawning);direction=dot(direction,direction)<1e-10?axis:normalize("
                             "direction);vec4 orientation=nb_find_between_raw(axis,direction);\n"});
                code += cat({"rotation=nb_quat_mul(", mesh ? "orientation,rotation" : "rotation,orientation",
                             ");\n"});
            }
            return code + "p.mesh_orientation=rotation;p.initial_rotation=rotation;";
        }
        String m = mode("MeshOrientationMode", "Random");
        if (m != "None") {
            if (m == "Random")
                code += cat({"vec4 orientation=nb_random_quaternion(",
                             intrinsic("Orientation", {"vec3(0)", "vec3(1)"}, "vec3"), ");\n"});
            else
                code += cat({"vec4 orientation=nb_find_between(",
                             input("MeshAxisToOrient", Array::make(1, 0, 0), "vec3"),
                             ",nb_transform(nb_safe_direction(",
                             input("OrientationVector", Array::make(1, 0, 0), "vec3"), "),",
                             space("OrientationCoordinateSpace", "Simulation"), ",false,p,spawning));\n"});
            code += cat(
                {"rotation=nb_quat_mul(", mesh ? "orientation,rotation" : "rotation,orientation", ");\n"});
        }
        return code + "p.mesh_orientation=rotation; p.initial_rotation=rotation;";
    }
    if (module == "V3/UpdateMeshOrientation" &&
        mode("OrientationMethod", "RotationRate") == "OrienttoPosition")
        return mesh_facing();
    bool mesh = mode("RotationCoordinateSpace", "Mesh") == "Mesh";
    String code = cat({"vec3 rv=nb_transform(", input("RotationVector", Array::make(1, 0, 0), "vec3"), "*",
                       input("RotationRate", 1.0), ",", orientation_space(),
                       ",false,p,spawning); vec4 rotation=nb_rotation_vector(rv,",
                       input("DeltaTime", reference("Engine.DeltaTime")), ",",
                       mesh ? "vec4(0,0,0,1)" : "p.mesh_orientation", ");\n"});
    String result = mesh ? "nb_quat_mul(p.mesh_orientation,rotation)" : "rotation";
    return code +
           cat({"p.mesh_orientation=", module.ends_with("@1.0") ? result : cat({"normalize(", result, ")"}),
                ";"});
}
} // namespace godot::nbgpu
