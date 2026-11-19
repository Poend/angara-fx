@tool
extends NBNativeRibbonGeometry
## Transient Plane/Screen ribbon geometry from Niagara's CPU generation and VF.
## Source references and remaining view/culling limitations are in runtime/README.md.
const Values = preload("res://addons/angara_fx/runtime/value_evaluator.gd")
const MIN_SEGMENT_CM: float = 1.0
