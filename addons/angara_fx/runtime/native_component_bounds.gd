@tool
extends NBNativeComponentBounds
## UE5.8 component bounds, independently of submitted/render-filtered geometry.
## configure() consumes verified source evidence; it never supplies missing policy.
## Boxes use float64 scalar arrays in UE centimetres until the final Godot AABB.
const POLICY_PRODUCER := "NiagaraBridgeSourceLibrary.DescribeRendererOrder bounds policy"
const CVAR_PREFIX := "fx.Niagara.EmitterBounds."
const SPRITE_CLASS := "NiagaraSpriteRendererProperties"
const UE_TO_GODOT := Basis(Vector3(0, 0, -1), Vector3(1, 0, 0), Vector3(0, 1, 0))
