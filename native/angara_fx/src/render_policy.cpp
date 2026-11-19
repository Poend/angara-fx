#include "render_policy.h"
#include "value_evaluator.h"
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <algorithm>
#include <cmath>
namespace godot {
namespace {
bool equal(const Variant &a, const Variant &b) {
    Variant result;
    bool valid = false;
    Variant::evaluate(Variant::OP_EQUAL, a, b, result, valid);
    return valid && bool(result);
}
} // namespace
void NBNativeRenderPolicy::_bind_methods() {
    ClassDB::bind_static_method("NBNativeRenderPolicy", D_METHOD("refraction", "policy"),
                                &NBNativeRenderPolicy::refraction);
    ClassDB::bind_static_method("NBNativeRenderPolicy", D_METHOD("fingerprint", "policy"),
                                &NBNativeRenderPolicy::fingerprint);
    ClassDB::bind_static_method("NBNativeRenderPolicy", D_METHOD("for_effect", "effect"),
                                &NBNativeRenderPolicy::for_effect);
    ClassDB::bind_static_method("NBNativeRenderPolicy", D_METHOD("valid_coverage_policy", "policy"),
                                &NBNativeRenderPolicy::valid_coverage_policy);
    ClassDB::bind_static_method("NBNativeRenderPolicy",
                                D_METHOD("coverage_configuration", "renderer", "order", "program"),
                                &NBNativeRenderPolicy::coverage_configuration);
}
Dictionary NBNativeRenderPolicy::refraction(const Variant &raw) {
    Dictionary result;
    Array errors, limitations;
    result["errors"] = errors;
    result["limitations"] = limitations;
    result["verified"] = false;
    result["supported"] = false;
    result["enabled"] = false;
    result["high_precision_offsets"] = false;
    result["scene_color_filter"] = "point";
    result["key"] = "";
    Dictionary policy = raw.get_type() == Variant::DICTIONARY ? Dictionary(raw) : Dictionary();
    if (raw.get_type() != Variant::DICTIONARY || !equal(policy.get("schema_version", Variant()), 1) ||
        !equal(policy.get("producer", Variant()), "NiagaraBridgeSourceLibrary.DescribeRefractionPolicy") ||
        !equal(policy.get("observation_scope", Variant()), "current_process_platform_policy") ||
        policy.get("complete", Variant()).get_type() != Variant::BOOL ||
        policy.get("rhi", Variant()).get_type() != Variant::STRING ||
        policy.get("cvars", Variant()).get_type() != Variant::DICTIONARY) {
        errors.append("Missing or malformed source refraction policy.");
        return result;
    }
    Dictionary cvars = policy["cvars"];
    for (const char *name :
         {"r.RefractionQuality", "r.DisableDistortion", "r.Refraction.OffsetQuality", "r.Refraction.Blur",
          "r.Substrate", "r.SeparateTranslucency", "r.Translucency.StandardSeparated"}) {
        Variant value = cvars.get(name, Variant());
        if (value.get_type() != Variant::STRING || !String(value).is_valid_int())
            errors.append("Source refraction CVar must be an observed integer: " + String(name));
    }
    for (const char *name : {"offset_quality_platform", "shader_platform"}) {
        Variant value = policy.get(name, Variant());
        if ((value.get_type() != Variant::INT && value.get_type() != Variant::FLOAT) ||
            !std::isfinite(double(value)) || double(value) != std::floor(double(value)))
            errors.append("Source refraction platform value must be an integer: " + String(name));
    }
    if (!errors.is_empty())
        return result;
    if (!bool(policy["complete"])) {
        limitations.append("Refraction policy was observed without a complete graphics platform; export with "
                           "the source RHI enabled.");
        return result;
    }
    String rhi = policy["rhi"];
    if (rhi == "Null" || rhi == "Unavailable" || rhi.is_empty() ||
        int64_t(policy["offset_quality_platform"]) < 0 || int64_t(policy["shader_platform"]) < 0) {
        errors.append("Complete source refraction policy has no valid graphics platform.");
        return result;
    }
    result["verified"] = true;
    bool enabled = String(cvars["r.RefractionQuality"]).to_int() > 0 &&
                   String(cvars["r.DisableDistortion"]).to_int() == 0;
    bool precision = int64_t(policy["offset_quality_platform"]) > 0;
    result["enabled"] = enabled;
    result["high_precision_offsets"] = precision;
    if (enabled) {
        if (String(cvars["r.Substrate"]).to_int() != 0 && String(cvars["r.Refraction.Blur"]).to_int() != 0)
            limitations.append("Rough refraction requires a dedicated implementation.");
        if (String(cvars["r.Translucency.StandardSeparated"]).to_int() != 0)
            limitations.append("Separated standard translucency requires a dedicated composition pass.");
    }
    result["supported"] = limitations.is_empty();
    result["key"] = enabled ? "enabled:" + UtilityFunctions::str(precision) : String("disabled");
    return result;
}
String NBNativeRenderPolicy::fingerprint(const Dictionary &policy) {
    return JSON::stringify(policy).sha256_text();
}
Dictionary NBNativeRenderPolicy::for_effect(const Ref<Resource> &effect) {
    if (effect.is_null() || effect->get("source_renderer_order").get_type() != Variant::DICTIONARY)
        return refraction(Variant());
    Dictionary order = effect->get("source_renderer_order");
    Variant policy = order.get("refraction_policy", Variant());
    Dictionary result = refraction(policy);
    if (policy.get_type() != Variant::DICTIONARY)
        return result;
    if (!equal(order.get("verified", Variant()), true) ||
        !equal(order.get("source_dfs_sha256", ""), effect->get("source_hash")) ||
        !equal(order.get("refraction_policy_sha256", ""), fingerprint(policy))) {
        Array errors = result["errors"];
        errors.append("Imported refraction policy binding is stale or edited.");
        result["verified"] = false;
        result["supported"] = false;
    }
    return result;
}
bool NBNativeRenderPolicy::valid_coverage_policy(const Variant &raw) {
    if (raw.get_type() != Variant::DICTIONARY)
        return false;
    Dictionary policy = raw;
    Variant version = policy.get("schema_version", Variant());
    if ((version.get_type() != Variant::INT && version.get_type() != Variant::FLOAT) || !equal(version, 1))
        return false;
    Variant producer = policy.get("producer", Variant());
    return producer.get_type() == Variant::STRING &&
           String(producer) == "UNiagaraSettings.DefaultPixelCoverageMode" &&
           policy.get("automatic_enabled", Variant()).get_type() == Variant::BOOL;
}
Dictionary NBNativeRenderPolicy::coverage_configuration(const Ref<Resource> &renderer,
                                                        const Dictionary &order, const Ref<Script> &program) {
    Dictionary result;
    result["verified"] = true;
    result["enabled"] = false;
    result["blend"] = Color(0, 0, 0, 0);
    result["diagnostic"] = "";
    auto unknown = [&](const String &message) {
        result["verified"] = false;
        result["diagnostic"] = message;
        return result;
    };
    String mode = NBNativeValues::enum_name(renderer->call("get_input", "PixelCoverageMode", "Automatic"));
    Variant amount = renderer->call("get_input", "PixelCoverageBlend", 1.);
    if ((amount.get_type() != Variant::INT && amount.get_type() != Variant::FLOAT) ||
        !std::isfinite(double(amount)))
        return unknown("PixelCoverageBlend must be a finite scalar.");
    double blend = std::clamp(double(amount), 0., 1.);
    if (mode == "Disabled")
        return result;
    if (mode == "Automatic") {
        Variant policy = order.get("sprite_pixel_coverage_policy", Dictionary());
        Variant verified = order.get("verified", Variant());
        if (verified.get_type() != Variant::BOOL || !bool(verified) || !valid_coverage_policy(policy))
            return unknown("Automatic sprite pixel coverage has no observed source project default; coverage "
                           "remains unverified.");
        if (!bool(Dictionary(policy)["automatic_enabled"]))
            return result;
        Ref<ShaderMaterial> material = renderer->get("material");
        if (material.is_null() || !Array(program->call("validate", material)).is_empty())
            return unknown("Automatic sprite pixel coverage requires a valid imported material blend mode.");
        Ref<Resource> graph = Dictionary(material->get_meta("angara_fx_material_program"))["graph"];
        String material_blend = UtilityFunctions::str(
            Dictionary(graph->get("material_properties")).get("BlendMode", "BLEND_Opaque"));
        if (material_blend == "BLEND_Translucent")
            mode = "Enabled_A";
        else if (material_blend == "BLEND_Additive")
            mode = "Enabled_RGBA";
        else
            return result;
    }
    if (mode == "Enabled_RGBA")
        result["blend"] = Color(blend, blend, blend, blend);
    else if (mode == "Enabled_RGB")
        result["blend"] = Color(blend, blend, blend, 0);
    else if (mode == "Enabled_A")
        result["blend"] = Color(0, 0, 0, blend);
    else if (mode != "Enabled")
        return unknown("Unsupported sprite PixelCoverageMode: " + mode);
    result["enabled"] = true;
    return result;
}
} // namespace godot
