#include "effect_compiler.h"
#include "attribute_values.h"
#include "particle_lights.h"
#include "renderer_filter.h"
#include "renderer_geometry.h"
#include "value_evaluator.h"
#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <cmath>

namespace godot::nbcompiler {
void Compiler::renderer(const Ref<Resource> &effect, const Ref<Resource> &emitter,
                        const Ref<Resource> &source, Dictionary plan, Dictionary result, const Dictionary &p,
                        const String &backend) const {
    String kind = source->get("renderer_type");
    Array renderers = plan["renderers"];
    auto error = [&](const String &message) {
        issue(result, emitter, Ref<Resource>(), "error", message);
        plan["runnable"] = false;
    };
    auto errors = [&](const Array &messages) {
        for (int i = 0; i < messages.size(); ++i)
            error(messages[i]);
    };
    auto store = [&](const String &key, const Variant &value) {
        if (!plan.has(key))
            plan[key] = Dictionary();
        Dictionary mapping = plan[key];
        mapping[source] = value;
    };
    if (kind == "LightRenderer") {
        Dictionary lighting =
            NBNativeParticleLights::configuration(source, effect->get("source_renderer_order"));
        Array reasons = lighting["errors"];
        errors(reasons);
        if (reasons.is_empty() && !NBNativeParticleLights::proves_empty(lighting)) {
            Array unsupported = NBNativeParticleLights::translucency_configuration_errors(lighting);
            errors(unsupported);
            if (unsupported.is_empty()) {
                lighting["backend"] = "native_translucency";
                issue(result, emitter, Ref<Resource>(), "warning",
                      "Active LightRenderer currently illuminates native translucent receivers. Opaque "
                      "surfaces and volumetric fog remain unsupported; a native scene/view context is "
                      "required.");
            }
        } else if (reasons.is_empty())
            lighting["backend"] = "native_empty";
        store("light_configurations", lighting);
        renderers.append(source);
        return;
    }
    Dictionary types = plan["attribute_types"], parameter_types = plan["renderer_parameter_types"],
               layout = plan["attribute_layout"];
    errors(camera_errors(source, types, p, layout));
    Dictionary filter = NBNativeRendererFilter::configuration(source, parameter_types);
    store("renderer_filters", filter);
    errors(filter["errors"]);
    Dictionary attributes =
        NBNativeRendererViews::configuration(source, parameter_types, builtins, contracts["renderer_fields"]);
    store("renderer_attributes", attributes);
    errors(attributes["errors"]);
    errors(written_errors(source, layout));
    Array bindings = source->get("bindings");
    for (int i = 0; i < bindings.size(); ++i) {
        Dictionary attribute = bindings[i];
        String name = attribute.get("name", "");
        if (!named(kind, {"SpriteRenderer", "MeshRenderer"}) && bool(attribute.get("enabled", true)) &&
            named(name, {"DynamicMaterial", "DynamicMaterial1", "DynamicMaterial2", "DynamicMaterial3"})) {
            String expected = "Particles.DynamicMaterialParameter" + name.trim_prefix("DynamicMaterial"),
                   actual = NBNativeValues::enum_name(attribute.get("value", ""));
            if (actual != expected && actual != "None")
                error("Custom dynamic material attribute binding is not implemented: " + name);
        }
    }
    Variant hint = input(source, "SortOrderHint", 0);
    if (!number(hint) || !std::isfinite(double(hint)) || double(hint) != std::floor(double(hint)) ||
        double(hint) < -2147483648.0 || double(hint) > 2147483647.0)
        error("SortOrderHint must be a signed int32; it is a renderer rank within the Niagara component.");
    if (!named(kind, {"SpriteRenderer", "MeshRenderer", "RibbonRenderer"})) {
        error("Renderer type is not implemented by the shared runtime.");
        return;
    }
    if (kind == "RibbonRenderer") {
        Dictionary settings =
            external("ribbons", "settings", Array::make(source, "MaterialUserParamBinding"));
        String parameter = Dictionary(settings.get("parameter", Dictionary())).get("name", "");
        Variant value = p.get(parameter, Variant());
        Object *object = value.get_type() == Variant::OBJECT ? value.get_validated_object() : nullptr;
        if (value.get_type() != Variant::NIL && value.get_type() != Variant::STRING &&
            Object::cast_to<Material>(object) == nullptr)
            error("Ribbon material override must be a Material resource: " + parameter);
        errors(external("ribbons", "errors", Array::make(source)));
        if (named(backend, {"GPU", "GPUComputeSim"}))
            error("Ribbon native GPU attribute/topology execution is not implemented; source GPU emitters "
                  "remain blocked.");
        issue(result, emitter, Ref<Resource>(), "warning",
              "Ribbon Plane/Screen uses source spline/UV equations. Source view LOD factor, CVar overrides, "
              "previous motion vectors and Unreal RNG/interpolation remain unverified; geometry is generated "
              "for the active viewport camera.");
    }
    if (kind == "MeshRenderer") {
        Dictionary binding = mesh_renderer(source, p, result, emitter);
        if (!bool(binding["valid"]))
            plan["runnable"] = false;
        else {
            Dictionary meshes = plan["mesh_bindings"];
            meshes[source] = binding;
        }
    }
    Dictionary modes = NBNativeSpriteFacing::configuration(source, layout["declarations"]);
    store("sprite_modes", modes);
    if (kind == "SpriteRenderer") {
        if (!Array(contracts["sprite_facing"]).has(modes["facing"]))
            error("Unsupported sprite facing mode.");
        errors(modes["errors"]);
        if (!Array(contracts["sprite_alignment"]).has(modes["alignment"]))
            error("Unsupported sprite alignment mode.");
    }
    Vector2 subimage = NBNativeValues::vector2(input(source, "SubImageSize", Array::make(1, 1)));
    Ref<Material> material = source->get("material");
    Ref<ShaderMaterial> shader_material = material;
    if (subimage.x * subimage.y > 1.0 && material.is_valid()) {
        bool supported = false;
        if (shader_material.is_valid() && shader_material->get_shader().is_valid()) {
            TypedArray<Dictionary> uniforms = shader_material->get_shader()->get_shader_uniform_list();
            for (int i = 0; i < uniforms.size(); ++i)
                if (equal(Dictionary(uniforms[i])["name"], String("nb_particle_attributes")))
                    supported = true;
        }
        if (!supported)
            error("SubUV material must implement the nb_particle_attributes shader contract.");
    }
    String sort = mode(source, "SortMode", "None");
    if (sort != "None" && bool(input(source, "bSortOnlyWhenTranslucent", false)) &&
        input(source, "MaterialTranslucent").get_type() == Variant::NIL && shader_material.is_valid())
        issue(result, emitter, Ref<Resource>(), "warning",
              "Shader material translucency is unknown; sorting remains enabled. Supply MaterialTranslucent "
              "metadata to honor SortOnlyWhenTranslucent.");
    if (!named(sort, {"None", "ViewDepth", "ViewDistance", "CustomAscending", "CustomDecending",
                      "CustomDescending"}))
        error("Unsupported renderer sort mode: " + sort);
    for (int i = 0; i < bindings.size(); ++i) {
        Dictionary binding = bindings[i];
        if (bool(binding.get("enabled", true)) && equal(binding.get("name", ""), String("CustomSorting")) &&
            sort.begins_with("Custom")) {
            String name = NBNativeValues::enum_name(binding.get("value", "Particles.NormalizedAge"));
            if (!named(name,
                       {"Particles.NormalizedAge", "Particles.Age", "Particles.Lifetime", "Particles.ID"}))
                error("Unsupported custom sort attribute: " + name);
        }
    }
    if (kind == "SpriteRenderer" && material.is_null())
        issue(result, emitter, Ref<Resource>(), "warning",
              "Material is missing; preview uses an untextured diagnostic material.");
    renderers.append(source);
}
} // namespace godot::nbcompiler
