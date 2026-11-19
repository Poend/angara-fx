#include "renderer_geometry.h"
#include "renderer_filter.h"
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <algorithm>
#include <cmath>
namespace godot {
Ref<Material> NBNativeRendererGeometry::volume_material(const Ref<Material> &source, const Dictionary &volumes, bool copy) {
    Ref<ShaderMaterial> material = source;
    if (material.is_null() || volumes.is_empty())
        return source;
    Dictionary program = material->get_meta("niagara_bridge_material_program", Dictionary());
    Array bindings = program.get("bindings", Array());
    for (int b = 0; b < bindings.size(); ++b) {
        Dictionary binding = bindings[b];
        String parameter = binding.get("parameter", "");
        if (String(binding.get("kind", "")) != "volume_texture" || !volumes.has(parameter))
            continue;
        if (copy && material.ptr() == source.ptr())
            material = source->duplicate();
        material->set_shader_parameter(binding["uniform"], volumes[parameter]);
    }
    return material;
}

void NBNativeRendererGeometry::bind_renderer_volumes(Dictionary renderer, const Dictionary &targets) {
    if (targets.is_empty())
        return;
    Variant raw = NBNativeRendererFilter::input(renderer["source"], "MaterialParameters");
    if (raw.get_type() == Variant::STRING)
        raw = JSON::parse_string(raw);
    if (raw.get_type() != Variant::DICTIONARY)
        return;
    Array attrs = Dictionary(raw).get("attributeBindings", Array());
    Dictionary volumes;
    for (int a = 0; a < attrs.size(); ++a) {
        Dictionary attr = attrs[a], variable = attr.get("niagaraVariable", Dictionary()),
                   child = attr.get("niagaraChildVariable", Dictionary());
        String key = variable.get("name", ""), child_name = child.get("name", "None");
        String parameter = attr.get("materialParameterName", "");
        if (targets.has(key) && targets[key].get_type() != Variant::NIL && child_name == "None" &&
            !parameter.is_empty())
            volumes[parameter] = targets[key];
    }
    if (volumes.is_empty())
        return;
    renderer["grid_volume_targets"] = targets;
    renderer["grid_volume_parameters"] = volumes;
    Array materials = renderer["materials"];
    for (int m = 0; m < materials.size(); ++m)
        volume_material(materials[m], volumes, false);
}

Ref<ArrayMesh> NBNativeRendererGeometry::opaque_sections(const Dictionary &renderer, const Array &sections) {
    Ref<Mesh> original = renderer["full_mesh"];
    Ref<ArrayMesh> source = original;
    if (source.is_valid()) {
        Ref<ArrayMesh> retained = source->duplicate();
        for (int section = retained->get_surface_count() - 1; section >= 0; --section)
            if (!sections.has(section))
                retained->surface_remove(section);
        return retained;
    }
    Ref<ArrayMesh> result;
    result.instantiate();
    Array materials = renderer["materials"];
    for (int n = 0; n < sections.size(); ++n) {
        int section = sections[n];
        // Mesh exposes these as virtuals; concrete primitives do not have
        // ArrayMesh's public surface_get_format/primitive_type methods.
        Dictionary surface = RenderingServer::get_singleton()->mesh_get_surface(original->get_rid(), section);
        uint64_t flags = 0, format = surface.get("format", 0);
        for (int shift : {Mesh::ARRAY_FORMAT_CUSTOM0_SHIFT, Mesh::ARRAY_FORMAT_CUSTOM1_SHIFT,
                          Mesh::ARRAY_FORMAT_CUSTOM2_SHIFT, Mesh::ARRAY_FORMAT_CUSTOM3_SHIFT})
            flags |= format & (uint64_t(Mesh::ARRAY_FORMAT_CUSTOM_MASK) << shift);
        result->add_surface_from_arrays(
            Mesh::PrimitiveType(int64_t(surface.get("primitive", Mesh::PRIMITIVE_TRIANGLES))),
            original->surface_get_arrays(section), Array(), Dictionary(), flags);
        result->surface_set_material(result->get_surface_count() - 1, materials[section]);
    }
    return result;
}
Array NBNativeRendererGeometry::primitive_bounds(Dictionary renderer, const Dictionary &bounds) {
    Array diagnostics, selected, materials = renderer["materials"];
    for (int n = 0; n < materials.size(); ++n) {
        Ref<ShaderMaterial> material = materials[n];
        if (material.is_valid() &&
            (material->get_shader_parameter("nb_mesh_bounds_min").get_type() != Variant::NIL ||
             material->get_shader_parameter("nb_object_world_center").get_type() != Variant::NIL))
            selected.append(material);
    }
    if (selected.is_empty())
        return diagnostics;
    bool verified = bounds.get("verified", false);
    Variant reasons = bounds.get("diagnostics", Array());
    int64_t status = Array::make(verified, reasons).hash();
    Variant previous = renderer.get("primitive_bounds_status", Variant());
    if (previous.get_type() != Variant::INT || int64_t(previous) != status) {
        renderer["primitive_bounds_status"] = status;
        if (!verified) {
            Dictionary diagnostic;
            diagnostic["severity"] = "warning";
            diagnostic["code"] = "primitive_material_bounds_unverified";
            diagnostic["message"] =
                "Material component bounds are unverified: " + UtilityFunctions::str(reasons);
            diagnostics.append(diagnostic);
        }
    }
    Dictionary box = bounds.get("primitive_local_bounds_cm", Dictionary());
    Variant world = bounds.get("world_bounds_godot",Variant());
    if(world.get_type()==Variant::AABB) {
        for(int i=0;i<selected.size();++i) {
            Ref<ShaderMaterial> material=selected[i];
            if(material->get_shader_parameter("nb_object_world_center").get_type()!=Variant::NIL)
                material->set_shader_parameter("nb_object_world_center",AABB(world).get_center());
        }
    }
    if (!bool(box.get("valid", false)))
        return diagnostics;
    Array minimum = box["min_cm"], maximum = box["max_cm"];
    Vector3 lo = Vector3(minimum[1], minimum[2], -double(maximum[0])) * .01;
    Vector3 size = Vector3(double(maximum[1]) - double(minimum[1]), double(maximum[2]) - double(minimum[2]),
                           double(maximum[0]) - double(minimum[0])) *
                   .01;
    for (int n = 0; n < selected.size(); ++n) {
        Ref<ShaderMaterial> material = selected[n];
        if(material->get_shader_parameter("nb_mesh_bounds_min").get_type()==Variant::NIL) continue;
        material->set_shader_parameter("nb_mesh_bounds_min", lo);
        material->set_shader_parameter("nb_mesh_bounds_size", size);
    }
    return diagnostics;
}
void NBNativeRendererGeometry::ribbon_relative_times(Dictionary renderer, const PackedFloat32Array &times) {
    if (times.size() > int64_t(renderer.get("relative_time_capacity", 0))) {
        int64_t capacity = std::max(int64_t(16), times.size() * 2),
                width = std::min(capacity * 5, int64_t(1024)),
                height = int64_t(std::ceil(double(capacity * 5) / double(width)));
        renderer["relative_time_capacity"] = capacity;
        Ref<Image> image = Image::create_empty(width, height, false, Image::FORMAT_RGBAF);
        renderer["relative_time_image"] = image;
        Ref<ImageTexture> texture = renderer.get("relative_time_texture", Variant());
        if (texture.is_valid())
            texture->set_image(image);
        else
            renderer["relative_time_texture"] = ImageTexture::create_from_image(image);
    }
    Ref<Image> image = renderer.get("relative_time_image", Variant());
    Ref<ImageTexture> texture = renderer.get("relative_time_texture", Variant());
    if (image.is_null() || texture.is_null())
        return;
    for (int64_t index = 0; index < times.size(); ++index) {
        int64_t offset = index * 5;
        image->set_pixel(offset % image->get_width(), offset / image->get_width(), Color(0, 0, 1, 1));
        offset += 4;
        image->set_pixel(offset % image->get_width(), offset / image->get_width(),
                         Color(times[index], 0, 0, 0));
    }
    texture->update(image);
    Array materials = renderer["materials"];
    for (int n = 0; n < materials.size(); ++n) {
        Ref<ShaderMaterial> material = materials[n];
        if (material.is_null())
            continue;
        material->set_shader_parameter("nb_particle_attributes", texture);
        material->set_shader_parameter("nb_particle_attributes_enabled", true);
        material->set_shader_parameter("nb_particle_attribute_stride", 5);
    }
}
} // namespace godot
