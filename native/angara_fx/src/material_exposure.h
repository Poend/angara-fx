#pragma once
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
namespace godot {
class NBNativeMaterialExposure : public RefCounted {
    GDCLASS(NBNativeMaterialExposure, RefCounted)
  protected:
    static void _bind_methods();

  public:
    static Dictionary for_camera(Camera3D *camera, bool physical_light_units);
};
} // namespace godot
