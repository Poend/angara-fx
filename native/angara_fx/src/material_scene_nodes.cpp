#include "material_compiler.h"
#include <cmath>
namespace godot::nbmaterial {
Dictionary Context::node_scene(const String &op, const Dictionary &n, const Dictionary &scope, int output,
                               bool &handled) {
    Dictionary p = n["properties"], inputs = n["inputs"], value = constant(0.0);
    handled = true;
    bool vertex = scope.get("stage", Variant()) == Variant("vertex"),
         batch = bool(options.get("batch", false));
    if (op == "ShadingPathSwitch" || op == "FeatureLevelSwitch" || op == "QualitySwitch") {
        int index = int(n.get("target_index",-1));
        if (index < 0) error("source_material_target","Re-export this material with its active source shader platform: " + op);
        String key = "Choice"+String::num_int64(index);
        if (!inputs.has(key)) key = "Default";
        if (!inputs.has(key)) error("material_switch_input","Source material switch has no connected input for its permutation.");
        value = input(n,key,scope,constant(0.0));
    } else if (op == "TextureCoordinate") {
        int index = int64_t(p.get("CoordinateIndex", 0));
        if ((index != 0 && index != 1) || bool(p.get("UnMirrorU", false)) || bool(p.get("UnMirrorV", false)))
            error("uv_mode", "Only UV0/UV1 without unmirror flags are implemented.");
        require("uv_channel_" + String::num_int64(index));
        String coordinate = uv(index), u = number(p.get("UTiling", 1.0)), v = number(p.get("VTiling", 1.0));
        value = expression("(" + coordinate + " * vec2(" + u + ", " + v + "))", 2);
    } else if (op == "Panner") {
        Dictionary coordinates = inputs.has("Coordinate")
                                     ? input(n, "Coordinate", scope, constant(0.0))
                                     : expression(uv(int64_t(p.get("ConstCoordinate", 0))), 2);
        note_uniform(coordinates);
        Dictionary speed =
            input(n, "Speed", scope, constant(list({p.get("SpeedX", 0.0), p.get("SpeedY", 0.0)})));
        feature("time");
        Dictionary fallback = expression("simulation_time", 1);
        fallback["uniform"] = false;
        Dictionary time = input(n, "Time", scope, fallback);
        String offset = "(" + code(cast(speed, 2)) + " * " + code(cast(time, 1)) + ")";
        if (bool(p.get("bFractionalPart", false)))
            offset = "fract(" + offset + ")";
        value = expression("(" + code(cast(coordinates, 2)) + " + " + offset + ")", 2);
    } else if (op == "Time") {
        bool periodic = p.get("bOverride_Period", false);
        double period = p.get("Period", 0.0);
        if (periodic && period == 0)
            value = constant(0.0);
        else {
            if (bool(p.get("bIgnorePause", false)))
                error("material_real_time",
                      "Time with IgnorePause requires an independent source real-time clock.");
            if (!std::isfinite(period))
                error("time_period", "Time period must be finite.");
            feature("time");
            String first = periodic ? number(period) : String(),
                   second = periodic ? number(period) : String();
            value = expression(periodic ? "(simulation_time - trunc(simulation_time / " + first + ") * " +
                                              second + ")"
                                        : String("simulation_time"),
                               1);
            value["uniform"] = false;
        }
    } else if (op == "Rotator") {
        Dictionary coordinates;
        if (inputs.has("Coordinate"))
            coordinates = input(n, "Coordinate", scope, constant(0.0));
        else {
            coordinates = expression(uv(int64_t(p.get("ConstCoordinate", 0))), 2);
            coordinates["uniform"] = false;
        }
        note_uniform(coordinates);
        if (width(coordinates) != 2 && width(coordinates) != 3)
            error("rotator_coordinate", "Rotator currently requires a float2 or float3 Coordinate.");
        if (!inputs.has("Time"))
            feature("time");
        Dictionary fallback = expression("simulation_time", 1);
        fallback["uniform"] = false;
        Dictionary time = input(n, "Time", scope, fallback);
        String angle = "(" + code(cast(time, 1)) + " * " + number(double(p.get("Speed", 0.25))) + ")",
               center = code(constant(list({p.get("CenterX", 0.5), p.get("CenterY", 0.5)}))),
               offset = "(" + code(cast(coordinates, 2)) + " - " + center + ")",
               rotated = "(vec2(dot(vec2(cos(" + angle + "), -sin(" + angle + ")), " + offset +
                         "), dot(vec2(sin(" + angle + "), cos(" + angle + ")), " + offset + ")) + " + center +
                         ")";
        value = expression(width(coordinates) == 3 ? "vec3(" + rotated + ", (" + code(coordinates) + ").z)"
                                                   : rotated,
                           width(coordinates) == 3 ? 3 : 2);
    } else if (op == "PixelDepth") {
        String depth = vertex ? "-(MODELVIEW_MATRIX * vec4(VERTEX,1.0)).z" : "-VERTEX.z";
        value = expression("(" + depth + " * 100.0)", 1);
        value["uniform"] = false;
    } else if (op == "ScreenPosition") {
        if (output != 0 && output != 1)
            error("output_index", "ScreenPosition has viewport UV and pixel-position outputs only.");
        value = expression(output == 1 ? "SCREEN_UV * VIEWPORT_SIZE" : "SCREEN_UV", 2);
    } else if (op == "ViewProperty")
        value = view_property(text(p, "Property", "MEVP_BufferSize"), output);
    else if (op == "SceneDepth") {
        feature("depth");
        String mode = text(p, "InputMode", "OffsetFraction").trim_prefix("EMaterialSceneAttributeInputMode::");
        String uv = "SCREEN_UV";
        if (mode == "Coordinates") {
            if (inputs.has("Input")) uv = code(cast(input(n, "Input", scope, constant(0.0)), 2));
        } else if (mode == "OffsetFraction") {
            Dictionary offset = input(n, "Input", scope, constant(components(p.get("ConstInput", Dictionary()), 2)));
            uv = "(SCREEN_UV + " + code(cast(offset, 2)) + ")";
        } else error("scene_depth_coordinates", "Unsupported SceneDepth input mode: " + mode);
        value = expression("(nb_view_depth(" + uv + ", INV_PROJECTION_MATRIX) * 100.0)", 1);
    } else if (op == "DepthFade") {
        feature("depth");
        Dictionary opacity = cast(input(n, "InOpacity", scope, constant(p.get("OpacityDefault", 1.0))), 1),
                   distance = cast(
                       input(n, "FadeDistance", scope, constant(p.get("FadeDistanceDefault", 100.0))), 1);
        value = expression(
            "(" + code(opacity) +
                " * clamp((nb_view_depth(SCREEN_UV, INV_PROJECTION_MATRIX) + VERTEX.z) * 100.0 / max(" +
                code(distance) + ", 0.00001), 0.0, 1.0))",
            1);
    } else if (op == "If") {
        for (const char *required : {"A", "AGreaterThanB", "ALessThanB"})
            if (!inputs.has(required))
                error("if_input", String("If requires ") + required);
        Dictionary a = cast(input(n, "A", scope, constant(0.0)), 1),
                   b = cast(input(n, "B", scope, constant(p.get("ConstB", 0.0))), 1),
                   greater = input(n, "AGreaterThanB", scope, constant(0.0)),
                   less = input(n, "ALessThanB", scope, constant(0.0)),
                   equal = input(n, "AEqualsB", scope, greater);
        int size = common(greater, less);
        value = expression("(" + code(a) + " >= " + code(b) + " ? " + code(cast(greater, size)) + " : " +
                               code(cast(less, size)) + ")",
                           size);
        if (inputs.has("AEqualsB"))
            value["code"] = "(abs(" + code(a) + " - " + code(b) +
                            ") <= " + number(p.get("EqualsThreshold", 0.00001)) + " ? " +
                            code(cast(equal, size)) + " : " + code(value) + ")";
    } else if (op == "Fresnel") {
        feature("world");
        String position = vertex ? "(MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz"
                                 : "(INV_VIEW_MATRIX * vec4(VERTEX, 1.0)).xyz";
        String view = "normalize(CAMERA_POSITION_WORLD - " + position + ")";
        String normal;
        if (inputs.has("Normal")) {
            Dictionary input_normal = cast(input(n, "Normal", scope, constant(0.0)), 3);
            // Niagara supplies world-space normals in Unreal coordinates, including non-unit inputs.
            normal = "nb_to_godot_direction(" + code(input_normal) + ")";
        } else {
            Dictionary roots = property("outputs");
            Dictionary normal_pin = roots.get("Normal", Dictionary());
            if (vertex || !text(normal_pin, "source").is_empty()) {
                error("fresnel_pixel_normal", "Implicit Fresnel normal requires the fragment surface normal; "
                      "vertex use and connected material Normal outputs require an explicit normal path.");
                return constant(0.0);
            }
            normal = "normalize(nb_original_world_normal)";
            if (bool(property("material_properties").get("TwoSided", false)))
                normal = "(" + normal + " * (FRONT_FACING ? 1.0 : -1.0))";
        }
        Dictionary exponent = cast(input(n, "ExponentIn", scope, constant(p.get("Exponent", 5.0))), 1);
        Dictionary base = cast(input(n, "BaseReflectFractionIn", scope,
                                     constant(p.get("BaseReflectFraction", 0.04))), 1);
        String angular = "max(abs(1.0 - max(0.0, dot(" + normal + ", " + view + "))), 0.0001)";
        value = expression("(pow(" + angular + ", " + code(exponent) + ") * (1.0 - " + code(base) +
                           ") + " + code(base) + ")", 1);
    } else if (op == "VertexNormalWS") {
        feature("world");
        String normal = vertex ? "normalize(MODEL_NORMAL_MATRIX * NORMAL)" : "nb_original_world_normal";
        value = expression("vec3(-(" + normal + ").z, (" + normal + ").x, (" + normal + ").y)", 3);
    } else if (op == "TwoSidedSign") {
        value = expression("(FRONT_FACING ? 1.0 : -1.0)", 1);
        value["uniform"] = false;
    } else if (op == "ObjectPositionWS") {
        String origin = text(p,"WorldPositionOriginType","Absolute").trim_prefix("EPositionOrigin::");
        if (origin != "Absolute") error("object_position_origin","Unsupported ObjectPositionWS origin: "+origin);
        feature("object_position");
        feature("world");
        require("primitive_world_bounds_center");
        Dictionary target = n.get("compile_target",Dictionary());
        String center = "nb_object_world_center";
        if (!target.has("primitive_object_position_fix")) require("mesh_object_position_policy_missing");
        else if (!bool(target["primitive_object_position_fix"])) {
            feature("particle");
            feature("instance_transform");
            if (!vertex) feature("fragment_local_to_world");
            String matrix = !vertex ? String("nb_instance_local_to_world") : (batch ? String("nb_geometry_local_to_world") : String("MODEL_MATRIX"));
            center = "(nb_renderer_kind == 3 ? " + matrix + "[3].xyz : nb_object_world_center)";
        }
        value = expression("(nb_to_ue_direction(" + center + ") * 100.0)",3);
    } else if (op == "CameraPositionWS") {
        feature("world");
        String origin = text(p, "WorldPositionOriginType", "EPositionOrigin::Absolute");
        if (origin != "EPositionOrigin::Absolute" && origin != "Absolute")
            error("camera_position_origin", "Unsupported camera position origin: " + origin);
        value = expression("(nb_to_ue_direction(CAMERA_POSITION_WORLD) * 100.0)", 3);
    } else if (op == "CameraVectorWS") {
        feature("world");
        value = expression(
            "nb_to_ue_direction(normalize(CAMERA_POSITION_WORLD - nb_original_world_position))", 3);
    } else if (op == "WorldPosition") {
        feature("world");
        String mode = text(p, "WorldPositionShaderOffset", "WPT_Default");
        if (mode != "WPT_Default" && mode != "WPT_ExcludeAllShaderOffsets")
            error("world_position_mode", "Camera-relative WorldPosition is not implemented.");
        String position = vertex ? String("(MODEL_MATRIX * vec4(VERTEX,1.0)).xyz")
                                 : (mode == "WPT_ExcludeAllShaderOffsets"
                                        ? String("nb_original_world_position")
                                        : String("(INV_VIEW_MATRIX * vec4(VERTEX,1.0)).xyz"));
        value = expression("(nb_to_ue_direction(" + position + ") * 100.0)", 3);
    } else if (op == "TransformPosition") {
        feature("world");
        feature("instance_transform");
        String source = text(p, "TransformSourceType", "TRANSFORMPOSSOURCE_Local"),
               target = text(p, "TransformType", "TRANSFORMPOSSOURCE_World");
        Dictionary v = cast(input(n, "Input", scope, constant(0.0)), 3);
        String position = "nb_to_godot_direction(" + code(v) + ") * 0.01";
        if (source == "TRANSFORMPOSSOURCE_World" &&
            (target == "TRANSFORMPOSSOURCE_Instance" || target == "TRANSFORMPOSSOURCE_Local")) {
            String matrix = batch ? "inverse(nb_geometry_local_to_world)" : "inverse(MODEL_MATRIX)";
            if (!vertex) {
                feature("fragment_world_to_local");
                matrix = "nb_instance_world_to_local";
            }
            position = "(" + matrix + " * vec4(" + position + ",1.0)).xyz";
        } else if ((source == "TRANSFORMPOSSOURCE_Instance" || source == "TRANSFORMPOSSOURCE_Local") &&
                   target == "TRANSFORMPOSSOURCE_World") {
            String matrix = batch ? "nb_geometry_local_to_world" : "MODEL_MATRIX";
            if (!vertex) {
                feature("fragment_local_to_world");
                matrix = "nb_instance_local_to_world";
            }
            position = "(" + matrix + " * vec4(" + position + ",1.0)).xyz";
        } else if (source != target)
            error("position_transform",
                  "Unsupported position transform: " + source + String(" to ") + target);
        value = expression("(nb_to_ue_direction(" + position + ") * 100.0)", 3);
    } else if (op == "Transform") {
        String source = text(p, "TransformSourceType", "TRANSFORMSOURCE_Tangent"),
               target = text(p, "TransformType", "TRANSFORM_World");
        Dictionary v = input(n, "Input", scope, constant(0.0));
        if (width(v) == 1 && source == "TRANSFORMSOURCE_Tangent")
            v = expression("vec3(0.0,0.0," + code(v) + ")", 3);
        else
            v = cast(v, 3);
        if (source == "TRANSFORMSOURCE_Tangent" && target == "TRANSFORM_View") {
            feature("tangent_frame");
            value = expression("((mat3(nb_view_tangent, nb_view_binormal, nb_view_normal) * " + code(v) +
                                   ") * vec3(1.0,1.0,-1.0))",
                               3);
        } else if (source == "TRANSFORMSOURCE_View" && target == "TRANSFORM_World") {
            feature("world");
            value = expression("nb_to_ue_direction(mat3(INV_VIEW_MATRIX) * (" + code(v) + " * vec3(1.0,1.0,-1.0)))", 3);
        } else if (source == "TRANSFORMSOURCE_World" && target == "TRANSFORM_Local") {
            feature("world");
            feature("instance_transform");
            String matrix = batch ? "inverse(nb_geometry_local_to_world)" : "inverse(MODEL_MATRIX)";
            if (!vertex) {
                feature("fragment_world_to_local");
                matrix = "nb_instance_world_to_local";
            }
            value = expression("nb_to_ue_direction(mat3(" + matrix + ") * nb_to_godot_direction(" + code(v) + "))", 3);
        } else
            error("direction_transform", "Unsupported vector transform: " + source + String(" to ") + target);
    } else if (op == "RotateAboutAxis") {
        feature("rotate_axis");
        Dictionary axis = cast(input(n, "NormalizedRotationAxis", scope, constant(list({0.0, 0.0, 1.0}))), 3);
        for (const char *required : {"NormalizedRotationAxis", "RotationAngle", "PivotPoint", "Position"})
            if (!inputs.has(required))
                error("rotate_axis_input", String("RotateAboutAxis requires ") + required);
        Dictionary angle = cast(input(n, "RotationAngle", scope, constant(0.0)), 1);
        angle["code"] = "(" + code(angle) + " / " + number(p.get("Period", 1.0)) + ")";
        Dictionary pivot = cast(input(n, "PivotPoint", scope, constant(0.0)), 3),
                   position = cast(input(n, "Position", scope, constant(0.0)), 3);
        value = expression("nb_rotate_axis(" + code(axis) + ", " + code(angle) + ", " + code(pivot) + ", " +
                               code(position) + ")",
                           3);
    } else if (op == "EyeAdaptation" || op == "EyeAdaptationInverse") {
        feature("exposure");
        require("source_exposure_must_be_supplied_for_auto_exposure_parity");
        value = expression("nb_source_exposure", 1);
        if (op == "EyeAdaptationInverse") {
            Dictionary light = input(n, "LightValueInput", scope, constant(list({1.0, 1.0, 1.0}))),
                       alpha = cast(input(n, "AlphaInput", scope, constant(1.0)), 1);
            value =
                expression("(" + code(light) + " * exp(-(" + code(alpha) + ") * log(nb_source_exposure)))",
                           width(light));
        }
    } else
        handled = false;
    return value;
}
Dictionary Context::view_property(const String &name, int output) {
    if (output != 0 && output != 1)
        error("output_index", "ViewProperty has value and reciprocal outputs only.");
    Dictionary value = expression("0.0", 1);
    if (name == "MEVP_ViewSize" || name == "MEVP_BufferSize")
        value = expression("VIEWPORT_SIZE", 2);
    else if (name == "MEVP_ViewportOffset")
        return constant(list({0.0, 0.0}));
    else if (name == "MEVP_TemporalSampleIndex" || name == "MEVP_TemporalSampleCount" ||
             name == "MEVP_TemporalSampleOffset") {
        feature("temporal_view");
        require("temporal_view_requires_TAA_disabled_in_source_and_target");
        value = expression(name == "MEVP_TemporalSampleIndex"
                               ? "nb_temporal_sample_index"
                               : (name == "MEVP_TemporalSampleCount" ? "nb_temporal_sample_count"
                                                                     : "nb_temporal_sample_offset"),
                           name == "MEVP_TemporalSampleOffset" ? 2 : 1);
    } else
        error("view_property", "View property has no verified lowering: " + name);
    if (output == 1)
        value["code"] = "(" + type(width(value)) + "(1.0) / " + code(value) + ")";
    return value;
}
Dictionary Context::custom(const Dictionary &n, const Dictionary &scope, int output_index) {
    Dictionary p = n["properties"], inputs = n["inputs"];
    String authored = text(p, "Code").replace("\r", "").strip_edges();
    if (!authored.contains("return")) authored = "return " + authored + ";";
    Dictionary translated = property("custom_shader_functions");
    Dictionary program = translated.get(authored.sha256_text(), Dictionary());
    if (program.has("body")) return translated_custom(n, scope, output_index, program);
    for (const char *name : {"IncludeFilePaths", "AdditionalDefines", "AdditionalOutputs"}) {
        Variant extension = p.get(name, Variant());
        if (extension.get_type() == Variant::ARRAY && !Array(extension).is_empty())
            error("custom_extensions",
                  "Custom expression includes/defines/additional outputs are not supported.");
    }
    String source = text(p, "Code"), compact = source.replace(" ", "")
                                                   .replace("\r", "")
                                                   .replace("\n", "")
                                                   .replace("\t", "")
                                                   .trim_prefix("return")
                                                   .trim_suffix(";");
    if (compact == "GetPrimitiveData(Parameters).LocalObjectBoundsMin.xyz" ||
        compact == "GetPrimitiveData(Parameters).LocalObjectBoundsMax.xyz") {
        feature("bounds");
        require("primitive_local_bounds_uniforms_must_match_the_scene_component");
        return expression(compact.ends_with("Min.xyz")
                              ? "vec3(-(nb_mesh_bounds_min.z + nb_mesh_bounds_size.z), nb_mesh_bounds_min.x, "
                                "nb_mesh_bounds_min.y) * 100.0"
                              : "vec3(-nb_mesh_bounds_min.z, nb_mesh_bounds_min.x + nb_mesh_bounds_size.x, "
                                "nb_mesh_bounds_min.y + nb_mesh_bounds_size.y) * 100.0",
                          3);
    }
    String output = text(p, "OutputType", "CMOT_Float3");
    if (compact.begins_with("PseudoRandom(") && compact.ends_with(")")) {
        String argument = compact.trim_prefix("PseudoRandom(").trim_suffix(")");
        if (inputs.has(argument) && output == "CMOT_Float1") {
            feature("pseudorandom");
            return expression(
                "nb_pseudorandom(" + code(cast(input(n, argument, scope, constant(0.0)), 2)) + ")", 1);
        }
    }
    PackedStringArray outputs = String("CMOT_Float1,CMOT_Float2,CMOT_Float3,CMOT_Float4").split(",");
    int channels = outputs.find(output) + 1;
    if (channels && (channels == 1 || !compact.is_empty())) {
        Dictionary values;
        Array names = inputs.keys();
        for (int64_t i = 0; i < names.size(); ++i) {
            String name = names[i];
            values[name] = input(n, name, scope, constant(0.0));
        }
        Dictionary arithmetic = NBNativeCustomArithmetic::compile_typed(source, values, channels,
            "nb_local_" + String::num_int64(serial) + "_");
        if (!arithmetic.has("error")) {
            if (arithmetic.get("pseudorandom", false)) feature("pseudorandom");
            arithmetic.erase("pseudorandom");
            PackedStringArray statements = arithmetic.get("statements", PackedStringArray());
            for (const String &line : statements)
                body.append(line);
            arithmetic.erase("statements");
            return arithmetic;
        }
        if (channels != 1) {
            error("custom_expression", "Custom expression has no verified builtin lowering: " + compact);
            return constant(0.0);
        }
        error("custom_expression", "Unsupported Custom arithmetic: " + text(arithmetic, "error"));
    } else
        error("custom_expression", "Custom expression has no verified builtin lowering: " + compact);
    return constant(0.0);
}
} // namespace godot::nbmaterial
