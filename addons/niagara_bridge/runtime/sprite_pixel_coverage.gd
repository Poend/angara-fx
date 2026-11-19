@tool
extends RefCounted
const Values=preload("value_evaluator.gd")
const Program=preload("../materials/conversion/material_program.gd")
static func configuration(renderer: Resource,source_order: Dictionary) -> Dictionary:return NBNativeRenderPolicy.coverage_configuration(renderer,source_order,Program)
static func sample(config: Dictionary,size_cm: Vector2,world_position: Vector3,camera: Camera3D) -> Dictionary:return NBNativeRendererGeometry.coverage(config,size_cm,world_position,camera)
static func _valid_policy(policy: Variant) -> bool:return NBNativeRenderPolicy.valid_coverage_policy(policy)
