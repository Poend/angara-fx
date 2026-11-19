@tool
extends Resource
## Shared scene input, independent of any Niagara effect. Authored as text.
## Light records feed the shared native TLV backend when its requirements match.
const HeightFog = preload("native_height_fog.gd")
const PRODUCER = "NiagaraBridgeSceneContext.Describe"
@export var component_sort_policy: int = 0
@export var component_sort_axis: Vector3 = Vector3.FORWARD
@export var height_fog: HeightFog
@export var lights: Array[Dictionary] = []
@export var source_cvars: Dictionary = {}
@export var source_world: String = ""
@export var source_packet_sha256: String = ""
@export var renderer_pre_exposure_observed: bool = false
@export var lighting_volume_observed: bool = false
@export var additional_scene_components: Array[String] = []

func validate() -> Array[String]:return NBNativeSceneContext.validate(self)
func matches_render_inputs(other: Resource) -> bool:return NBNativeSceneContext.matches_render_inputs(self,other)
static func same_gpu_values(a: Variant,b: Variant) -> bool:return NBNativeSceneContext.same_gpu_values(a,b)
static func _number(record: Dictionary,key: String,errors: Array[String]) -> float:return NBNativeSceneContext.number(record,key,errors)
static func _boolean(record: Dictionary,key: String,errors: Array[String]) -> bool:return NBNativeSceneContext.boolean(record,key,errors)
static func _vector(record: Dictionary,key: String,errors: Array[String]) -> Vector3:return NBNativeSceneContext.vector(record,key,errors)
static func _direction_ue(vector: Vector3) -> Vector3:return NBNativeSceneContext.direction_ue(vector)
static func from_packet(packet: Variant) -> Dictionary:return NBNativeSceneContext.from_packet(packet,load("res://addons/angara_fx/runtime/native_scene_context.gd"),HeightFog)
static func _cvar(values: Dictionary,name: String,errors: Array[String]) -> int:return NBNativeSceneContext.cvar(values,name,errors)
