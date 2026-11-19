#include "material_graph_loader.h"
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/templates/sort_array.hpp>
#include <vector>
namespace godot {
namespace {
using nbdocument::dict;
String text(const Dictionary &d, const String &key, const String &fallback = "") {
    return Variant(d.get(key, fallback)).stringify();
}
void issue(const Ref<Resource> &graph, const String &code, const String &message) {
    nbdocument::append(graph, "diagnostics",
                     dict({{"severity", "error"}, {"code", code}, {"message", message}}));
}
Dictionary property(const Ref<Resource> &graph, const StringName &name) {
    return graph->get(name);
}
struct OutputOrder {
    bool operator()(const Dictionary &a, const Dictionary &b) const {
        return int64_t(a["sort_priority"]) < int64_t(b["sort_priority"]);
    }
};
} // namespace
String NBNativeMaterialGraphLoader::asset_path(const String &value) {
    return NBNativeT3DParser::object_path(value);
}
String NBNativeMaterialGraphLoader::project_path(const String &value) {
    return value.is_absolute_path() || value.begins_with("res://") ? value : "res://" + value;
}
String NBNativeMaterialGraphLoader::manifest_key(const Dictionary &records, const String &path) {
    if (records.has(path))
        return path;
    String object = path + String(".") + path.get_file();
    return records.has(object) ? object : String();
}
Ref<Resource> NBNativeMaterialGraphLoader::load_graph(const String &manifest_path,
                                                      const String &material_path,
                                                      const Ref<Script> &schema) {
    if (schema.is_null())
        return Ref<Resource>();
    Ref<Resource> graph = schema->call("new");
    if (graph.is_null())
        return graph;
    graph->set("source_manifest", manifest_path);
    graph->set("source_material", asset_path(material_path));
    if (!FileAccess::file_exists(manifest_path)) {
        issue(graph, "manifest_missing", "Material manifest not found: " + manifest_path);
        return graph;
    }
    Variant raw = JSON::parse_string(FileAccess::get_file_as_string(manifest_path));
    Dictionary manifest = raw.get_type() == Variant::DICTIONARY ? Dictionary(raw) : Dictionary();
    if (raw.get_type() != Variant::DICTIONARY || int64_t(manifest.get("schema_version", 0)) != 1 ||
        manifest.get("graphs", Variant()).get_type() != Variant::DICTIONARY ||
        manifest.get("textures", Variant()).get_type() != Variant::DICTIONARY) {
        issue(graph, "manifest_schema", "Unsupported material export manifest schema.");
        return graph;
    }
    Dictionary hashes = property(graph, "source_hashes");
    hashes[manifest_path] = FileAccess::get_sha256(manifest_path);
    graph->set("graph_dependencies", Dictionary(manifest["graphs"]).duplicate(true));
    graph->set("texture_dependencies", Dictionary(manifest["textures"]).duplicate(true));
    String path = manifest_key(property(graph, "graph_dependencies"), graph->get("source_material"));
    if (path.is_empty()) {
        issue(graph, "material_missing",
              "Material is absent from manifest: " + String(graph->get("source_material")));
        return graph;
    }
    graph->set("source_material", path);
    TypedArray<Dictionary> chain;
    Dictionary visited;
    while (!path.is_empty()) {
        if (visited.has(path)) {
            issue(graph, "parent_cycle", "Material instance parent cycle: " + path);
            return graph;
        }
        visited[path] = true;
        Dictionary document = load_document(graph, path);
        if (document.is_empty())
            return graph;
        Dictionary objects = document["objects"], root = objects[document["root"]];
        Array values = objects.values();
        for (int64_t i = 0; i < values.size(); ++i) {
            Dictionary object = values[i];
            if (object["class"] == Variant("MaterialInstanceEditorOnlyData"))
                root["editor_properties"] = Dictionary(object["properties"]).duplicate(true);
        }
        chain.append(root);
        String cls = root["class"];
        if (cls == "Material") {
            graph->set("root_material", path);
            graph->set("material_properties", Dictionary(root["properties"]).duplicate(true));
            Dictionary normalized = normalize(document, graph);
            TypedArray<Dictionary> nodes = graph->get("nodes");
            nodes.assign(normalized["nodes"]);
            graph->set("outputs", normalized["outputs"]);
            break;
        }
        if (cls != "MaterialInstanceConstant" && cls != "MaterialInstance") {
            issue(graph, "root_class", "Unsupported material root class: " + cls);
            return graph;
        }
        Dictionary properties = root["properties"];
        path = manifest_key(property(graph, "graph_dependencies"), asset_path(text(properties, "Parent")));
        if (path.is_empty()) {
            issue(graph, "parent_missing", "Material instance parent is absent from manifest.");
            return graph;
        }
    }
    chain.reverse();
    graph->set("parameters", dict({{"scalar", Dictionary()},
                                   {"vector", Dictionary()},
                                   {"texture", Dictionary()},
                                   {"static_switch", Dictionary()},
                                   {"static_component_mask", Dictionary()}}));
    for (int64_t i = 0; i < chain.size(); ++i)
        apply_instance(graph, chain[i]);
    Array materials = manifest.get("materials", Array());
    Dictionary parameters = property(graph, "parameters");
    Array kinds = parameters.keys();
    for (int64_t i = 0; i < materials.size(); ++i) {
        if (materials[i].get_type() != Variant::DICTIONARY)
            continue;
        Dictionary material = materials[i];
        if (asset_path(text(material, "object_path")) != String(graph->get("source_material")))
            continue;
        if (material.get("status", Variant()) != Variant("exported")) {
            issue(graph, "parameter_export", "Effective material parameter export is incomplete.");
            continue;
        }
        graph->set("custom_shader_functions", material.get("custom_shader_functions", Dictionary()));
        Dictionary exported = material.get("parameters", Dictionary());
        for (int64_t j = 0; j < kinds.size(); ++j) {
            Variant value = exported.get(kinds[j], Variant());
            if (value.get_type() == Variant::DICTIONARY) {
                Dictionary target = parameters[kinds[j]];
                target.merge(value, true);
            }
        }
    }
    return graph;
}
Dictionary NBNativeMaterialGraphLoader::load_document(const Ref<Resource> &graph, const String &path) {
    Dictionary dependencies = property(graph, "graph_dependencies");
    Variant raw = dependencies.get(path, Variant());
    Dictionary descriptor = raw.get_type() == Variant::DICTIONARY ? Dictionary(raw) : Dictionary();
    if (raw.get_type() != Variant::DICTIONARY || descriptor.get("status", Variant()) != Variant("exported")) {
        issue(graph, "graph_export", "Missing/incomplete exported graph: " + path);
        return Dictionary();
    }
    String file = project_path(text(descriptor, "file"));
    if (!FileAccess::file_exists(file) || text(descriptor, "sha256").is_empty() ||
        Variant(FileAccess::get_sha256(file)) != descriptor["sha256"]) {
        issue(graph, "graph_hash", "Exported graph SHA-256 validation failed: " + file);
        return Dictionary();
    }
    Dictionary document = NBNativeT3DParser::parse_file(file);
    Array diagnostics = document["diagnostics"];
    for (int64_t i = 0; i < diagnostics.size(); ++i) {
        Dictionary diagnostic = Dictionary(diagnostics[i]).duplicate();
        diagnostic["file"] = file;
        nbdocument::append(graph, "diagnostics", diagnostic);
    }
    if (!diagnostics.is_empty())
        return Dictionary();
    if (document["root"] != Variant(path)) {
        issue(graph, "graph_identity", "T3D root object does not match manifest identity: " + path);
        return Dictionary();
    }
    Dictionary hashes = property(graph, "source_hashes");
    hashes[file] = descriptor["sha256"];
    document["compile_target"] = descriptor.get("compile_target", Dictionary());
    return document;
}
Dictionary NBNativeMaterialGraphLoader::load_function(const Ref<Resource> &graph, const String &source_path) {
    String path = manifest_key(property(graph, "graph_dependencies"), asset_path(source_path));
    if (path.is_empty()) {
        issue(graph, "function_missing",
              "Referenced material function is absent from manifest: " + source_path);
        return Dictionary();
    }
    Dictionary functions = property(graph, "functions");
    if (functions.has(path))
        return functions[path];
    Dictionary document = load_document(graph, path);
    if (document.is_empty())
        return Dictionary();
    Dictionary objects = document["objects"], root = objects[document["root"]];
    String cls = root["class"];
    if (cls != "MaterialFunction" && cls != "MaterialFunctionInstance") {
        issue(graph, "function_class", "Dependency is not a material function: " + path);
        return Dictionary();
    }
    if (cls == "MaterialFunctionInstance") {
        issue(graph, "function_instance",
              "Material function-instance overrides require dedicated resolution: " + path);
        return Dictionary();
    }
    Dictionary result = normalize(document, graph);
    functions[path] = result;
    return result;
}
Dictionary NBNativeMaterialGraphLoader::pin(const Variant &raw, const Dictionary &document,
                                            const Ref<Resource> &graph) {
    Array mask;
    Dictionary result = dict(
        {{"source", ""}, {"output", 0}, {"mask", mask}, {"constant", Variant()}, {"use_constant", false}});
    if (raw.get_type() != Variant::DICTIONARY)
        return result;
    Dictionary value = raw;
    result["output"] = int64_t(value.get("OutputIndex", 0));
    result["constant"] = value.get("Constant", Variant());
    result["use_constant"] = bool(value.get("UseConstant", false));
    if (bool(value.get("Mask", false))) {
        const char *fields[] = {"MaskR", "MaskG", "MaskB", "MaskA"};
        for (int i = 0; i < 4; ++i)
            if (bool(value.get(fields[i], false)))
                mask.append(i);
    }
    String source = asset_path(text(value, "Expression"));
    if (source.is_empty() || source == "None")
        return result;
    Dictionary objects = document["objects"];
    if (objects.has(source)) {
        result["source"] = source;
        return result;
    }
    String name = source.get_slice(":", source.get_slice_count(":") - 1);
    Array values = objects.values();
    Array matches;
    for (int64_t i = 0; i < values.size(); ++i) {
        Dictionary object = values[i];
        // Saved normalized graphs retain the full id, including the local object name.
        String id = object["id"];
        if (text(object, "name", id.get_slice(":", id.get_slice_count(":") - 1)) == name)
            matches.append(object);
    }
    if (matches.size() != 1)
        issue(graph, "pin_reference", "Unresolved/ambiguous expression reference: " + source);
    else
        result["source"] = Dictionary(matches[0])["id"];
    return result;
}
Dictionary NBNativeMaterialGraphLoader::normalize(const Dictionary &document, const Ref<Resource> &graph) {
    Array nodes, function_outputs;
    Dictionary outputs, function_inputs;
    Dictionary result = dict({{"source", document["root"]},
                              {"nodes", nodes},
                              {"outputs", outputs},
                              {"function_inputs", function_inputs},
                              {"function_outputs", function_outputs}});
    PackedStringArray output_names =
        String("BaseColor,Metallic,Specular,Roughness,Anisotropy,EmissiveColor,Opacity,OpacityMask,Normal,"
               "Tangent,WorldPositionOffset,SubsurfaceColor,ClearCoat,ClearCoatRoughness,AmbientOcclusion,"
               "Refraction,PixelDepthOffset,MaterialAttributes,FrontMaterial,Displacement")
            .split(",");
    Dictionary objects = document["objects"];
    Array values = objects.values();
    for (int64_t i = 0; i < values.size(); ++i) {
        Dictionary object = values[i], properties = object["properties"];
        String cls = object["class"];
        if (cls == "MaterialEditorOnlyData")
            for (int64_t j = 0; j < output_names.size(); ++j)
                if (properties.has(output_names[j]))
                    outputs[output_names[j]] = pin(properties[output_names[j]], document, graph);
        if (!cls.begins_with("MaterialExpression") || cls == "MaterialExpressionComment")
            continue;
        String op = cls.trim_prefix("MaterialExpression");
        Dictionary inputs;
        Dictionary node = dict({{"id", object["id"]},
                                {"op", op},
                                {"properties", properties.duplicate(true)},
                                {"inputs", inputs},
                                {"line", object["line"]}});
        Array keys = properties.keys();
        if (op == "ShadingPathSwitch" || op == "FeatureLevelSwitch" || op == "QualitySwitch") {
            Array choices = properties.get("Inputs", Array());
            Dictionary target = document.get("compile_target", Dictionary());
            node["target_index"] = target.get(op, -1);
            for (int j = 0; j < choices.size(); ++j)
                if (choices[j].get_type() == Variant::DICTIONARY && Dictionary(choices[j]).has("Expression"))
                    inputs["Choice" + String::num_int64(j)] = pin(choices[j], document, graph);
        }
        if(op == "ObjectPositionWS") node["compile_target"] = document.get("compile_target",Dictionary());
        for (int64_t j = 0; j < keys.size(); ++j) {
            Variant value = properties[keys[j]];
            if (value.get_type() == Variant::DICTIONARY && Dictionary(value).has("Expression"))
                inputs[keys[j]] = pin(value, document, graph);
        }
        if (op == "Custom") {
            Variant raw = properties.get("Inputs", Variant());
            Array entries = raw.get_type() == Variant::ARRAY ? Array(raw) : Array();
            for (int64_t j = 0; j < entries.size(); ++j)
                if (entries[j].get_type() == Variant::DICTIONARY) {
                    Dictionary entry = entries[j];
                    inputs[text(entry, "InputName")] = pin(entry.get("Input", Dictionary()), document, graph);
                }
        }
        if (op == "MaterialFunctionCall") {
            node["function"] = asset_path(text(properties, "MaterialFunction"));
            Array records;
            node["function_inputs"] = records;
            Array entries = properties.get("FunctionInputs", Array());
            for (int64_t j = 0; j < entries.size(); ++j)
                if (entries[j].get_type() == Variant::DICTIONARY) {
                    Dictionary entry = entries[j];
                    records.append(dict({{"id", text(entry, "ExpressionInputId")},
                                         {"pin", pin(entry.get("Input", Dictionary()), document, graph)}}));
                }
            node["function_outputs"] = Array(properties.get("FunctionOutputs", Array())).duplicate(true);
        }
        if (op == "FunctionInput")
            function_inputs[text(properties, "Id")] = object["id"];
        if (op == "FunctionOutput")
            function_outputs.append(dict({{"id", text(properties, "Id")},
                                          {"node", object["id"]},
                                          {"sort_priority", int64_t(properties.get("SortPriority", 0))}}));
        nodes.append(node);
    }
    // Use the engine's sorting algorithm for the same equal-priority behavior.
    std::vector<Dictionary> sorted;
    for (int64_t i = 0; i < function_outputs.size(); ++i)
        sorted.push_back(function_outputs[i]);
    SortArray<Dictionary, OutputOrder>().sort(sorted.data(), int64_t(sorted.size()));
    function_outputs.clear();
    for (const Dictionary &record : sorted)
        function_outputs.append(record);
    return result;
}
void NBNativeMaterialGraphLoader::apply_instance(const Ref<Resource> &graph, const Dictionary &root) {
    Dictionary properties = root["properties"], parameters = property(graph, "parameters"),
               material_properties = property(graph, "material_properties");
    const char *names[] = {"ScalarParameterValues", "VectorParameterValues", "TextureParameterValues"};
    const char *kinds[] = {"scalar", "vector", "texture"};
    for (int i = 0; i < 3; ++i) {
        Array entries = properties.get(names[i], Array());
        Dictionary target = parameters[kinds[i]];
        for (int64_t j = 0; j < entries.size(); ++j) {
            if (entries[j].get_type() != Variant::DICTIONARY)
                continue;
            Dictionary entry = entries[j], info = entry.get("ParameterInfo", Dictionary());
            if (text(info, "Association", "GlobalParameter") != "GlobalParameter") {
                issue(graph, "parameter_layer", "Layer/blend parameter association is not implemented.");
                continue;
            }
            target[text(info, "Name", "Param")] = entry.get("ParameterValue", 0.0);
        }
    }
    Dictionary overrides = properties.get("BasePropertyOverrides", Dictionary()),
               supported = dict({{"bOverride_TwoSided", "TwoSided"},
                                 {"bOverride_BlendMode", "BlendMode"},
                                 {"bOverride_ShadingModel", "ShadingModel"},
                                 {"bOverride_OpacityMaskClipValue", "OpacityMaskClipValue"}});
    Array keys = overrides.keys();
    for (int64_t i = 0; i < keys.size(); ++i) {
        String key = keys[i];
        if (key.begins_with("bOverride_") && bool(overrides[key])) {
            if (supported.has(key)) {
                String field = supported[key];
                material_properties[field] = overrides.get(field, Variant());
            } else
                issue(graph, "base_override", "Unsupported material base-property override: " + key);
        }
    }
    for (const Dictionary &container : {properties, Dictionary(root.get("editor_properties", Dictionary()))})
        for (const char *key :
             {"StaticParametersRuntime", "StaticParameters", "StaticParametersEditorOnly"}) {
            Variant raw = container.get(key, Dictionary());
            if (raw.get_type() != Variant::DICTIONARY)
                continue;
            Dictionary statics = raw;
            Array groups = statics.keys();
            for (int64_t i = 0; i < groups.size(); ++i) {
                String kind = groups[i];
                Variant values = statics[kind];
                if (kind != "StaticSwitchParameters" && kind != "StaticComponentMaskParameters") {
                    if (values.get_type() == Variant::ARRAY && !Array(values).is_empty())
                        issue(graph, "static_parameter_kind", "Unsupported static parameter group: " + kind);
                    continue;
                }
                Array entries = values;
                for (int64_t j = 0; j < entries.size(); ++j) {
                    if (entries[j].get_type() != Variant::DICTIONARY)
                        continue;
                    Dictionary entry = entries[j];
                    if (!bool(entry.get("bOverride", false)))
                        continue;
                    Dictionary info = entry.get("ParameterInfo", Dictionary());
                    if (text(info, "Association", "GlobalParameter") != "GlobalParameter") {
                        issue(graph, "static_parameter_layer",
                              "Static layer/blend parameter association is unresolved.");
                        continue;
                    }
                    String name = text(info, "Name", "Param");
                    Dictionary target =
                        parameters[kind == "StaticSwitchParameters" ? "static_switch"
                                                                    : "static_component_mask"];
                    if (kind == "StaticSwitchParameters")
                        target[name] = bool(entry.get("Value", false));
                    else {
                        Array mask;
                        for (const char *field : {"R", "G", "B", "A"})
                            mask.append(bool(entry.get(field, false)));
                        target[name] = mask;
                    }
                }
            }
        }
    keys = properties.keys();
    for (int64_t i = 0; i < keys.size(); ++i)
        if (String(keys[i]).begins_with("MaterialLayers"))
            issue(graph, "material_layers", "Material layers require explicit graph expansion.");
}
void NBNativeMaterialGraphLoader::_bind_methods() {
    ClassDB::bind_static_method("NBNativeMaterialGraphLoader",
                                D_METHOD("load_graph", "manifest_path", "material_path", "graph_schema"),
                                &NBNativeMaterialGraphLoader::load_graph);
    ClassDB::bind_static_method("NBNativeMaterialGraphLoader",
                                D_METHOD("load_function", "graph", "source_path"),
                                &NBNativeMaterialGraphLoader::load_function);
    ClassDB::bind_static_method("NBNativeMaterialGraphLoader", D_METHOD("load_document", "graph", "path"),
                                &NBNativeMaterialGraphLoader::load_document);
    ClassDB::bind_static_method("NBNativeMaterialGraphLoader", D_METHOD("normalize", "document", "graph"),
                                &NBNativeMaterialGraphLoader::normalize);
    ClassDB::bind_static_method("NBNativeMaterialGraphLoader", D_METHOD("pin", "raw", "document", "graph"),
                                &NBNativeMaterialGraphLoader::pin);
    ClassDB::bind_static_method("NBNativeMaterialGraphLoader", D_METHOD("apply_instance", "graph", "root"),
                                &NBNativeMaterialGraphLoader::apply_instance);
    ClassDB::bind_static_method("NBNativeMaterialGraphLoader", D_METHOD("asset_path", "value"),
                                &NBNativeMaterialGraphLoader::asset_path);
    ClassDB::bind_static_method("NBNativeMaterialGraphLoader", D_METHOD("project_path", "value"),
                                &NBNativeMaterialGraphLoader::project_path);
    ClassDB::bind_static_method("NBNativeMaterialGraphLoader", D_METHOD("manifest_key", "records", "path"),
                                &NBNativeMaterialGraphLoader::manifest_key);
}
} // namespace godot
