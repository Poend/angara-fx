@tool
extends NBNativeSpriteFacing
## Source-space NiagaraSpriteVertexFactory tangents, including their intentional
## non-unit lengths during distance blending. Godot quad Y reverses source UV Y.
const Values=preload("value_evaluator.gd")
const Settings=preload("ribbon_geometry.gd")
const Filter=preload("renderer_filter.gd")
const CONVERSION=Basis(Vector3(0,0,-1),Vector3(1,0,0),Vector3(0,1,0))
const FACING=["FaceCamera","FaceCameraPlane","FaceCameraPosition","FaceCameraDistanceBlend","CustomFacingVector"]
const ALIGNMENT=["Unaligned","CustomAlignment","VelocityAligned"]

