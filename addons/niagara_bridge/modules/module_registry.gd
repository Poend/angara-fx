@tool
extends RefCounted
## Authoring catalog. The compiler checks the selected execution backend and module inputs.
const Module = preload("res://addons/niagara_bridge/document/nb_module.gd")
const InputResource = preload("res://addons/niagara_bridge/document/nb_input.gd")
## Pins evaluated from the instance context. Compile-time switches and enum inputs
## deliberately keep their literal editor. A suffix selects a duplicate pin by occurrence.
const LIVE_INPUTS := {
	"SpawnRate": {"SpawnRate":"float", "SpawnGroup":"int"},
	"SpawnBurst_Instantaneous": {"SpawnGroup":"int"},
	"SpawnBurst_Instantaneous@1.0": {"SpawnGroup":"int"},
	"V2/InitializeParticle": {"Lifetime":"float", "LifetimeMin":"float", "LifetimeMax":"float", "Color":"color", "SpriteSizeMin":"vec2", "SpriteSizeMax":"vec2", "UniformSpriteSize":"float", "PositionOffset":"vec3", "Mass":"float"},
	"AddVelocity": {"Velocity":"vec3", "VelocitySpeedScale":"float"},
	"AddVelocity@1.0": {"Velocity":"vec3", "ScaleAddedVelocity":"vec3"},
	"AddVelocityFromPoint": {"VelocityStrength":"float", "OriginOffset":"vec3", "VelocityFalloffDistance":"float"},
	"GravityForce": {"Gravity":"vec3"},
	"BoxLocation": {"SpawnGroupMask":"int", "RandomSeed":"int", "BoxSize":"vec3", "BoxOffset":"vec3", "RotateBoxOnAxis":"vec3", "NormalizedBoxRotationAngle":"float", "SurfaceOnlyBandThickness":"float"},
	"KillParticlesInVolume": {"BoxSize":"vec3", "SphereRadius":"float", "PlaneNormal":"vec3", "SlabAxis":"vec3", "SlabWidth":"float", "ConeAxis":"vec3", "ConeAngle":"float", "ConeLength":"float", "OriginOffset":"vec3", "KillVolumeEnabled":"bool", "InvertVolume":"bool"},
	"WindForce@1.0": {"WindSpeed":"vec3", "WindSpeedScale":"float", "AirResistance":"float"},
	"Drag@1.0": {"Drag":"float", "RotationalDrag":"float"},
	"ParticleState@1.0": {"Lifetime":"float", "DeltaTime":"float"},
	"ParticleState@1.1": {"Lifetime":"float", "DeltaTime":"float"},
	"KillParticles": {"KillParticles":"bool"},
	"Color": {"Color":"color", "ScaleColor":"vec3", "ScaleAlpha":"float"},
	"ScaleColor@1.0": {"ColorValueToScale":"color", "ScaleRGB#1":"vec3", "ScaleAlpha":"float"},
	"ScaleSpriteSize": {"UniformCurveScale":"float"},
	"ScaleSpriteSizeBySpeed": {"MinScaleFactor":"vec2", "MaxScaleFactor":"vec2", "VelocityThreshold":"float"},
	"SolveForcesAndVelocity": {"DeltaTime":"float", "Position":"vec3", "Velocity":"vec3", "Force":"vec3"},
	"DynamicMaterialParameters@1.1": {"Index0Param1":"float", "Index0Param2":"float", "Index0Param3":"float", "Index0Param4":"float"},
}

static func live_input_type(module: Resource, input: Resource) -> String:
	if module.operation_kind == "assignment":
		return NBNativeAttributeValues.type_name(module.target_type)
	var occurrence := 0
	for other in module.inputs:
		if other == input:
			break
		if other.input_name == input.input_name:
			occurrence += 1
	var key: String = input.input_name if occurrence == 0 else "%s#%d" % [input.input_name, occurrence]
	return LIVE_INPUTS.get(module.module_name, {}).get(key, "")

