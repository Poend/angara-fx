@tool
extends RefCounted
## Typed stage-local module outputs. Names remain data, never shader identifiers.
const SCHEMAS: Dictionary = {
	"BoxLocation": {"BoxVector":"vec3", "BoxNormal":"vec3"},
	"WindForce@1.0": {"IsAffectedByWind":"bool"},
	"TorusLocation": {"TorusVector":"vec3", "TorusTangent":"vec3", "TorusBitangent":"vec3", "TorusNormal":"vec3"},
	"VortexForce": {"VortexForce":"vec3"},
	"VortexForce@1.1": {"VortexForce":"vec3"},
}

static func names(operation: Resource) -> Dictionary:
	return NBNativeCompilerAttributes.module_outputs(operation,SCHEMAS)
