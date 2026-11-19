@tool
extends Resource
## Authored Mesh Renderer entry. Geometry is converted from Unreal exactly once.
@export var mesh: Mesh
@export var source_path: String = ""
## Full User.* parameter name. A missing override can use the authored mesh fallback.
@export var user_parameter: String = ""
## Godot-space entry rotation and scale. Translation belongs to pivot_offset.
@export var local_transform: Transform3D = Transform3D.IDENTITY
## Rotation is explicit because pivot rotation excludes signed mesh scale.
@export var rotation: Quaternion = Quaternion.IDENTITY
## Metres in the space named below, after coordinate conversion at import.
@export var pivot_offset: Vector3 = Vector3.ZERO
@export var pivot_space: String = "Mesh"
@export var lod_index: int = 0
