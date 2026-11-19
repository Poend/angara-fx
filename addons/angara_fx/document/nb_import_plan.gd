@tool
extends Resource
## Declarative source-neutral migration plan. Every output is a standard text asset.
@export var schema_version: int = 1
## Reuse authored text dependencies instead of rebuilding them for each effect.
@export_file("*.tres") var dependency_profile: String = ""
@export_file("*.dfs") var source_dfs: String = ""
@export var native_metadata_sources: PackedStringArray = []
@export var renderer_order_sources: PackedStringArray = []
## Original compiled programs; seed/clock settings are preserved, never taken from a capture.
@export_file("*.json") var execution_source: String = ""
## Optional shared world export; independent of individual effect capabilities.
@export_file("*.json") var scene_context_source: String = ""
@export var material_manifests: PackedStringArray = []
@export var texture_evidence_sources: PackedStringArray = []
@export var native_texture_sources: PackedStringArray = []
@export var mesh_geometry_sources: PackedStringArray = []
@export_dir var output_directory: String = "res://converted"
@export var effect_filename: String = "effect.tres"
