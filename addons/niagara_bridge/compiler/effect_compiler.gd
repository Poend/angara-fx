@tool
extends RefCounted
const DynamicMath = preload("res://addons/niagara_bridge/runtime/dynamic_math.gd")
## Builds an ephemeral execution plan. The authored document remains a .tres.
const Values = preload("res://addons/niagara_bridge/runtime/value_evaluator.gd")
const GPUProgram = preload("res://addons/niagara_bridge/compiler/gpu_program.gd")
const Ribbons = preload("res://addons/niagara_bridge/runtime/ribbon_geometry.gd")
const SourceRNGProgram = preload("res://addons/niagara_bridge/compiler/source_rng_program.gd")
const SourceClock = preload("res://addons/niagara_bridge/runtime/source_clock.gd")
const Lifecycle = preload("res://addons/niagara_bridge/runtime/lifecycle.gd")
const CurlNoise = preload("res://addons/niagara_bridge/runtime/curl_noise.gd")
const Attributes = preload("res://addons/niagara_bridge/compiler/attribute_layout.gd")
const NativeParticles = preload("res://addons/niagara_bridge/runtime/native_vm_particles.gd")
const MeshFacing = preload("res://addons/niagara_bridge/runtime/mesh_facing.gd")
const NativeSystem = preload("res://addons/niagara_bridge/runtime/native_system_vm.gd")
const SpriteFacing = preload("res://addons/niagara_bridge/runtime/sprite_facing.gd")
const ParticleLights = preload("res://addons/niagara_bridge/runtime/particle_lights.gd")
const RendererFilter = preload("res://addons/niagara_bridge/runtime/renderer_filter.gd")
const RendererAttributes=preload("res://addons/niagara_bridge/runtime/renderer_attributes.gd")