const ENTRIES: Array[Dictionary] = [
	{"name": "KillParticles", "stages": ["ParticleUpdate"], "defaults": [{"name": "KillParticles", "value": false}]},
	{"name": "SystemState", "stages": ["SystemUpdate"], "defaults": [{"name": "LoopBehavior", "value": "Infinite"}, {"name": "LoopDuration", "value": 1.0}]},
	{"name": "EmitterState", "stages": ["EmitterUpdate"], "defaults": [{"name": "LoopBehavior", "value": "Infinite"}, {"name": "LoopDuration", "value": 1.0}, {"name": "UseLoopDelay", "value": false}]},
	{"name": "SpawnBurst_Instantaneous", "stages": ["EmitterUpdate"], "defaults": [{"name": "SpawnCount", "value": 10}, {"name": "SpawnTime", "value": 0.0}, {"name":"SpawnGroup","value":0}, {"name": "UseSpawnProbability", "value": false}]},
	{"name": "SpawnRate", "stages": ["EmitterUpdate"], "defaults": [{"name": "SpawnRate", "value": 10.0}, {"name":"SpawnGroup","value":0}, {"name": "UseSpawnProbability", "value": false}]},
	{"name": "V2/InitializeParticle", "stages": ["ParticleSpawn"], "defaults": [{"name": "SpriteSizeMode", "value": "RandomNonUniform"}, {"name": "ColorMode", "value": "DirectSet"}, {"name": "PositionMode", "value": "SimulationPosition"}, {"name": "LifetimeMode", "value": "Random"}, {"name": "LifetimeMin", "value": 0.5}, {"name": "LifetimeMax", "value": 1.0}, {"name": "Color", "value": [1.0, 1.0, 1.0, 1.0]}, {"name": "SpriteSizeMin", "value": [10.0, 10.0]}, {"name": "SpriteSizeMax", "value": [20.0, 20.0]}]},
	{"name": "AddVelocity", "stages": ["ParticleSpawn", "ParticleUpdate"], "defaults": [{"name": "VelocityMode", "value": "Linear"}, {"name": "Velocity", "value": [100.0, 0.0, 0.0]}, {"name": "VelocitySpeedScale", "value": 1.0}]},
	{"name": "AddVelocity@1.0", "stages": ["ParticleSpawn", "ParticleUpdate"], "defaults": [{"name": "Velocity", "value": [0.0, 0.0, 0.0]}, {"name": "ScaleAddedVelocity", "value": [1.0, 1.0, 1.0]}, {"name": "CoordinateSpace", "value": "Local"}]},
	{"name": "AddVelocityFromPoint", "stages": ["ParticleSpawn", "ParticleUpdate"], "defaults": [{"name": "VelocityStrength", "value": 100.0}, {"name": "OriginOffset", "value": [0.0, 0.0, 0.0]}, {"name": "OriginOffsetCoordinateSpace", "value": "Local"}, {"name": "UseFalloffDistance", "value": false}, {"name": "VelocityFalloffDistance", "value": 100.0}, {"name": "InvertVelocityFalloff", "value": true}]},
	{"name": "ParticleState@1.1", "stages": ["ParticleUpdate"], "defaults": [{"name": "KillParticlesWhenLifetimeHasElapsed", "value": true}]},
	{"name": "GravityForce", "stages": ["ParticleUpdate"], "defaults": [{"name": "CoordinateSpace", "value": "World"}, {"name": "Gravity", "value": [0.0, 0.0, -980.0]}]},
	{"name": "BoxLocation", "stages": ["ParticleSpawn"], "defaults": [
		{"name":"BoxSize","value":[100.0,100.0,100.0]}, {"name":"BoxOffset","value":[0.0,0.0,0.0]}, {"name":"CoordinateSpace","value":"Local"},
		{"name":"SurfaceOnly","value":false}, {"name":"SurfaceOnlyBandThickness","value":0.0}, {"name":"SurfaceExpansionMode","value":"Inside"},
		{"name":"MaskSpawnGroup","value":false}, {"name":"SpawnGroupMask","value":0},
		{"name":"OverrideRandomness","value":false}, {"name":"RandomnessMode","value":"SimulationDefaults"},
		{"name":"OverrideSeed","value":false}, {"name":"RandomSeed","value":0}, {"name":"FixedRandomSeed","value":false},
		{"name":"RotateBox","value":false}, {"name":"RotateBoxOnAxis","value":[0.0,0.0,1.0]}, {"name":"NormalizedBoxRotationAngle","value":0.0}]},
	{"name": "KillParticlesInVolume", "stages": ["ParticleUpdate"], "defaults": [
		{"name":"KillShape","value":"Box"}, {"name":"BoxSize","value":[100.0,100.0,100.0]}, {"name":"SphereRadius","value":100.0},
		{"name":"PlaneNormal","value":[0.0,0.0,1.0]}, {"name":"SlabAxis","value":[0.0,0.0,1.0]}, {"name":"SlabWidth","value":100.0},
		{"name":"ConeAxis","value":[1.0,0.0,0.0]}, {"name":"ConeAngle","value":45.0}, {"name":"ConeLength","value":200.0},
		{"name":"OriginOffset","value":[0.0,0.0,0.0]}, {"name":"KillVolumeEnabled","value":true}, {"name":"InvertVolume","value":false}]},
	{"name": "WindForce@1.0", "stages": ["ParticleUpdate"], "defaults": [{"name":"WindSpeed","value":[0.0,0.0,50.0]}, {"name":"WindSpeedScale","value":1.0}, {"name":"AirResistance","value":0.2}]},
	{"name": "ScaleColor@1.0", "stages": ["ParticleUpdate"], "defaults": [{"name": "ScaleRGB", "value": true}, {"name": "ScaleRGB", "value": [1.0, 1.0, 1.0]}, {"name": "ScaleA", "value": true}, {"name": "ScaleAlpha", "value": 1.0}]},
	{"name": "Color", "stages": ["ParticleSpawn", "ParticleUpdate"], "defaults": [{"name": "Color", "value": [1.0, 1.0, 1.0, 1.0]}, {"name": "ScaleColor", "value": [1.0, 1.0, 1.0]}, {"name": "ScaleAlpha", "value": 1.0}]},
	{"name": "Drag@1.0", "stages": ["ParticleUpdate"], "defaults": [{"name": "UseLinearDrag", "value": true}, {"name": "Drag", "value": 1.0}, {"name": "UseRotationalDrag", "value": false}]},
	{"name": "ScaleSpriteSize", "stages": ["ParticleUpdate"], "defaults": [{"name": "ScaleSpriteSizeMode", "value": "UniformCurve"}, {"name": "UniformCurveSpriteScale", "value": {"kind": "curve", "keys": [{"time": 0.0, "value": 0.0, "interp": "Linear", "arrive": 0.0, "leave": 0.0}, {"time": 1.0, "value": 1.0, "interp": "Linear", "arrive": 0.0, "leave": 0.0}]}}, {"name": "UniformCurveScale", "value": 1.0}]},
	{"name": "SolveForcesAndVelocity", "stages": ["ParticleUpdate"], "defaults": [{"name": "ClampVelocity", "value": false}, {"name": "LimitAcceleration", "value": false}]},
	{"name": "ScaleSpriteSizeBySpeed", "stages": ["ParticleUpdate"], "defaults": [{"name": "MinScaleFactor", "value": [1.0, 1.0]}, {"name": "MaxScaleFactor", "value": [1.0, 2.0]}, {"name": "VelocityThreshold", "value": 1000.0}, {"name": "SampleScaleFactorByCurve", "value": false}]},
	{"name": "DynamicMaterialParameters@1.1", "stages": ["ParticleSpawn", "ParticleUpdate"], "defaults": [{"name": "WriteParameterIndex0", "value": true}, {"name": "Param0WriteEnabled", "value": true}, {"name": "Index0Param1", "value": 0.0}, {"name": "Param1WriteEnabled", "value": true}, {"name": "Index0Param2", "value": 0.0}, {"name": "Param2WriteEnabled", "value": true}, {"name": "Index0Param3", "value": 0.0}, {"name": "Param3WriteEnabled", "value": true}, {"name": "Index0Param4", "value": 0.0}]},
]

static func list_for_stage(stage: String) -> Array[Dictionary]:
	return NBNativeEditorActions.catalog(ENTRIES,stage)

static func create_module(name: String) -> Resource:
	return NBNativeEditorActions.create_module(ENTRIES,name,Module,InputResource)

static func validate_stage(module: Resource,stage_name: String,original_stage: String="") -> String:
	return NBNativeEditorActions.validate_module_stage(ENTRIES,module,stage_name,original_stage)
