#include "gpu_program.h"
#include "effect_compiler.h"
namespace godot::nbgpu {
using nbcompiler::reference;
String Builder::box_point() {
    String code = cat({"vec3 size=", input("BoxSize", Array::make(100,100,100), "vec3"), ";\n"});
    if (!flag("SurfaceOnly", false))
        return code + cat({"vec3 point=", intrinsic("BoxPoint/0", {"vec3(0)", "vec3(1)"}, "vec3", 1), "*size-size*0.5;\n"});
    String expansion = mode("SurfaceExpansionMode", "Inside");
    String multiplier = expansion == "Outside" ? "2.0" : expansion == "Centered" ? "1.0" : "0.0";
    return code + cat({"float band=", input("SurfaceOnlyBandThickness", 0.0), ";size+=vec3(", multiplier, "*band);\n",
        "float t=min(max(band,0.0),0.5*max(size.x,max(size.y,size.z)));\n",
        "vec4 draw=", intrinsic("BoxShellPoint/0", {"vec4(0)", "vec4(1)"}, "vec4", 1), ";float side=roundEven(draw.x);\n",
        "float iz=size.z-2.0*t,iy=size.y-2.0*t,top=size.x*size.y,sides=top+size.x*iz;\n",
        "float area=", intrinsic("BoxShellFace/0", {"0.0","1.0"}, "float", 2), "*(sides+iy*iz);vec3 offset,extent;\n",
        "if(area<top){offset=vec3(0,0,side*(size.z-t));extent=vec3(size.xy,t);}\n",
        "else if(area<sides){offset=vec3(0,side*(size.y-t),t);extent=vec3(size.x,t,iz);}\n",
        "else{offset=vec3(side*(size.x-t),t,t);extent=vec3(t,iy,iz);}\n",
        "vec3 point=offset+extent*draw.yzw-size*0.5;\n"});
}

String Builder::kill_volume() {
    String shape = mode("KillShape", "Sphere");
    String code = cat({"vec3 center=", input("VolumeOrigin", reference("Engine.Emitter.SimulationPosition"), "vec3"),
        "+nb_transform(", input("OriginOffset", Array::make(0,0,0), "vec3"), ",1,false,p,spawning);\n",
        "vec3 rel=", input("Position", reference("Particles.Position"), "vec3"), "-center;\n"});
    if (shape == "Box")
        code += cat({"vec3 local=nb_transform_between(rel,0,1,false,false,p,spawning);\n",
            "bool inside=all(lessThan(abs(local),0.5*abs(", input("BoxSize", Array::make(100,100,100), "vec3"), ")));\n"});
    else if (shape == "Plane")
        code += cat({"bool inside=dot(rel,", input("PlaneNormal", Array::make(0,0,1), "vec3"), ")<0.0;\n"});
    else if (shape == "Slab")
        code += cat({"vec3 extent=", input("SlabAxis", Array::make(0,0,1), "vec3"), "*(0.5*", input("SlabWidth", 100.0),
            ");bool inside=abs(dot(rel,extent))<dot(extent,extent);\n"});
    else if (shape == "Sphere")
        code += cat({"float dist=length(rel);bool inside=(dist<1e-5?1.0:dist)<", input("SphereRadius", 100.0), ";\n"});
    else
        code += cat({"vec3 axis=", input("ConeAxis", Array::make(1,0,0), "vec3"), ";if(dot(axis,axis)>0.0)axis=normalize(axis);\n",
            "float dist=length(rel);vec3 dir=dist<1e-5?axis:-rel/dist;bool inside=(dist<1e-5?length(axis):dist)<",
            input("ConeLength", 200.0), "&&dot(dir,axis)>cos(radians(0.5*", input("ConeAngle", 45.0), "));\n"});
    return code + cat({"if(", input("KillVolumeEnabled", true, "bool"), "&&(inside!=",
        input("InvertVolume", false, "bool"), "))p.alive=0.0;"});
}
String Builder::shape() {
    String code = cat(
               {"vec3 shape_scale=", input("NonUniformScale", Array::make(1, 1, 1), "vec3"), "; vec3 v;\n"}),
           id = operation->get("stable_id");
    if (mode("ShapePrimitive", "Sphere") == "Sphere") {
        code += cat({"float radius=max(abs(", input("SphereRadius", 100.0),
                     "),0.00001); float distribution=max(", input("SphereSurfaceDistribution", 0.0),
                     ",0.0); float radial=distribution==1.0?1.0:pow(nb_range(p,distribution,1.0),0.333333); "
                     "float c=nb_range(p,-1.0,1.0); float phi=nb_range(p,0.0,6.283185307179586); float "
                     "s=sqrt(max(1.0-c*c,0.0)); v=vec3(cos(phi)*s,sin(phi)*s,c)*radius*radial;\n"});
        if (!source_rng.is_empty())
            code += cat({"float radial_sample=",
                         native_range(id + "/$ShapeSphere/0", {"distribution", "1.0"}, "float"),
                         "; radial=distribution==1.0?1.0:pow(max(radial_sample,0.0),0.333333); float "
                         "theta=nb_ue_gpu_acos(",
                         native_range(id + "/$ShapeSphere/1", {"-1.0", "1.0"}, "float"),
                         "); c=cos(theta); s=sin(theta); phi=",
                         native_range(id + "/$ShapeSphere/2", {"0.0", "6.283185307179586"}, "float"),
                         "; v=(vec3(cos(phi)*s,sin(phi)*s,c)*radial)*radius;\n"});
    } else {
        code += cat({"float radius=", input("CylinderRadius", 100.0),
                     "; float height=", input("CylinderHeight", 100.0),
                     "; vec3 random_point=vec3(nb_random(p),nb_random(p),nb_random(p)); float "
                     "band_random=nb_random(p); bool top=nb_random(p)<0.5; float "
                     "surface_random=nb_range(p,0.0,2.0); bool cap=false;\n"});
        if (!source_rng.is_empty())
            code += cat(
                {"random_point=", native_range(id + "/$CylinderPoint", {"vec3(0)", "vec3(1)"}, "vec3"),
                 "; band_random=", native_range(id + "/$CylinderBand", {"0.0", "1.0"}, "float"),
                 "; top=", native_range(id + "/$CylinderTop", {"0.0", "1.0"}, "float", true),
                 "!=0.0; surface_random=", native_range(id + "/$CylinderSurface", {"0.0", "2.0"}, "float"),
                 ";\n"});
        if (flag("UseEndcapsInSurfaceOnlyMode", true))
            code += "vec3 scale=abs(shape_scale); float "
                    "lateral=3.141592653589793*max(radius,0.0)*(scale.x+scale.y)*max(height,0.0)*scale.z; "
                    "float caps=6.283185307179586*max(radius,0.0)*max(radius,0.0)*scale.x*scale.y; "
                    "cap=lateral>=caps&&lateral>0.0?caps/lateral>surface_random:(caps>0.0?lateral/"
                    "caps<surface_random:false);\n";
        code += cat({"float radial=cap?sqrt(random_point.x):1.0; float "
                     "z=cap?(top?floor(random_point.z):ceil(random_point.z)):random_point.z; "
                     "v=vec3(cos(random_point.y*6.283185307179586)*radial,sin(random_point.y*6."
                     "283185307179586)*radial,z); v=(v-vec3(0,0,0.5))*(1.0-band_random*clamp(",
                     input("SurfaceOnlyBandThickness", 0.0), ",0.0,1.0))+vec3(0,0,0.5); v.z-=",
                     input("CylinderHeightMidpoint", .5), "; v*=vec3(radius,radius,height);\n"});
    }
    code += "vec3 shape_vector=nb_transform(v*shape_scale,1,true,p,spawning);\n";
    if (mode("OffsetMode", "None") == "Default")
        code += cat({"shape_vector+=nb_transform(", input("Offset", Array::make(0, 0, 0), "vec3"), ",",
                     space("OffsetCoordinateSpace", "Local"), ",false,p,spawning);\n"});
    return code + cat({"p.position=", input("ShapeOrigin", reference("Particles.Position"), "vec3"), "+shape_vector;"});
}
String Builder::sphere() {
    String code = cat({"float radius=", input("SphereRadius", 100.0), ";float inner=0.0;float outer=1.0;\n"});
    if (flag("SurfaceOnly", true))
        code += cat({"float thickness=", input("SurfaceOnlyBandThickness", 0.0), ";if(!(",
                     input("NormalizeThickness", true, "bool"),
                     ")){thickness/=radius;}precise float raw_inner=(1.0-thickness)+thickness*0.5*",
                     mode("SurfaceExpansionMode", "Inside") == "Outside" ? "2.0" : "0.0",
                     ";inner=max(raw_inner,0.0);outer=raw_inner+thickness;\n"});
    code += "float lower_cube=pow(max(inner,0.0),3.0);float upper_cube=pow(max(outer,0.0),3.0);\n";
    if (source_rng.is_empty())
        code += "float phi=nb_range(p,0.0,6.283185307179586);float c=nb_range(p,-1.0,1.0);float "
                "r=pow(max(nb_range(p,lower_cube,upper_cube),0.0),0.333333);\n";
    else
        code += cat({"vec3 sphere_random=",
                     native_range(cat({operation->get("stable_id"), "/$Sphere/0"}),
                                  {"vec3(0,-1,lower_cube)", "vec3(6.283185307179586,1,upper_cube)"}, "vec3"),
                     ";float phi=sphere_random.x;float c=sphere_random.y;float "
                     "r=pow(max(0.0,sphere_random.z),0.333333);\n"});
    code += "vec3 v=vec3(cos(phi)*sqrt(max(1.0-c*c,0.0)),sin(phi)*sqrt(max(1.0-c*c,0.0)),c)*r;\n";
    Array axes = Array::make("HemisphereX", "HemisphereY", "HemisphereZ");
    for (int i = 0; i < 3; ++i)
        if (flag(axes[i], false))
            code += cat({"v[", integer(i), "]=abs(v[", integer(i), "]);\n"});
    code += "v*=radius;\n";
    if (flag("UseNonUniformScale", false))
        code += cat({"v*=", input("NonUniformScale", Array::make(1, 1, 1), "vec3"), ";\n"});
    code += cat({"v=nb_transform(v,", space("SphereCoordinateSpace", "Local"),
                 ",true,p,spawning);\nvec3 offset=vec3(0.0);\n"});
    if (flag("UseOffset", false))
        code += cat({"offset=nb_transform(", input("Offset", Array::make(0, 0, 0), "vec3"), ",",
                     space("OffsetCoordinateSpace", "Local"), ",true,p,spawning);\n"});
    return code +
           cat({"p.position=", input("SphereOrigin", reference("Particles.Position"), "vec3"), "+offset+v;"});
}
String Builder::point_velocity() {
    String sample = "vec2(nb_range(p,0.0,6.28319),nb_range(p,-1.0,1.0))";
    if (!source_rng.is_empty())
        sample = native_range(cat({operation->get("stable_id"), "/$PointDirection/0"}),
                              {"vec2(0,-1)", "vec2(6.28319,1)"}, "vec2");
    String code =
        cat({"vec3 origin=", input("VelocityOrigin", reference("Engine.Emitter.SimulationPosition"), "vec3"),
             "+nb_transform(", input("OriginOffset", Array::make(0, 0, 0), "vec3"), ",",
             space("OriginOffsetCoordinateSpace", "Local"), ",false,p,spawning);\n"});
    code += cat({"vec3 v=", input("DefaultPosition", reference("Particles.Position"), "vec3"),
                 "-origin; vec2 random_direction=", sample, ";\n"});
    if (source_rng.is_empty())
        code += "nb_random(p);\n";
    code += "float distance=0.0; if(dot(v,v)<1e-10){float "
            "s=sqrt(max(0.0,1.0-random_direction.y*random_direction.y));v=vec3(cos(random_direction.x)*s,sin("
            "random_direction.x)*s,random_direction.y);}else{float "
            "reciprocal_length=inversesqrt(dot(v,v));v*=reciprocal_length;distance=1.0/reciprocal_length;}\n";
    code += cat({"float falloff_distance=", input("VelocityFalloffDistance", 100.0),
                 ";float normalized=min(distance,falloff_distance)/max(falloff_distance,0.0001);if(",
                 input("InvertVelocityFalloff", true, "bool"), "){normalized=1.0-normalized;}\n"});
    return code + cat({"float strength=", input("VelocityStrength", 100.0), ";if(",
                       input("UseFalloffDistance", false, "bool"),
                       "){strength*=normalized;}p.velocity+=v*strength;"});
}
String Builder::velocity() {
    String value;
    if (mode("VelocityMode", "Linear") == "InCone") {
        value = cat({"nb_cone(p,", input("ConeAngle", 0.0), ",", input("InnerConeAngle", 0.0), ",",
                     input("ConeAxis", Array::make(0, 0, 1), "vec3"), ",", input("VelocitySpeed", 0.0), ")"});
        if (!source_rng.is_empty()) {
            String cosine = native_range(
                cat({operation->get("stable_id"), "/$Cone/2"}),
                {cat({"cos(radians(", input("ConeAngle", 0.0), "*0.5))"}),
                 cat({"cos((1.0-", input("InnerConeAngle", 0.0), "*0.5/360.0)*6.283185307179586)"})},
                "float");
            String azimuth = native_range(cat({operation->get("stable_id"), "/$Cone/3"}),
                                          {"0.0", "6.283185307179586"}, "float");
            value =
                cat({"nb_ue_cone(", cosine, ",", azimuth, ",",
                     input("ConeAxis", Array::make(0, 0, 1), "vec3"), ",", input("VelocitySpeed", 0.0), ")"});
        }
    } else
        value = cat({"(", input("Velocity", Array::make(0, 0, 0), "vec3"), "*",
                     input("VelocitySpeedScale", 1.0), ")"});
    return cat({"vec3 v=", value, "; p.velocity+=nb_transform(v,", space("RotationCoordinateSpace", "Local"),
                ",false,p,spawning);"});
}
} // namespace godot::nbgpu
