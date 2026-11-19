#include "scene_context.h"
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <cmath>
#include <cstring>
#include <limits>
namespace godot {
using namespace nbmaterial;
TypedArray<String> NBNativeSceneContext::validate(const Ref<Resource> &context) {
    TypedArray<String> errors;
    int64_t policy = context->get("component_sort_policy");
    Vector3 axis = context->get("component_sort_axis");
    if (policy < 0 || policy > 2 || !axis.is_finite() || (policy == 2 && !axis.is_normalized()))
        errors.append("Source scene sort policy/axis is invalid.");
    Ref<Resource> fog = context->get("height_fog");
    if (fog.is_null())
        errors.append("Source scene context must explicitly describe fog, including no fog.");
    else
        errors.append_array(NBNativeMaterialAuthoring::validate_height_fog(fog));
    return errors;
}
bool NBNativeSceneContext::same_gpu_values(const Variant &a, const Variant &b) {
    if (nbdocument::number(a) && nbdocument::number(b)) {
        float left = float(double(a)), right = float(double(b));
        uint32_t x, y;
        std::memcpy(&x, &left, sizeof(x));
        std::memcpy(&y, &right, sizeof(y));
        return x == y;
    }
    if (a.get_type() == Variant::DICTIONARY && b.get_type() == Variant::DICTIONARY) {
        Dictionary x = a, y = b;
        if (x.size() != y.size())
            return false;
        Array keys = x.keys();
        for (int64_t i = 0; i < keys.size(); ++i)
            if (!y.has(keys[i]) || !same_gpu_values(x[keys[i]], y[keys[i]]))
                return false;
        return true;
    }
    if (a.get_type() == Variant::ARRAY && b.get_type() == Variant::ARRAY) {
        Array x = a, y = b;
        if (x.size() != y.size())
            return false;
        for (int64_t i = 0; i < x.size(); ++i)
            if (!same_gpu_values(x[i], y[i]))
                return false;
        return true;
    }
    return a == b;
}
bool NBNativeSceneContext::matches_render_inputs(const Ref<Resource> &context, const Ref<Resource> &other) {
    if (other.is_null())
        return false;
    Ref<Resource> left = context->get("height_fog"), right = other->get("height_fog");
    if (left.is_null() || right.is_null())
        return false;
    return context->get("component_sort_policy") == other->get("component_sort_policy") &&
           context->get("component_sort_axis") == other->get("component_sort_axis") &&
           context->get("source_cvars") == other->get("source_cvars") &&
           same_gpu_values(NBNativeMaterialAuthoring::height_fog_uniforms(left),
                           NBNativeMaterialAuthoring::height_fog_uniforms(right)) &&
           same_gpu_values(context->get("lights"), other->get("lights"));
}
double NBNativeSceneContext::number(const Dictionary &record, const String &key, TypedArray<String> errors) {
    Variant value = record.get(key, Variant());
    if (!nbdocument::number(value) || !std::isfinite(double(value))) {
        errors.append("Missing/nonfinite source scene number: " + key);
        return 0.0;
    }
    return value;
}
bool NBNativeSceneContext::boolean(const Dictionary &record, const String &key, TypedArray<String> errors) {
    Variant value = record.get(key, Variant());
    if (value.get_type() != Variant::BOOL) {
        errors.append("Missing source scene boolean: " + key);
        return false;
    }
    return value;
}
Vector3 NBNativeSceneContext::vector(const Dictionary &record, const String &key, TypedArray<String> errors) {
    Variant raw = record.get(key, Variant());
    if (raw.get_type() != Variant::ARRAY || Array(raw).size() != 3) {
        errors.append("Source scene vector requires three numbers: " + key);
        return Vector3();
    }
    Array values = raw;
    Vector3 result;
    for (int i = 0; i < 3; ++i) {
        Dictionary single;
        single[key] = values[i];
        result[i] = number(single, key, errors);
    }
    return result;
}
Vector3 NBNativeSceneContext::direction_ue(const Vector3 &v) {
    return Vector3(v.y, v.z, -v.x);
}
int64_t NBNativeSceneContext::cvar(const Dictionary &values, const String &name, TypedArray<String> errors) {
    Variant raw = values.get(name, Variant());
    if (raw == Variant("true"))
        return 1;
    if (raw == Variant("false"))
        return 0;
    if (raw.get_type() != Variant::STRING || !String(raw).is_valid_int()) {
        errors.append("Missing observed source scene integer CVar: " + name);
        return 0;
    }
    return String(raw).to_int();
}
Dictionary NBNativeSceneContext::from_packet(const Variant &raw, const Ref<Script> &schema,
                                             const Ref<Script> &fog_schema) {
    TypedArray<String> errors;
    Dictionary result = dict({{"context", Variant()}, {"errors", errors}}),
               packet = raw.get_type() == Variant::DICTIONARY ? Dictionary(raw) : Dictionary();
    if (raw.get_type() != Variant::DICTIONARY ||
        !nbdocument::equal(packet.get("schema_version", Variant()), 1) ||
        packet.get("producer", Variant()) != Variant("NiagaraBridgeSceneContext.Describe") ||
        packet.get("observation_scope", Variant()) !=
            Variant("registered_world_components_and_project_settings") ||
        packet.get("complete", Variant()) != Variant(true)) {
        errors.append("Missing or incomplete native scene-context observation.");
        return result;
    }
    if (packet.get("lights", Variant()).get_type() != Variant::ARRAY ||
        packet.get("height_fogs", Variant()).get_type() != Variant::ARRAY ||
        packet.get("cvars", Variant()).get_type() != Variant::DICTIONARY ||
        packet.get("additional_scene_components", Variant()).get_type() != Variant::ARRAY ||
        packet.get("world", Variant()).get_type() != Variant::STRING) {
        errors.append("Malformed native scene-context collections.");
        return result;
    }
    Ref<Resource> context = schema->call("new");
    context->set("source_world", packet["world"]);
    context->set("source_packet_sha256", JSON::stringify(packet, "", true).sha256_text());
    Dictionary cvars = Dictionary(packet["cvars"]).duplicate(true);
    context->set("source_cvars", cvars);
    bool pre_exposure = boolean(packet, "renderer_pre_exposure_observed", errors),
         lighting_volume = boolean(packet, "lighting_volume_observed", errors);
    context->set("renderer_pre_exposure_observed", pre_exposure);
    context->set("lighting_volume_observed", lighting_volume);
    if (pre_exposure || lighting_volume)
        errors.append("Component-input packets cannot certify evaluated renderer view/lighting buffers.");
    double policy = number(packet, "translucent_sort_policy", errors);
    if (policy != std::floor(policy))
        errors.append("Source sort policy must be an integer.");
    int64_t policy_value = policy >= 9223372036854775808.0 || policy < -9223372036854775808.0
                               ? std::numeric_limits<int64_t>::min()
                               : int64_t(policy);
    context->set("component_sort_policy", policy_value);
    context->set("component_sort_axis", direction_ue(vector(packet, "translucent_sort_axis_ue", errors)));
    Array additional = packet["additional_scene_components"];
    TypedArray<String> components = context->get("additional_scene_components");
    for (int64_t i = 0; i < additional.size(); ++i) {
        if (additional[i].get_type() != Variant::STRING)
            errors.append("Additional scene component identity must be text.");
        else
            components.append(additional[i]);
    }
    Array input_lights = packet["lights"];
    TypedArray<Dictionary> lights = context->get("lights");
    for (int64_t i = 0; i < input_lights.size(); ++i) {
        if (input_lights[i].get_type() != Variant::DICTIONARY) {
            errors.append("Malformed source light record.");
            continue;
        }
        Dictionary source = input_lights[i], light = source.duplicate(true);
        light["position_godot_m"] = direction_ue(vector(source, "position_ue_cm", errors)) * .01;
        light["direction_godot"] = direction_ue(vector(source, "direction_ue", errors));
        Vector3 color = vector(source, "renderer_color_linear", errors);
        if (MIN(color.x, MIN(color.y, color.z)) < 0)
            errors.append("Source light color must be nonnegative.");
        for (const char *flag : {"affects_translucency", "cast_shadows", "light_function", "ies_profile"})
            boolean(source, flag, errors);
        if (source.get("class", Variant()).get_type() != Variant::STRING ||
            source.get("path", Variant()).get_type() != Variant::STRING)
            errors.append("Source light requires class/path identity.");
        lights.append(light);
    }
    Ref<Resource> fog = fog_schema->call("new");
    fog->set("enabled", false);
    context->set("height_fog", fog);
    Array fogs = packet["height_fogs"];
    if (fogs.size() > 1)
        errors.append("Multiple source height-fog components require native renderer selection evidence.");
    else if (fogs.size() == 1) {
        if (fogs[0].get_type() != Variant::DICTIONARY) {
            errors.append("Malformed source height fog.");
            return result;
        }
        Dictionary source = fogs[0];
        fog->set("enabled", true);
        for (const char *key :
             {"density", "height_falloff", "height_cm", "second_density", "second_height_falloff",
              "second_height_offset_cm", "maximum_opacity", "start_distance_cm", "end_distance_cm",
              "cutoff_distance_cm", "directional_exponent", "directional_start_distance_cm"})
            fog->set(key, number(source, key, errors));
        Vector3 color = vector(source, "scattering_color_linear", errors);
        fog->set("scattering_color", Color(color.x, color.y, color.z));
        bool directional = boolean(source, "directional_enabled", errors);
        fog->set("directional_enabled", directional);
        Vector3 component_color = vector(source, "directional_component_color_linear", errors);
        if (!boolean(source, "directional_selection_verified", errors) &&
            component_color.length_squared() > 0)
            errors.append("Native directional fog light selection is ambiguous.");
        if (directional) {
            fog->set("direction_to_light", direction_ue(vector(source, "direction_to_light_ue", errors)));
            color = vector(source, "directional_renderer_color_linear", errors);
            fog->set("directional_color", Color(color.x, color.y, color.z));
        }
        TypedArray<String> unsupported = fog->get("unsupported_source_features");
        if (boolean(source, "volumetric", errors))
            unsupported.append("volumetric_fog");
        if (boolean(source, "cubemap", errors))
            unsupported.append("cubemap");
        if (number(source, "skylight_capture_strength", errors) > 0 &&
            cvar(cvars, "r.SupportsSkyLightCaptureAffectsHeightFogInScattering", errors) != 0)
            unsupported.append("skylight_capture");
        if (cvar(cvars, "r.SupportExpFogMatchesVolumetricFog", errors) != 0)
            unsupported.append("expfog_matches_volumetric");
        if (components.has("SkyAtmosphereComponent") &&
            cvar(cvars, "r.SupportSkyAtmosphereAffectsHeightFog", errors) != 0)
            unsupported.append("sky_atmosphere");
        if (components.has("LocalFogVolumeComponent"))
            unsupported.append("local_fog_volumes");
        bool ortho = cvar(cvars, "r.Ortho.FogHeightAdjustment", errors) != 0;
        fog->set("orthographic_height_adjustment", ortho);
        Dictionary camera = packet.get("camera_inputs", Dictionary());
        if (nbdocument::equal(camera.get("projection_type", 0), 1) && ortho)
            errors.append("Orthographic source fog requires observed camera-to-target distance.");
    }
    errors.append_array(validate(context));
    if (errors.is_empty())
        result["context"] = context;
    return result;
}
void NBNativeSceneContext::_bind_methods() {
    ClassDB::bind_static_method("NBNativeSceneContext", D_METHOD("validate", "context"),
                                &NBNativeSceneContext::validate);
    ClassDB::bind_static_method("NBNativeSceneContext", D_METHOD("matches_render_inputs", "context", "other"),
                                &NBNativeSceneContext::matches_render_inputs);
    ClassDB::bind_static_method("NBNativeSceneContext", D_METHOD("same_gpu_values", "a", "b"),
                                &NBNativeSceneContext::same_gpu_values);
    ClassDB::bind_static_method("NBNativeSceneContext", D_METHOD("number", "record", "key", "errors"),
                                &NBNativeSceneContext::number);
    ClassDB::bind_static_method("NBNativeSceneContext", D_METHOD("boolean", "record", "key", "errors"),
                                &NBNativeSceneContext::boolean);
    ClassDB::bind_static_method("NBNativeSceneContext", D_METHOD("vector", "record", "key", "errors"),
                                &NBNativeSceneContext::vector);
    ClassDB::bind_static_method("NBNativeSceneContext", D_METHOD("direction_ue", "vector"),
                                &NBNativeSceneContext::direction_ue);
    ClassDB::bind_static_method("NBNativeSceneContext", D_METHOD("cvar", "values", "name", "errors"),
                                &NBNativeSceneContext::cvar);
    ClassDB::bind_static_method("NBNativeSceneContext",
                                D_METHOD("from_packet", "packet", "schema", "fog_schema"),
                                &NBNativeSceneContext::from_packet);
}
} // namespace godot
