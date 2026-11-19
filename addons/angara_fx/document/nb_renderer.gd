@tool
extends Resource
@export var stable_id: String = ""
@export var enabled: bool = true
@export var renderer_type: String = "SpriteRenderer"
@export var properties: Array[Resource] = []
@export var bindings: Array[Dictionary] = []
@export var material: Material
## Ordered Mesh Renderer entries. Mesh geometry is already in Godot coordinates.
@export var mesh_entries: Array[Resource] = []
## Per-surface overrides, aligned with material_parameter_names by slot index.
@export var surface_materials: Array[Material] = []
## Full User.* names; an empty string leaves the slot's authored fallback active.
@export var material_parameter_names: Array[String] = []

func get_input(key: String, fallback: Variant = null, occurrence: int = 0) -> Variant:
	return NBNativeDocument.input_value(properties,key,fallback,occurrence)
