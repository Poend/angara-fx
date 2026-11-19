@tool
extends RefCounted
## Reviewed emitted helper semantics, independent of generated local names.
## All tokens except whitespace/local identifiers/map stage remain significant.
## Changed formulas, branches, struct members, literals or calls fail closed.
const INFO = "2580a7fc4dbf69afc6eff2aaea5c79e72b70585b78d0498c3eeca76f6746155d"
const RANGE = {"e20fdef635a8b3e059d248bd899962e53351f900dd4ccb6bd60b552bf896ead6": "float2", "f6903ffde5d0253be400cadcec752945cf532356112f8986065dfeb5955f4a68": "float"}
const SCALAR_RANGE = "b497fe66e2cd172129ce11beb3fed277ac5d0f4bfd59d41ef4bf37701ff5b2f0"

static func signature(body: String) -> String:
	return NBNativeLegacyRNGShape.signature(body)

static func inspect(name: String,functions: Dictionary,environment: Dictionary) -> Dictionary:
	return NBNativeLegacyRNGShape.inspect(name,functions,environment)

static func zero_spawn_id_on_update(source: String,functions: Dictionary) -> bool:
	return NBNativeLegacyRNGShape.zero_spawn_id_on_update(source,functions)
