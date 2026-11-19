@tool
extends NBNativeCurlNoise
## Three resolutions of the original Angara FX periodic flow field.
## Keep the resource paths stable for authored effects.
const Values = preload("res://addons/niagara_bridge/runtime/value_evaluator.gd")
const RNG = preload("res://addons/niagara_bridge/runtime/source_rng.gd")
const FIELDS: Array = [
	preload("res://addons/niagara_bridge/data/vector_fields/TilingCurl16.tres"),
	preload("res://addons/niagara_bridge/data/vector_fields/TilingCurl32.tres"),
	preload("res://addons/niagara_bridge/data/vector_fields/TilingCurl64.tres"),
]
