@tool
extends RefCounted
const Program = preload("material_program.gd")
const META = "angara_fx_system_program"
static func seal(shader_code: String,contexts: Array[Dictionary],aliases: Array[Dictionary],defaults: Dictionary) -> Dictionary:return NBNativeMaterialPrograms.seal_system(shader_code,contexts,aliases,defaults)
static func validate(material: ShaderMaterial) -> Array[String]:return NBNativeMaterialPrograms.validate_system(material,Program.Graph)
static func fingerprint(material: ShaderMaterial) -> int:return NBNativeMaterialPrograms.fingerprint_system(material)
static func cached_validate(material: ShaderMaterial) -> Array[String]:return NBNativeMaterialPrograms.cached_validate_system(material,Program.Graph)
static func _context_hash(contexts: Array,aliases: Array) -> String:return NBNativeMaterialPrograms.context_hash(contexts,aliases)
static func current_values(material: ShaderMaterial) -> Dictionary:return NBNativeMaterialPrograms.system_values(material)
static func source_contexts(material: ShaderMaterial) -> Array[Dictionary]:return NBNativeMaterialPrograms.source_contexts(material)
