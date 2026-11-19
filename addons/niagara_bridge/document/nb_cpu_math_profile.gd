@tool
extends Resource
## Source execution profile, not host CPU instructions. Stored as readable text.
@export var profile_id: String = ""
@export var source_cpu_vendor: String = ""
@export var reciprocal_mantissa_shift: int = 11
@export var reciprocal_bits: PackedInt32Array = PackedInt32Array()
