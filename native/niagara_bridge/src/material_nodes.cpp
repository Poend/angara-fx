#include "material_compiler.h"
namespace godot::nbmaterial {
Dictionary Context::node_map(const Array &nodes) {
    Dictionary result;
    for (int64_t i = 0; i < nodes.size(); ++i) {
        Dictionary n = nodes[i];
        result[n["id"]] = n;
    }
    return result;
}
Dictionary Context::root_pin(const String &name, const Dictionary &fallback, const Dictionary &scope) {
    Dictionary outputs = property("outputs");
    return outputs.has(name) ? pin(outputs[name], scope, fallback) : fallback;
}
Dictionary Context::pin(const Dictionary &value, const Dictionary &scope, const Dictionary &fallback) {
    String id = text(value, "source");
    if (id.is_empty()) {
        if (bool(value.get("use_constant", false)) &&
            value.get("constant", Variant()).get_type() != Variant::NIL)
            return constant(value["constant"]);
        return fallback;
    }
    Dictionary nodes = scope["nodes"];
    if (!nodes.has(id)) {
        error("node_reference", "Expression is absent from its graph scope: " + id);
        return constant(0.0);
    }
    Dictionary n = nodes[id];
    int output = int64_t(value.get("output", 0));
    Dictionary result = node(n, scope, output);
    Array mask = value.get("mask", Array());
    if (mask.is_empty())
        mask = output_mask(text(n, "op"), output);
    return mask.is_empty() ? result : swizzle(result, mask);
}
Dictionary Context::input(const Dictionary &n, const String &name, const Dictionary &scope,
                          const Dictionary &fallback) {
    Dictionary inputs = n["inputs"];
    Dictionary value = inputs.has(name) ? pin(inputs[name], scope, fallback) : fallback;
    note_uniform(value);
    return value;
}
Dictionary Context::node(const Dictionary &n, const Dictionary &scope, int output) {
    String op = n["op"], id = n["id"], scope_id = scope["id"],
           key = scope_id + ":" + id + ":" +
                 String::num_int64(
                     op == "MaterialFunctionCall" || op == "ScreenPosition" || op == "ViewProperty" || op == "Custom" ? output
                                                                                                    : 0);
    if (memo.has(key)) {
        Dictionary result = memo[key];
        note_uniform(result);
        return result;
    }
    if (active.has(key)) {
        error("expression_cycle", "Cycle in material expression graph: " + id);
        return constant(0.0);
    }
    active[key] = true;
    uniform_stack.push_back(true);
    if (!used_ops.has(op))
        used_ops.append(op);
    PackedStringArray fragment_only = String("DepthFade,SceneDepth,SceneColor,ScreenPosition,ViewProperty,"
                                             "ParticleSubUV,TextureSampleParameterSubUV,TwoSidedSign")
                                          .split(",");
    if (scope.get("stage", Variant()) == Variant("vertex") && fragment_only.has(op))
        error("node_stage", op + String(" is only implemented in the fragment stage."));
    Dictionary p = n["properties"], inputs = n["inputs"], parameters = property("parameters"),
               value = constant(0.0);
    bool handled = true;
    if (op == "Constant")
        value = constant(p.get("R", 0.0));
    else if (op == "Constant2Vector")
        value = constant(list({p.get("R", 0.0), p.get("G", 0.0)}));
    else if (op == "Constant3Vector" || op == "Constant4Vector")
        value = constant(components(p.get("Constant", Dictionary()), op == "Constant3Vector" ? 3 : 4));
    else if (op == "ScalarParameter" || op == "VectorParameter") {
        if (bool(p.get("bUseCustomPrimitiveData", false)))
            error("custom_primitive_data", "Custom primitive parameter data is not implemented.");
        String kind = op == "ScalarParameter" ? "scalar" : "vector", name = text(p, "ParameterName", "Param");
        int channels = kind == "scalar" ? 1 : 4;
        Dictionary params = parameters[kind];
        Variant initial =
            params.get(name, p.get("DefaultValue", channels == 1 ? Variant(0.0) : Variant(Dictionary())));
        value = parameter(kind, name, initial, channels);
    } else if (op == "ParticleColor") {
        value = expression(bool(options.get("batch", false)) ? "nb_particle_color" : "COLOR", 4);
        feature("particle");
        require("ParticleColor_uses_COLOR_requires_white_mesh_vertex_colors");
    } else if (op == "ParticleRelativeTime") {
        value = expression("nb_particle_relative_time", 1);
        feature("particle");
        feature("particle_relative_time");
    } else if (op == "StaticBool" || op == "StaticBoolParameter") {
        Dictionary switches = parameters["static_switch"];
        bool boolean =
            op == "StaticBool"
                ? bool(p.get("Value", false))
                : bool(switches.get(text(p, "ParameterName", "Param"), p.get("DefaultValue", false)));
        value = dict({{"code", boolean ? "true" : "false"}, {"channels", 0}, {"constant", boolean}});
    } else if (op == "StaticSwitch" || op == "StaticSwitchParameter") {
        Dictionary switches = parameters["static_switch"];
        bool choose =
            op == "StaticSwitchParameter"
                ? bool(switches.get(text(p, "ParameterName", "Param"), p.get("DefaultValue", false)))
                : bool(p.get("DefaultValue", false));
        if (op == "StaticSwitch" && inputs.has("Value")) {
            Dictionary condition =
                input(n, "Value", scope, dict({{"code", "false"}, {"channels", 0}, {"constant", false}}));
            if (!condition.has("constant") || Variant(condition["constant"]).get_type() != Variant::BOOL)
                error("static_condition", "Static switch condition was not resolved at conversion time.");
            else
                choose = condition["constant"];
        }
        value = input(n, choose ? "A" : "B", scope, constant(0.0));
    } else if (op == "Switch")
        value = switch_node(n, scope);
    else if (op == "MaterialFunctionCall")
        value = function_call(n, scope, output);
    else if (op == "FunctionInput") {
        String input_id = text(p, "Id");
        Dictionary bound = scope["bindings"];
        if (bound.has(input_id)) {
            Dictionary binding = bound[input_id];
            value = pin(binding["pin"], binding["scope"], constant(0.0));
        } else if (bool(p.get("bUsePreviewValueAsDefault", false))) {
            Dictionary dimensions = dict({{"FunctionInput_Scalar", 1},
                                          {"FunctionInput_Vector2", 2},
                                          {"FunctionInput_Vector3", 3},
                                          {"FunctionInput_Vector4", 4},
                                          {"FunctionInput_StaticBool", 0},
                                          {"FunctionInput_Texture2D", -1}});
            int dimension = int64_t(dimensions.get(text(p, "InputType", "FunctionInput_Vector3"), -1));
            if (dimension < 0 && !inputs.has("Preview"))
                error("function_input_type",
                      "Texture function input requires an authored preview texture or connection.");
            Dictionary preview = constant(components(p.get("PreviewValue", Dictionary()), MAX(1, dimension)));
            if (dimension == 0) {
                bool selected = double(components(p.get("PreviewValue", Dictionary()), 1)[0]) != 0;
                preview =
                    dict({{"code", selected ? "true" : "false"}, {"channels", 0}, {"constant", selected}});
            }
            value = input(n, "Preview", scope, preview);
        } else
            error("function_input_missing",
                  "Required function input is unconnected: " + text(p, "InputName", input_id));
    } else if (op == "FunctionOutput" || op == "Reroute" || op == "NamedRerouteDeclaration")
        value = input(n, op == "FunctionOutput" ? "A" : "Input", scope, constant(0.0));
    else if (op == "NamedRerouteUsage") {
        String declaration = NBNativeMaterialGraphLoader::asset_path(text(p, "Declaration"));
        Dictionary nodes = scope["nodes"];
        Array all = nodes.values(), matches;
        for (int64_t i = 0; i < all.size(); ++i) {
            Dictionary item = all[i];
            String item_id = text(item, "id");
            if (item["op"] == Variant("NamedRerouteDeclaration") &&
                (item_id == declaration || item_id.ends_with(":" + declaration.get_slice(":", 1))))
                matches.append(item);
        }
        if (matches.size() != 1)
            error("named_reroute", "Named reroute declaration is absent or ambiguous: " + declaration);
        else
            value = node(matches[0], scope);
    } else if (op == "TextureObject" || op == "TextureObjectParameter") {
        String name = text(p, "ParameterName", "Param");
        Dictionary params = parameters["texture"];
        value = dict({{"code", ""},
                      {"channels", -1},
                      {"texture", op == "TextureObjectParameter" ? params.get(name, p.get("Texture", ""))
                                                                 : p.get("Texture", "")},
                      {"parameter", op == "TextureObjectParameter" ? name : String()},
                      {"sampler_type", text(p, "SamplerType", "SAMPLERTYPE_Color")}});
    } else if (op == "DynamicParameter") {
        int index = int64_t(p.get("ParameterIndex", 0));
        if (index < 0 || index > 3)
            error("dynamic_parameter_index", "DynamicParameter index must be between 0 and 3.");
        feature("particle");
        String name = "nb_dynamic_values" + (index == 0 ? String() : String::num_int64(index));
        Dictionary defaults = dict({{"R", 1.0}, {"G", 1.0}, {"B", 1.0}, {"A", 1.0}});
        value = expression("nb_dynamic_parameters_at(" + name + String(", ") +
                               code(constant(components(p.get("DefaultValue", defaults), 4))) +
                               ", nb_use_vertex_custom || nb_particle_attributes_enabled, " +
                               String::num_int64(index) + ")",
                           4);
    } else if (op == "Custom")
        value = custom(n, scope, output);
    else if (op == "TextureSample" || op == "TextureSampleParameter2D" || op == "TextureSampleParameterVolume" || op == "ParticleSubUV" ||
             op == "TextureSampleParameterSubUV")
        value = texture(n, scope);
    else {
        value = node_math(op, n, scope, handled);
        if (!handled)
            value = node_scene(op, n, scope, output, handled);
        if (!handled)
            error("unsupported_node", "Reachable node is not implemented: " + op + String(" at ") + id);
    }
    if (op == "FunctionInput") {
        Dictionary dimensions = dict({{"FunctionInput_Scalar", 1},
                                      {"FunctionInput_Vector2", 2},
                                      {"FunctionInput_Vector3", 3},
                                      {"FunctionInput_Vector4", 4},
                                      {"FunctionInput_StaticBool", 0},
                                      {"FunctionInput_Texture2D", -1}});
        int dimension = int64_t(dimensions.get(text(p, "InputType", "FunctionInput_Vector3"), -2));
        if (dimension > 0)
            value = cast(value, dimension);
        else if (dimension < -1 || width(value) != dimension)
            error("function_input_type",
                  "Function input does not match its declared type: " + text(p, "InputName"));
    }
    if (!value.has("constant") && width(value) > 0) {
        String temporary = "n" + String::num_int64(serial++);
        body.append(type(width(value)) + " " + temporary + " = " + code(value) + ";");
        value = expression(temporary, width(value));
    }
    bool uniform = uniform_stack.back();
    uniform_stack.pop_back();
    PackedStringArray varying =
        String(
            "Time,PixelDepth,ScreenPosition,ViewProperty,TextureCoordinate,TextureSample,"
            "TextureSampleParameter2D,TextureSampleParameterVolume,ParticleSubUV,TextureSampleParameterSubUV,ParticleColor,"
            "ParticleRelativeTime,DynamicParameter,WorldPosition,ObjectPositionWS,CameraPositionWS,SceneDepth,VertexNormalWS,TwoSidedSign,CameraVectorWS,Fresnel,"
            "Transform,TransformPosition,Custom,DepthFade,EyeAdaptation,EyeAdaptationInverse")
            .split(",");
    if (varying.has(op))
        uniform = false;
    if (value.has("constant"))
        uniform = true;
    value["uniform"] = uniform;
    note_uniform(value);
    memo[key] = value;
    active.erase(key);
    return value;
}
Dictionary Context::switch_node(const Dictionary &n, const Dictionary &scope) {
    Dictionary properties = n["properties"];
    Array entries = properties.get("Inputs", Array());
    Dictionary selector = cast(input(n, "SwitchValue", scope, constant(properties.get("ConstSwitchValue", 0.0))), 1);
    Dictionary fallback = input(n, "Default", scope, constant(properties.get("ConstDefault", 0.0)));
    if (entries.is_empty()) {
        error("switch_inputs", "Material Switch requires at least one case.");
        return fallback;
    }
    Dictionary document = dict({{"objects", scope["nodes"]}});
    Array branches;
    int channels = width(fallback);
    for (int64_t i = 0; i < entries.size(); ++i) {
        Dictionary entry = entries[i].get_type() == Variant::DICTIONARY ? Dictionary(entries[i]) : Dictionary();
        Dictionary connection = NBNativeMaterialGraphLoader::pin(entry.get("Input", Dictionary()), document, graph);
        if (text(connection, "source").is_empty()) {
            error("switch_input", "Material Switch is missing its case input at index " + String::num_int64(i));
            return fallback;
        }
        Dictionary branch = pin(connection, scope, fallback);
        note_uniform(branch);
        if (width(branch) < 1 || channels < 1 || (channels != 1 && width(branch) != 1 && width(branch) != channels)) {
            error("switch_type", "Material Switch cases and default must have compatible numeric widths.");
            return fallback;
        }
        channels = MAX(channels, width(branch));
        branches.append(branch);
    }
    if (selector.has("constant")) {
        double index = float32(selector["constant"]);
        return cast(index >= 0 && index < branches.size() ? Dictionary(branches[int64_t(index)]) : fallback, channels);
    }
    String index_var = "n" + String::num_int64(serial++);
    body.append("float " + index_var + " = floor(" + code(selector) + ");");
    String selected = code(cast(fallback, channels));
    for (int64_t i = branches.size() - 1; i >= 0; --i)
        selected = "(" + index_var + " == " + number(i) + " ? " + code(cast(branches[i], channels)) + " : " + selected + ")";
    return expression(selected, channels);
}
Dictionary Context::function_call(const Dictionary &n, const Dictionary &scope, int output) {
    String path = n["function"];
    Array paths = scope["function_paths"];
    if (paths.has(path)) {
        error("function_cycle", "Recursive material function dependency: " + path);
        return constant(0.0);
    }
    Dictionary function;
    if (bool(options.get("embedded", false))) {
        String key = NBNativeMaterialGraphLoader::manifest_key(property("graph_dependencies"),
                                                               NBNativeMaterialGraphLoader::asset_path(path));
        Dictionary functions = property("functions");
        if (functions.has(key))
            function = functions[key];
        else
            error("sealed_function_missing", "Referenced function was not sealed into the program: " + path);
    } else
        function = NBNativeMaterialGraphLoader::load_function(graph, path);
    if (function.is_empty())
        return constant(0.0);
    Dictionary bound, inputs = function["function_inputs"];
    Array records = n["function_inputs"];
    for (int64_t i = 0; i < records.size(); ++i) {
        Dictionary record = records[i], p = record["pin"];
        String input_id = record["id"];
        if (!inputs.has(input_id))
            error("function_input_guid", "Function input GUID does not exist: " + input_id);
        if (!text(p, "source").is_empty())
            bound[input_id] = dict({{"pin", p}, {"scope", scope}});
    }
    String id;
    Array outputs = n["function_outputs"];
    if (output >= 0 && output < outputs.size() && outputs[output].get_type() == Variant::DICTIONARY)
        id = text(outputs[output], "ExpressionOutputId");
    Array matches, available = function["function_outputs"];
    for (int64_t i = 0; i < available.size(); ++i) {
        Dictionary record = available[i];
        if (record["id"] == Variant(id))
            matches.append(record);
    }
    if (matches.size() != 1) {
        error("function_output", "Function output GUID is absent/ambiguous: " + id);
        return constant(0.0);
    }
    paths = paths.duplicate();
    paths.append(path);
    Dictionary nodes = node_map(function["nodes"]),
               nested = dict({{"id", text(scope, "id") + "/" + text(n, "id")},
                              {"nodes", nodes},
                              {"bindings", bound},
                              {"function_paths", paths},
                              {"stage", scope.get("stage", "fragment")}});
    Dictionary selected = matches[0];
    return node(nodes[selected["node"]], nested);
}
} // namespace godot::nbmaterial
