@tool
extends RefCounted
static func snapshot_bounds(snapshot: Array) -> Dictionary:return NBNativeEditorOperations.snapshot_bounds(snapshot)
static func frame(camera: Camera3D,bounds: AABB,viewport_size: Vector2) -> Vector3:return NBNativeEditorOperations.frame(camera,bounds,viewport_size)
