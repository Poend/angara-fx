@tool
extends Resource
## Source-space vector lattice. Authored as an ordinary text .tres, without binary payloads.
## Axis/unit conversion belongs to the consuming module, after field sampling.
@export var source_path: String = ""
@export var source_package_sha256: String = ""
@export var size: Vector3i = Vector3i.ZERO
@export var bounds_min: PackedFloat64Array = PackedFloat64Array([0, 0, 0])
@export var bounds_max: PackedFloat64Array = PackedFloat64Array([1, 1, 1])
@export var allow_cpu_access: bool = true
@export var vectors_xyz: PackedFloat32Array = PackedFloat32Array()

func validation_error() -> String:
	return NBNativeVectorFields.validation_error(self)

func sample_cpu(position: Vector3,tiling: Vector3i=Vector3i.ONE) -> Vector3:
	return NBNativeVectorFields.sample_cpu(self,position,tiling)

static func _index(value: float,dimension: int,tiled: bool) -> int:
	return NBNativeVectorFields._index(value,dimension,tiled)

static func _lerp(a: float,b: float,weight: float) -> float:
	return NBNativeVectorFields._lerp(a,b,weight)

func _value(x: int,y: int,z: int,component: int) -> float:
	return NBNativeVectorFields._value(self,x,y,z,component)
