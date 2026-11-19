#pragma once
#include "geometry_packet.h"
#include "particle_view.h"
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/script.hpp>
namespace godot {
struct NBRendererRows;
struct NBRendererRow;
enum class NBFacingMode { Camera, Plane, Custom, Position, DistanceBlend, Unknown };
enum class NBAlignmentMode { Unaligned, Velocity, Custom, Other };
struct NBSpriteFrame {
    Transform3D space;
    Basis inverse, view, vector_transform;
    Vector3 camera_position, view_right, view_up, view_forward;
    bool has_camera = false, attributes_supported = false;
    String facing, alignment;
    NBFacingMode facing_mode=NBFacingMode::Unknown;
    NBAlignmentMode alignment_mode=NBAlignmentMode::Other;
    double min_blend_cm = 0., max_blend_cm = 0.;
    Variant facing_attribute, alignment_attribute, velocity_attribute;
    Vector2 pivot = Vector2(.5, .5);
};
// Renderer-specific row after attribute bindings. Numerical geometry consumes
// this directly; the public Dictionary adapter fills the same row.
struct NBSpriteRow {
    Vector3 position, custom_facing, custom_alignment, velocity;
    Vector2 size, pivot;
    double rotation = 0., uv_x = 1., uv_y = 1.;
};
struct NBCoverageFrame {
    bool enabled = false;
    Vector3 camera_position, forward;
    double width = 0., scale = 0.;
    Color blend;
};
struct NBCoverageSample {
    double size_ratio = 1.;
    Color color_scale = Color(1,1,1,1);
};
struct NBSpriteColumns {
    Ref<NBNativeParticleView> view;
    int64_t position, size, pivot, alignment, velocity, rotation, uv_x, uv_y;
    int64_t custom_facing, custom_alignment, custom_velocity;
};
struct NBMeshRow { Vector3 position,velocity,scale; Quaternion orientation; };
class NBNativeSpriteFacing : public RefCounted {
    GDCLASS(NBNativeSpriteFacing, RefCounted)
  protected:
    static void _bind_methods();

  public:
    static Dictionary configuration(const Ref<Resource> &renderer, const Dictionary &attributes);
    static Vector3 safe_normalize(const Vector3 &vector);
    static Dictionary tangents(const Dictionary &parameters);
    static Dictionary parameters(const Dictionary &config, const Dictionary &particle, Camera3D *camera,
                                 const Transform3D &space);
};
class NBNativeRendererGeometry : public RefCounted {
    GDCLASS(NBNativeRendererGeometry, RefCounted)
  protected:
    static void _bind_methods();

