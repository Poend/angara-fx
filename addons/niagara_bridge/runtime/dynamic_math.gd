@tool
extends NBNativeDynamicMath
## Native dynamic-input defaults from the UE5.8 object graphs. No asset routing.
const SPECS: Dictionary = {
	"Lerp_Float": {"type": "float", "inputs": {"A": ["float", 0.0], "B": ["float", 1.0], "Alpha": ["float", 0.5]}},
	"Subtract_Float": {"type": "float", "inputs": {"A": ["float", 0.0], "B": ["float", 0.0]}},
	"Divide_Float": {"type": "float", "inputs": {"A": ["float", 1.0], "B": ["float", 1.0]}},
	"Multiply_Vector2D_ByFloat": {"type": "vec2", "inputs": {"Vector2D": ["vec2", [1.0, 1.0]], "Float": ["float", 1.0]}},
	"InvertBool": {"type": "bool", "inputs": {"Boolean": ["bool", false]}},
}

