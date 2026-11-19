#include "renderer_geometry.h"
#include "renderer_filter.h"
#include "value_evaluator.h"
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/multi_mesh_instance3d.hpp>
#include <godot_cpp/classes/multi_mesh.hpp>
#include <godot_cpp/classes/quad_mesh.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/image_texture.hpp>
namespace godot {
Dictionary NBNativeRendererGeometry::create_renderer(Node3D *host, const Ref<Resource> &renderer,
                                                     int64_t index, const Dictionary &binding,
                                                     int64_t dynamic_mask) {
    Dictionary result;
    String kind = renderer->get("renderer_type");
    if (kind == "RibbonRenderer") {
        auto *instance = memnew(MeshInstance3D);
        instance->set_name("Ribbon_" + String::num_int64(index));
        instance->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
        host->add_child(instance);
        instance->set_as_top_level(true);
        instance->set_global_transform(Transform3D());
        result["source"] = renderer;
        result["instance"] = instance;
        result["materials"] = Array();
        result["selected_material"] = Variant();
        result["dynamic_mask"] = dynamic_mask;
        result["history"] = Dictionary();
        result["last_tick"] = -1;
        return result;
    }
    Ref<Mesh> mesh;
    TypedArray<Material> materials;
    auto material_copy = [&](const Ref<Material> &selected) -> Ref<Material> {
        return selected.is_valid() ? Ref<Material>(selected->duplicate())
                                   : Ref<Material>(host->call("_diagnostic_material"));
    };
    if (kind == "MeshRenderer") {
        Ref<Mesh> original = binding["mesh"];
        mesh = original->duplicate();
        Array selected = binding["materials"];
        for (int slot = 0; slot < selected.size(); ++slot) {
            Ref<Material> material = material_copy(selected[slot]);
            materials.append(material);
            mesh->surface_set_material(slot, material);
        }
    } else {
        Ref<QuadMesh> quad;
        quad.instantiate();
        quad->set_size(Vector2(1, 1));
        mesh = quad;
        materials.append(material_copy(renderer->get("material")));
    }
    mesh->set_meta("angara_fx_immutable_geometry", true);
    Ref<MultiMesh> multimesh;
    multimesh.instantiate();
    multimesh->set_transform_format(MultiMesh::TRANSFORM_3D);
    multimesh->set_use_colors(true);
    multimesh->set_use_custom_data(true);
    multimesh->set_mesh(mesh);
    auto *instance = memnew(MultiMeshInstance3D);
    instance->set_name("Particles_" + String::num_int64(index));
    instance->set_multimesh(multimesh);
    instance->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
    if (kind == "SpriteRenderer")
        instance->set_material_override(materials[0]);
    host->add_child(instance);
    instance->set_as_top_level(true);
    instance->set_global_transform(Transform3D());
    Ref<Image> image = Image::create_empty(5, 1, false, Image::FORMAT_RGBAF);
    image->fill(Color(0, 0, 1, 1));
    Ref<ImageTexture> texture = ImageTexture::create_from_image(image);
    bool supported = false;
    for (int n = 0; n < materials.size(); ++n) {
        Ref<ShaderMaterial> material = materials[n];
        if (material.is_null())
            continue;
        material->set_shader_parameter("nb_renderer_kind", kind == "SpriteRenderer" ? 1
                                                           : kind == "MeshRenderer" ? 3
                                                                                    : 0);
        material->set_shader_parameter("nb_dynamic_parameter_valid_mask", dynamic_mask);
        bool attributes = false;
        Ref<Shader> shader = material->get_shader();
        if (shader.is_valid()) {
            TypedArray<Dictionary> uniforms = shader->get_shader_uniform_list();
            for (int i = 0; i < uniforms.size(); ++i)
                if (String(Dictionary(uniforms[i]).get("name", "")) == "nb_particle_attributes")
                    attributes = true;
        }
        if (!attributes)
            continue;
        supported = true;
        material->set_shader_parameter("nb_particle_attributes", texture);
        material->set_shader_parameter("nb_particle_attributes_enabled", true);
        material->set_shader_parameter("nb_particle_attribute_stride", 5);
        material->set_shader_parameter(
            "nb_subimage_size",
            NBNativeValues::vector2(NBNativeRendererFilter::input(renderer, "SubImageSize", Vector2(1, 1))));
        material->set_shader_parameter(
            "nb_subimage_blend", bool(NBNativeRendererFilter::input(renderer, "bSubImageBlend", false)));
    }
    result["source"] = renderer;
    result["instance"] = instance;
    result["materials"] = materials;
    result["dynamic_mask"] = dynamic_mask;
    result["mesh_binding"] = binding;
    result["full_mesh"] = mesh;
    result["mesh_aabb"] = mesh->get_aabb();
    result["resource_parameter_values"] = host->call("_mesh_resource_parameters", renderer);
    result["capacity"] = 0;
    result["attributes_image"] = image;
    result["attributes_texture"] = texture;
    result["attributes_supported"] = supported;
    return result;
}
} // namespace godot
