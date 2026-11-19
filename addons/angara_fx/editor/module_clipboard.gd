@tool
extends RefCounted
const Module=preload("res://addons/angara_fx/document/nb_module.gd")
const InputResource=preload("res://addons/angara_fx/document/nb_input.gd")
const FORMAT="angara_fx.module/1"
static func encode(module: Resource,stage_name: String) -> String:return NBNativeEditorOperations.encode(module,stage_name)
static func decode(text: String) -> Dictionary:return NBNativeEditorOperations.decode(text,Module,InputResource)
static func _pack(value: Variant) -> Dictionary:return NBNativeEditorOperations.pack(value)
static func _unpack(encoded: Variant,depth: int) -> Dictionary:return NBNativeEditorOperations.unpack(encoded,depth)
