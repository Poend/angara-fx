@tool
extends RefCounted
const Compiler = preload("shader_compiler.gd")
const Composer = preload("system_material_composer.gd")
const Program = preload("material_program.gd")
const SystemProgram = preload("system_material_program.gd")
const Raster = preload("raster_material_program.gd")
const HeightFog = preload("../../runtime/native_height_fog.gd")
static func build(material: ShaderMaterial) -> Dictionary:return NBNativeMaterialDerived.surface(material,Program.Graph,HeightFog)
