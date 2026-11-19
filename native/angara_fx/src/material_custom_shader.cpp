#include "material_compiler.h"

namespace godot::nbmaterial {
Dictionary Context::translated_custom(const Dictionary &node, const Dictionary &scope, int output,
                                      const Dictionary &program) {
    String key = text(scope, "id") + ":" + text(scope, "stage") + ":" + text(node, "id");
    Array results = custom_results.get(key, Array());
    Array outputs = program.get("outputs", Array());
    if (output < 0 || output >= outputs.size() || outputs.size() > 8 || text(scope, "stage") == "vertex") {
        error("custom_shader_output", "Incompatible shader stage or Custom output index.");
        return constant(0.0);
    }
    if (!results.is_empty())
        return results[output];
    Array parameters = program.get("inputs", Array());
    Dictionary pins = node["inputs"];
    PackedStringArray signature, arguments;
    for (int i = 0; i < parameters.size(); ++i) {
        Dictionary param = parameters[i];
        if (!bool(param.get("used", false)))
            continue;
        String name = text(param, "name"), shader_type;
        Dictionary value;
        if (text(param, "builtin") == "CameraVectorWS") {
            feature("world");
            value = expression(
                "nb_to_ue_direction(normalize(CAMERA_POSITION_WORLD - nb_original_world_position))", 3);
        } else {
            if (!pins.has(name)) {
                error("custom_shader_input", "Source permutation requires an unconnected input: " + name);
                return constant(0.0);
            }
            value = input(node, name, scope, constant(0.0));
        }
        if (param.has("texture")) {
            if (width(value) != -1) {
                error("custom_shader_texture", "Custom texture is missing: " + name);
                return constant(0.0);
            }
            Dictionary sample = dict(
                {{"op", "TextureSample"},
                 {"raw_sampler", true},
                 {"inputs", Dictionary()},
                 {"properties", dict({{"SamplerType", value.get("sampler_type", "SAMPLERTYPE_Color")}})}});
            value = texture(sample, scope, value);
            if (value.get("texture_type", Variant()) != param["texture"]) {
                error("custom_shader_texture",
                      "Texture dimensions differ from the source signature: " + name);
                return constant(0.0);
            }
            shader_type = param["texture"] == Variant("Texture3D") ? "sampler3D" : "sampler2D";
        } else {
            int lanes = int64_t(param.get("width", 0));
            if (lanes < 1 || lanes > 4 || width(value) < 1) {
                error("custom_shader_input", "Invalid numeric Custom input: " + name);
                return constant(0.0);
            }
            value = cast(value, lanes);
            shader_type = type(lanes);
        }
        signature.append(shader_type + " nbin" + String::num_int64(i));
        arguments.append(code(value));
    }
    String id = String::num_int64(serial++);
    String helper = "nb_custom_" + text(options, "symbol_prefix") + id;
    for (int i = 0; i < outputs.size(); ++i) {
        Dictionary pin = outputs[i];
        int lanes = int64_t(pin.get("width", 0));
        if (lanes < 1 || lanes > 4) {
            error("custom_shader_output", "Invalid Custom expression result type.");
            return constant(0.0);
        }
        String index = String::num_int64(i), local = "nb_result_" + id + "_" + index;
        signature.append("out " + type(lanes) + " nbout" + index);
        arguments.append(local);
        body.append(type(lanes) + " " + local + ";");
        results.append(expression(local, lanes));
    }
    declarations.append("void " + helper + "(" + String(", ").join(signature) + ") {\n" +
                        text(program, "body") + "\n}");
    body.append(helper + "(" + String(", ").join(arguments) + ");");
    custom_results[key] = results;
    return results[output];
}
} // namespace godot::nbmaterial
