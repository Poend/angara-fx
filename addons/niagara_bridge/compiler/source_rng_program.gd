@tool
extends RefCounted
## Native binding of emitted source RNG/VM calls and curve-program provenance.
const Values=preload("res://addons/niagara_bridge/runtime/value_evaluator.gd")

static func system_fingerprint(effect: Resource) -> String:
	return NBNativeSourceProgram.system_fingerprint(effect)

static func fingerprint(emitter: Resource) -> String:
	return NBNativeSourceProgram.fingerprint(emitter)

static func build(effect: Resource, plan: Dictionary, parameters: Dictionary = {}) -> Dictionary:
	return NBNativeSourceProgram.build(effect,plan,parameters)

static func _bind_lifecycle(effect: Resource,plan: Dictionary,packet: Dictionary,result: Dictionary) -> void:
	NBNativeSourceProgram.invoke("_bind_lifecycle",[effect,plan,packet,result])

static func _build_module_programs(plan: Dictionary,packet: Dictionary,result: Dictionary) -> Dictionary:
	return NBNativeSourceProgram.invoke("_build_module_programs",[plan,packet,result])

static func _bind_emitter_spawn(plan: Dictionary,packet: Dictionary,result: Dictionary) -> void:
	NBNativeSourceProgram.invoke("_bind_emitter_spawn",[plan,packet,result])

static func _signed32(value: Variant) -> bool:
	return NBNativeSourceProgram.invoke("_signed32",[value])

static func _instance_int_parameter(contract: Dictionary,expression: String) -> Dictionary:
	return NBNativeSourceProgram.invoke("_instance_int_parameter",[contract,expression])

static func _bind_curl_offset(calls: Array, used: Array[int], bindings: Dictionary, operation: Resource, alias: String, emitter_seed: int, errors: Array) -> void:
	NBNativeSourceProgram.invoke("_bind_curl_offset",[calls,used,bindings,operation,alias,emitter_seed,errors])

static func _bind_intrinsic(calls: Array,used: Array[int],bindings: Dictionary,key: String,alias: String,helper_prefix: String,components: int,count: int,errors: Array) -> void:
	NBNativeSourceProgram.invoke("_bind_intrinsic",[calls,used,bindings,key,alias,helper_prefix,components,count,errors])

static func _bind_orientation(calls: Array,used: Array[int],bindings: Dictionary,operation: Resource,alias: String,errors: Array) -> void:
	NBNativeSourceProgram.invoke("_bind_orientation",[calls,used,bindings,operation,alias,errors])

static func _bind_color_adjustment(calls: Array,used: Array[int],bindings: Dictionary,key: String,alias: String,component: String,errors: Array) -> void:
	NBNativeSourceProgram.invoke("_bind_color_adjustment",[calls,used,bindings,key,alias,component,errors])

static func _bind(calls: Array,used: Array[int],bindings: Dictionary,key: String,target: String,components: int,errors: Array) -> void:
	NBNativeSourceProgram.invoke("_bind",[calls,used,bindings,key,target,components,errors])

static func _random_count(value: Variant) -> int:
	return NBNativeSourceProgram.invoke("_random_count",[value])

static func _random_leaves(value: Variant,path: String) -> Array:
	return NBNativeSourceProgram.invoke("_random_leaves",[value,path])

static func _bind_curves(operations: Array,contract: Dictionary,emitter_name: String,errors: Array) -> Dictionary:
	return NBNativeSourceProgram.invoke("_bind_curves",[operations,contract,emitter_name,errors])

static func _curve_sample_for_path(contract: Dictionary,target: String,arguments: Array,call_path: Array=[]) -> Dictionary:
	return NBNativeSourceProgram.invoke("_curve_sample_for_path",[contract,target,arguments,call_path])

static func _curve_sample_owner(sample: Dictionary) -> String:
	return NBNativeSourceProgram.invoke("_curve_sample_owner",[sample])

static func _curve_leaves(value: Variant,path: String,argument_path: Array=[],call_path: Array=[]) -> Array:
	return NBNativeSourceProgram.invoke("_curve_leaves",[value,path,argument_path,call_path])

static func _curve_identifier(name: String) -> String:
	return NBNativeSourceProgram.invoke("_curve_identifier",[name])

static func _compiled_curve(contract: Dictionary,owner: String,emitter_name: String,errors: Array) -> Dictionary:
	return NBNativeSourceProgram.invoke("_compiled_curve",[contract,owner,emitter_name,errors])

