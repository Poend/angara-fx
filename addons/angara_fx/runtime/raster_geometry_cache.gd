@tool
extends RefCounted
## Native cache holds weak mesh ownership and invalidates on changed.
const META="angara_fx_immutable_geometry"
static var _entries: Dictionary:
	get:return NBNativeGeometryCache.entries()
static func packet(mesh: Mesh,surface: int) -> Dictionary:return NBNativeGeometryCache.packet(mesh,surface)
static func build(mesh: Mesh,surface: int) -> Dictionary:return NBNativeGeometryCache.build(mesh,surface)
static func invalidate(identity: int) -> void:NBNativeGeometryCache.invalidate(identity)
