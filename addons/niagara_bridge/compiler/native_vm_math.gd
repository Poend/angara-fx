@tool
extends RefCounted
## Emitted compiler assembly evidence; independent of observed particle output.
static func extract(hlsl: String,assembly: String) -> Dictionary:
	return NBNativeProgramSchema.extract_math(hlsl,assembly)
