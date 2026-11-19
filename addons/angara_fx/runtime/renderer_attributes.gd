@tool
extends RefCounted
## Per-renderer draw views. Source simulation fields/attributes stay untouched.
const Attributes=preload("../compiler/attribute_layout.gd")
const Filter=preload("renderer_filter.gd")
const Values=preload("value_evaluator.gd")
const FIELDS={
	"Position":["Particles.Position","position","PositionBinding"],
	"Velocity":["Particles.Velocity","vec3","VelocityBinding"],
	"Color":["Particles.Color","color","ColorBinding"],
	"MaterialRandom":["Particles.MaterialRandom","float","MaterialRandomBinding"],
	"NormalizedAge":["Particles.NormalizedAge","float","NormalizedAgeBinding"],
	"SubImageIndex":["Particles.SubImageIndex","float","SubImageIndexBinding"],
	"SpriteSize":["Particles.SpriteSize","vec2","SpriteSizeBinding"],
	"SpriteRotation":["Particles.SpriteRotation","float","SpriteRotationBinding"],
	"UVScale":["Particles.UVScale","vec2","UVScaleBinding"],
	"PivotOffset":["Particles.PivotOffset","vec2","PivotOffsetBinding"],
	"SpriteFacing":["Particles.SpriteFacing","vec3","SpriteFacingBinding"],
	"SpriteAlignment":["Particles.SpriteAlignment","vec3","SpriteAlignmentBinding"],
	"CameraOffset":["Particles.CameraOffset","float","CameraOffsetBinding"],
	"DynamicMaterial":["Particles.DynamicMaterialParameter","vec4","DynamicMaterialBinding"],
	"DynamicMaterial1":["Particles.DynamicMaterialParameter1","vec4","DynamicMaterial1Binding"],
	"DynamicMaterial2":["Particles.DynamicMaterialParameter2","vec4","DynamicMaterial2Binding"],
	"DynamicMaterial3":["Particles.DynamicMaterialParameter3","vec4","DynamicMaterial3Binding"],
	"Scale":["Particles.Scale","vec3","ScaleBinding"],
	"MeshOrientation":["Particles.MeshOrientation","quat","MeshOrientationBinding"],
	"MeshIndex":["Particles.MeshIndex","int","MeshIndexBinding"]}
const SPRITE_ONLY=["SpriteSize","SpriteRotation","UVScale","PivotOffset","SpriteFacing","SpriteAlignment"]
const MESH_ONLY=["Scale","MeshOrientation","MeshIndex"]

static func configuration(renderer: Resource,types: Dictionary) -> Dictionary:
	return NBNativeRendererViews.configuration(renderer,types,Attributes.BUILTINS,FIELDS)

static func fallback(key: String,position: Vector3) -> Variant:
	return NBNativeRendererViews.fallback(key,position)

static func read_particle(particle: Dictionary,name: String) -> Variant:
	return NBNativeRendererViews.read_particle(particle,name,Attributes.BUILTINS)

static func valid(value: Variant,type: String) -> bool:
	return NBNativeRendererViews.valid(value,type)

static func project(config: Dictionary,particles: Array,parameters: Dictionary,default_position: Vector3) -> Dictionary:
	return NBNativeRendererViews.project(config,particles,parameters,default_position,Attributes.BUILTINS)

static func emitter_particle(position: Vector3) -> Dictionary:
	return NBNativeRendererViews.emitter_particle(position)