  public:
    static Dictionary create_renderer(Node3D *host, const Ref<Resource> &renderer, int64_t index,
                                      const Dictionary &binding, int64_t dynamic_mask);
    static Ref<ArrayMesh> opaque_sections(const Dictionary &renderer, const Array &sections);
    static Array primitive_bounds(Dictionary renderer, const Dictionary &bounds);
    static void ribbon_relative_times(Dictionary renderer, const PackedFloat32Array &times);
    static Ref<ImageTexture> vertex_colors(const Ref<ImageTexture> &texture, const PackedColorArray &colors);
    static void synchronize_ribbon(Node3D *host, Dictionary emitter, Dictionary renderer,
                                   const Array &particles, Camera3D *camera, const Transform3D &space);
    static void synchronize_player(Node3D *host, const Ref<Script> &compiler,const Dictionary &builtins=Dictionary());
    static void dispose_composite(Node *host);
    static int64_t composite_signature(Node3D *host, const Ref<Script> &exposure);
    static Ref<Material> composite_ribbon_material(Node *host, const Ref<Resource> &source);
    static String material_pass(const Ref<Material> &material, const Ref<Script> &program);
    static void prepare_composite(Node3D *host, const Ref<Script> &composer, const Ref<Script> &program,
                                  const Ref<Script> &composite_type);
    static void composite_unavailable(Node *host, const String &message);
    static void synchronize_composite(Node3D *host, Camera3D *camera, const Ref<Script> &compiler,
                                      const Dictionary &builtins);
    static void release_lights(Node *host);
    static void synchronize_lights(Node3D *host, Camera3D *camera, const Ref<Script> &pass_type);
    static void release_surface(Node *host);
    static void synchronize_surface(Node3D *host, Camera3D *camera, const Ref<Script> &pass_type);
    static void synchronize_refraction(Node *host, Camera3D *camera, const Ref<Script> &pass_type);
    static Dictionary mesh_resource_parameters(Node *host, const Ref<Resource> &source);
    static void bind_renderer_volumes(Dictionary renderer, const Dictionary &targets);
    static Ref<Material> volume_material(const Ref<Material> &source, const Dictionary &bindings, bool copy);
    static void refresh_mesh_resources(Node *host, const Ref<Script> &compiler);
    static void refresh_dynamic_masks(Node *host);
    static Dictionary renderer_view(Node3D *host, Dictionary emitter, const Ref<Resource> &source, const Dictionary &builtins);
    static void renderer_view_errors(Node3D *host, Dictionary emitter, const Ref<Resource> &source, const Array &errors);
    static void refresh_coverage(Node *host, const Ref<Script> &policy);
    static bool validate_temporal(Node3D *host);
    static Ref<Material> diagnostic_material();
    static void synchronize(Dictionary renderer, Dictionary emitter, const Array &particles, Camera3D *camera,
                            const Transform3D &owner, const Dictionary &frame, const Dictionary &builtins,
                            const String &offset_name, bool mesh);
    static Dictionary synchronize_resident(Dictionary renderer, Dictionary emitter, Camera3D *camera,
                                            const Transform3D &owner, const Dictionary &frame,
                                            const Dictionary &builtins, const Dictionary &parameters,
                                            const String &offset_name, bool mesh);
    static Dictionary composite_records(const Array &states, Camera3D *camera, const Transform3D &owner,
                                        const Dictionary &frame, const Dictionary &builtins, int64_t tick);
    static NBGeometryPacket composite_packet(const Array &states, Camera3D *camera, const Transform3D &owner,
                                              const Dictionary &frame, const Dictionary &builtins, int64_t tick);
    static Array packet_records(const std::vector<NBGeometryRecord> &records);
    static Dictionary coverage(const Dictionary &config, const Vector2 &size_cm, const Vector3 &position,
                               Camera3D *camera);
    static NBCoverageFrame prepare_coverage(const Dictionary &config, Camera3D *camera);
    static NBCoverageSample coverage_prepared(const NBCoverageFrame &frame, const Vector2 &size_cm,
                                              const Vector3 &position);
    static NBCoverageSample coverage_sample(const Dictionary &config, const Vector2 &size_cm,
                                             const Vector3 &position, Camera3D *camera);
    static Transform3D sprite_transform(const Dictionary &renderer, const Dictionary &particle,
                                        Camera3D *camera, const Transform3D &space, double camera_offset,
                                        double size_ratio);
    static NBSpriteFrame prepare_sprite_frame(const Dictionary &renderer, Camera3D *camera,
                                               const Transform3D &space);
    static Transform3D sprite_transform_prepared(const NBSpriteFrame &frame, const Dictionary &particle,
                                                 double camera_offset, double size_ratio);
    static NBSpriteRow sprite_row(const NBSpriteFrame &frame, const Dictionary &particle);
    static NBSpriteColumns sprite_columns(const NBSpriteFrame &frame, const Ref<NBNativeParticleView> &view);
    static NBSpriteRow sprite_resident_row(const NBSpriteFrame &frame, const NBSpriteColumns &columns,
                                           int64_t lane, const Dictionary &overrides);
    static NBSpriteRow sprite_renderer_row(const NBSpriteFrame &frame, const NBSpriteColumns &columns,
                                            const NBRendererRows &rows, const NBRendererRow &row);
    static Transform3D sprite_transform_row(const NBSpriteFrame &frame, const NBSpriteRow &particle,
                                             double camera_offset, double size_ratio);
    static Transform3D sprite_transform_resident(const Dictionary &renderer, const Ref<NBNativeParticleView> &view,
                                                  int64_t lane, const Dictionary &overrides, Camera3D *camera,
                                                  const Transform3D &space, double camera_offset, double size_ratio);
    static Transform3D mesh_transform(const Ref<Resource> &entry, const Dictionary &particle, bool local,
                                      const Transform3D &owner, Camera3D *camera, double camera_offset,
                                      const Dictionary &facing);
    static Transform3D mesh_transform_row(const Ref<Resource> &entry,const NBMeshRow &particle,bool local,
                                           const Transform3D &owner,Camera3D *camera,double offset,const Dictionary &facing);
    static bool mesh_selected(const Dictionary &renderer, const Dictionary &particle);
    static Array sorted_particles(const Array &particles, const Ref<Resource> &renderer, Camera3D *camera,
                                  const Transform3D &space);
    static void write_attributes(const Ref<Image> &image, int64_t index, const Dictionary &particle,
                                 const Color &attributes);
    static double camera_offset(const String &name, Dictionary particle, Dictionary emitter,
                                const Dictionary &frame, const Dictionary &builtins);
};
} // namespace godot
