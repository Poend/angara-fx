#include "material_exposure.h"
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/world3d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <cmath>
namespace godot {
void NBNativeMaterialExposure::_bind_methods() {
    ClassDB::bind_static_method("NBNativeMaterialExposure",
                                D_METHOD("for_camera", "camera", "physical_light_units"),
                                &NBNativeMaterialExposure::for_camera);
}
Dictionary NBNativeMaterialExposure::for_camera(Camera3D *camera, bool physical) {
    Ref<Resource> attributes;
    if (camera) {
        attributes = camera->get("attributes");
        if (attributes.is_null() && camera->is_inside_tree()) {
            Ref<World3D> world = camera->get_world_3d();
            if (world.is_valid())
                attributes = world->get("camera_attributes");
        }
    }
    double factor = 1.;
    Dictionary result;
    if (attributes.is_valid()) {
        factor = attributes->get("exposure_multiplier");
        if (physical) {
            double sensitivity = attributes->get("exposure_sensitivity");
            if (attributes->is_class("CameraAttributesPractical"))
                factor *= sensitivity / 3072007.;
            else if (attributes->is_class("CameraAttributesPhysical")) {
                double aperture = attributes->get("exposure_aperture"),
                       shutter = attributes->get("exposure_shutter_speed");
                double denominator = aperture * aperture * shutter * 100. * 1.2;
                factor *= denominator > 0 ? sensitivity / denominator : NAN;
            } else {
                result["supported"] = false;
                result["error"] = "Unknown physical CameraAttributes normalization.";
                return result;
            }
        }
    }
    if (!std::isfinite(factor) || factor <= 0 || !std::isfinite(1. / factor) ||
        1. / factor > 3.402823466e38) {
        result["supported"] = false;
        result["error"] = "Mixed lit/unlit rendering requires positive finite camera emission normalization.";
        result["normalization"] = factor;
        return result;
    }
    result["supported"] = true;
    result["emission_scale"] = 1. / factor;
    result["normalization"] = factor;
    result["physical_light_units"] = physical;
    return result;
}
} // namespace godot
