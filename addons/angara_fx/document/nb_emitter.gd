@tool
extends Resource
@export var stable_id: String = ""
@export var display_name: String = "Emitter"
@export var enabled: bool = true
## Unresolved source inheritance is preserved; an override is not a complete stack.
@export var parent_emitter_source: String = ""
## Versioned inheritance must retain the source parent revision alongside its path.
@export var parent_emitter_version: String = ""
## Export provenance for a flattened selected child. Never an inheritance blocker.
@export var source_lineage: Dictionary = {}
## Readable compiled-call provenance; source-owned, never particle replay data.
@export var source_rng_contracts: Dictionary = {}
@export var settings: Array[Resource] = []
@export var stages: Array[Resource] = []
@export var renderers: Array[Resource] = []

func get_stage(key: String) -> Resource:
	return NBNativeDocument.find(stages,"stage_name",key)

func get_input(key: String, fallback: Variant = null) -> Variant:
	return NBNativeDocument.input_value(settings,key,fallback)
