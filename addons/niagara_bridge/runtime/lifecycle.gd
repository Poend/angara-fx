@tool
extends NBNativeLifecycle
## Niagara EmitterState/ SystemState Source0, with float32 VM boundaries.
## State belongs to the player; authored resources remain ordinary text .tres.
const Values = preload("res://addons/niagara_bridge/runtime/value_evaluator.gd")
const F32 = preload("res://addons/niagara_bridge/runtime/source_rng.gd")
const ACTIVE = 0
const INACTIVE = 1
const CLEAR = 2
const COMPLETE = 3
const DISABLED = 4
const SCALABILITY = 0
const INTERNAL = 1
const OWNER = 2
const COMPLETION = 3
const FIELDS = {"Age": "float", "LoopedAge": "float", "NormalizedLoopAge": "float", "LoopCount": "int", "CurrentLoopDelay": "float", "CurrentLoopDuration": "float", "ExecutionState": "int", "ExecutionStateSource": "int"}
