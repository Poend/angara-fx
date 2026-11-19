@tool
extends Resource
@export var stable_id: String = ""
@export var module_name: String = ""
@export var alias_name: String = ""
@export var enabled: bool = true
@export var line: int = 0
@export var operation_kind: String = "call"
@export var target_type: String = ""
@export var inputs: Array[Resource] = []

func get_input(key: String, fallback: Variant = null, occurrence: int = 0) -> Variant:
	return NBNativeDocument.input_value(inputs,key,fallback,occurrence)
