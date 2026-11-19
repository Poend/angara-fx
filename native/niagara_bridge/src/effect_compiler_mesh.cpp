#include "effect_compiler.h"
#include "mesh_orientation.h"
#include "value_evaluator.h"
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/json.hpp>

namespace godot::nbcompiler {
namespace {
template <class T> Ref<T> object(const Variant &v) {
    if (v.get_type() != Variant::OBJECT)
        return Ref<T>();
    return Ref<T>(Object::cast_to<T>(v.get_validated_object()));
}
bool attributes_shader(const Ref<Material> &material) {
    Ref<ShaderMaterial> shader_material = material;
    if (shader_material.is_null() || shader_material->get_shader().is_null())
        return false;
    TypedArray<Dictionary> uniforms = shader_material->get_shader()->get_shader_uniform_list();
    for (int i = 0; i < uniforms.size(); ++i)
        if (equal(Dictionary(uniforms[i])["name"], String("nb_particle_attributes")))
            return true;
    return false;
}
} // namespace
Dictionary Compiler::mesh_renderer(const Ref<Resource> &renderer, const Dictionary &p, Dictionary result,
                                   const Ref<Resource> &emitter) const {
    Dictionary binding;
    binding["valid"] = true;
    binding["entry"] = Variant();
    binding["mesh"] = Variant();
    binding["materials"] = Array();
    Array entries;
    binding["entries"] = entries;
    TypedArray<String> reasons;
    Dictionary facing = NBNativeMeshFacing::configuration(renderer);
    binding["facing"] = facing;
    reasons.append_array(facing["errors"]);
    Array meshes = renderer->get("mesh_entries"), surface_materials = renderer->get("surface_materials"),
          parameter_names = renderer->get("material_parameter_names");
    if (meshes.is_empty())
        reasons.append("MeshRenderer has no resolved mesh entries.");
    for (int index = 0; index < meshes.size(); ++index) {
        Ref<Resource> entry = meshes[index];
        Array materials;
        binding["entry"] = entry;
        binding["materials"] = materials;
        Dictionary row;
        row["entry"] = entry;
        row["mesh"] = Variant();
        row["materials"] = materials;
        row["facing"] = facing;
        row["source_index"] = index;
        if (entry.is_null()) {
            reasons.append("Mesh entry " + String::num_int64(index) + " is missing.");
            entries.append(row);
            continue;
        }
        if (!equal(entry->get("lod_index"), 0))
            reasons.append("Mesh LOD selection currently supports LOD 0 only.");
        if (Transform3D(entry->get("local_transform")).origin != Vector3())
            reasons.append("Mesh-entry translation must use pivot_offset; local_transform stores only "
                           "rotation and scale.");
        if (!named(entry->get("pivot_space"), {"Mesh", "Simulation", "Local", "World"}))
            reasons.append("Unsupported mesh pivot space: " + String(entry->get("pivot_space")));
        String parameter = entry->get("user_parameter");
        Variant override = p.get(parameter, Variant());
        Ref<Mesh> mesh_override = object<Mesh>(override);
        if (override.get_type() != Variant::NIL && mesh_override.is_null() &&
            override.get_type() != Variant::STRING)
            reasons.append("Mesh parameter override must be a Mesh resource: " + parameter);
        Ref<Mesh> mesh = mesh_override.is_valid() ? mesh_override : object<Mesh>(entry->get("mesh"));
        binding["mesh"] = mesh;
        if (mesh.is_null())
            reasons.append("Mesh asset is unresolved" +
                           (!parameter.is_empty() ? ": " + parameter : String(".")));
        else if (mesh->get_surface_count() == 0)
            reasons.append("Resolved mesh has no renderable surfaces.");
        else {
            for (int slot = 0; slot < mesh->get_surface_count(); ++slot) {
                Ref<Material> material;
                if (slot < parameter_names.size()) {
                    Variant candidate = p.get(parameter_names[slot], Variant());
                    material = object<Material>(candidate);
                    if (material.is_null() && candidate.get_type() != Variant::NIL &&
                        candidate.get_type() != Variant::STRING)
                        reasons.append("Mesh material override must be a Material resource: " +
                                       String(parameter_names[slot]));
                }
                if (material.is_null() && slot < surface_materials.size())
                    material = object<Material>(surface_materials[slot]);
                if (material.is_null())
                    material = object<Material>(renderer->get("material"));
                if (material.is_null())
                    material = mesh->surface_get_material(slot);
                if (material.is_null())
                    issue(result, emitter, Ref<Resource>(), "warning",
                          "Mesh surface " + String::num_int64(slot) +
                              " has no mapped material; it uses the diagnostic material.");
                Vector2 subimage =
                    NBNativeValues::vector2(input(renderer, "SubImageSize", Array::make(1, 1)));
                if (subimage.x * subimage.y > 1.0 && material.is_valid() && !attributes_shader(material))
                    reasons.append("Mesh SubUV surface " + String::num_int64(slot) +
                                   " must implement nb_particle_attributes.");
                materials.append(material);
            }
            if (surface_materials.size() > mesh->get_surface_count() ||
                parameter_names.size() > mesh->get_surface_count())
                issue(result, emitter, Ref<Resource>(), "warning",
                      "Mesh material bindings beyond the imported surface count are retained but have no "
                      "surface to render.");
        }
        row["mesh"] = mesh;
        entries.append(row);
    }
    Variant raw = input(renderer, "Meshes", "");
    if (raw.get_type() == Variant::ARRAY || (raw.get_type() == Variant::STRING && !String(raw).is_empty())) {
        Variant parsed = raw.get_type() == Variant::STRING ? JSON::parse_string(raw) : raw;
        if (parsed.get_type() != Variant::ARRAY)
            reasons.append(
                "Source Meshes metadata could not be decoded; its LOD/binding branches are unresolved.");
        else {
            Array rows = parsed;
            for (int i = 0; i < rows.size(); ++i) {
                if (rows[i].get_type() != Variant::DICTIONARY) {
                    reasons.append("Source mesh entry metadata is malformed.");
                    continue;
                }
                Dictionary row = rows[i];
                if (!equal(row.get("lODMode", "LODLevel"), String("LODLevel")) ||
                    int64_t(row.get("lODLevel", 0)) != 0 || int64_t(row.get("lODBias", 0)) != 0 ||
                    bool(row.get("bUseLODRange", false)))
                    reasons.append(
                        "Source mesh LOD mode/range/bias is outside the supported fixed LOD 0 branch.");
                for (const char *property : {"lODLevelBinding", "lODBiasBinding"}) {
                    String name = Dictionary(Dictionary(row.get(property, Dictionary()))
                                                 .get("resolvedParameter", Dictionary()))
                                      .get("name", "None");
                    if (!named(name, {"", "None"}))
                        reasons.append("Dynamic mesh LOD binding is not implemented: " + name);
                }
            }
        }
    }
    Variant meshes_binding = input(renderer, "MeshesBinding");
    if (meshes_binding.get_type() == Variant::STRING && String(meshes_binding).begins_with("{"))
        meshes_binding = JSON::parse_string(meshes_binding);
    if (meshes_binding.get_type() == Variant::DICTIONARY &&
        !named(
            Dictionary(Dictionary(meshes_binding).get("resolvedParameter", Dictionary())).get("name", "None"),
            {"", "None"}))
        reasons.append("Mesh-array parameter binding is not implemented.");
    Dictionary defaults;
    for (const char *name : {"Position", "Velocity", "Color", "Scale", "MeshOrientation", "MeshIndex",
                             "MaterialRandom", "NormalizedAge", "SubImageIndex", "CameraOffset"})
        defaults[name] = String("Particles.") + name;
    for (int i = 0; i < 4; ++i) {
        String suffix = i == 0 ? String() : String::num_int64(i);
        defaults["DynamicMaterial" + suffix] = "Particles.DynamicMaterialParameter" + suffix;
    }
    defaults["RendererVisibilityTag"] = "Particles.VisibilityTag";
    defaults["RendererEnabled"] = "None";
    for (const char *name : {"Position", "Scale", "MeshOrientation", "CameraOffset", "Velocity"})
        defaults[String("Prev") + name] = String("Particles.Previous.") + name;
    if (mode(renderer, "MotionVectorSetting", "AutoDetect") != "Disable")
        issue(
            result, emitter, Ref<Resource>(), "warning",
            "Mesh current-frame transforms are implemented; Niagara previous-frame motion-vector output and "
            "AutoDetect project settings are not reproduced. Motion-blur/TAA equivalence is unverified.");
    Array attributes = renderer->get("bindings");
    Dictionary shared_fields = contracts["renderer_fields"];
    for (int i = 0; i < attributes.size(); ++i) {
        Dictionary attribute = attributes[i];
        String name = attribute.get("name", "");
        if (!bool(attribute.get("enabled", true)) || name == "CustomSorting")
            continue;
        if (named(name, {"CameraOffset", "RendererEnabled", "RendererVisibilityTag"}) ||
            shared_fields.has(name))
            continue;
        if (named(name, {"DynamicMaterial", "DynamicMaterial1", "DynamicMaterial2", "DynamicMaterial3"}) &&
            NBNativeValues::enum_name(attribute.get("value", "")) == "None")
            continue;
        if (!defaults.has(name) ||
            !equal(NBNativeValues::enum_name(attribute.get("value", "")), defaults[name]))
            reasons.append("Custom mesh attribute binding is not implemented: " + name);
    }
    for (int i = 0; i < reasons.size(); ++i)
        issue(result, emitter, Ref<Resource>(), "error", reasons[i]);
    binding["valid"] = reasons.is_empty();
    if (!entries.is_empty())
        for (const char *key : {"entry", "mesh", "materials", "source_index"})
            binding[key] = Dictionary(entries[0])[key];
    return binding;
}
} // namespace godot::nbcompiler
