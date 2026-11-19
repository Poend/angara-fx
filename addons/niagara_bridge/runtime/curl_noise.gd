@tool
extends NBNativeCurlNoise
## V2 CurlNoiseForce 1.0/1.1/1.2 baked-field branches, from native graphs and DI source.
## Source1 changes only Ultra procedural scale; the baked fields match Source0.
const Values = preload("res://addons/niagara_bridge/runtime/value_evaluator.gd")
const RNG = preload("res://addons/niagara_bridge/runtime/source_rng.gd")
const FIELDS: Array = [
	preload("res://addons/niagara_bridge/data/vector_fields/TilingCurl16.tres"),
	preload("res://addons/niagara_bridge/data/vector_fields/TilingCurl32.tres"),
	preload("res://addons/niagara_bridge/data/vector_fields/TilingCurl64.tres"),
]

