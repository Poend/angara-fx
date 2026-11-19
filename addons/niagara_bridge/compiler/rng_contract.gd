@tool
extends NBNativeRNGContract
## Native conservative emitted-source RNG/curve/value dependency extraction.
## Unsupported control flow remains explicit; this is not an HLSL interpreter.
const LegacyRNG=preload("res://addons/niagara_bridge/compiler/legacy_rng_shape.gd")
