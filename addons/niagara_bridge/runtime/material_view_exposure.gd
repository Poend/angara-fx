@tool
extends RefCounted
## Preserve an unshaded material's output when sharing a lit spatial shader.
## This cancels Godot's emissive normalization, not source Unreal exposure.
## Source: Godot ed1daf0bf scene/resources/camera_attributes.cpp (54,209,387)
## and servers/rendering/storage/camera_attributes_storage.cpp (122).
## Physical-light units are a startup setting, cached by the engine as well.
static var _physical_light_units: bool = bool(ProjectSettings.get_setting_with_override("rendering/lights_and_shadows/use_physical_light_units"))

static func for_camera(camera: Camera3D) -> Dictionary:
	return NBNativeMaterialExposure.for_camera(camera,_physical_light_units)
