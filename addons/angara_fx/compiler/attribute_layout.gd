@tool
extends NBNativeAttributeValues
const DynamicMath = preload("res://addons/angara_fx/runtime/dynamic_math.gd")
## Per-plan typed storage. Arbitrary names never become shader identifiers.
const Values=preload("res://addons/angara_fx/runtime/value_evaluator.gd")
const ModuleOutputs=preload("res://addons/angara_fx/compiler/module_outputs.gd")
const BUILTINS: Dictionary={
	"Particles.Position":["position","position","p.position"],"Particles.Velocity":["vec3","velocity","p.velocity"],
	"Particles.Age":["float","age","p.age"],"Particles.NormalizedAge":["float","normalized_age","p.normalized_age"],"Particles.Lifetime":["float","lifetime","p.lifetime"],
	"Particles.Alive":["bool","alive","p.alive"],"Particles.Color":["color","color","p.color"],"Particles.Mass":["float","mass","p.mass"],
	"Particles.SpriteSize":["vec2","size","p.size"],"Particles.SpriteAlignment":["vec3","alignment","p.alignment"],"Particles.SpriteRotation":["float","rotation","p.rotation"],
	"Particles.UVScale":["vec2","",""], "Particles.SpawnGroup":["int","spawn_group","p.spawn_group"],
	"Particles.Scale":["vec3","mesh_scale","p.mesh_scale"],"Particles.MeshOrientation":["quat","mesh_orientation","p.mesh_orientation"],"Particles.MeshIndex":["int","mesh_index","p.mesh_index"],
	"Particles.SubImageIndex":["float","subimage_index","p.subimage"],"Particles.MaterialRandom":["float","material_random","p.material_random"],"Particles.CameraOffset":["float","camera_offset","p.camera_offset"],
	"Particles.DynamicMaterialParameter":["vec4","dynamic","p.dynamic_value"],
	"Particles.DynamicMaterialParameter1":["vec4","dynamic1","p.dynamic_value1"],
	"Particles.DynamicMaterialParameter2":["vec4","dynamic2","p.dynamic_value2"],
	"Particles.DynamicMaterialParameter3":["vec4","dynamic3","p.dynamic_value3"],"Particles.RibbonWidth":["float","ribbon_width",""],
	"Particles.RibbonFacing":["vec3","ribbon_facing",""]}

static func set_particle(particle: Dictionary,name: String,type: String,value: Variant) -> void:
	NBNativeAttributeValues.write_particle(particle,name,type,value,BUILTINS)

static func value_type(value: Variant,known: Dictionary) -> String:
	return NBNativeCompilerAttributes.value_type(value,known,BUILTINS,DynamicMath.SPECS)

static func assignment_error(value: Variant,type: String,known: Dictionary) -> String:
	return NBNativeCompilerAttributes.assignment_error(value,type,known,BUILTINS,DynamicMath.SPECS)

static func references(value: Variant) -> Array[String]:
	return NBNativeCompilerAttributes.references(value)

static func build(emitter: Resource,native_particles: bool=false) -> Dictionary:
	return NBNativeCompilerAttributes.build(emitter,native_particles,BUILTINS,ModuleOutputs.SCHEMAS)
