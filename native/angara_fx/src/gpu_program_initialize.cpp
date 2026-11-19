#include "gpu_program.h"
#include "effect_compiler.h"

namespace godot::nbgpu {
using nbcompiler::named;
String Builder::initialize() {
    if (!named(mode("ColorMode", "DirectSet"), {"DirectSet", "Unset", "RandomHue/Saturation/Value"}) ||
        mode("PositionMode", "SimulationPosition") != "SimulationPosition")
        return unsupported("initial color/position mode");
    String code, lifetime = mode("LifetimeMode", "DirectSet");
    if (lifetime == "Random")
        code += cat({"p.lifetime=",
                     intrinsic("Lifetime", {input("LifetimeMin", 1.0), input("LifetimeMax", 1.0)}, "float"),
                     ";\n"});
    else if (lifetime == "DirectSet")
        code += cat({"p.lifetime=", input("Lifetime", 1.0), ";\n"});
    else
        return unsupported("lifetime mode");
    String color = mode("ColorMode", "DirectSet");
    if (color == "DirectSet")
        code += cat({"p.color=", input("Color", Array::make(1, 1, 1, 1), "vec4"), ";\n"});
    else if (color == "RandomHue/Saturation/Value") {
        Array adjustments = Array::make("0.0", "1.0", "1.0", "1.0"),
              components = Array::make("Hue", "Saturation", "Value", "Alpha");
        for (int i = 0; i < 4; ++i) {
            String component = components[i];
            if (flag("Adjust" + component, i == 0)) {
                String key = component == "Hue"     ? "HueShiftRange"
                             : component == "Alpha" ? "AlphaScaleRange"
                                                    : component + "Range",
                       range = "color_range_" + integer(i);
                code += cat({"vec2 ", range, "=",
                             input(key, i == 0 ? Array::make(-.1, .1) : Array::make(.8, 1), "vec2"), ";\n"});
                adjustments[i] = intrinsic("Color" + component, {range + ".x", range + ".y"}, "float");
            }
        }
        code += cat({"p.color=nb_hue_shift(", input("Color", Array::make(1, 1, 1, 1), "vec4"), ",",
                     adjustments[0], ",", adjustments[1], ",", adjustments[2],
                     "); p.color.a*=", adjustments[3], ";\n"});
    }
    code += "p.initial_color=p.color;\n";
    String size = mode("SpriteSizeMode", "Uniform");
    if (size == "RandomNonUniform")
        code += cat({"p.size=",
                     intrinsic("SpriteSize",
                               {input("SpriteSizeMin", Array::make(1, 1), "vec2"),
                                input("SpriteSizeMax", Array::make(1, 1), "vec2")},
                               "vec2"),
                     ";\n"});
    else if (size == "RandomUniform")
        code +=
            cat({"p.size=vec2(",
                 intrinsic("SpriteSize",
                           {input("UniformSpriteSizeMin", 1.0), input("UniformSpriteSizeMax", 1.0)}, "float"),
                 ");\n"});
    else if (size == "Uniform")
        code += cat({"p.size=vec2(", input("UniformSpriteSize", 1.0), ");\n"});
    else if (size == "NonUniform" || size == "DirectSet")
        code += cat({"p.size=", input("SpriteSize", Array::make(1, 1), "vec2"), ";\n"});
    else if (size != "Unset")
        return unsupported("sprite size mode");
    code += "p.initial_size=p.size;\n";
    String mesh = mode("MeshScaleMode", "Unset");
    if (mesh == "Uniform")
        code += cat({"p.mesh_scale=vec3(", input("MeshUniformScale", 1.0), ");\n"});
    else if (mesh == "NonUniform")
        code += cat({"p.mesh_scale=", input("MeshScale", Array::make(1, 1, 1), "vec3"), ";\n"});
    else if (mesh == "RandomUniform")
        code +=
            cat({"p.mesh_scale=vec3(",
                 intrinsic("MeshScale",
                           {input("MeshUniformScaleMin", 1.0), input("MeshUniformScaleMax", 2.0)}, "float"),
                 ");\n"});
    else if (mesh == "RandomNonUniform")
        code += cat({"p.mesh_scale=",
                     intrinsic("MeshScale",
                               {input("MeshScaleMin", Array::make(1, 1, 1), "vec3"),
                                input("MeshScaleMax", Array::make(2, 2, 2), "vec3")},
                               "vec3"),
                     ";\n"});
    String mass = mode("MassMode", "NewEnumerator0");
    if (mass == "Random")
        code += cat(
            {"p.mass=", intrinsic("Mass", {input("MassMin", .75), input("MassMax", 1.25)}, "float"), ";\n"});
    else if (mass == "DirectSet")
        code += cat({"p.mass=", input("Mass", 1.0), ";\n"});
    else
        code += "p.mass=1.0;\n";
    if (flag("UsePositionOffset", false))
        code += cat({"p.position+=nb_transform(", input("PositionOffset", Array::make(0, 0, 0), "vec3"), ",",
                     space("PositionOffsetCoordinateSpace", "Local"), ",true,p,spawning);\n"});
    String uv = mode("SpriteUVMode", "Unset");
    if (named(uv, {"RandomX", "RandomX/Y"}))
        code += source_rng.is_empty() ? String("p.uv_flip=nb_random(p)<0.5?-1.0:1.0;\n")
                                      : cat({"p.uv_flip=",
                                             native_range(cat({operation->get("stable_id"), "/$UVFlip"}),
                                                          {"0.0", "1.0"}, "float", true),
                                             "!=0.0?-1.0:1.0;\n"});
    if (named(uv, {"RandomY", "RandomX/Y"}))
        code += source_rng.is_empty() ? String("p.uv_flip_y=nb_random(p)<0.5?-1.0:1.0;\n")
                                      : cat({"p.uv_flip_y=",
                                             native_range(cat({operation->get("stable_id"), "/$UVFlipY"}),
                                                          {"0.0", "1.0"}, "float", true),
                                             "!=0.0?-1.0:1.0;\n"});
    if (uv == "RandomX")
        code += "p.uv_flip_y=1.0;\n";
    else if (uv == "RandomY")
        code += "p.uv_flip=1.0;\n";
    else if (uv == "Custom")
        code += cat({"vec2 uv=", input("SpriteUVScale", Array::make(1, 1), "vec2"),
                     "; p.uv_flip=uv.x; p.uv_flip_y=uv.y;\n"});
    else if (uv != "Unset" && uv != "RandomX/Y")
        return unsupported("sprite UV mode");
    String rotation = mode("SpriteRotationMode", "Unset");
    if (rotation == "Random")
        code += cat({"p.rotation=radians(",
                     intrinsic("SpriteRotation",
                               {input("SpriteRotationAngleMin", 0.0), input("SpriteRotationAngleMax", 360.0)},
                               "float"),
                     ");\n"});
    else if (rotation != "Unset")
        return unsupported("sprite rotation mode");
    if (flag("WriteMaterialRandom", false))
        code += cat({"p.material_random=", intrinsic("MaterialRandom", {"0.0", "1.0"}, "float"), ";"});
    return code;
}
} // namespace godot::nbgpu
