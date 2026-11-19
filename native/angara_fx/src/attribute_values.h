#pragma once
#include "particle_view.h"
#include "renderer_rows.h"
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/resource.hpp>
namespace godot {
class NBNativeAttributeValues : public RefCounted {
    GDCLASS(NBNativeAttributeValues, RefCounted)
  protected:
    static void _bind_methods();

  public:
    static String type_name(const String &name);
    static String shader_type(const String &type);
    static bool compatible(const String &a, const String &b);
    static Variant zero(const String &type);
    static Variant coerce(const Variant &value, const String &type);
    static void write_particle(Dictionary particle, const String &name, const String &type,
                               const Variant &value, const Dictionary &builtins);
    static Array component_array(const Variant &value, const String &type);
};
class NBNativeRendererViews : public RefCounted {
    GDCLASS(NBNativeRendererViews, RefCounted)
  protected:
    static void _bind_methods();

  public:
    static Dictionary view(Dictionary emitter, const Ref<Resource> &renderer, const Dictionary &parameters,
                           const Vector3 &global_position, const Dictionary &frame,
                           const Dictionary &builtins);
    static Dictionary configuration(const Ref<Resource> &renderer, const Dictionary &types,
                                    const Dictionary &builtins, const Dictionary &fields);
    static Variant fallback(const String &key, const Vector3 &position);
    static Variant read_particle(Dictionary particle, const String &name, const Dictionary &builtins);
    static bool valid(const Variant &value, const String &type);
    static Dictionary project(const Dictionary &config, const Array &particles, const Dictionary &parameters,
                              const Vector3 &default_position, const Dictionary &builtins);
    static Dictionary project_view(const Dictionary &config, const Array &particles, const Dictionary &parameters,
                                    const Vector3 &default_position, const Dictionary &builtins,
                                    const Ref<NBNativeParticleView> &view, const Array &indices);
    static Dictionary emitter_particle(const Vector3 &position);
    static NBRendererRows resident_rows(Dictionary emitter, const Ref<Resource> &renderer,
                                         const Dictionary &parameters, const Vector3 &global_position,
                                         const Dictionary &frame, const Dictionary &builtins);
    static Dictionary resident_projection(Dictionary emitter, const Ref<Resource> &renderer,
                                           const Dictionary &parameters, const Vector3 &global_position,
                                           const Dictionary &frame, const Dictionary &builtins,
                                           Camera3D *camera, const Transform3D &space);
};
} // namespace godot
