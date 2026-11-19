@tool
extends RefCounted
## Import schema validation is separate from native VM executable capabilities.
const SCALAR_TYPES: Array[String] = ["float32", "int32", "float16"]

static func same_constants(left: Array,right: Array) -> bool:
	return NBNativeProgramSchema.same_constants(left,right)

static func validate(value: Variant) -> Array[String]:
	return NBNativeProgramSchema.validate(value)

static func _uint(value: Variant) -> bool:
	return NBNativeProgramSchema.unsigned_integer(value)

static func _binding(row: Dictionary) -> bool:
	return NBNativeProgramSchema.binding(row)

static func _scalar(value: Variant,type: String) -> bool:
	return NBNativeProgramSchema.scalar(value,type)
