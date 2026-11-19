@tool
extends Resource
## Readable source height-fog context. Densities/falloffs retain Unreal component
## units; heights and distances are centimeters. Directions use Godot world axes.
## Renderer policy/features must be exported with the scene, not guessed from
## an effect name. Unsupported atmosphere/volume/cubemap features remain explicit.
@export var enabled: bool = true
@export var density: float = .02
@export var height_falloff: float = .2
@export var height_cm: float = 0.0
@export var second_density: float = 0.0
@export var second_height_falloff: float = .2
@export var second_height_offset_cm: float = 0.0
@export var scattering_color: Color = Color(.447,.638,1.0,1.0)
@export var maximum_opacity: float = 1.0
@export var start_distance_cm: float = 0.0
@export var end_distance_cm: float = 0.0
@export var cutoff_distance_cm: float = 0.0
## CVar r.Ortho.FogHeightAdjustment and source camera-to-target distance.
@export var orthographic_height_adjustment: bool = true
@export var camera_to_target_distance_cm: float = 0.0
## Standard non-atmosphere directional-scattering branch. Color is the source
## renderer value (component luminance times selected sun luminance), linear.
@export var directional_enabled: bool = false
@export var direction_to_light: Vector3 = Vector3.UP
@export var directional_color: Color = Color.BLACK
@export var directional_exponent: float = 4.0
@export var directional_start_distance_cm: float = 10000.0
## Examples: sky_atmosphere, volumetric_fog, cubemap, skylight_capture,
## expfog_matches_volumetric, local_fog_volumes, planar_reflection_clip_plane.
@export var unsupported_source_features: Array[String] = []

func validate() -> Array[String]:return NBNativeMaterialAuthoring.validate_height_fog(self)
func uniforms() -> Dictionary:return NBNativeMaterialAuthoring.height_fog_uniforms(self)
