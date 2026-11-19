#include "particle_lights.h"
#include "renderer_filter.h"
#include "value_evaluator.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <cmath>
namespace godot {
namespace {
const char *BINDINGS[][3] = {{"position", "PositionBinding", "position"},
                             {"color", "ColorBinding", "color"},
                             {"radius", "RadiusBinding", "float"},
                             {"scattering", "VolumetricScatteringBinding", "float"},
                             {"enabled", "LightRenderingEnabledBinding", "bool"},
                             {"visibility", "RendererVisibilityTagBinding", "int"},
                             {"exponent", "LightExponentBinding", "float"},
                             {"specular", "SpecularScaleBinding", "float"},
                             {"diffuse", "DiffuseScaleBinding", "float"}};
Variant input(const Ref<Resource> &renderer, const String &key) {
    return NBNativeRendererFilter::input(renderer, key);
}
} // namespace
void NBNativeParticleLights::_bind_methods() {
    ClassDB::bind_static_method("NBNativeParticleLights", D_METHOD("_number", "value"),
                                &NBNativeParticleLights::number);
    ClassDB::bind_static_method("NBNativeParticleLights", D_METHOD("_int32", "value"),
                                &NBNativeParticleLights::int32);
    ClassDB::bind_static_method("NBNativeParticleLights", D_METHOD("_vector", "value", "size"),
                                &NBNativeParticleLights::vector);
    ClassDB::bind_static_method("NBNativeParticleLights", D_METHOD("_typed", "value", "type"),
                                &NBNativeParticleLights::typed);
    ClassDB::bind_static_method("NBNativeParticleLights", D_METHOD("policy_errors", "policy"),
                                &NBNativeParticleLights::policy_errors);
    ClassDB::bind_static_method("NBNativeParticleLights",
                                D_METHOD("configuration", "renderer", "source_order"),
                                &NBNativeParticleLights::configuration);
    ClassDB::bind_static_method("NBNativeParticleLights", D_METHOD("proves_empty", "config"),
                                &NBNativeParticleLights::proves_empty);
    ClassDB::bind_static_method("NBNativeParticleLights",
                                D_METHOD("translucency_configuration_errors", "config"),
                                &NBNativeParticleLights::translucency_configuration_errors);
    ClassDB::bind_static_method(
        "NBNativeParticleLights",
        D_METHOD("generate", "config", "particles", "parameters", "owner", "local", "primitive_id"),
        &NBNativeParticleLights::generate, DEFVAL(0));
    ClassDB::bind_static_method("NBNativeParticleLights",
                                D_METHOD("exposure_scale", "exposure", "inverse_blend"),
                                &NBNativeParticleLights::exposure_scale);
    ClassDB::bind_static_method("NBNativeParticleLights",
                                D_METHOD("translucency_lights", "records", "exposure"),
                                &NBNativeParticleLights::translucency_lights);
}
bool NBNativeParticleLights::number(const Variant &v) {
    return (v.get_type() == Variant::INT || v.get_type() == Variant::FLOAT) && std::isfinite(double(v));
}
bool NBNativeParticleLights::int32(const Variant &v) {
    return NBNativeRendererFilter::int32(v);
}
bool NBNativeParticleLights::vector(const Variant &v, int64_t size) {
    if (v.get_type() != Variant::ARRAY || Array(v).size() != size)
        return false;
    Array parts = v;
    for (int64_t n = 0; n < size; ++n)
        if (!number(parts[n]))
            return false;
    return true;
}
bool NBNativeParticleLights::typed(const Variant &v, const String &type) {
    if (type == "bool")
        return v.get_type() == Variant::BOOL;
    if (type == "int")
        return int32(v);
    if (type == "float")
        return number(v);
    if (type == "position")
        return v.get_type() == Variant::VECTOR3 && Vector3(v).is_finite();
    if (type == "color") {
        if (v.get_type() != Variant::COLOR)
            return false;
        Color c = v;
        return std::isfinite(c.r) && std::isfinite(c.g) && std::isfinite(c.b) && std::isfinite(c.a);
    }
    return false;
}
TypedArray<String> NBNativeParticleLights::policy_errors(const Variant &value) {
    TypedArray<String> errors;
    if (value.get_type() != Variant::DICTIONARY) {
        errors.append("Missing native Niagara particle-light policy.");
        return errors;
    }
    Dictionary p = value;
    if ((!number(p.get("schema_version", Variant())) || double(p["schema_version"]) != 1.) ||
        p.get("producer", Variant()) != Variant("NiagaraBridgeSourceLibrary.NiagaraLightDefaults")) {
        errors.append("Missing native Niagara particle-light policy.");
        return errors;
    }
    if (p.get("rendering_enabled", Variant()).get_type() != Variant::BOOL)
        errors.append("Missing native particle-light rendering switch.");
    for (const char *key :
         {"default_radius_cm", "default_volumetric_scattering", "default_inverse_exposure_blend"})
        if (!number(p.get(key, Variant())))
            errors.append(String("Invalid particle-light policy: ") + key);
    if (!vector(p.get("default_color", Variant()), 4))
        errors.append("Invalid particle-light default color.");
    return errors;
}
Dictionary NBNativeParticleLights::configuration(const Ref<Resource> &renderer, const Dictionary &order) {
    Variant raw_policy = order.get("particle_light_policy", Variant());
    Dictionary result, bindings, defaults;
    TypedArray<String> errors = policy_errors(raw_policy);
    result["errors"] = errors;
    result["bindings"] = bindings;
    result["defaults"] = defaults;
    if (order.get("verified", Variant()) != Variant(true))
        errors.append("Particle lights require verified native source settings.");
    if (!errors.is_empty())
        return result;
    Dictionary policy = raw_policy;
    result["rendering_enabled"] =
        bool(policy["rendering_enabled"]) && renderer.is_valid() && bool(renderer->get("enabled"));
    String mode = NBNativeValues::enum_name(input(renderer, "SourceMode"));
    result["source_mode"] = mode;
    if (mode != "Particles" && mode != "Emitter")
        errors.append("Unknown light renderer source mode.");
    for (const char *key :
         {"bUseInverseSquaredFalloff", "bAffectsTranslucency", "bAllowMegaLights", "bMegaLightsCastShadows",
          "bAlphaScalesBrightness", "bOverrideInverseExposureBlend"}) {
        Variant v = input(renderer, key);
        if (v.get_type() != Variant::BOOL)
            errors.append(String("Missing particle-light flag: ") + key);
        else
            result[key] = v;
    }
    for (const char *key :
         {"RadiusScale", "DefaultExponent", "SpecularScale", "DiffuseScale", "InverseExposureBlend"}) {
        Variant v = input(renderer, key);
        if (!number(v))
            errors.append(String("Invalid particle-light scalar: ") + key);
        else
            result[key] = double(v);
    }
    Variant visibility = input(renderer, "RendererVisibility");
    if (!int32(visibility))
        errors.append("Invalid particle-light visibility tag.");
    else
        result["RendererVisibility"] = int64_t(visibility);
    Variant add = input(renderer, "ColorAdd");
    if (!vector(add, 3))
        errors.append("Invalid particle-light ColorAdd.");
    else {
        Array a = add;
        result["ColorAdd"] = Vector3(a[0], a[1], a[2]);
    }
    for (auto &pair : BINDINGS) {
        Variant raw_binding = input(renderer, pair[1]);
        Dictionary b, variable;
        bool valid = raw_binding.get_type() == Variant::DICTIONARY;
        if (valid) {
            b = raw_binding;
            valid = b.get("bBindingExistsOnSource", Variant()).get_type() == Variant::BOOL &&
                    b.get("bIsCachedParticleValue", Variant()).get_type() == Variant::BOOL &&
                    b.get("paramMapVariable", Variant()).get_type() == Variant::DICTIONARY &&
                    b.get("dataSetName", Variant()).get_type() == Variant::STRING;
        }
        if (valid) {
            variable = b["paramMapVariable"];
            valid = variable.get("name", Variant()).get_type() == Variant::STRING;
        }
        if (!valid) {
            errors.append(String("Missing native particle-light binding: ") + pair[1]);
            continue;
        }
        Dictionary entry;
        entry["parameter"] = variable["name"];
        entry["attribute"] = String("Particles.") + String(b["dataSetName"]);
        entry["particle"] = b["bIsCachedParticleValue"];
        entry["exists"] = b["bBindingExistsOnSource"];
        entry["type"] = pair[2];
        bindings[pair[0]] = entry;
    }
    Variant enabled = input(renderer, "RendererEnabledBinding");
    Dictionary enabled_variable;
    bool enabled_valid =
        enabled.get_type() == Variant::DICTIONARY &&
        Dictionary(enabled).get("paramMapVariable", Variant()).get_type() == Variant::DICTIONARY;
    if (enabled_valid) {
        enabled_variable = Dictionary(enabled)["paramMapVariable"];
        enabled_valid = enabled_variable.get("name", Variant()).get_type() == Variant::STRING;
    }
    if (!enabled_valid)
        errors.append("Missing light renderer enable binding.");
    else
        result["renderer_enabled_parameter"] = enabled_variable["name"];
    if (!errors.is_empty())
        return result;
    result["inverse_exposure_blend"] = bool(result["bOverrideInverseExposureBlend"])
                                           ? result["InverseExposureBlend"]
                                           : Variant(double(policy["default_inverse_exposure_blend"]));
    Array color = policy["default_color"];
    defaults["position"] = Vector3();
    defaults["color"] = Color(color[0], color[1], color[2], color[3]);
    defaults["radius"] = double(policy["default_radius_cm"]);
    defaults["scattering"] = double(policy["default_volumetric_scattering"]);
    defaults["enabled"] = true;
    defaults["visibility"] = result["RendererVisibility"];
    defaults["exponent"] = result["DefaultExponent"];
    defaults["specular"] = result["SpecularScale"];
    defaults["diffuse"] = result["DiffuseScale"];
    return result;
}
bool NBNativeParticleLights::proves_empty(const Dictionary &c) {
    if (!Array(c.get("errors", Array())).is_empty())
        return false;
    if (!bool(c.get("rendering_enabled", true)))
        return true;
    Dictionary bindings = c.get("bindings", Dictionary()), radius = bindings.get("radius", Dictionary()),
               defaults = c.get("defaults", Dictionary());
    return c.get("source_mode", Variant()) == Variant("Particles") &&
           radius.get("particle", Variant()) == Variant(true) &&
           radius.get("exists", Variant()) == Variant(false) &&
           String(radius.get("parameter", "")).begins_with("Particles.") &&
           double(defaults.get("radius", -1.)) == 0.;
}
TypedArray<String> NBNativeParticleLights::translucency_configuration_errors(const Dictionary &c) {
    TypedArray<String> errors;
    if (!bool(c.get("bAffectsTranslucency", false)))
        errors.append("Active LightRenderer has no implemented receiver backend: opaque-only particle lights "
                      "remain unsupported.");
    if (bool(c.get("bAllowMegaLights", true)))
        errors.append("Active LightRenderer MegaLights selection/shadow policy is not integrated.");
    return errors;
}
Dictionary NBNativeParticleLights::generate(const Dictionary &c, const Array &particles,
                                            const Dictionary &parameters, const Transform3D &owner,
                                            bool local, int64_t primitive_id) {
    Dictionary result;
    TypedArray<String> errors = Array(c["errors"]).duplicate();
    TypedArray<Dictionary> lights;
    result["errors"] = errors;
    result["lights"] = lights;
    if (!errors.is_empty() || !bool(c["rendering_enabled"]))
        return result;
    String parameter = c["renderer_enabled_parameter"];
    Variant enabled =
        parameter != "" && parameter != "None" ? parameters.get(parameter, true) : Variant(true);
    if (enabled.get_type() != Variant::BOOL) {
        errors.append("Light renderer enable parameter must be boolean.");
        return result;
    }
    if (!bool(enabled))
        return result;
    Dictionary defaults = Dictionary(c["defaults"]).duplicate(), bindings = c["bindings"];
    for (auto &pair : BINDINGS) {
        Dictionary binding = bindings[pair[0]];
        defaults[pair[0]] = parameters.get(binding["parameter"], defaults[pair[0]]);
        if (!typed(defaults[pair[0]], binding["type"]))
            errors.append(String("Invalid renderer-bound light parameter: ") + String(binding["parameter"]));
    }
    if (!errors.is_empty())
        return result;
    bool particle_source = String(c["source_mode"]) == "Particles";
    Array rows;
    if (particle_source)
        rows = particles;
    else
        rows.append(Dictionary());
    for (int n = 0; n < rows.size(); ++n) {
        Dictionary p = rows[n], attributes = p.get("attributes", Dictionary()), values = defaults.duplicate();
        if (particle_source)
            for (auto &pair : BINDINGS) {
                Dictionary binding = bindings[pair[0]];
                if (!bool(binding["particle"]) || !bool(binding["exists"]))
                    continue;
                Variant v = attributes.get(binding["attribute"], Variant());
                if (!typed(v, binding["type"]))
                    errors.append(String("Missing/invalid native light attribute: ") +
                                  String(binding["attribute"]));
                else
                    values[pair[0]] = v;
            }
        if (!errors.is_empty())
            break;
        double radius = double(values["radius"]) * double(c["RadiusScale"]);
        if (!bool(values["enabled"]) || values["visibility"] != c["RendererVisibility"] || radius <= 0.)
            continue;
        Color color = values["color"];
        Vector3 rgb =
                    Vector3(color.r, color.g, color.b) * (bool(c["bAlphaScalesBrightness"]) ? color.a : 1.) +
                    Vector3(c["ColorAdd"]),
                position = values["position"];
        if (local) {
            Vector3 world = owner.xform(Vector3(position.y, position.z, -position.x) * .01);
            position = Vector3(-world.z, world.x, world.y) * 100.;
        }
        if (!position.is_finite() || !rgb.is_finite() || !std::isfinite(radius)) {
            errors.append("Particle-light transform or color/radius overflow.");
            break;
        }
        Variant id = particle_source ? attributes.get("Particles.UniqueID", -1) : Variant(0);
        if (!int32(id)) {
            errors.append("Invalid particle-light UniqueID.");
            break;
        }
        Dictionary record;
        record["position_ue_cm"] = Array::make(position.x, position.y, position.z);
        record["color_linear"] = Array::make(rgb.x, rgb.y, rgb.z);
        record["radius_cm"] = radius;
        record["exponent"] = bool(c["bUseInverseSquaredFalloff"]) ? Variant(0.) : values["exponent"];
        record["inverse_exposure_blend"] = c["inverse_exposure_blend"];
        record["affects_translucency"] = c["bAffectsTranslucency"];
        record["allow_megalights"] = c["bAllowMegaLights"];
        record["megalights_cast_shadows"] = c["bMegaLightsCastShadows"];
        record["volumetric_scattering"] = values["scattering"];
        record["specular_scale"] = values["specular"];
        record["diffuse_scale"] = values["diffuse"];
        record["light_id"] = Array::make(int64_t(id), primitive_id);
        lights.append(record);
    }
    if (!errors.is_empty())
        lights.clear();
    return result;
}
double NBNativeParticleLights::exposure_scale(double exposure, double inverse_blend) {
    return exposure > 0. ? UtilityFunctions::exp(-inverse_blend * UtilityFunctions::log(exposure)) : 1.;
}
Dictionary NBNativeParticleLights::translucency_lights(const Array &records, double exposure) {
    Dictionary result;
    TypedArray<String> errors;
    TypedArray<Dictionary> lights;
    result["errors"] = errors;
    result["lights"] = lights;
    if (!std::isfinite(exposure)) {
        errors.append("Particle-light exposure must be finite.");
        return result;
    }
    for (int n = 0; n < records.size(); ++n) {
        Dictionary light = records[n];
        bool valid = vector(light.get("position_ue_cm", Variant()), 3) &&
                     vector(light.get("color_linear", Variant()), 3);
        for (const char *key : {"radius_cm", "exponent", "inverse_exposure_blend"})
            valid = valid && number(light.get(key, Variant()));
        if (!valid || light.get("affects_translucency", Variant()).get_type() != Variant::BOOL) {
            errors.append("Malformed native simple-light record.");
            continue;
        }
        if (!bool(light["affects_translucency"]) || double(light["radius_cm"]) <= 0.)
            continue;
        if (light.get("allow_megalights", Variant()).get_type() != Variant::BOOL ||
            light.get("megalights_cast_shadows", Variant()).get_type() != Variant::BOOL) {
            errors.append("Missing native simple-light MegaLights participation flags.");
            continue;
        }
        if (bool(light["allow_megalights"])) {
            errors.append(
                "MegaLights selection/shadow policy is not integrated with simple-light TLV injection.");
            continue;
        }
        if (double(light["exponent"]) < 0.) {
            errors.append("Negative simple-light falloff exponents are not implemented.");
            continue;
        }
        double scale = exposure_scale(exposure, light["inverse_exposure_blend"]);
        Array source = light["color_linear"], color;
        for (int i = 0; i < source.size(); ++i)
            color.append(double(source[i]) * scale);
        if (!vector(color, 3)) {
            errors.append("Nonfinite particle-light exposure result.");
            continue;
        }
        Dictionary record;
        record["class"] = "NiagaraSimpleLight";
        record["position_ue_cm"] = Array(light["position_ue_cm"]).duplicate();
        record["direction_ue"] = Array::make(1, 0, 0);
        record["renderer_color_linear"] = color;
        record["attenuation_radius_cm"] = light["radius_cm"];
        record["falloff_exponent"] = light["exponent"];
        record["inverse_squared"] = double(light["exponent"]) == 0.;
        lights.append(record);
    }
    if (!errors.is_empty())
        lights.clear();
    return result;
}
} // namespace godot
