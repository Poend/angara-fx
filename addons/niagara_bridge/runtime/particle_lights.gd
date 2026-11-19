@tool
extends NBNativeParticleLights
## Niagara simple-light records. No Godot light-energy or radius approximation.
## Positions in records use source world centimetres; owner transforms use Godot metres.
const Values=preload("value_evaluator.gd")
const BINDINGS={
	"position":["PositionBinding","position"],"color":["ColorBinding","color"],
	"radius":["RadiusBinding","float"],"scattering":["VolumetricScatteringBinding","float"],
	"enabled":["LightRenderingEnabledBinding","bool"],"visibility":["RendererVisibilityTagBinding","int"],
	"exponent":["LightExponentBinding","float"],"specular":["SpecularScaleBinding","float"],
	"diffuse":["DiffuseScaleBinding","float"]}

