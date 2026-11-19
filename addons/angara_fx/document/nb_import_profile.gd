@tool
extends Resource
## Explicit dependency mappings, shared between UI and command-line imports.
@export var profile_name: String = ""
@export var material_bindings: Array[Resource] = []
@export var mesh_bindings: Array[Resource] = []
@export var texture_bindings: Array[Resource] = []
@export var metadata_sources: PackedStringArray = []
@export var renderer_order_sources: PackedStringArray = []
