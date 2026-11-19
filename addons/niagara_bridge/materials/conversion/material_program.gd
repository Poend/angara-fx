@tool
extends RefCounted
const Graph = preload("material_graph.gd")
const META = "niagara_bridge_material_program"
const SCHEMA_VERSION = 1
const COMPILER_ABI = 1
const GRAPH_FIELDS: Array[String] = ["schema_version","source_material","root_material","source_manifest","source_hashes","material_properties","nodes","outputs","functions","parameters","graph_dependencies","texture_dependencies","diagnostics"]
static func seal(result: Dictionary) -> Dictionary:return NBNativeMaterialPrograms.seal(result)
static func validate(material: ShaderMaterial) -> Array[String]:return NBNativeMaterialPrograms.validate(material,Graph)
static func graph_hash(graph: Resource) -> String:return NBNativeMaterialPrograms.graph_hash(graph)
static func fingerprint(material: ShaderMaterial) -> int:return NBNativeMaterialPrograms.fingerprint(material)
static func cached_validate(material: ShaderMaterial) -> Array[String]:return NBNativeMaterialPrograms.cached_validate(material,Graph)
static func current_values(material: ShaderMaterial) -> Dictionary:return NBNativeMaterialPrograms.current_values(material)