const MODULE_STAGES: Dictionary = {
	"/NiagaraFluids/Modules/Particles/SetFluidSourceAttributes@3.0": ["ParticleUpdate", "ParticleSpawn"],
	"SystemState": ["SystemUpdate"],
	"EmitterState": ["EmitterUpdate"],
	"SpawnBurst_Instantaneous": ["EmitterUpdate"],
	"SpawnBurst_Instantaneous@1.0": ["EmitterUpdate"],
	"SpawnRate": ["EmitterUpdate"],
	"V2/InitializeParticle": ["ParticleSpawn"],
	"V2/SphereLocation": ["ParticleSpawn"],
	"ShapeLocation": ["ParticleSpawn"],
	"TorusLocation": ["ParticleSpawn"],
	"BoxLocation": ["ParticleSpawn"],
	"KillParticlesInVolume": ["ParticleUpdate"],
	"WindForce@1.0": ["ParticleUpdate"],
	"AddVelocity": ["ParticleSpawn", "ParticleUpdate"],
	"AddVelocity@1.0": ["ParticleSpawn", "ParticleUpdate"],
	"AddVelocityFromPoint": ["ParticleSpawn", "ParticleUpdate"],
	"ParticleState@1.1": ["ParticleUpdate"],
	"ParticleState@1.0": ["ParticleUpdate"],
	"GravityForce": ["ParticleUpdate"],
	"VortexForce": ["ParticleUpdate"],
	"VortexForce@1.1": ["ParticleUpdate"],
	"LineAttractionForce": ["ParticleUpdate"],
	"LineAttractionForce@1.0": ["ParticleUpdate"],
	"V2/CurlNoiseForce@1.2": ["ParticleUpdate"],
	"V2/CurlNoiseForce@1.1": ["ParticleUpdate"],
	"V2/CurlNoiseForce@1.0": ["ParticleUpdate"],
	"ScaleColor@1.0": ["ParticleUpdate"],
	"Color": ["ParticleSpawn", "ParticleUpdate"],
	"CameraOffset": ["ParticleSpawn", "ParticleUpdate"],
	"KillParticles": ["ParticleSpawn", "ParticleUpdate"],
	"SpriteRotationRate": ["ParticleUpdate"],
	"Drag@1.0": ["ParticleUpdate"],
	"Drag": ["ParticleUpdate"],
	"Drag@1.1": ["ParticleUpdate"],
	"V2/SubUVAnimation@1.0": ["ParticleUpdate"],
	"ScaleSpriteSize": ["ParticleUpdate"],
	"ScaleSpriteSize@1.0": ["ParticleUpdate"],
	"ScaleMeshSize": ["ParticleUpdate"],
	"V3/InitialMeshOrientation": ["ParticleSpawn"],
	"V3/InitialMeshOrientation@1.0": ["ParticleSpawn"],
	"V3/UpdateMeshOrientation": ["ParticleUpdate"],
	"V3/UpdateMeshOrientation@1.0": ["ParticleUpdate"],
	"SolveForcesAndVelocity": ["ParticleUpdate"],
	"ScaleSpriteSizeBySpeed": ["ParticleUpdate"],
	"DynamicMaterialParameters@1.1": ["ParticleSpawn", "ParticleUpdate"],
	"DynamicMaterialParameters@1.0": ["ParticleSpawn", "ParticleUpdate"],
}
const DYNAMIC_CALLS: Array[String] = ["RandomRangeFloat", "RandomRangeVector", "RandomRangeInt", "FloatFromCurve", "ColorFromCurve", "Vector2DFromCurve", "VectorFromCurve", "ReturnNormalizedExecIndex", "VectorFromFloat", "Vector2DFromFloat", "Add_Float", "Multiply_Float", "MakeVector", "MakeVector2D", "MakeFloatFromVector2d", "MakeVectorFromLinearColorRGB", "Multiply_Vector", "Multiply_VectorByFloat", "Add_Vector", "AddVectorToPosition", "TransformVector", "Lerp_Float", "Subtract_Float", "Divide_Float", "Multiply_Vector2D_ByFloat", "InvertBool"]
## Controls currently lowered as authored constants, never evaluated per particle.
## Ordinary numeric inputs sharing a name (ScaleRGB occurrence1) are excluded.
const CONSTANT_BOOL_INPUTS: Array[String]=["EnableDistanceCulling","EnableVisibilityCulling","bApplySpawnCountScaleByDistance","ScaleSpawnCount","UseLoopDelay","UseVelDistribution","UseConeFalloff","OverrideSeed","SurfaceOnly","UseSphereOrientation","HemisphereX","HemisphereY","HemisphereZ","MaskSpawnGroup","WritetoIntrinsicVariables","KillParticlesWhenLifetimeHasElapsed","LetInfinitelyLivedParticlesDieWhenEmitterDeactivates","LoopParticlesLifetime","OverrideQuat","ClampVelocity","LimitAcceleration","ManuallyEnableRotationalSolver","WritetoIntrinsicProperties","UseSpawnProbability","UseLoopCountLimit","SampleScaleFactorByCurve","WriteParameterIndex0","WriteParameterIndex1","WriteParameterIndex2","WriteParameterIndex3","UseOwnerScale","HemicircleX","HemicircleY","EnableLatheProfile","UseEndcapsInSurfaceOnlyMode","UseNonUniformScale","UseOffset","UseLinearDrag","IgnoreMass","UseRotationalDrag","ScaleRGB","ScaleA","UseRotationVector","WritetoPresolveProperties","Param0WriteEnabled","Param1WriteEnabled","Param2WriteEnabled","Param3WriteEnabled","AdjustHue","AdjustSaturation","AdjustValue","AdjustAlpha","UsePositionOffset","WriteMaterialRandom","TransformFacingOrigin","TransformFacingPosition","FacingInterpolation","TransformSide","TransformUp"]
const REFERENCES: Array[String] = ["Particles.SpawnGroup", "Particles.Age", "Particles.Lifetime", "Particles.NormalizedAge", "Particles.Position", "Particles.Velocity", "Particles.Previous.Velocity", "Particles.SpriteAlignment", "Particles.SpriteSize", "Particles.Initial.SpriteSize", "Particles.Color", "Particles.Initial.Color", "Particles.Mass", "Particles.SubImageIndex", "Particles.MaterialRandom", "Particles.UVScale", "Particles.Scale", "Particles.MeshOrientation", "Particles.MeshIndex", "Transient.PhysicsForce", "Transient.PhysicsDrag", "Transient.DragIgnoreMass", "Transient.AdvectionVelocity", "Transient.SpriteSizeScaleFactor", "Particles.Presolve.Position", "Particles.Presolve.Velocity", "Particles.Presolve.PhysicsForce", "Engine.DeltaTime", "Emitter.LoopedAge", "Emitter.Age", "System.Age"]

## Plan construction and validation execute in C++; the remaining generators are explicit seams.
const NATIVE_DEPENDENCIES = {
	"gpu":GPUProgram,"source_rng":SourceRNGProgram,"ribbons":Ribbons,
	"events":preload("res://addons/niagara_bridge/runtime/native_vm_events.gd"),
}
const NATIVE_CONTRACTS = {
	"builtins":Attributes.BUILTINS,"dynamic_specs":DynamicMath.SPECS,
	"lifecycle_fields":Lifecycle.FIELDS,"dynamic_calls":DYNAMIC_CALLS,"references":REFERENCES,
	"module_stages":MODULE_STAGES,"constant_bool_inputs":CONSTANT_BOOL_INPUTS,
	"module_outputs":Attributes.ModuleOutputs.SCHEMAS,"renderer_fields":RendererAttributes.FIELDS,
	"sprite_facing":SpriteFacing.FACING,"sprite_alignment":SpriteFacing.ALIGNMENT,
	"gpu_contracts":GPUProgram.CONTRACTS,
}
static func _native_call(method: String, arguments: Array) -> Variant:
	return NBNativeEffectCompiler.invoke(method,arguments,NATIVE_DEPENDENCIES,NATIVE_CONTRACTS)

