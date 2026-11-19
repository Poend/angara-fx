@tool
extends Resource
## Name-independent material graph IR, saved as a normal text .tres when desired.
@export var schema_version: int = 1
@export var source_material: String = ""
@export var root_material: String = ""
@export var source_manifest: String = ""
@export var source_hashes: Dictionary = {}
@export var material_properties: Dictionary = {}
@export var nodes: Array[Dictionary] = []
@export var outputs: Dictionary = {}
@export var functions: Dictionary = {}
@export var custom_shader_functions: Dictionary = {}
@export var parameters: Dictionary = {}
@export var graph_dependencies: Dictionary = {}
@export var texture_dependencies: Dictionary = {}
@export var diagnostics: Array[Dictionary] = []
