@tool
extends NBNativeRasterPass
## Standard Godot authoring resources; raster/submission/GPU lifecycle is C++.
const Builder=preload("../materials/conversion/native_surface_program.gd")
const Raster=preload("../materials/conversion/raster_material_program.gd")
const Program=preload("../materials/conversion/material_program.gd")
const SystemProgram=preload("../materials/conversion/system_material_program.gd")
const HeightFog=preload("native_height_fog.gd")
const SceneContext=preload("native_scene_context.gd")
const LightingVolume=preload("native_lighting_volume.gd")
const GeometryCache=preload("raster_geometry_cache.gd")
@export var source_scene_context: SceneContext:
	set(value):set_option("source_scene_context",value)
	get:return get_option("source_scene_context") as SceneContext
@export var source_pre_exposure: float = 1.0:
	set(value):set_option("source_pre_exposure",value)
	get:return float(get_option("source_pre_exposure"))
@export var source_eye_adaptation_exposure: float = 1.0:
	set(value):set_option("source_eye_adaptation_exposure",value)
	get:return float(get_option("source_eye_adaptation_exposure"))
@export var source_eye_adaptation_exposure_observed: bool:
	set(value):set_option("source_eye_adaptation_exposure_observed",value)
	get:return bool(get_option("source_eye_adaptation_exposure_observed"))
@export var source_color_limit: float = 32256.0:
	set(value):set_option("source_color_limit",value)
	get:return float(get_option("source_color_limit"))
@export var source_height_fog: HeightFog:
	set(value):set_option("source_height_fog",value)
	get:return get_option("source_height_fog") as HeightFog
@export_enum("Distance", "Projected Z", "Axis") var component_sort_policy: int:
	set(value):set_option("component_sort_policy",value)
	get:return int(get_option("component_sort_policy"))
@export var component_sort_axis: Vector3 = Vector3.FORWARD:
	set(value):set_option("component_sort_axis",value)
	get:return get_option("component_sort_axis")
func _init() -> void:
	initialize_pass(true,{"builder":Builder,"raster":Raster,"material":Program,"system":SystemProgram,"geometry":GeometryCache,"lighting_type":LightingVolume,"lighting_engine":LightingVolume.new()})
