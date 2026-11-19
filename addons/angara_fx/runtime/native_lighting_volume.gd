@tool
extends NBNativeLightingVolume
## Native configuration, cascade math, packing and GPU injection/filter lifecycle.
const GPU=preload("refraction_pass.gd")
const PREFIX="r.TranslucencyLightingVolume"
const SHADER_ROOT="res://addons/angara_fx/materials/"
