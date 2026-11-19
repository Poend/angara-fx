@tool
extends RefCounted
const Compiler = preload("shader_compiler.gd")
const Program = preload("material_program.gd")
const SystemProgram = preload("system_material_program.gd")
const PROTOCOL_VERSION = 1
static func build(contexts: Array[Dictionary],render_pass: String="surface") -> Dictionary:return NBNativeMaterialComposer.build(contexts,render_pass,Program.Graph)
static func _common_modes(modes: Array) -> Array:return NBNativeMaterialComposer.common_modes(modes)
static func _check_binding_limits(textures: int,samplers: int,reported: Dictionary,driver: String) -> Dictionary:return NBNativeMaterialComposer.check_binding_limits(textures,samplers,reported,driver)
