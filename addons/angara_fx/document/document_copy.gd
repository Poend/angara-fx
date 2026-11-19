@tool
extends RefCounted
## Copies authored document resources while retaining external Mesh/Material assets.
static func copy(value: Variant, renew_ids: bool = false, seen: Dictionary = {}) -> Variant:
	return NBNativeDocumentCopy.copy(value,renew_ids,seen)
