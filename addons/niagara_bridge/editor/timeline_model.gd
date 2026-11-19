@tool
extends RefCounted
const Compiler=preload("res://addons/niagara_bridge/compiler/effect_compiler.gd")
const Values=preload("res://addons/niagara_bridge/runtime/value_evaluator.gd")
static func build(effect: Resource) -> Array[Dictionary]:return NBNativeEditorOperations.timeline(effect,Compiler.NATIVE_DEPENDENCIES,Compiler.NATIVE_CONTRACTS)
