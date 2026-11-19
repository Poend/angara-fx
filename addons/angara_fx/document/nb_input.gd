@tool
extends Resource
## Ordered input: duplicate names in DreamFX are intentional and retained.
@export var input_name: String = ""
@export var target_type: String = ""
## False represents a declaration without a default, distinct from explicit null.
@export var has_default: bool = true
@export var value: Variant = null
@export var line: int = 0
@export var provenance: String = "explicit"
