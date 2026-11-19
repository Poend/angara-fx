@tool
extends NBNativeCompositeRenderer
const ViewExposure=preload("material_view_exposure.gd")
func update_view(camera: Camera3D) -> Dictionary:return material_view(camera,ViewExposure._physical_light_units)
