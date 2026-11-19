@tool
extends RefCounted
## Lowers the same ordered NBModule/dynamic-input execution plan as the CPU target.
## There are no effect/emitter-name branches. Unsupported expressions fail closed.
const Values = preload("res://addons/niagara_bridge/runtime/value_evaluator.gd")
const Kernel = preload("res://addons/niagara_bridge/runtime/gpu_shader_template.gd")
const Attributes = preload("res://addons/niagara_bridge/compiler/attribute_layout.gd")
const DynamicMath = preload("res://addons/niagara_bridge/runtime/dynamic_math.gd")
const Lifecycle = preload("res://addons/niagara_bridge/runtime/lifecycle.gd")
const CurlNoise = preload("res://addons/niagara_bridge/runtime/curl_noise.gd")
const VectorFieldGPU = preload("res://addons/niagara_bridge/runtime/vector_field_gpu.gd")
const REF: Dictionary = {
	"Particles.Age": "p.age", "Particles.Lifetime": "p.lifetime", "Particles.NormalizedAge": "nb_age(p)",
	"Particles.Position": "p.position", "StackContext.Position": "p.position", "Engine.Owner.Scale": "nb_owner_scale(p,spawning)", "Particles.Velocity": "p.velocity", "Particles.Previous.Velocity": "p.previous_velocity",
	"Particles.SpriteAlignment": "p.alignment", "Particles.SpriteSize": "p.size", "Particles.Initial.SpriteSize": "p.initial_size",
	"Particles.Color": "p.color", "Particles.Initial.Color": "p.initial_color", "Particles.Mass": "p.mass",
	"Particles.SubImageIndex": "p.subimage", "Particles.MaterialRandom": "p.material_random", "Particles.UVScale": "vec2(p.uv_flip,p.uv_flip_y)",
	"Particles.Scale":"p.mesh_scale","Particles.Initial.Scale":"p.initial_mesh_scale","Particles.MeshOrientation":"p.mesh_orientation","Particles.Initial.MeshOrientation":"p.initial_mesh_orientation","Particles.MeshIndex":"p.mesh_index","Transient.MeshScaleFactor":"p.mesh_scale_factor",
	"Particles.InitialRotation":"p.initial_rotation","Particles.Initial.Position":"p.initial_position","Transient.FirstFrame":"p.first_frame","Engine.Emitter.SimulationPosition":"(params.data[7].x>0.5?vec3(0):nb_origin(p,spawning))",
	"Transient.PhysicsForce": "p.force", "Transient.PhysicsDrag": "p.drag", "Transient.DragIgnoreMass": "p.ignore_mass",
	"Transient.AdvectionVelocity": "p.advection", "Transient.SpriteSizeScaleFactor": "p.size_factor",
	"Particles.Presolve.Position": "p.presolve_position", "Particles.Presolve.Velocity": "p.presolve_velocity", "Particles.Presolve.PhysicsForce": "p.presolve_force",
	"Engine.DeltaTime": "dt"
}
## Native per-compilation builder; shader templates and schemas remain readable data.
const CONTRACTS = {
	"kernel":Kernel.SOURCE,"stride":Kernel.STRIDE_VEC4,"cache_slots":Kernel.CACHE_SLOTS,
	"builtins":Attributes.BUILTINS,"module_schemas":Attributes.ModuleOutputs.SCHEMAS,
	"dynamic_specs":DynamicMath.SPECS,"lifecycle_fields":Lifecycle.FIELDS,"references":REF,
	"curl_fields":CurlNoise.FIELDS,"vector_sample":VectorFieldGPU.SAMPLE_GLSL,
	"torus_source":preload("res://addons/niagara_bridge/runtime/torus_geometry.gd").GPU_SOURCE,
}

static func build(plan: Dictionary,user_parameters: Dictionary={}) -> Dictionary:
	return NBNativeGPUProgram.build(plan,user_parameters,CONTRACTS)
