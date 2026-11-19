@tool
extends RefCounted
const NativeBounds = preload("../../runtime/native_component_bounds.gd")
static func bind(material: Material,mesh: Mesh,renderer_kind: int,diagnostics: Array[Dictionary],location: String="",surface: int=-1,component_bounds: Variant=null) -> Material:return NBNativeMaterialAuthoring.bind_material(material,mesh,renderer_kind,diagnostics,location,surface,component_bounds)
static func apply(effect: Resource,diagnostics: Array[Dictionary]) -> void:NBNativeMaterialAuthoring.apply(effect,diagnostics)
