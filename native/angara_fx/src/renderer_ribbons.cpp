#include "renderer_geometry.h"
#include "renderer_filter.h"
#include "ribbon_geometry.h"
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <algorithm>
namespace godot {
Ref<ImageTexture> NBNativeRendererGeometry::vertex_colors(const Ref<ImageTexture> &texture,
                                                          const PackedColorArray &colors) {
    if (colors.is_empty())
        return texture;
    int64_t width = std::min(colors.size(), int64_t(1024)), height = (colors.size() + width - 1) / width;
    PackedByteArray bytes = colors.to_byte_array();
    bytes.resize(width * height * 16);
    Ref<Image> image = Image::create_from_data(width, height, false, Image::FORMAT_RGBAF, bytes);
    if (texture.is_null())
        return ImageTexture::create_from_image(image);
    if (texture->get_width() == width && texture->get_height() == height)
        texture->update(image);
    else
        texture->set_image(image);
    return texture;
}
void NBNativeRendererGeometry::synchronize_ribbon(Node3D *host, Dictionary emitter, Dictionary renderer,
                                                  const Array &particles, Camera3D *camera,
                                                  const Transform3D &space) {
    Ref<Resource> source = renderer["source"];
    Ref<Material> selected = source->get("material");
    Variant raw = NBNativeRendererFilter::input(source, "MaterialUserParamBinding", Dictionary());
    if (raw.get_type() == Variant::STRING)
        raw = JSON::parse_string(raw);
    Dictionary parameter = raw.get_type() == Variant::DICTIONARY ? Dictionary(raw) : Dictionary();
    String name = UtilityFunctions::str(Dictionary(parameter.get("parameter", Dictionary())).get("name", ""));
    Dictionary values = host->get("_instance_parameters"), plan = emitter["plan"];
    Ref<Resource> emitter_source = plan["source"];
    String emitter_name = emitter_source->get("display_name");
    Array diagnostics = host->get("diagnostics");
    auto diagnostic = [&](const String &severity, const String &message) {
        Dictionary d;
        d["severity"] = severity;
        d["emitter"] = emitter_name;
        d["message"] = message;
        diagnostics.append(d);
    };
    Variant override_value = values.get(name, Variant());
    Ref<Material> override_material;
    if (override_value.get_type() == Variant::OBJECT)
        override_material = override_value;
    Object *object = renderer["instance"];
    auto *instance = Object::cast_to<MeshInstance3D>(object);
    if (!instance)
        return;
    if (override_value.get_type() != Variant::NIL && override_value.get_type() != Variant::STRING &&
        override_material.is_null()) {
        if (!bool(renderer.get("material_invalid", false)))
            diagnostic("error", "Ribbon material override must be a Material resource: " + name);
        renderer["material_invalid"] = true;
        instance->set_visible(false);
        return;
    }
    renderer["material_invalid"] = false;
    if (override_material.is_valid())
        selected = override_material;
    Array materials = renderer["materials"];
    Ref<Material> previous = renderer.get("selected_material", Variant());
    if (materials.is_empty() || previous != selected) {
        Ref<Material> material = selected.is_valid() ? Ref<Material>(selected->duplicate())
                                                     : Ref<Material>(host->call("_diagnostic_material"));
        renderer["float_vertex_colors"] = false;
        renderer["relative_time_supported"] = false;
        Ref<ShaderMaterial> shader_material = material;
        if (shader_material.is_valid()) {
            Ref<Shader> shader = shader_material->get_shader();
            if (shader.is_valid()) {
                TypedArray<Dictionary> uniforms = shader->get_shader_uniform_list();
                for (int n = 0; n < uniforms.size(); ++n) {
                    String uniform = Dictionary(uniforms[n]).get("name", "");
                    if (uniform == "nb_vertex_colors_enabled")
                        renderer["float_vertex_colors"] = true;
                    if (uniform == "nb_particle_relative_time_record")
                        renderer["relative_time_supported"] = true;
                }
            }
            if (!bool(renderer["float_vertex_colors"]))
                diagnostic(
                    "warning",
                    "Ribbon shader has no float vertex-color input. Reimport its material to preserve "
                    "signed/HDR ParticleColor; the legacy ArrayMesh COLOR path clamps to normalized bytes.");
            shader_material->set_shader_parameter("nb_renderer_kind", 2);
            shader_material->set_shader_parameter("nb_use_vertex_custom", true);
            shader_material->set_shader_parameter("nb_particle_attributes_enabled", false);
            shader_material->set_shader_parameter("nb_dynamic_parameter_valid_mask",
                                                  renderer["dynamic_mask"]);
        }
        materials = Array::make(material);
        renderer["materials"] = materials;
        renderer["selected_material"] = selected;
        instance->set_material_override(material);
    }
    host->call("_sync_renderer_primitive_bounds", renderer);
    for (int n = 0; n < materials.size(); ++n) {
        Ref<ShaderMaterial> material = materials[n];
        if (material.is_valid())
            material->set_shader_parameter("simulation_time", host->get("simulation_time"));
    }
    instance->set_visible(bool(source->get("enabled")) &&
                          bool(host->call("_render_emitter_visible", emitter)) &&
                          !bool(emitter.get("curve_parameters_invalid", false)));
    Dictionary geometry =
        NBNativeRibbonGeometry::build(particles, source, space, camera, renderer["history"], false);
    renderer["last_tick"] = host->get("_tick");
    Dictionary stats;
    for (const char *key : {"points", "segments", "subdivisions"})
        stats[key] = geometry[key];
    stats["float_vertex_colors"] = renderer.get("float_vertex_colors", false);
    renderer["geometry_statistics"] = stats;
    if (geometry.has("error") && geometry["error"] != renderer.get("geometry_error", ""))
        diagnostic("error", geometry["error"]);
    renderer["geometry_error"] = geometry.get("error", "");
    Array arrays = geometry["arrays"];
    if (arrays.is_empty()) {
        instance->set_mesh(Ref<Mesh>());
        return;
    }
    if (bool(renderer.get("relative_time_supported", false)))
        host->call("_sync_ribbon_relative_times", renderer, geometry["relative_times"]);
    if (bool(renderer.get("float_vertex_colors", false))) {
        Ref<ImageTexture> texture =
            vertex_colors(renderer.get("vertex_colors_texture", Variant()), arrays[Mesh::ARRAY_COLOR]);
        renderer["vertex_colors_texture"] = texture;
        for (int n = 0; n < materials.size(); ++n) {
            Ref<ShaderMaterial> material = materials[n];
            material->set_shader_parameter("nb_vertex_colors", texture);
            material->set_shader_parameter("nb_vertex_colors_enabled", true);
        }
    }
    Ref<ArrayMesh> mesh;
    mesh.instantiate();
    mesh->set_meta("angara_fx_immutable_geometry", true);
    uint64_t flags = 0;
    for (int shift : {Mesh::ARRAY_FORMAT_CUSTOM0_SHIFT, Mesh::ARRAY_FORMAT_CUSTOM1_SHIFT,
                      Mesh::ARRAY_FORMAT_CUSTOM2_SHIFT, Mesh::ARRAY_FORMAT_CUSTOM3_SHIFT})
        flags |= uint64_t(Mesh::ARRAY_CUSTOM_RGBA_FLOAT) << shift;
    mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays, Array(), Dictionary(), flags);
    instance->set_mesh(mesh);
}
} // namespace godot