static func effective_parameters(effect: Resource, overrides: Dictionary = {}) -> Dictionary:
	return _native_call("effective_parameters",[effect,overrides])

static func compile_effect(effect: Resource, user_parameters: Dictionary = {}, options: Dictionary = {}) -> Dictionary:
	return _native_call("compile_effect",[effect,user_parameters,options])

static func resolved_sprite_modes(renderer: Resource, attributes: Dictionary) -> Dictionary:
	return _native_call("resolved_sprite_modes",[renderer,attributes])

static func renderer_dynamic_parameter_mask(plan: Dictionary, renderer: Resource) -> int:
	return _native_call("renderer_dynamic_parameter_mask",[plan,renderer])

static func _rate_value_supported(value: Variant, parameters: Dictionary) -> bool:
	return _native_call("_rate_value_supported",[value,parameters])

static func _legacy_orientation_errors(operation: Resource, types: Dictionary) -> Array[String]:
	return _native_call("_legacy_orientation_errors",[operation,types])

static func _torus_errors(operation: Resource, types: Dictionary) -> Array[String]:
	return _native_call("_torus_errors",[operation,types])

static func _shape_errors(operation: Resource, parameters: Dictionary) -> Array[String]:
	return _native_call("_shape_errors",[operation,parameters])

static func _compile_mesh_renderer(renderer: Resource, parameters: Dictionary, result: Dictionary, emitter: Resource) -> Dictionary:
	return _native_call("_compile_mesh_renderer",[renderer,parameters,result,emitter])

static func _check_value(value: Variant, user_parameters: Dictionary = {}) -> bool:
	return _native_call("_check_value",[value,user_parameters])

static func _math_arguments_valid(value: Dictionary, parameters: Dictionary) -> bool:
	return _native_call("_math_arguments_valid",[value,parameters])

static func _execution_index_arguments_valid(value: Dictionary, parameters: Dictionary) -> bool:
	return _native_call("_execution_index_arguments_valid",[value,parameters])

static func _uses_execution_index(value: Variant) -> bool:
	return _native_call("_uses_execution_index",[value])

static func _lifecycle_expression_error(value: Variant, system: bool) -> String:
	return _native_call("_lifecycle_expression_error",[value,system])

static func _lifecycle_errors(operation: Resource, parameters: Dictionary, types: Dictionary, system: bool = false) -> Array[String]:
	return _native_call("_lifecycle_errors",[operation,parameters,types,system])

static func _curl_errors(operation: Resource, types: Dictionary) -> Array[String]:
	return _native_call("_curl_errors",[operation,types])

static func _attribute_types(effect: Resource,layout: Dictionary) -> Dictionary:
	return _native_call("_attribute_types",[effect,layout])

static func renderer_camera_offset_reference(renderer: Resource) -> String:
	return _native_call("renderer_camera_offset_reference",[renderer])

static func _camera_offset_binding_errors(renderer: Resource,types: Dictionary,parameters: Dictionary={},layout: Dictionary={}) -> Array[String]:
	return _native_call("_camera_offset_binding_errors",[renderer,types,parameters,layout])

static func _written_renderer_attribute_errors(renderer: Resource,layout: Dictionary) -> Array[String]:
	return _native_call("_written_renderer_attribute_errors",[renderer,layout])

static func _curve_errors(value: Variant, user_parameters: Dictionary) -> Array[String]:
	return _native_call("_curve_errors",[value,user_parameters])

static func _missing_user_parameters(value: Variant, user_parameters: Dictionary) -> Array[String]:
	return _native_call("_missing_user_parameters",[value,user_parameters])

static func _source_spawn_order(emitter: Resource) -> Dictionary:
	return _native_call("_source_spawn_order",[emitter])

static func _mesh_scale_default(emitter: Resource) -> Vector3:
	return _native_call("_mesh_scale_default",[emitter])

static func _issue(result: Dictionary, emitter: Resource, operation: Resource, severity: String, message: String) -> void:
	_native_call("_issue",[result,emitter,operation,severity,message])
