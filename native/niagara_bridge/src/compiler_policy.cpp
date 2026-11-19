#include "ribbon_geometry.h"
#include "vm_events.h"
#include "effect_controller.h"
#include "effect_compiler.h"
#include "value_evaluator.h"
#include <godot_cpp/classes/json.hpp>

namespace godot {
Dictionary NBNativeRibbonGeometry::settings(const Ref<Resource> &source, const String &key) {
    Variant value = source.is_valid() ? source->call("get_input", key, Dictionary()) : Variant(Dictionary());
    if (value.get_type() == Variant::STRING)
        value = JSON::parse_string(value);
    return value.get_type() == Variant::DICTIONARY ? Dictionary(value) : Dictionary();
}
TypedArray<String> NBNativeRibbonGeometry::errors(const Ref<Resource> &source) {
    using nbcompiler::equal;
    using nbcompiler::named;
    TypedArray<String> result;
    auto input = [&](const String &key, const Variant &fallback = Variant()) {
        return source->call("get_input", key, fallback);
    };
    auto flag = [&](const Variant &value, const String &name) {
        if (value.get_type() == Variant::BOOL || nbcompiler::number(value))
            return bool(value);
        result.append("Ribbon Boolean input is invalid: " + name);
        return false;
    };
    if (!equal(NBNativeValues::enum_name(input("Shape", "Plane")), String("Plane")))
        result.append("Only Plane ribbon shape is implemented.");
    if (!named(NBNativeValues::enum_name(input("FacingMode", "Screen")), {"Screen", "Custom", "CustomSideVector"}))
        result.append("Unsupported ribbon FacingMode.");
    if (input("bLinkOrderUseUniqueID").get_type() == Variant::NIL)
        result.append("Ribbon fallback ordering requires native bLinkOrderUseUniqueID metadata; it cannot be "
                      "inferred from an old asset's omitted default.");
    if (flag(input("bUseGPUInit", false), "bUseGPUInit"))
        result.append("Native ribbon GPU topology generation is not implemented.");
    if (flag(input("bEnableAccurateGeometry", false), "bEnableAccurateGeometry") ||
        flag(input("bUseMaterialBackfaceCulling", false), "bUseMaterialBackfaceCulling"))
        result.append("Accurate/multiple-sided ribbon geometry and material backface-culling branches are "
                      "unsupported.");
    if (int64_t(input("WidthSegmentationCount", 1)) < 1 || int64_t(input("WidthSegmentationCount", 1)) > 16)
        result.append("Ribbon width segmentation must be between 1 and 16.");
    if (!named(NBNativeValues::enum_name(input("TessellationMode", "Automatic")),
               {"Automatic", "Disabled", "Custom"}))
        result.append("Unsupported ribbon tessellation mode.");
    if (!named(NBNativeValues::enum_name(input("DrawDirection", "FrontToBack")),
               {"FrontToBack", "BackToFront"}))
        result.append("Unsupported ribbon draw direction.");
    for (const char *key : {"UV0Settings", "UV1Settings"}) {
        Dictionary uv = settings(source, key);
        if (!named(uv.get("distributionMode", "ScaledUsingRibbonSegmentLength"),
                   {"ScaledUniformly", "ScaledUsingRibbonSegmentLength", "TiledOverRibbonLength"}))
            result.append(String("Unsupported ribbon UV distribution: ") + key);
        for (const char *edge : {"leadingEdgeMode", "trailingEdgeMode"})
            if (!named(uv.get(edge, "Locked"), {"Locked", "SmoothTransition"}))
                result.append(String("Unsupported ribbon UV edge mode: ") + key + "." + edge);
        if (flag(uv.get("bEnablePerParticleUOverride", false),
                 String(key) + ".bEnablePerParticleUOverride") ||
            flag(uv.get("bEnablePerParticleVRangeOverride", false),
                 String(key) + ".bEnablePerParticleVRangeOverride"))
            result.append("Per-particle ribbon UV overrides are not implemented.");
        if (equal(uv.get("distributionMode", ""), String("TiledOverRibbonLength")) &&
            double(uv.get("tilingLength", 100.0)) <= 0.0)
            result.append("Ribbon tiling length must be positive.");
    }
    Dictionary standard;
    for (const char *name : {"Position", "Color", "Velocity", "NormalizedAge", "RibbonTwist", "RibbonWidth",
                             "RibbonFacing", "RibbonLinkOrder", "MaterialRandom", "RibbonUVDistance"})
        standard[name] = name;
    standard["RibbonId"] = "RibbonID";
    for (int i = 0; i < 4; ++i) {
        String suffix = i == 0 ? String() : String::num_int64(i);
        standard["DynamicMaterial" + suffix] = "DynamicMaterialParameter" + suffix;
    }
    standard["U0Override"] = "RibbonU0Override";
    standard["U1Override"] = "RibbonU1Override";
    standard["V0RangeOverride"] = "RibbonV0RangeOverride";
    standard["V1RangeOverride"] = "RibbonV1RangeOverride";
    for (const char *name : {"Position", "RibbonWidth", "RibbonFacing", "RibbonTwist"})
        standard[String("Prev") + name] = String("Previous.") + name;
    Array bindings = source->get("bindings");
    for (int i = 0; i < bindings.size(); ++i) {
        Dictionary binding = bindings[i];
        if (!bool(binding.get("enabled", true)))
            continue;
        String name = binding.get("name", ""), value = NBNativeValues::enum_name(binding.get("value", ""));
        if (name == "RendererEnabled" || (name.begins_with("DynamicMaterial") && value == "None"))
            continue;
        if (!standard.has(name) || !equal(value, "Particles." + String(standard[name])))
            result.append("Unsupported custom ribbon binding: " + name);
    }
    return result;
}
Dictionary NBNativeVMEvents::graph(const Array &plans) {
    using nbcompiler::equal;
    Dictionary result, names, dependencies;
    Array errors, order;
    result["errors"] = errors;
    result["order"] = order;
    for (int i = 0; i < plans.size(); ++i) {
        Dictionary plan = plans[i];
        Ref<Resource> source = plan["source"];
        Variant name = source->get("display_name");
        names[name] = plan;
        dependencies[name] = Array();
    }
    Array keys = names.keys();
    for (int i = 0; i < keys.size(); ++i) {
        String name = keys[i];
        Dictionary plan = names[name];
        Array deps = dependencies[name];
        Array modules = Dictionary(plan.get("gpu_readers",Dictionary())).values();
        for (int i = 0; i < modules.size(); ++i) {
            String source = Dictionary(modules[i])["source"];
            if (source == name || !names.has(source) || !bool(Dictionary(names[source])["runnable"])) {
                errors.append("Unavailable indexed GPU particle source: " + name + " <- " + source);
                plan["runnable"] = false;
            } else if (!deps.has(source)) deps.append(source);
        }
        Array grid_stages=Dictionary(plan.get("grid_stages",Dictionary())).get("stages",Array());
        for(int s=0;s<grid_stages.size();++s) {
            String reader=Dictionary(grid_stages[s]).get("reader","");
            if(reader.is_empty()) continue;
            if(reader==name || !names.has(reader) || !bool(Dictionary(names[reader])["runnable"])) {
                errors.append("Unavailable particle scatter source: "+name+" <- "+reader);
                plan["runnable"]=false;
            } else if(!deps.has(reader)) deps.append(reader);
        }
        Array stages =
            Dictionary(Dictionary(plan.get("source_rng", Dictionary())).get("vm_stages", Dictionary()))
                .values();
        auto available = [&](const String &source) {
            return names.has(source) && bool(Dictionary(names[source])["runnable"]) &&
                   bool(Dictionary(names[source]).get("native_vm_requested", false));
        };
        for (int s = 0; s < stages.size(); ++s) {
            Array interfaces = Dictionary(stages[s]).get("interfaces", Array());
            for (int d = 0; d < interfaces.size(); ++d) {
                Dictionary descriptor = interfaces[d],
                           reader = descriptor.get("particle_reader", Dictionary());
                if (!equal(descriptor.get("type", Variant()), String("NiagaraDataInterfaceParticleRead")) ||
                    !bool(reader.get("resolved", false)))
                    continue;
                String source = reader.get("source_emitter", "");
                if (!available(source)) {
                    errors.append("Native particle reader source is unavailable: " + name + " <- " + source);
                    plan["runnable"] = false;
                    continue;
                }
                if (source != name && !deps.has(source))
                    deps.append(source);
            }
        }
        Array handlers = NBNativeEffectController::handlers(plan);
        for (int h = 0; h < handlers.size(); ++h) {
            Dictionary handler = handlers[h], settings = handler["settings"];
            String source = settings["source_emitter"];
            if (!available(source)) {
                errors.append("Native event source is unavailable: " + name + " <- " + source);
                plan["runnable"] = false;
                continue;
            }
            bool found = false;
            Array source_stages =
                Dictionary(Dictionary(Dictionary(names[source]).get("source_rng", Dictionary()))
                               .get("vm_stages", Dictionary()))
                    .values();
            for (int s = 0; s < source_stages.size(); ++s) {
                Array outputs =
                    Dictionary(Dictionary(source_stages[s])["program"]).get("event_outputs", Array());
                for (int o = 0; o < outputs.size(); ++o)
                    if (equal(Dictionary(outputs[o])["name"], settings["source_event"]))
                        found = true;
            }
            if (!found) {
                errors.append("Native event generator is absent: " + source + "/" +
                              String(settings["source_event"]));
                plan["runnable"] = false;
            }
            if (source != name && !deps.has(source))
                deps.append(source);
        }
    }
    while (order.size() < names.size()) {
        bool advanced = false;
        for (int i = 0; i < keys.size(); ++i) {
            if (order.has(keys[i]))
                continue;
            Array deps = dependencies[keys[i]];
            bool ready = true;
            for (int d = 0; d < deps.size(); ++d)
                if (!order.has(deps[d]))
                    ready = false;
            if (ready) {
                order.append(keys[i]);
                advanced = true;
            }
        }
        if (!advanced) {
            for (int i = 0; i < keys.size(); ++i)
                if (!order.has(keys[i])) {
                    Dictionary plan = names[keys[i]];
                    plan["runnable"] = false;
                }
            errors.append("Cyclic inter-emitter event execution dependencies are not supported.");
            break;
        }
    }
    return result;
}
} // namespace godot
