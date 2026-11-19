@tool
extends NBNativeVMEvents
## Event datasets are frame-local and source-named. Inter-emitter receivers see
## this tick; self receivers retain the pre-update buffer used by Niagara.

static func handlers(plan: Dictionary) -> Array:
	return NBNativeEffectController.handlers(plan)
