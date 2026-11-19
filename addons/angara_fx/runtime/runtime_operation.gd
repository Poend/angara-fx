@tool
extends "../document/nb_module.gd"
## Transient resource schema and native operation factory bridge.
const Curves=preload("curve_evaluator.gd")
const NBInput=preload("../document/nb_input.gd")
var indexed: Dictionary={}
var source_signature: int=0
var source: Resource

static func references(value: Variant,output: Dictionary) -> void:
	NBNativeRuntimeOperations.references(value,output)

static func signature(operation: Resource) -> int:
	return NBNativeRuntimeOperations.signature(operation)

static func create(operation: Resource) -> Resource:
	return NBNativeRuntimeOperations.create(operation,load("res://addons/angara_fx/runtime/runtime_operation.gd"),NBInput)

static func prepare_value(value: Variant,type: String="") -> Variant:
	return NBNativeRuntimeOperations.prepare_value(value,type)

func get_input(key: String,fallback: Variant=null,occurrence: int=0) -> Variant:
	return NBNativeRuntimeOperations.input_value(indexed,key,fallback,occurrence)
