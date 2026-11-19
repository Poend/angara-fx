@tool
extends RefCounted
static func compute(resource: Resource) -> String:return NBNativeEditorModels.compute(resource)
static func _token(digest: HashingContext,value: String) -> void:NBNativeEditorModels.token(digest,value)
static func _feed(value: Variant,digest: HashingContext,ancestors: Dictionary) -> void:NBNativeEditorModels.feed(value,digest,ancestors)
static func _plain_container(value: Variant) -> bool:return NBNativeEditorModels.plain_container(value)
