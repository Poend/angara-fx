@tool
extends NBNativeValues
## Dynamic expressions, coordinates, ranges and curves execute in C++.
const Curves = preload("res://addons/niagara_bridge/runtime/curve_evaluator.gd")
const SourceRNG = preload("res://addons/niagara_bridge/runtime/source_rng.gd")
const DynamicMath = preload("res://addons/niagara_bridge/runtime/dynamic_math.gd")
