@tool
extends NBNativeValues
## Dynamic expressions, coordinates, ranges and curves execute in C++.
const Curves = preload("res://addons/angara_fx/runtime/curve_evaluator.gd")
const SourceRNG = preload("res://addons/angara_fx/runtime/source_rng.gd")
const DynamicMath = preload("res://addons/angara_fx/runtime/dynamic_math.gd")
