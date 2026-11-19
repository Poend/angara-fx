@tool
extends RefCounted
## Compile a restricted Custom expression with a scalar result.
func compile(source: String,inputs: Dictionary) -> Dictionary:return NBNativeCustomArithmetic.compile(source,inputs)
