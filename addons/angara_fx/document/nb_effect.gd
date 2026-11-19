@tool
extends Resource
## Persistent authored data only. Playback state belongs to EffectPlayer.
@export var schema_version: int = 1
@export var stable_id: String = ""
@export var display_name: String = "Untitled effect"
@export var source_path: String = ""
@export var source_hash: String = ""
@export var exporter_revision: String = ""
@export var import_profile: Resource
@export var coordinate_policy: String = "UE_cm_Xforward_Zup_to_Godot_m_minusZforward_Yup"
@export var settings: Array[Resource] = []
## Typed user declarations/defaults, separate from system settings and runtime overrides.
@export var user_parameters: Array[Resource] = []
@export var stages: Array[Resource] = []
@export var emitters: Array[Resource] = []
@export var diagnostics: Array[Dictionary] = []
@export var source_snapshot: Dictionary = {}
## Verified source-owned component/renderer ordering, separate from material priority.
@export var source_renderer_order: Dictionary = {}
## Source execution provenance activates strict compiled-program playback automatically.
@export var source_execution: Dictionary = {}
## Last accepted import's immutable authored-value snapshot for three-way reimport.
@export var source_baseline: Dictionary = {}
@export var unknown_source: Array[Dictionary] = []

func get_stage(key: String) -> Resource:
	return NBNativeDocument.find(stages,"stage_name",key)

func get_input(key: String, fallback: Variant = null) -> Variant:
	return NBNativeDocument.input_value(settings,key,fallback)
