#pragma once
#include "geometry_packet.h"
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <vector>
namespace godot {
class NBNativeCompositeRenderer : public RefCounted {
    GDCLASS(NBNativeCompositeRenderer, RefCounted)
    uint64_t instance_id = 0;
    Ref<ShaderMaterial> material;
    Ref<ArrayMesh> draw_mesh;
    Ref<Image> attributes_image;
    Ref<ImageTexture> attributes_texture;
    Array quad_arrays;
    Dictionary statistics, bounds_context;
    int capacity = 0;
    std::vector<float> attributes;
    bool write_values(int index, const Color &parameters, const Color &color,
                      const Color &dynamic1, const Color &dynamic2, const Color &dynamic3,
                      double relative_time, const Transform3D &transform);
    bool write(int index, const Dictionary &record, const Transform3D &transform);
    void commit_image();
    void fail(const String &message);

  protected:
    static void _bind_methods();

  public:
    static constexpr int ATTRIBUTE_STRIDE = 10, TEXTURE_WIDTH = 1024;
    void configure(Node3D *parent, const Ref<ShaderMaterial> &value, int priority, double distance_offset);
    void dispose();
    Dictionary material_view(Camera3D *camera, bool physical_light_units);
    void update(const Array &records, double time, const Variant &bounds_override = Variant());
    void update_packet(const std::vector<NBGeometryRecord> &records, double time,
                       const Variant &bounds_override = Variant());
    void write_record(int index, const Dictionary &record, const Transform3D &transform);
    static Color custom(const Array &arrays, int slot, int index);
    MeshInstance3D *get_instance() const;
    Ref<ShaderMaterial> get_material() const {
        return material;
    }
    Ref<Image> get_attributes_image() const {
        return attributes_image;
    }
    void set_attributes_image(const Ref<Image> &value);
    Ref<ImageTexture> get_attributes_texture() const {
        return attributes_texture;
    }
    Dictionary get_statistics() const {
        return statistics;
    }
    Dictionary get_bounds_context() const {
        return bounds_context;
    }
    int get_capacity() const {
        return capacity;
    }
};
} // namespace godot
