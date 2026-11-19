@tool
extends RefCounted
const Compiler = preload("shader_compiler.gd")
const Program = preload("material_program.gd")
const SystemProgram = preload("system_material_program.gd")
const Composer = preload("system_material_composer.gd")
const Raster = preload("raster_material_program.gd")
static func validate(material: ShaderMaterial) -> Array[String]:return NBNativeMaterialDerived.validate(material,Program.Graph)
static func current_values(material: ShaderMaterial) -> Dictionary:return NBNativeMaterialDerived.current_values(material)
static func build(material: ShaderMaterial) -> Dictionary:return NBNativeMaterialDerived.distortion(material,Program.Graph)
static func uniform_bytes(program: Dictionary,values: Dictionary) -> PackedByteArray:return Raster.uniform_bytes(program,values)
static func geometry_bytes(arrays: Array) -> PackedByteArray:return Raster.geometry_bytes(arrays)
