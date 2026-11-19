#include "renderer_rows.h"
#include "renderer_geometry.h"
#include "renderer_filter.h"
#include "particle_context.h"
#include "mesh_orientation.h"
#include "value_evaluator.h"
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/multi_mesh_instance3d.hpp>
#include <godot_cpp/classes/multi_mesh.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <algorithm>
#include <cmath>
#include <vector>
namespace godot {
namespace {
Basis conversion() {
    return Basis(Vector3(0, 0, -1), Vector3(1, 0, 0), Vector3(0, 1, 0));
}
Variant input(const Ref<Resource> &renderer, const String &key, const Variant &fallback = Variant()) {
    return NBNativeRendererFilter::input(renderer, key, fallback);
}
Dictionary settings(const Ref<Resource> &renderer, const String &key) {
    Variant value = input(renderer, key, Dictionary());
    if (value.get_type() == Variant::STRING)
        value = JSON::parse_string(value);
    return value.get_type() == Variant::DICTIONARY ? Dictionary(value) : Dictionary();
}
Basis owner_rotation(const Basis &basis) {
    return Basis(basis.get_column(0).normalized(), basis.get_column(1).normalized(),
                 basis.get_column(2).normalized());
}
} // namespace
void NBNativeSpriteFacing::_bind_methods() {
    ClassDB::bind_static_method("NBNativeSpriteFacing", D_METHOD("configuration", "renderer", "attributes"),
                                &NBNativeSpriteFacing::configuration);
    ClassDB::bind_static_method("NBNativeSpriteFacing", D_METHOD("safe_normalize", "vector"),
                                &NBNativeSpriteFacing::safe_normalize);
    ClassDB::bind_static_method("NBNativeSpriteFacing", D_METHOD("tangents", "parameters"),
                                &NBNativeSpriteFacing::tangents);
    ClassDB::bind_static_method("NBNativeSpriteFacing",
                                D_METHOD("parameters", "config", "particle", "camera", "space"),
                                &NBNativeSpriteFacing::parameters);
}
Dictionary NBNativeSpriteFacing::configuration(const Ref<Resource> &renderer, const Dictionary &attributes) {
    Dictionary result;
    Array errors;
    result["errors"] = errors;
    result["facing"] = NBNativeValues::enum_name(input(renderer, "FacingMode", "FaceCamera"));
    result["alignment"] = NBNativeValues::enum_name(input(renderer, "Alignment", "Unaligned"));
    result["min_blend_cm"] = input(renderer, "MinFacingCameraBlendDistance", 0.);
    result["max_blend_cm"] = input(renderer, "MaxFacingCameraBlendDistance", 0.);
    const char *pairs[][5] = {
        {"facing", "SpriteFacingBinding", "SpriteFacing", "CustomFacingVector", "FaceCamera"},
        {"alignment", "SpriteAlignmentBinding", "SpriteAlignment", "CustomAlignment", "Unaligned"},
        {"velocity", "VelocityBinding", "Velocity", "", ""}};
    for (auto &pair : pairs) {
        Dictionary binding = settings(renderer, pair[1]);
        String name =
            String("Particles.") + String(binding.get("dataSetName", pair[2])).trim_prefix("Particles.");
        name = NBNativeRendererFilter::binding_reference(renderer, pair[2], pair[1], name);
        result[String(pair[0]) + "_attribute"] = name;
        if (String(pair[0]) == "velocity")
            continue;
        bool present =
            attributes.has(name) || (binding.get("bBindingExistsOnSource", Variant()) == Variant(true) &&
                                     binding.get("bIsCachedParticleValue", Variant()) == Variant(false));
        if (String(result[pair[0]]) == "Automatic")
            result[pair[0]] = present ? pair[3] : pair[4];
        if (String(result[pair[0]]) == pair[3] &&
            binding.get("bBindingExistsOnSource", Variant()) == Variant(false))
            result[pair[0]] = pair[4];
        if (String(result[pair[0]]) == pair[3] &&
            binding.get("bIsCachedParticleValue", Variant()) == Variant(false) &&
            !NBNativeRendererFilter::host_binding(renderer, pair[2], pair[1], name))
            errors.append("Host sprite vector binding has no supported parameter-store reference: " + name);
        if (String(result[pair[0]]) == pair[3] && attributes.has(name) && String(attributes[name]) != "vec3")
            errors.append("Sprite facing/alignment binding must be a vector3: " + name);
    }
    for (const char *key : {"min_blend_cm", "max_blend_cm"}) {
        Variant value = result[key];
        if ((value.get_type() != Variant::INT && value.get_type() != Variant::FLOAT) ||
            !std::isfinite(double(value)))
            errors.append(String("Invalid sprite distance blend setting: ") + key);
    }
    return result;
}
Vector3 NBNativeSpriteFacing::safe_normalize(const Vector3 &vector) {
    return vector / std::max(double(vector.length()), .000001);
}
namespace {
NBFacingMode facing_mode(const String &mode) {
    if(mode=="FaceCamera") return NBFacingMode::Camera;
    if(mode=="FaceCameraPlane") return NBFacingMode::Plane;
    if(mode=="CustomFacingVector") return NBFacingMode::Custom;
    if(mode=="FaceCameraPosition") return NBFacingMode::Position;
    if(mode=="FaceCameraDistanceBlend") return NBFacingMode::DistanceBlend;
    return NBFacingMode::Unknown;
}
NBAlignmentMode alignment_mode(const String &mode) {
    if(mode=="Unaligned") return NBAlignmentMode::Unaligned;
    if(mode=="VelocityAligned") return NBAlignmentMode::Velocity;
    if(mode=="CustomAlignment") return NBAlignmentMode::Custom;
    return NBAlignmentMode::Other;
}
struct SpriteParameters {
    NBFacingMode facing=NBFacingMode::Unknown;
    NBAlignmentMode alignment=NBAlignmentMode::Other;
    double min_blend_cm = 0., max_blend_cm = 0., rotation = 0.;
    Vector3 position, camera_position, view_right, view_up, view_forward,
            custom_facing, custom_alignment, velocity;
};
struct SpriteDirections { Vector3 right, up; };
SpriteDirections sprite_directions(const SpriteParameters &p) {
    const auto facing_mode=p.facing;const auto alignment=p.alignment;
    Vector3 right,up;
    if (alignment != NBAlignmentMode::Unaligned) {
        Vector3 facing=facing_mode==NBFacingMode::Custom
            ? NBNativeSpriteFacing::safe_normalize(p.custom_facing)
            : NBNativeSpriteFacing::safe_normalize(p.camera_position-p.position);
        Vector3 direction;
        if(alignment==NBAlignmentMode::Custom) direction=NBNativeSpriteFacing::safe_normalize(p.custom_alignment);
        else direction=p.velocity.length()>.000001 ? p.velocity/p.velocity.length() : Vector3(0,0,1);
        right = NBNativeSpriteFacing::safe_normalize(facing.cross(direction));
        up = facing_mode == NBFacingMode::Custom ? facing.cross(right) : -direction;
    } else if (facing_mode == NBFacingMode::Camera) {
        right = p.view_right;
        up = -Vector3(p.view_up);
    } else if (facing_mode == NBFacingMode::Plane) {
        Vector3 normal = -Vector3(p.view_forward);
        right = NBNativeSpriteFacing::safe_normalize(normal.cross(p.view_up));
        up = normal.cross(right);
    } else if (facing_mode == NBFacingMode::Custom) {
        Vector3 facing=NBNativeSpriteFacing::safe_normalize(p.custom_facing);
        right = NBNativeSpriteFacing::safe_normalize(facing.cross(p.view_up));
        up = facing.cross(right);
    } else if (facing_mode == NBFacingMode::Position) {
        Vector3 camera_direction=NBNativeSpriteFacing::safe_normalize(p.camera_position-p.position);
        right = NBNativeSpriteFacing::safe_normalize(camera_direction.cross(Vector3(0, 0, 1)));
        up = camera_direction.cross(right);
    } else if (facing_mode == NBFacingMode::DistanceBlend) {
        Vector3 camera_vector=p.camera_position-p.position,
                camera_direction=NBNativeSpriteFacing::safe_normalize(camera_vector);
        double near_sq = double(p.min_blend_cm) * double(p.min_blend_cm),
               far_sq = double(p.max_blend_cm) * double(p.max_blend_cm),
               inverse = 1. / std::max(far_sq - near_sq, 1.),
               weight = std::clamp(camera_vector.length_squared() * inverse - near_sq * inverse, 0., 1.);
        right = camera_direction.cross(Vector3(0, 0, 1));
        up = camera_direction.cross(right);
        right = right.lerp(p.view_right, weight);
        up = up.lerp(-Vector3(p.view_up), weight);
    }
    double angle = alignment == NBAlignmentMode::Velocity ? 0. : double(p.rotation),
           sine = UtilityFunctions::sin(angle), cosine = UtilityFunctions::cos(angle);
    return {up * sine + right * cosine, up * cosine - right * sine};
}
NBSpriteFrame facing_frame(const Dictionary &config, Camera3D *camera, const Transform3D &space) {
    NBSpriteFrame result;
    Basis convert = conversion();
    result.space = space;
    result.inverse = convert.inverse();
    result.view = camera ? camera->get_global_basis() : Basis();
    result.vector_transform = result.inverse * space.basis * convert;
    result.has_camera = camera != nullptr;
    if (camera) result.camera_position = camera->get_global_position();
    result.view_right = result.inverse.xform(result.view.get_column(0));
    result.view_up = result.inverse.xform(result.view.get_column(1));
    result.view_forward = result.inverse.xform(-result.view.get_column(2));
    result.facing = config["facing"];
    result.alignment = config["alignment"];
    result.facing_mode=facing_mode(result.facing);result.alignment_mode=alignment_mode(result.alignment);
    result.min_blend_cm = config["min_blend_cm"];
    result.max_blend_cm = config["max_blend_cm"];
    result.facing_attribute = config["facing_attribute"];
    result.alignment_attribute = config["alignment_attribute"];
    result.velocity_attribute = config["velocity_attribute"];
    return result;
}
SpriteParameters sprite_parameters(const NBSpriteFrame &frame, const NBSpriteRow &particle,bool geometry_only=false) {
    Vector3 position = frame.space.xform(NBNativeValues::unreal_to_godot(particle.position));
    SpriteParameters result;
    result.facing = frame.facing_mode;
    result.alignment = frame.alignment_mode;
    result.min_blend_cm = frame.min_blend_cm;
    result.max_blend_cm = frame.max_blend_cm;
    result.position = NBNativeValues::godot_to_unreal(position);
    result.camera_position = NBNativeValues::godot_to_unreal(frame.has_camera ? frame.camera_position
                                                    : position + frame.view.get_column(2) * 10.);
    result.view_right = frame.view_right;
    result.view_up = frame.view_up;
    result.view_forward = frame.view_forward;
    if(!geometry_only || frame.facing_mode==NBFacingMode::Custom)
        result.custom_facing = frame.vector_transform.xform(particle.custom_facing);
    if(!geometry_only || frame.alignment_mode==NBAlignmentMode::Custom)
        result.custom_alignment = frame.vector_transform.xform(particle.custom_alignment);
    if(!geometry_only || (frame.alignment_mode!=NBAlignmentMode::Unaligned && frame.alignment_mode!=NBAlignmentMode::Custom))
        result.velocity = frame.vector_transform.xform(particle.velocity);
    result.rotation = particle.rotation;
    return result;
}
} // namespace
Dictionary NBNativeSpriteFacing::tangents(const Dictionary &p) {
    SpriteParameters values;
    values.camera_position = p["camera_position"];
    values.position = p["position"];
    values.custom_facing = p["custom_facing"];
    values.custom_alignment = p["custom_alignment"];
    values.velocity = p["velocity"];
    values.facing = facing_mode(p["facing"]);
    values.alignment = alignment_mode(p["alignment"]);
    values.view_right = p["view_right"];
    values.view_up = p["view_up"];
    values.view_forward = p["view_forward"];
    values.min_blend_cm = p["min_blend_cm"];
    values.max_blend_cm = p["max_blend_cm"];
    values.rotation = p["rotation"];
    const auto directions = sprite_directions(values);
    Dictionary result;
    result["right"] = directions.right;
    result["up"] = directions.up;
    return result;
}
Dictionary NBNativeSpriteFacing::parameters(const Dictionary &config, const Dictionary &particle,
                                            Camera3D *camera, const Transform3D &space) {
    const auto frame = facing_frame(config, camera, space);
    const auto values = sprite_parameters(frame, NBNativeRendererGeometry::sprite_row(frame, particle));
    Dictionary result;
    for (const char *key : {"facing", "alignment", "min_blend_cm", "max_blend_cm"})
        result[key] = config[key];
    result["position"] = values.position;
    result["camera_position"] = values.camera_position;
    result["view_right"] = values.view_right;
    result["view_up"] = values.view_up;
    result["view_forward"] = values.view_forward;
    result["custom_facing"] = values.custom_facing;
    result["custom_alignment"] = values.custom_alignment;
    result["velocity"] = values.velocity;
    result["rotation"] = values.rotation;
    return result;
}
void NBNativeRendererGeometry::_bind_methods() {
    ClassDB::bind_static_method("NBNativeRendererGeometry",D_METHOD("synchronize_resident","renderer","emitter","camera","owner","frame","builtins","parameters","offset_name","mesh"),&NBNativeRendererGeometry::synchronize_resident);
    ClassDB::bind_static_method("NBNativeRendererGeometry", D_METHOD("sprite_transform_resident", "renderer", "view", "lane", "overrides", "camera", "space", "camera_offset", "size_ratio"), &NBNativeRendererGeometry::sprite_transform_resident);
    ClassDB::bind_static_method("NBNativeRendererGeometry", D_METHOD("mesh_resource_parameters", "host", "source"), &NBNativeRendererGeometry::mesh_resource_parameters);
    ClassDB::bind_static_method("NBNativeRendererGeometry", D_METHOD("refresh_mesh_resources", "host", "compiler"), &NBNativeRendererGeometry::refresh_mesh_resources);
    ClassDB::bind_static_method("NBNativeRendererGeometry", D_METHOD("refresh_dynamic_masks", "host"), &NBNativeRendererGeometry::refresh_dynamic_masks);
    ClassDB::bind_static_method("NBNativeRendererGeometry", D_METHOD("renderer_view", "host", "emitter", "source", "builtins"), &NBNativeRendererGeometry::renderer_view);
    ClassDB::bind_static_method("NBNativeRendererGeometry", D_METHOD("refresh_coverage", "host", "policy"), &NBNativeRendererGeometry::refresh_coverage);
    ClassDB::bind_static_method("NBNativeRendererGeometry", D_METHOD("validate_temporal", "host"), &NBNativeRendererGeometry::validate_temporal);
    ClassDB::bind_static_method("NBNativeRendererGeometry", D_METHOD("diagnostic_material"), &NBNativeRendererGeometry::diagnostic_material);
    ClassDB::bind_static_method("NBNativeRendererGeometry", D_METHOD("release_lights", "host"), &NBNativeRendererGeometry::release_lights);
    ClassDB::bind_static_method("NBNativeRendererGeometry", D_METHOD("synchronize_lights", "host", "camera", "pass_type"), &NBNativeRendererGeometry::synchronize_lights);
    ClassDB::bind_static_method("NBNativeRendererGeometry", D_METHOD("release_surface", "host"), &NBNativeRendererGeometry::release_surface);
    ClassDB::bind_static_method("NBNativeRendererGeometry", D_METHOD("synchronize_surface", "host", "camera", "pass_type"), &NBNativeRendererGeometry::synchronize_surface);
    ClassDB::bind_static_method("NBNativeRendererGeometry", D_METHOD("synchronize_refraction", "host", "camera", "pass_type"), &NBNativeRendererGeometry::synchronize_refraction);
    ClassDB::bind_static_method("NBNativeRendererGeometry", D_METHOD("dispose_composite", "host"), &NBNativeRendererGeometry::dispose_composite);
    ClassDB::bind_static_method("NBNativeRendererGeometry", D_METHOD("composite_signature", "host", "exposure"), &NBNativeRendererGeometry::composite_signature);
    ClassDB::bind_static_method("NBNativeRendererGeometry", D_METHOD("composite_ribbon_material", "host", "source"), &NBNativeRendererGeometry::composite_ribbon_material);
    ClassDB::bind_static_method("NBNativeRendererGeometry", D_METHOD("material_pass", "material", "program"), &NBNativeRendererGeometry::material_pass);
    ClassDB::bind_static_method("NBNativeRendererGeometry", D_METHOD("prepare_composite", "host", "composer", "program", "composite_type"), &NBNativeRendererGeometry::prepare_composite);
    ClassDB::bind_static_method("NBNativeRendererGeometry", D_METHOD("composite_unavailable", "host", "message"), &NBNativeRendererGeometry::composite_unavailable);
    ClassDB::bind_static_method("NBNativeRendererGeometry", D_METHOD("synchronize_composite", "host", "camera", "compiler", "builtins"), &NBNativeRendererGeometry::synchronize_composite);
    ClassDB::bind_static_method("NBNativeRendererGeometry", D_METHOD("vertex_colors", "texture", "colors"),
                                &NBNativeRendererGeometry::vertex_colors);
    ClassDB::bind_static_method("NBNativeRendererGeometry", D_METHOD("synchronize_ribbon", "host", "emitter", "renderer", "particles", "camera", "space"),
                                &NBNativeRendererGeometry::synchronize_ribbon);
    ClassDB::bind_static_method("NBNativeRendererGeometry", D_METHOD("synchronize_player", "host", "compiler","builtins"),
                                &NBNativeRendererGeometry::synchronize_player,DEFVAL(Dictionary()));
    ClassDB::bind_static_method("NBNativeRendererGeometry",
                                D_METHOD("opaque_sections", "renderer", "sections"),
                                &NBNativeRendererGeometry::opaque_sections);
    ClassDB::bind_static_method("NBNativeRendererGeometry",
                                D_METHOD("primitive_bounds", "renderer", "bounds"),
                                &NBNativeRendererGeometry::primitive_bounds);
    ClassDB::bind_static_method("NBNativeRendererGeometry",
                                D_METHOD("ribbon_relative_times", "renderer", "times"),
                                &NBNativeRendererGeometry::ribbon_relative_times);
    ClassDB::bind_static_method(
        "NBNativeRendererGeometry",
        D_METHOD("create_renderer", "host", "renderer", "index", "binding", "dynamic_mask"),
        &NBNativeRendererGeometry::create_renderer);
    ClassDB::bind_static_method(
        "NBNativeRendererGeometry",
        D_METHOD("composite_records", "states", "camera", "owner", "frame", "builtins", "tick"),
        &NBNativeRendererGeometry::composite_records);
    ClassDB::bind_static_method("NBNativeRendererGeometry",
                                D_METHOD("synchronize", "renderer", "emitter", "particles", "camera", "owner",
                                         "frame", "builtins", "offset_name", "mesh"),
                                &NBNativeRendererGeometry::synchronize);
    ClassDB::bind_static_method("NBNativeRendererGeometry",
                                D_METHOD("coverage", "config", "size_cm", "position", "camera"),
                                &NBNativeRendererGeometry::coverage);
    ClassDB::bind_static_method("NBNativeRendererGeometry",
                                D_METHOD("sprite_transform", "renderer", "particle", "camera", "space",
                                         "camera_offset", "size_ratio"),
                                &NBNativeRendererGeometry::sprite_transform);
    ClassDB::bind_static_method("NBNativeRendererGeometry",
                                D_METHOD("mesh_transform", "entry", "particle", "local", "owner", "camera",
                                         "camera_offset", "facing"),
                                &NBNativeRendererGeometry::mesh_transform);
    ClassDB::bind_static_method("NBNativeRendererGeometry", D_METHOD("mesh_selected", "renderer", "particle"),
                                &NBNativeRendererGeometry::mesh_selected);
    ClassDB::bind_static_method("NBNativeRendererGeometry",
                                D_METHOD("sorted_particles", "particles", "renderer", "camera", "space"),
                                &NBNativeRendererGeometry::sorted_particles);
    ClassDB::bind_static_method("NBNativeRendererGeometry",
                                D_METHOD("write_attributes", "image", "index", "particle", "attributes"),
                                &NBNativeRendererGeometry::write_attributes);
    ClassDB::bind_static_method("NBNativeRendererGeometry",
                                D_METHOD("camera_offset", "name", "particle", "emitter", "frame", "builtins"),
                                &NBNativeRendererGeometry::camera_offset);
}
Dictionary NBNativeRendererGeometry::coverage(const Dictionary &config, const Vector2 &size,
                                              const Vector3 &position, Camera3D *camera) {
    Dictionary result;
    const auto sample = coverage_sample(config,size,position,camera);
    result["size_ratio"] = sample.size_ratio;
    result["color_scale"] = sample.color_scale;
    return result;
}
NBCoverageFrame NBNativeRendererGeometry::prepare_coverage(const Dictionary &config,Camera3D *camera) {
    NBCoverageFrame frame;
    if(!bool(config.get("enabled",false)) || !camera || camera->get_projection()==Camera3D::PROJECTION_ORTHOGONAL)
        return frame;
    Viewport *viewport=camera->get_viewport();if(!viewport) return frame;
    frame.width=viewport->get_visible_rect().size.x;
    frame.scale=camera->get_camera_projection().columns[0].x;
    if(frame.width<=0. || frame.scale<=0.) return frame;
    frame.enabled=true;frame.camera_position=camera->get_global_position();
    frame.forward=-camera->get_global_basis().get_column(2).normalized();frame.blend=config["blend"];
    return frame;
}
NBCoverageSample NBNativeRendererGeometry::coverage_prepared(const NBCoverageFrame &frame,const Vector2 &size,
                                                            const Vector3 &position) {
    NBCoverageSample result;if(!frame.enabled) return result;
    double half=std::max(std::abs(double(size.x)),std::abs(double(size.y)))*.005;
    if(half<=0.) return result;
    double depth=(position-frame.camera_position).dot(frame.forward),
           pixel=std::max(2.*depth/(frame.width*frame.scale),half),
           amount=std::clamp(2.*half/pixel,0.,1.);
    result.size_ratio=pixel/half;amount*=amount;
    result.color_scale=Color(1,1,1,1)+frame.blend*(amount-1.);
    return result;
}
NBCoverageSample NBNativeRendererGeometry::coverage_sample(const Dictionary &config, const Vector2 &size,
                                                           const Vector3 &position, Camera3D *camera) {
    NBCoverageSample result;
    if (!bool(config.get("enabled", false)) || !camera ||
        camera->get_projection() == Camera3D::PROJECTION_ORTHOGONAL)
        return result;
    double half = std::max(std::abs(double(size.x)), std::abs(double(size.y))) * .005;
    Viewport *viewport = camera->get_viewport();
    if (half <= 0. || !viewport)
        return result;
    double width = viewport->get_visible_rect().size.x, scale = camera->get_camera_projection().columns[0].x;
    if (width <= 0. || scale <= 0.)
        return result;
    double depth = (position - camera->get_global_position())
                       .dot(-camera->get_global_basis().get_column(2).normalized()),
           pixel = std::max(2. * depth / (width * scale), half),
           amount = std::clamp(2. * half / pixel, 0., 1.);
    result.size_ratio = pixel / half;
    amount *= amount;
    result.color_scale = Color(1, 1, 1, 1) + Color(config["blend"]) * (amount - 1.);
    return result;
}
NBSpriteFrame NBNativeRendererGeometry::prepare_sprite_frame(const Dictionary &renderer, Camera3D *camera,
                                                             const Transform3D &space) {
    Ref<Resource> source = renderer["source"];
    Dictionary config = renderer.get("sprite_modes", Dictionary());
    if (config.is_empty()) config = NBNativeSpriteFacing::configuration(source, Dictionary());
    NBSpriteFrame result = facing_frame(config, camera, space);
    result.pivot = NBNativeValues::vector2(input(source, "PivotInUVSpace", Vector2(.5, .5)));
    result.attributes_supported = renderer["attributes_supported"];
    return result;
}
Transform3D NBNativeRendererGeometry::sprite_transform(const Dictionary &renderer, const Dictionary &particle,
                                                       Camera3D *camera, const Transform3D &space,
                                                       double offset, double size_ratio) {
    return sprite_transform_prepared(prepare_sprite_frame(renderer, camera, space), particle, offset, size_ratio);
}
Transform3D NBNativeRendererGeometry::sprite_transform_prepared(const NBSpriteFrame &frame,
                                                                const Dictionary &particle,
                                                                double offset, double size_ratio) {
    return sprite_transform_row(frame, sprite_row(frame, particle), offset, size_ratio);
}
NBSpriteRow NBNativeRendererGeometry::sprite_row(const NBSpriteFrame &frame, const Dictionary &particle) {
    Dictionary attributes = particle.get("attributes", Dictionary());
    NBSpriteRow row;
    row.position = particle["position"]; row.size = particle.get("size", Vector2());
    row.pivot = attributes.get("Particles.PivotOffset", frame.pivot);
    row.uv_x = particle.get("uv_flip",1.); row.uv_y = particle.get("uv_flip_y",1.);
    row.custom_facing = particle.get("_renderer_SpriteFacing", attributes.get(frame.facing_attribute, Vector3(0,0,1)));
    row.custom_alignment = particle.get("_renderer_SpriteAlignment", attributes.get(frame.alignment_attribute, particle.get("alignment",Vector3(1,0,0))));
    row.velocity = particle.get("_renderer_velocity", attributes.get(frame.velocity_attribute,particle["velocity"]));
    row.rotation = particle["rotation"];
    return row;
}
NBSpriteColumns NBNativeRendererGeometry::sprite_columns(const NBSpriteFrame &frame,
                                                         const Ref<NBNativeParticleView> &view) {
    NBSpriteColumns result;
    result.view = view;
    result.position = view->field_handle("position"); result.size = view->field_handle("size");
    result.alignment = view->field_handle("alignment"); result.velocity = view->field_handle("velocity");
    result.rotation = view->field_handle("rotation"); result.uv_x = view->field_handle("uv_flip");
    result.uv_y = view->field_handle("uv_flip_y"); result.pivot = view->attribute_handle("Particles.PivotOffset");
    result.custom_facing = view->attribute_handle(frame.facing_attribute.get_type() == Variant::NIL ? String() : String(frame.facing_attribute));
    result.custom_alignment = view->attribute_handle(frame.alignment_attribute.get_type() == Variant::NIL ? String() : String(frame.alignment_attribute));
    result.custom_velocity = view->attribute_handle(frame.velocity_attribute.get_type() == Variant::NIL ? String() : String(frame.velocity_attribute));
    return result;
}
NBSpriteRow NBNativeRendererGeometry::sprite_resident_row(const NBSpriteFrame &frame,
                                                          const NBSpriteColumns &columns,
                                                          int64_t lane, const Dictionary &overrides) {
    const auto &view = columns.view;
    auto field = [&](const char *name, int64_t handle, const Variant &fallback = Variant()) {
        return overrides.has(name) ? overrides[name] : view->has_field(handle,lane) ? view->read(handle,lane) : fallback;
    };
    Dictionary attributes = overrides.get("attributes",Dictionary());
    auto attribute = [&](const Variant &name, int64_t handle, const Variant &fallback) {
        return attributes.has(name) ? attributes[name] : view->has_attribute(handle,lane) ? view->read(handle,lane) : fallback;
    };
    NBSpriteRow row;
    row.position = field("position",columns.position); row.size = field("size",columns.size,Vector2());
    row.rotation = field("rotation",columns.rotation); row.uv_x = field("uv_flip",columns.uv_x,1.);
    row.uv_y = field("uv_flip_y",columns.uv_y,1.);
    row.pivot = attribute("Particles.PivotOffset",columns.pivot,frame.pivot);
    row.custom_facing = overrides.get("_renderer_SpriteFacing",attribute(frame.facing_attribute,columns.custom_facing,Vector3(0,0,1)));
    row.custom_alignment = overrides.get("_renderer_SpriteAlignment",attribute(frame.alignment_attribute,columns.custom_alignment,field("alignment",columns.alignment,Vector3(1,0,0))));
    row.velocity = overrides.get("_renderer_velocity",attribute(frame.velocity_attribute,columns.custom_velocity,field("velocity",columns.velocity)));
    return row;
}
NBSpriteRow NBNativeRendererGeometry::sprite_renderer_row(const NBSpriteFrame &frame,
                                                          const NBSpriteColumns &columns,
                                                          const NBRendererRows &rows,const NBRendererRow &input) {
    NBSpriteRow row;
    row.position=rows.field_native<Vector3>(input,columns.position);row.size=rows.field_native<Vector2>(input,columns.size);
    row.rotation=rows.field_native<double>(input,columns.rotation);row.uv_x=rows.field_native<double>(input,columns.uv_x,1.);
    row.uv_y=rows.field_native<double>(input,columns.uv_y,1.);
    row.pivot=rows.attribute(input,columns.pivot,frame.pivot);
    if(frame.facing_mode==NBFacingMode::Custom) row.custom_facing=rows.facing_slot>=0 ? rows.special(input,rows.facing_slot,Variant()) : rows.attribute(input,columns.custom_facing,Vector3(0,0,1));
    if(frame.alignment_mode==NBAlignmentMode::Custom) row.custom_alignment=rows.alignment_slot>=0 ? rows.special(input,rows.alignment_slot,Variant()) : rows.attribute(input,columns.custom_alignment,Variant(rows.field_native<Vector3>(input,columns.alignment,Vector3(1,0,0))));
    if(frame.alignment_mode!=NBAlignmentMode::Unaligned && frame.alignment_mode!=NBAlignmentMode::Custom) row.velocity=rows.velocity_slot>=0 ? rows.special(input,rows.velocity_slot,Variant()) : rows.attribute(input,columns.custom_velocity,Variant(rows.field_native<Vector3>(input,columns.velocity)));
    return row;
}
Transform3D NBNativeRendererGeometry::sprite_transform_row(const NBSpriteFrame &frame,
                                                           const NBSpriteRow &particle,
                                                           double offset, double size_ratio) {
    Vector2 pivot = particle.pivot;
    Vector3 position = frame.space.xform(NBNativeValues::unreal_to_godot(particle.position));
    const auto directions = sprite_directions(sprite_parameters(frame, particle,true));
    Vector3 right = conversion().xform(directions.right), up = -conversion().xform(directions.up),
            normal = right.cross(up).normalized();
    Vector2 original = particle.size, size = original.abs() * .01 * size_ratio;
    if (frame.has_camera) {
        Vector3 difference = frame.camera_position - position;
        position += difference / std::max(double(difference.length()), 1e-8) * offset * .01;
    }
    position += right * (.5 - pivot.x) * size.x + up * (pivot.y - .5) * size.y;
    Vector2 sign(original.x < 0. ? -1. : 1., original.y < 0. ? -1. : 1.),
        mirror = frame.attributes_supported
                     ? Vector2(1, 1)
                     : Vector2(particle.uv_x, particle.uv_y) * sign;
    return Transform3D(Basis(right * size.x * mirror.x, up * size.y * mirror.y, normal), position);
}
Transform3D NBNativeRendererGeometry::sprite_transform_resident(const Dictionary &renderer,
                                                                const Ref<NBNativeParticleView> &view,
                                                                int64_t lane, const Dictionary &overrides,
                                                                Camera3D *camera, const Transform3D &space,
                                                                double offset, double ratio) {
    if (view.is_null() || !view->get_errors().is_empty() || lane < 0 || lane >= view->get_count()) return Transform3D();
    const auto frame = prepare_sprite_frame(renderer,camera,space);
    return sprite_transform_row(frame,sprite_resident_row(frame,sprite_columns(frame,view),lane,overrides),offset,ratio);
}
Transform3D NBNativeRendererGeometry::mesh_transform(const Ref<Resource> &entry, const Dictionary &particle,
                                                     bool local, const Transform3D &owner, Camera3D *camera,
                                                     double offset, const Dictionary &facing) {
    NBMeshRow row;row.position=particle["position"];row.velocity=particle["velocity"];
    row.scale=particle["mesh_scale"];row.orientation=particle["mesh_orientation"];
    return mesh_transform_row(entry,row,local,owner,camera,offset,facing);
}
Transform3D NBNativeRendererGeometry::mesh_transform_row(const Ref<Resource> &entry,const NBMeshRow &particle,
                                                         bool local,const Transform3D &owner,Camera3D *camera,
                                                         double offset,const Dictionary &facing) {
    Basis rotation =
        NBNativeMeshFacing::rotation(facing, particle.position, particle.velocity, local, owner,
                                     camera ? camera->get_global_transform() : Transform3D()) *
        NBNativeValues::unreal_rotation_to_godot(particle.orientation);
    Vector3 source_scale = particle.scale, scale(source_scale.y, source_scale.z, source_scale.x);
    Transform3D entry_transform = entry->get("local_transform");
    Basis basis = rotation * entry_transform.basis.scaled_local(scale);
    Vector3 position = NBNativeValues::unreal_to_godot(particle.position);
    if (camera && offset != 0.) {
        Vector3 world = local ? owner.xform(position) : position,
                direction = (camera->get_global_position() - world).normalized();
        if (local)
            direction = owner_rotation(owner.basis).transposed().xform(direction);
        position += direction * offset * .01;
    }
    Vector3 pivot = entry->get("pivot_offset");
    String pivot_space = entry->get("pivot_space");
    if (pivot_space == "Mesh")
        position += (rotation * Basis(Quaternion(entry->get("rotation")))).xform(pivot * scale);
    else {
        if (pivot_space == "Local" || (pivot_space == "Simulation" && local))
            pivot = owner.basis.xform(pivot);
        if (local)
            pivot = owner_rotation(owner.basis).transposed().xform(pivot);
        position += pivot;
    }
    Transform3D result(basis, position);
    return local ? owner * result : result;
}
bool NBNativeRendererGeometry::mesh_selected(const Dictionary &renderer, const Dictionary &particle) {
    Dictionary binding = renderer["mesh_binding"];
    int64_t index = particle["mesh_index"];
    return (bool(particle.get("_emitter_source", false)) && index == -1) ||
           index == int64_t(binding.get("source_index", 0));
}
Array NBNativeRendererGeometry::sorted_particles(const Array &particles, const Ref<Resource> &renderer,
                                                 Camera3D *camera, const Transform3D &space) {
    String mode = NBNativeValues::enum_name(input(renderer, "SortMode", "None"));
    if (mode == "None" || particles.size() < 2)
        return particles;
    if (bool(input(renderer, "bSortOnlyWhenTranslucent", false))) {
        Variant translucent = input(renderer, "MaterialTranslucent");
        Ref<Resource> material = renderer->get("material");
        if (translucent == Variant(false) ||
            (translucent.get_type() == Variant::NIL && material.is_valid() &&
             material->is_class("BaseMaterial3D") && int64_t(material->get("transparency")) == 0))
            return particles;
    }
    String attribute = "Particles.NormalizedAge";
    Array bindings = renderer->get("bindings");
    for (int n = 0; n < bindings.size(); ++n) {
        Dictionary b = bindings[n];
        if (bool(b.get("enabled", true)) && b.get("name", "") == Variant("CustomSorting"))
            attribute = NBNativeValues::enum_name(b.get("value", attribute));
    }
    struct Key {
        Dictionary particle;
        double key;
        int64_t id;
    };
    std::vector<Key> keyed;
    for (int n = 0; n < particles.size(); ++n) {
        Dictionary p = particles[n];
        double key = 0.;
        if (mode == "ViewDepth" || mode == "ViewDistance") {
            if (!camera)
                return particles;
            Vector3 position = space.xform(NBNativeValues::unreal_to_godot(p["position"])),
                    difference = position - camera->get_global_position();
            key = mode == "ViewDistance" ? position.distance_squared_to(camera->get_global_position())
                                         : -camera->get_global_basis().get_column(2).dot(difference);
        } else {
            Dictionary original = p.get("_renderer_source", p);
            key = original[attribute == "Particles.Age"        ? "age"
                           : attribute == "Particles.Lifetime" ? "lifetime"
                           : attribute == "Particles.ID"       ? "id"
                                                               : "normalized_age"];
        }
        keyed.push_back({p, key, p["id"]});
    }
    bool ascending = mode == "CustomAscending";
    std::stable_sort(keyed.begin(), keyed.end(), [ascending](const Key &a, const Key &b) {
        if (UtilityFunctions::is_equal_approx(a.key, b.key))
            return a.id < b.id;
        return ascending ? a.key < b.key : a.key > b.key;
    });
    Array result;
    for (auto &entry : keyed)
        result.append(entry.particle);
    return result;
}
void NBNativeRendererGeometry::write_attributes(const Ref<Image> &image, int64_t index,
                                                const Dictionary &particle, const Color &attributes) {
    if (image.is_null() || index < 0 || index >= (int64_t(image->get_width()) * image->get_height()) / 5)
        return;
    int width = image->get_width();
    for (int field = 0; field < 5; ++field) {
        int64_t offset = index * 5 + field;
        Color value = field == 0   ? attributes
                      : field == 4 ? Color(particle["normalized_age"], 0, 0, 0)
                                   : Color(particle[String("dynamic") + String::num_int64(field)]);
        image->set_pixel(offset % width, offset / width, value);
    }
}
double NBNativeRendererGeometry::camera_offset(const String &name, Dictionary particle, Dictionary emitter,
                                               const Dictionary &frame, const Dictionary &builtins) {
    if (bool(particle.get("_emitter_source", false)))
        return particle["camera_offset"];
    particle = particle.get("_renderer_source", particle);
    if (name == "None" || name.is_empty())
        return 0.;
    if (name == "Particles.SpriteRotation")
        return Math::rad_to_deg(double(particle["rotation"]));
    if (builtins.has(name)) {
        Array fields = builtins[name];
        return particle.get(fields[1], 0.);
    }
    if (name.begins_with("Particles.")) {
        Dictionary attributes = particle.get("attributes", Dictionary());
        return attributes.get(name, 0.);
    }
    if (name.begins_with("Emitter."))
        return NBNativeParticleContext::for_emitter(emitter, 0., frame.get("simulation_time", 0.), frame)
            .get(name, 0.);
    Dictionary parameters = frame.get("parameters", Dictionary());
    return parameters.get(name, 0.);
}
void NBNativeRendererGeometry::synchronize(Dictionary renderer, Dictionary emitter, const Array &particles,
                                           Camera3D *camera, const Transform3D &owner,
                                           const Dictionary &frame, const Dictionary &builtins,
                                           const String &offset_name, bool mesh) {
    Object *object = renderer.get("instance", Variant());
    auto *instance = Object::cast_to<MultiMeshInstance3D>(object);
    if (!instance)
        return;
    Ref<MultiMesh> multimesh = instance->get_multimesh();
    if (multimesh.is_null())
        return;
    Ref<Resource> source = renderer["source"];
    Dictionary plan = emitter["plan"], binding = renderer.get("mesh_binding", Dictionary());
    bool local = plan["local_space"];
    Transform3D space = local ? owner : Transform3D();
    Array selected;
    if (mesh) {
        for (int n = 0; n < particles.size(); ++n)
            if (mesh_selected(renderer, particles[n]))
                selected.append(particles[n]);
    } else
        selected = particles;
    Array visible = sorted_particles(selected, source, camera, space);
    multimesh->set_visible_instance_count(visible.size());
    Ref<Image> image = renderer["attributes_image"];
    AABB bounds, mesh_bounds = renderer["mesh_aabb"];
    Ref<Resource> entry = binding.get("entry", Variant());
    Dictionary facing = binding.get("facing", Dictionary());
    // Emitter/host offsets are constant during this submission. Particle-source
    // overrides remain per record, including the renderer's emitter-source view.
    bool fixed_offset = !offset_name.begins_with("Particles.");
    double shared_offset =
        fixed_offset ? camera_offset(offset_name, Dictionary(), emitter, frame, builtins) : 0.;
    NBSpriteFrame sprite_frame;
    if (!mesh) sprite_frame = prepare_sprite_frame(renderer, camera, space);
    for (int index = 0; index < visible.size(); ++index) {
        Dictionary p = visible[index];
        double offset = fixed_offset && !bool(p.get("_emitter_source", false))
                            ? shared_offset
                            : camera_offset(offset_name, p, emitter, frame, builtins);
        Transform3D transform;
        Color color = p["color"], attributes;
        if (mesh) {
            transform = mesh_transform(entry, p, local, owner, camera, offset, facing);
            attributes = Color(p["subimage_index"], p["material_random"], 1, 1);
        } else {
            const NBSpriteRow row = sprite_row(sprite_frame, p);
            const auto footprint =
                coverage_sample(renderer.get("pixel_coverage", Dictionary()), row.size,
                         space.xform(NBNativeValues::unreal_to_godot(row.position)), camera);
            transform = sprite_transform_row(sprite_frame, row, offset, footprint.size_ratio);
            color *= footprint.color_scale;
            Vector2 size = row.size;
            attributes = Color(p["subimage_index"], p["material_random"],
                               row.uv_x * (size.x < 0 ? -1. : 1.),
                               row.uv_y * (size.y < 0 ? -1. : 1.));
        }
        multimesh->set_instance_transform(index, transform);
        multimesh->set_instance_color(index, color);
        multimesh->set_instance_custom_data(index, p["dynamic"]);
        AABB current = transform.xform(mesh_bounds);
        bounds = index == 0 ? current : bounds.merge(current);
        write_attributes(image, index, p, attributes);
    }
    multimesh->set_custom_aabb(bounds.grow(.01));
    if (!visible.is_empty()) {
        Ref<ImageTexture> texture = renderer["attributes_texture"];
        if (texture.is_valid())
            texture->update(image);
    }
}
} // namespace godot
