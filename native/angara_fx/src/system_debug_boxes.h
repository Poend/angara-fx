#pragma once
#include "value_evaluator.h"
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>

namespace godot {
inline void draw_system_boxes(Node *player, const Variant &shapes) {
    MeshInstance3D *node = Object::cast_to<MeshInstance3D>(player->get("_native_debug_boxes"));
    if (shapes.get_type() != Variant::ARRAY || Array(shapes).is_empty()) {
        if (node)
            node->hide();
        return;
    }
    Ref<ArrayMesh> mesh;
    if (!node) {
        node = memnew(MeshInstance3D);
        mesh.instantiate();
        node->set_mesh(mesh);
        node->set_as_top_level(true);
        node->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
        Ref<StandardMaterial3D> material;
        material.instantiate();
        material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
        material->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
        node->set_material_override(material);
        player->add_child(node);
        node->set_global_transform(Transform3D());
        player->set("_native_debug_boxes", node);
    } else {
        mesh = node->get_mesh();
        mesh->clear_surfaces();
    }
    node->show();
    PackedVector3Array vertices;
    PackedColorArray colors;
    Array boxes = shapes;
    for (int n = 0; n < boxes.size(); ++n) {
        Dictionary shape = boxes[n];
        Quaternion rotation = shape["rotation"];
        Vector3 center = shape["center"], extent = shape["extents"];
        Vector3 corners[8];
        for (int i = 0; i < 8; ++i) {
            Vector3 p = Vector3(i & 1 ? extent.x : -extent.x, i & 2 ? extent.y : -extent.y,
                                i & 4 ? extent.z : -extent.z);
            corners[i] = NBNativeValues::unreal_to_godot(center + rotation.xform(p));
        }
        for (int i = 0; i < 8; ++i) {
            for (int axis : {1, 2, 4}) {
                if (i & axis)
                    continue;
                vertices.append(corners[i]);
                colors.append(shape["color"]);
                vertices.append(corners[i | axis]);
                colors.append(shape["color"]);
            }
        }
    }
    Array arrays;
    arrays.resize(Mesh::ARRAY_MAX);
    arrays[Mesh::ARRAY_VERTEX] = vertices;
    arrays[Mesh::ARRAY_COLOR] = colors;
    mesh->add_surface_from_arrays(Mesh::PRIMITIVE_LINES, arrays);
}
} // namespace godot
