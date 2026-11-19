@tool
extends NBNativeMeshFacing
## NiagaraMeshParticleUtils.ush current-frame facing, evaluated in UE axes.
## Direction transforms remove owner scale; positions retain the full transform.
const Values=preload("value_evaluator.gd")
const AXES=Basis(Vector3(0,0,-1),Vector3(1,0,0),Vector3(0,1,0))
