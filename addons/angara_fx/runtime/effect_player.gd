@tool
class_name AngaraEffect3D
extends Node3D
const CurlNoise = preload("res://addons/angara_fx/runtime/curl_noise.gd")
const CPUMath = preload("res://addons/angara_fx/runtime/niagara_cpu_math.gd")
const MeshOrientation = preload("res://addons/angara_fx/runtime/mesh_orientation.gd")
const MeshFacing = preload("res://addons/angara_fx/runtime/mesh_facing.gd")
const NativeSystem = preload("res://addons/angara_fx/runtime/native_system_vm.gd")
## Shared preview/game CPU and native compute player. Effects are ordinary Resources.
## Simulation is per-instance; the effect resource is never mutated by playback.
const Compiler = preload("res://addons/angara_fx/compiler/effect_compiler.gd")
const RendererFilter=preload("renderer_filter.gd")
const RendererAttributes=preload("renderer_attributes.gd")
const RuntimeOperation=preload("runtime_operation.gd")
const PARTICLE_ATTRIBUTE_STRIDE: int=5 # SubUV, dynamic1..3, relative time.
const Values = preload("res://addons/angara_fx/runtime/value_evaluator.gd")
const GPUBackend = preload("res://addons/angara_fx/runtime/gpu_backend.gd")
const Ribbons = preload("res://addons/angara_fx/runtime/ribbon_geometry.gd")
const VertexColors = preload("res://addons/angara_fx/runtime/vertex_color_texture.gd")
const SourceRNG = preload("res://addons/angara_fx/runtime/source_rng.gd")
const SourceClock = preload("res://addons/angara_fx/runtime/source_clock.gd")
const Lifecycle = preload("res://addons/angara_fx/runtime/lifecycle.gd")
const LifecycleModule = preload("res://addons/angara_fx/document/nb_module.gd")
const LinearColor = preload("res://addons/angara_fx/runtime/linear_color.gd")
const Attributes = preload("res://addons/angara_fx/compiler/attribute_layout.gd")
const CompositeSprites = preload("res://addons/angara_fx/runtime/composite_sprite_renderer.gd")
const ViewExposure = preload("res://addons/angara_fx/runtime/material_view_exposure.gd")
const SystemMaterialComposer = preload("res://addons/angara_fx/materials/conversion/system_material_composer.gd")
const MaterialProgram = preload("res://addons/angara_fx/materials/conversion/material_program.gd")
const NativeBounds = preload("res://addons/angara_fx/runtime/native_component_bounds.gd")
const AttractionForces = preload("res://addons/angara_fx/runtime/attraction_forces.gd")
const PixelCoverage = preload("res://addons/angara_fx/runtime/sprite_pixel_coverage.gd")
const RefractionPass = preload("res://addons/angara_fx/runtime/refraction_pass.gd")
const NativeSurfacePass = preload("res://addons/angara_fx/runtime/native_surface_pass.gd")
const NativeParticles = preload("native_vm_particles.gd")
const NativeIDs = preload("native_vm_ids.gd")
const NativeEvents = preload("native_vm_events.gd")
const ParticleReaders = preload("native_vm_particle_readers.gd")
const SpriteFacing = preload("sprite_facing.gd")
const ParticleLights = preload("particle_lights.gd")
const NativeParameters = preload("native_vm_parameters.gd")
const SavedEffects = preload("saved_effects.gd")

var _saved_path := ""
var _applying_save := false
var _authoring_preview := false
var _saved_revision: NBNativeEffectRevision

@export var effect: Resource:
	set(value):
		if not _applying_save:
			SavedEffects.unwatch(self, _saved_path)
			_saved_path = SavedEffects.document_path(value)
		effect = value
		if is_inside_tree() and not _applying_save:
			restart()
@export var autoplay: bool = true
@export var seed: int = 1
@export_range(0.001, 0.1, 0.001) var fixed_step: float = 0.0166666666667
## Player budget, also bounded by the Niagara emitter's exported maximum. Buffers grow on demand.
@export_range(0, 100000, 1, "or_greater") var max_particles_per_emitter: int = 100000
@export var playing: bool = true
## Instance-only parameter values, keyed by their full Niagara name (User.Name).
@export var user_parameters: Dictionary = {}:
	set(values):
		# Exported scenes and game code may supply the same Dictionary to several players.
		user_parameters = values.duplicate(true)
## Scene scalability inputs for original System/Emitter programs.
@export var source_lod_distance_fraction: float = 0.0
@export var source_spawn_count_scale: float = 1.0
## Niagara's global quality level. Unreal's default scalability level is 3 (Epic).
@export_range(0, 31, 1) var source_quality_level: int = 3
## Explicit opt-in; keeps source GPU requirements and emits a fallback diagnostic.
@export var allow_gpu_cpu_fallback: bool = false
## Explicit source comparison mode. Missing/stale/partial compiled contracts block.
## A transient deterministic capture contract does not change authored source flags.
@export var use_compiled_source_rng: bool = false
## Source trace channels map to this world's collider layers. Complex traces
## require explicit complex_channel_masks. Ordinary Godot bodies block all source
## channels; niagara_trace_channels metadata preserves imported response masks.
@export var collision_settings: Dictionary = {}
## A verified native order and compatible sealed material programs are required.
@export var use_scoped_sprite_rendering: bool = true

var simulation_time: float = 0.0
var diagnostics: Array[Dictionary] = []
var _emitters: Array[Dictionary] = []
var _accumulator: float = 0.0
var _tick: int = 0
var _native_system: RefCounted
var _native_debug_boxes: MeshInstance3D
var _collision_world: RefCounted
var _collision_initialization_errors: Array[String]=[]
var _uses_collision: bool = false
var _native_system_seed: int=0
var _capacity_reported: bool = false
var _source_transform: Transform3D = Transform3D.IDENTITY
var _step_source_start: Transform3D = Transform3D.IDENTITY
var _step_source_end: Transform3D = Transform3D.IDENTITY
var _run_step: float = 1.0 / 60.0
var _instance_parameters: Dictionary = {}
var _source_clock: RefCounted
var _source_variable_tick: bool=false
var _simulation_started: bool = false
var _composite: RefCounted
var _composite_contexts: Array[Dictionary] = []
var _scoped_signature: int = 0
var _scoped_status: Dictionary = {}
var _render_filter_enabled: bool = false
var _render_emitter_filter: PackedStringArray = []
var _native_bounds: RefCounted
var _native_bounds_signature: int = 0
var _native_bounds_result: Dictionary = {}
var _native_bounds_omitted: Array[String] = []
var _system_lifecycle: Dictionary = {}
var _system_state: Dictionary = {}
var _system_updated: bool = false
var _system_update_on_spawn: int = 1
var _lifecycle_delta: float = 0.0
var _owner_execution_state: int = Lifecycle.ACTIVE
var _lifecycle_failed: bool = false
var _refraction_pass: WeakRef
var _native_surface_pass: WeakRef
var _native_surface_status: Dictionary = {"active":false}
var _temporal_view_reported: bool = false
var _native_events: NBNativeVMEvents=NBNativeVMEvents.new()
var _event_execution_order: Array=[]
var _particle_readers: NBNativeVMReaders=NBNativeVMReaders.new()
var _particle_light_pass: WeakRef
var _particle_light_status: Dictionary={"active":false,"records":0}
var _particle_light_error_key: int=0
var _context_batch_active: bool=false
var _context_epoch: int=0

# Only established intrinsic writes are specialized. Unknown/new modules retain
# the complete context refresh, including arbitrary typed attributes.
const OPERATION_WRITES={
	"ParticleState@1.0":["Particles.Age","Particles.NormalizedAge","Particles.Alive","Particles.Lifetime"],
	"ParticleState@1.1":["Particles.Age","Particles.NormalizedAge","Particles.Alive","Particles.Lifetime"],
	"Color":["Particles.Color"],"ScaleColor@1.0":["Particles.Color"],
	"GravityForce":["Transient.PhysicsForce"],"VortexForce":["Transient.PhysicsForce"],"VortexForce@1.1":["Transient.PhysicsForce"],
	"WindForce@1.0":["Transient.PhysicsForce"],"KillParticlesInVolume":["Particles.Alive"],
	"BoxLocation":["Particles.Position","StackContext.Position"],
	"V2/CurlNoiseForce@1.0":["Transient.PhysicsForce"],"V2/CurlNoiseForce@1.1":["Transient.PhysicsForce"],"V2/CurlNoiseForce@1.2":["Transient.PhysicsForce"],
	"LineAttractionForce":["Transient.PhysicsForce"],"LineAttractionForce@1.0":["Transient.PhysicsForce"],
	"AddVelocity":["Particles.Velocity"],"AddVelocity@1.0":["Particles.Velocity"],"AddVelocityFromPoint":["Particles.Velocity"],
	"Drag":["Transient.PhysicsDrag","Transient.PhysicsRotationalDrag","Transient.DragIgnoreMass"],
	"Drag@1.0":["Transient.PhysicsDrag","Transient.PhysicsRotationalDrag","Transient.DragIgnoreMass"],
	"Drag@1.1":["Transient.PhysicsDrag","Transient.PhysicsRotationalDrag","Transient.DragIgnoreMass"],
	"ScaleSpriteSize":["Particles.SpriteSize","Transient.SpriteSizeScaleFactor"],"ScaleSpriteSize@1.0":["Particles.SpriteSize","Transient.SpriteSizeScaleFactor"],
	"ScaleSpriteSizeBySpeed":["Particles.SpriteSize","Transient.SpriteSizeScaleFactor"],"ScaleMeshSize":["Particles.Scale","Transient.MeshScaleFactor"],
	"CameraOffset":["Particles.CameraOffset"],"KillParticles":["Particles.Alive"],"SpriteRotationRate":["Particles.SpriteRotation"],
	"V2/SubUVAnimation@1.0":["Particles.SubImageIndex"],
	"DynamicMaterialParameters@1.0":["Particles.DynamicMaterialParameter","Particles.DynamicMaterialParameter1","Particles.DynamicMaterialParameter2","Particles.DynamicMaterialParameter3"],
	"DynamicMaterialParameters@1.1":["Particles.DynamicMaterialParameter","Particles.DynamicMaterialParameter1","Particles.DynamicMaterialParameter2","Particles.DynamicMaterialParameter3"],
	"SolveForcesAndVelocity":["Particles.Position","StackContext.Position","Particles.Velocity","Particles.Previous.Velocity","Particles.Presolve.Position","Particles.Presolve.Velocity","Particles.Presolve.PhysicsForce","Transient.PhysicsForce","Transient.PhysicsDrag","Transient.AdvectionVelocity"]}
const EXTRA_CONTEXT_FIELDS={"StackContext.Position":"position","Particles.Previous.Velocity":"previous_velocity","Transient.PhysicsForce":"force","Transient.PhysicsDrag":"drag","Transient.PhysicsRotationalDrag":"rotational_drag","Transient.DragIgnoreMass":"drag_ignore_mass","Transient.AdvectionVelocity":"advection_velocity","Transient.SpriteSizeScaleFactor":"size_scale_factor","Transient.MeshScaleFactor":"mesh_scale_factor","Particles.Presolve.Position":"presolve_position","Particles.Presolve.Velocity":"presolve_velocity","Particles.Presolve.PhysicsForce":"presolve_force","Particles.Initial.Color":"initial_color","Particles.Initial.SpriteSize":"initial_size","Particles.Initial.Scale":"initial_mesh_scale","Particles.Initial.MeshOrientation":"initial_mesh_orientation","Particles.InitialRotation":"initial_rotation","Particles.Initial.Position":"initial_position","Transient.FirstFrame":"first_frame"}

func _ready() -> void:
	restart()

func _process(delta: float) -> void:
	if _uses_collision and autoplay and playing: return
	if autoplay and playing:
		advance(delta)
	else:
		_sync_renderers()

func _physics_process(delta: float) -> void:
	if _uses_collision and autoplay and playing: advance(delta)

func restart() -> void:
	_saved_revision = null
	if not _authoring_preview and not Engine.is_editor_hint() and not _saved_path.is_empty():
		_reset_saved_revision()
		if is_inside_tree():
			var latest := SavedEffects.watch(self, _saved_path)
			if latest != null and latest != effect and _apply_saved_effect(latest, _saved_path):
				return
	NBNativeEffectController.restart(self,Compiler,LifecycleModule)

func _reset_saved_revision() -> void:
	var stock_player: bool = get_script().resource_path == "res://addons/angara_fx/runtime/effect_player.gd"
	_saved_revision = NBNativeEffectRevision.new() if stock_player and SavedEffects.has_static_renderer_layout(effect) else null

func _apply_saved_effect(candidate: Resource, path: String) -> bool:
	if path != _saved_path:
		return false
	var plan := Compiler.compile_effect(candidate, user_parameters, {
		"allow_gpu_cpu_fallback": allow_gpu_cpu_fallback, "use_compiled_source_rng": use_compiled_source_rng,
		"enabled_emitters_only": true})
	var problem := SavedEffects.first_error(plan)
	if not problem.is_empty():
		push_warning("Angara FX kept the running effect for %s: %s" % [path, problem])
		return false
	_applying_save = true
	effect = candidate
	_applying_save = false
	_reset_saved_revision()
	NBNativeEffectController.restart(self, Compiler, LifecycleModule, plan)
	return true

func _exit_tree() -> void:
	SavedEffects.unwatch(self, _saved_path)
	NBNativeEffectController.release(self)
	if not _saved_path.is_empty() and not _authoring_preview and not Engine.is_editor_hint():
		request_ready()

func get_backend_statistics() -> Array[Dictionary]:
	var statistics: Array[Dictionary] = []
	for emitter: Dictionary in _emitters:
		var entry: Dictionary = {"emitter": emitter.plan.source.display_name, "backend": emitter.plan.backend, "particles": NBNativeParticleView.particle_count(emitter),"rng_backend":emitter.plan.rng_backend}
		entry["system_backend"]="ue_native_vm" if _native_system!=null else "shared_modules"
		if emitter.has("native_vm"):
			entry["execution_language"]="C++"
			entry["native_vm_statistics"]={}
			for stage: String in emitter.native_vm:
				entry.native_vm_statistics[stage]=emitter.native_vm[stage].executor.get_statistics()
		if emitter.plan.has("source_rng"): entry.merge({"rng_seed_mode":emitter.plan.source_rng.seed_mode,"rng_emitter_seed":emitter.plan.source_rng.seed})
		if emitter.has("gpu"):
			entry.merge({"dispatches": emitter.gpu.dispatch_count, "last_dispatch_and_readback_usec": emitter.gpu.last_dispatch_usec, "bytes_transferred": emitter.gpu.bytes_transferred})
			entry["source_gpu_dispatch_counter"]=emitter.gpu.last_source_dispatch_counter
		if emitter.has("source_instance_seed"):entry["source_instance_seed"]=emitter.source_instance_seed
		statistics.append(entry)
	return statistics

## Render-only filtering never changes simulation or source cached rank.
## An enabled empty list hides every emitter, including a composite system.
func set_render_emitter_filter(names: PackedStringArray, enabled: bool = true) -> void:
	_render_emitter_filter = names.duplicate()
	_render_filter_enabled = enabled
	if is_inside_tree(): _sync_renderers()

func get_render_statistics() -> Dictionary:
	var result: Dictionary = _scoped_status.duplicate(true)
	result["native_surface"] = _native_surface_status.duplicate(true)
	result["particle_lights"]=_particle_light_status.duplicate(true)
	result["render_filter_enabled"] = _render_filter_enabled
	result["render_emitters"] = _render_emitter_filter
	result["native_bounds"] = _native_bounds_result.duplicate(true)
	result["sprite_pixel_coverage"] = []
	for emitter: Dictionary in _emitters:
		for renderer: Dictionary in emitter.renderers:
			if renderer.source.renderer_type == "SpriteRenderer":
				result.sprite_pixel_coverage.append({"emitter":emitter.plan.source.display_name,"renderer":renderer.source.stable_id,"policy":renderer.get("pixel_coverage", {})})
	if _composite != null: result["geometry"] = _composite.statistics.duplicate(true)
	return result

func _render_emitter_visible(emitter: Dictionary) -> bool:
	return bool(emitter.plan.source.enabled) and (not _render_filter_enabled or str(emitter.plan.source.display_name) in _render_emitter_filter)

func set_paused(paused: bool) -> void:
	playing = not paused

## Owner deactivation follows SystemState's response on the next simulation tick.
func deactivate(immediate: bool = false) -> void:
	_owner_execution_state = Lifecycle.COMPLETE if immediate else Lifecycle.INACTIVE

func get_lifecycle_snapshot() -> Dictionary:
	var emitters: Dictionary = {}
	for emitter: Dictionary in _emitters:
		emitters[emitter.plan.source.display_name] = emitter.lifecycle.duplicate(true)
	return {"system": _system_state.duplicate(true), "emitters": emitters, "failed": _lifecycle_failed}

func get_clock_statistics() -> Dictionary:
	return {"source_fixed_tick": _source_clock != null, "source_variable_tick":_source_variable_tick, "simulation_step": _run_step, "simulation_age": simulation_time, "simulation_ticks": _tick, "engine_age": _source_clock.engine_age if _source_clock != null else simulation_time + _accumulator, "max_substeps": _source_clock.max_substeps if _source_clock != null else (1 if _source_variable_tick else 3600), "dropped_substeps": _source_clock.dropped_substeps if _source_clock != null else 0}

func set_user_parameter(parameter_name: String, value: Variant, restart_simulation: bool = true) -> void:
	var qualified_name: String = parameter_name if parameter_name.begins_with("User.") else "User." + parameter_name
	user_parameters[qualified_name] = value
	if restart_simulation and is_inside_tree():
		restart()

## Set a declared parameter on this instance. The next simulation step sees the
## value and existing particles keep their age and stage history.
func set_user_parameter_live(parameter_name: String, value: Variant) -> Error:
	var declaration := NBNativeUserParameters.declaration(effect, parameter_name)
	if declaration == null:
		return ERR_DOES_NOT_EXIST
	var typed: Variant = NBNativeUserParameters.typed_value(declaration.target_type, value)
	if typed == null:
		return ERR_INVALID_PARAMETER
	var key := parameter_name if parameter_name.begins_with("User.") else "User." + parameter_name
	user_parameters[key] = typed
	return OK

## Remove this instance's override so subsequent steps use the saved default.
func reset_user_parameter(parameter_name: String) -> bool:
	var key := parameter_name if parameter_name.begins_with("User.") else "User." + parameter_name
	return user_parameters.erase(key)

func get_user_parameter(parameter_name: String) -> Variant:
	var key := parameter_name if parameter_name.begins_with("User.") else "User." + parameter_name
	var declaration := NBNativeUserParameters.declaration(effect, key)
	var value: Variant
	if user_parameters.has(key):
		value = user_parameters[key]
	elif declaration != null and declaration.has_default:
		value = declaration.value
	if declaration != null:
		value = NBNativeCurves.prepare(value, declaration.target_type)
	return value.duplicate(true) if value is Array or value is Dictionary else value

## Explicit advancement is allowed while paused (timeline single-step).
func advance(seconds: float) -> void:
	NBNativeEffectController.advance(self,seconds,Compiler)

func seek(time: float) -> void:
	NBNativeEffectController.seek(self,time)

func get_particle_count() -> int:
	var total: int = 0
	for emitter: Dictionary in _emitters:
		total += NBNativeParticleView.particle_count(emitter)
	return total

func get_emitter_particle_counts() -> Dictionary:
	var counts: Dictionary = {}
	for emitter: Dictionary in _emitters:
		counts[emitter.plan.source.display_name] = NBNativeParticleView.particle_count(emitter)
	return counts

## Read-only numeric snapshot for the editor and comparison tools.
func get_particle_snapshot() -> Array[Dictionary]:
	var snapshot: Array[Dictionary] = []
	var camera: Camera3D=get_viewport().get_camera_3d() if is_inside_tree() else null
	var stock: bool = get_script().resource_path == "res://addons/angara_fx/runtime/effect_player.gd"
	for state: Dictionary in _emitters:
		# Project a numerical buffer for this reader without changing how the
		# next game tick consumes it. Particle arrays are assigned by materialize.
		var emitter: Dictionary = state.duplicate() if stock and state.get("particle_projection_pending", false) else state
		var renderer_views: Dictionary={}
		NBNativeParticleView.materialize(emitter)
		for draw: Dictionary in emitter.renderers:
			if not renderer_views.has(draw.source):renderer_views[draw.source]=_renderer_particle_view(emitter,draw.source)
		for particle_index: int in NBNativeParticleView.particle_count(emitter):
			var particle: Dictionary=emitter.particles[particle_index]
			var world_position: Vector3 = Values.unreal_to_godot(particle.position)
			var world_velocity: Vector3 = Values.unreal_to_godot(particle.velocity)
			if emitter.plan.local_space:
				world_position = global_transform * world_position
				world_velocity = global_basis * world_velocity
			var entry: Dictionary = {"emitter": emitter.plan.source.display_name, "id": particle.id, "birth_time": particle.birth_time, "age": particle.age, "normalized_age": particle.normalized_age, "lifetime": particle.lifetime, "world_position": world_position, "world_velocity": world_velocity, "size_cm": particle.size, "color": particle.color, "dynamic": particle.dynamic, "subimage_index": particle.subimage_index, "material_random": particle.material_random, "uv_scale": Vector2(particle.uv_flip, particle.uv_flip_y)}
			entry.merge({"mesh_scale":particle.mesh_scale,"mesh_orientation_ue":particle.mesh_orientation,"initial_mesh_scale":particle.initial_mesh_scale,"initial_rotation_ue":particle.initial_rotation,"mesh_index":particle.mesh_index})
			entry["attributes"]=particle.get("attributes",{}).duplicate(true)
			entry["camera_offset_cm"]=particle.get("camera_offset",0.0)
			for renderer: Dictionary in emitter.renderers:
				if not renderer_views[renderer.source].all_visible and not renderer_views[renderer.source].visible_indices.has(particle_index):continue
				var rendered: Dictionary=renderer_views[renderer.source].get("projected",{}).get(particle_index,particle)
				var camera_offset: float=_renderer_camera_offset(renderer.source,rendered,emitter)
				if renderer.source.renderer_type=="SpriteRenderer":
					var space: Transform3D=global_transform if emitter.plan.local_space else Transform3D.IDENTITY
					var coverage: Dictionary = _sprite_coverage(renderer,rendered,camera,space)
					var sprite_transform: Transform3D=_sprite_particle_transform(renderer,rendered,camera,space,camera_offset,coverage.size_ratio)
					var sprite_bounds: AABB=sprite_transform*renderer.mesh_aabb
					entry["world_bounds"]=entry.world_bounds.merge(sprite_bounds) if entry.has("world_bounds") else sprite_bounds
				if renderer.source.renderer_type == "RibbonRenderer":
					var radius: Vector3 = Vector3.ONE * absf(float(particle.get("ribbon_width", 1.0))) * 0.005
					var ribbon_bounds: AABB = AABB(world_position - radius, radius * 2.0)
					entry["world_bounds"] = entry.world_bounds.merge(ribbon_bounds) if entry.has("world_bounds") else ribbon_bounds
				if renderer.source.renderer_type == "MeshRenderer" and _mesh_particle_selected(renderer,rendered):
					var mesh_bounds: AABB = _mesh_particle_transform(renderer.mesh_binding.entry, rendered, emitter.plan.local_space,camera,camera_offset,renderer.mesh_binding.facing) * renderer.mesh_aabb
					entry["world_bounds"] = entry.world_bounds.merge(mesh_bounds) if entry.has("world_bounds") else mesh_bounds
			snapshot.append(entry)
	return snapshot

func _step(delta: float) -> void:
	NBNativeEffectController.step(self,delta)

func _collision_failure(errors: Array) -> void:
	NBNativeEffectController.collision_failure(self,errors)

func get_collision_statistics() -> Dictionary:
	return _collision_world.get_statistics() if _collision_world!=null else {}

func _bind_native_world(executor: RefCounted, scope: String) -> Array[String]:
	return NBNativeEffectController.bind_world(self,executor,scope)

func _simulate_step(delta: float) -> void:
	NBNativeEffectController.simulate(self,delta)

func _update_ribbon_history(emitter: Dictionary) -> void:
	NBNativeEffectController.ribbon_history(emitter)

func _native_vm_frame(emitter: Dictionary, delta: float, time: float) -> Dictionary:
	return NBNativeEffectController.frame(self,emitter,delta,time)

func _run_native_system(stage: String, delta: float) -> bool:
	return NBNativeEffectController.run_system(self,stage,delta)

func _native_vm_failure(emitter: Dictionary, errors: Array) -> void:
	NBNativeEffectController.vm_failure(self,emitter,errors)

func _publish_native_events(emitter: Dictionary, events: Dictionary) -> bool:
	return NBNativeEffectController.publish_events(self,emitter,events)

func _run_native_vm(emitter: Dictionary, delta: float, time: float) -> void:
	NBNativeEffectController.run_vm(self,emitter,delta,time)

func _run_gpu(emitter: Dictionary, delta: float, time: float) -> void:
	NBNativeEffectController.run_gpu(self,emitter,delta,time)

func _curve_parameter_values(plan: Dictionary) -> Dictionary:
	return NBNativeEffectController.curve_parameters(self,plan)

func _collect_curve_parameters(value: Variant, result: Dictionary) -> void:
	NBNativeEffectController.collect_curve_parameters(self,value,result)

func _refresh_curve_parameters() -> void:
	NBNativeEffectController.refresh_curves(self,Compiler.GPUProgram)

func _schedule_spawn(emitter: Dictionary, delta: float, include_initial: bool = false) -> void:
	NBNativeEffectController.schedule_spawn(self,emitter,delta,include_initial)

func _burst_10_event(operation: Resource,age: float,delta: float) -> Dictionary:
	return NBNativeSpawnScheduler.burst_10(operation,age,delta)

func _schedule_source_spawn(emitter: Dictionary, delta: float) -> void:
	NBNativeEffectController.schedule_source(self,emitter,delta)

func _spawn_events(emitter: Dictionary, events: Array, delta: float) -> void:
	NBNativeEffectController.spawn_events(self,emitter,events,delta)

func _particle_schema() -> Dictionary:
	return {"builtins":Attributes.BUILTINS,"extra":EXTRA_CONTEXT_FIELDS,"writes":OPERATION_WRITES}

func _spawn_particle(emitter: Dictionary, birth_time: float, birth_transform: Transform3D, execution_index: int=0, execution_count: int=1) -> void:
	NBNativeParticleModules.spawn(emitter,birth_time,birth_transform,execution_index,execution_count,_context_frame(),_particle_schema())

func _update_curated(emitter: Dictionary, delta: float) -> void:
	NBNativeParticleModules.update(emitter,delta,simulation_time+delta,_context_frame(),_particle_schema())

func _context_frame(spawning: bool=false, spawn_execution: bool=false) -> Dictionary:
	return {"system":_system_state,"parameters":_instance_parameters,"tick":_tick,"batch":_context_batch_active,"epoch":_context_epoch,"simulation_time":simulation_time,"source_transform":_step_source_end,"spawning":spawning,"spawn_execution":spawn_execution}

func _context(particle: Dictionary, emitter: Dictionary, delta: float, time: float, source_transform: Transform3D, spawning: bool=false, spawn_execution: bool=false) -> Dictionary:
	return NBNativeParticleContext.for_particle(particle,emitter,delta,time,source_transform,_context_frame(spawning,spawn_execution),Attributes.BUILTINS,EXTRA_CONTEXT_FIELDS)

func _emitter_context(emitter: Dictionary,delta: float,time: float) -> Dictionary:
	return NBNativeParticleContext.for_emitter(emitter,delta,time,_context_frame())

func _execute_emitter_assignment(operation: Resource,emitter: Dictionary,delta: float,time: float,stage_name: String="EmitterUpdate") -> void:
	NBNativeParticleContext.assign_emitter(operation,emitter,delta,time,stage_name,_context_frame())

func _refresh_runtime_operations() -> void:
	NBNativeRuntimeOperations.refresh_emitters(_emitters,RuntimeOperation,RuntimeOperation.NBInput,OPERATION_WRITES)

func _refresh_operation_context(operation: Resource,context: Dictionary,particle: Dictionary) -> void:
	NBNativeParticleContext.refresh_operation(operation,context,particle,OPERATION_WRITES,Attributes.BUILTINS,EXTRA_CONTEXT_FIELDS)

func _refresh_context(context: Dictionary,particle: Dictionary) -> void:
	NBNativeParticleContext.refresh(context,particle,Attributes.BUILTINS,EXTRA_CONTEXT_FIELDS)

func _execute(operation: Resource, particle: Dictionary, context: Dictionary, delta: float) -> void:
	NBNativeParticleModules.execute(operation,particle,context,delta,Attributes.BUILTINS)

func _create_renderer(renderer: Resource, index: int, mesh_binding: Dictionary = {}, dynamic_parameter_mask: int = 0) -> Dictionary:
	return NBNativeRendererGeometry.create_renderer(self,renderer,index,mesh_binding,dynamic_parameter_mask)

func _mesh_resource_parameters(renderer: Resource) -> Dictionary:
	return NBNativeRendererGeometry.mesh_resource_parameters(self,renderer)

func _refresh_mesh_resource_bindings() -> void:
	NBNativeRendererGeometry.refresh_mesh_resources(self,Compiler)

func _diagnostic_material() -> StandardMaterial3D:
	return NBNativeRendererGeometry.diagnostic_material()

func _validate_temporal_view() -> bool:
	return NBNativeRendererGeometry.validate_temporal(self)

func _sync_renderers() -> void:
	NBNativeRendererGeometry.synchronize_player(self,Compiler,Attributes.BUILTINS)

func _release_particle_lights() -> void:
	NBNativeRendererGeometry.release_lights(self)

func _sync_particle_lights(camera: Camera3D) -> void:
	NBNativeRendererGeometry.synchronize_lights(self,camera,NativeSurfacePass)

func _release_native_surface() -> void:
	NBNativeRendererGeometry.release_surface(self)

func _sync_native_surface(camera: Camera3D) -> void:
	NBNativeRendererGeometry.synchronize_surface(self,camera,NativeSurfacePass)

func _sync_refraction(camera: Camera3D) -> void:
	NBNativeRendererGeometry.synchronize_refraction(self,camera,RefractionPass)

func _bounds_input_signature() -> int:
	return NBNativeEffectController.bounds_signature(self)

func _prepare_native_bounds() -> void:
	NBNativeEffectController.prepare_bounds(self)

func _check_native_bounds_inputs() -> void:
	if _saved_revision == null:
		NBNativeEffectController.check_bounds(self)

func _advance_native_bounds(world_delta_seconds: float) -> void:
	NBNativeEffectController.advance_bounds(self,world_delta_seconds)

func _mark_partial_bounds() -> void:
	NBNativeEffectController.mark_partial_bounds(self)

func _sync_primitive_material_bounds() -> void:
	NBNativeEffectController.sync_primitive_bounds(self)

func _sync_renderer_primitive_bounds(renderer: Dictionary) -> void:
	diagnostics.append_array(NBNativeRendererGeometry.primitive_bounds(renderer,_native_bounds_result))

func _dispose_composite() -> void:
	NBNativeRendererGeometry.dispose_composite(self)

func _scoped_input_signature() -> int:
	return NBNativeRendererGeometry.composite_signature(self,ViewExposure)

func _composite_ribbon_material(source: Resource) -> Material:
	return NBNativeRendererGeometry.composite_ribbon_material(self,source)

static func _material_pass(material: Material) -> String:
	return NBNativeRendererGeometry.material_pass(material,MaterialProgram)

static func _opaque_mesh_sections(renderer: Dictionary,sections: Array[int]) -> ArrayMesh:
	return NBNativeRendererGeometry.opaque_sections(renderer,sections)

func _scoped_material_signature(material: Material) -> Variant:
	return NBNativeRasterBuffers.material_signature(material)

func _prepare_composite() -> void:
	NBNativeRendererGeometry.prepare_composite(self,SystemMaterialComposer,MaterialProgram,CompositeSprites)

func _scoped_unavailable(message: String) -> void:
	NBNativeRendererGeometry.composite_unavailable(self,message)

func _sync_composite(camera: Camera3D) -> void:
	NBNativeRendererGeometry.synchronize_composite(self,camera,Compiler,Attributes.BUILTINS)

func _refresh_host_dynamic_masks() -> void:
	NBNativeRendererGeometry.refresh_dynamic_masks(self)

func _renderer_camera_offset(renderer: Resource,particle: Dictionary,emitter: Dictionary) -> float:
	return NBNativeRendererGeometry.camera_offset(Compiler.renderer_camera_offset_reference(renderer),particle,emitter,_context_frame(),Attributes.BUILTINS)

func _renderer_particle_view(emitter: Dictionary,source: Resource) -> Dictionary:
	return NBNativeRendererGeometry.renderer_view(self,emitter,source,Attributes.BUILTINS)

func _refresh_sprite_coverage() -> void:
	NBNativeRendererGeometry.refresh_coverage(self,PixelCoverage)

func _sprite_coverage(renderer: Dictionary, particle: Dictionary, camera: Camera3D, space: Transform3D) -> Dictionary:
	return PixelCoverage.sample(renderer.get("pixel_coverage", {}), particle.size, space * Values.unreal_to_godot(particle.position), camera)

func _sprite_particle_transform(renderer: Dictionary,particle: Dictionary,camera: Camera3D,space: Transform3D,camera_offset: float, size_ratio: float = 1.0) -> Transform3D:
	return NBNativeRendererGeometry.sprite_transform(renderer,particle,camera,space,camera_offset,size_ratio)

func _sync_ribbon_renderer(emitter: Dictionary, renderer: Dictionary, particles: Array, camera: Camera3D, space: Transform3D) -> void:
	NBNativeRendererGeometry.synchronize_ribbon(self,emitter,renderer,particles,camera,space)

func _sync_ribbon_relative_times(renderer: Dictionary,times: PackedFloat32Array) -> void:
	NBNativeRendererGeometry.ribbon_relative_times(renderer,times)

func _sync_sprite_renderer(emitter: Dictionary, renderer: Dictionary, particles: Array, camera: Camera3D, _space: Transform3D) -> void:
	NBNativeRendererGeometry.synchronize(renderer,emitter,particles,camera,global_transform,_context_frame(),Attributes.BUILTINS,Compiler.renderer_camera_offset_reference(renderer.source),false)

func _sync_mesh_renderer(emitter: Dictionary, renderer: Dictionary, particles: Array, camera: Camera3D, _space: Transform3D) -> void:
	NBNativeRendererGeometry.synchronize(renderer,emitter,particles,camera,global_transform,_context_frame(),Attributes.BUILTINS,Compiler.renderer_camera_offset_reference(renderer.source),true)

func _mesh_particle_selected(renderer: Dictionary, particle: Dictionary) -> bool:
	return NBNativeRendererGeometry.mesh_selected(renderer,particle)

func _write_particle_attributes(renderer: Dictionary, index: int, particle: Dictionary, attributes: Color) -> void:
	NBNativeRendererGeometry.write_attributes(renderer.attributes_image,index,particle,attributes)

func _mesh_particle_transform(entry: Resource, particle: Dictionary, local_space: bool,camera: Camera3D=null,camera_offset: float=0.0,facing: Dictionary={}) -> Transform3D:
	return NBNativeRendererGeometry.mesh_transform(entry,particle,local_space,global_transform,camera,camera_offset,facing)

func _sorted_particles(particles: Array, renderer: Resource, camera: Camera3D, space_transform: Transform3D) -> Array:
	return NBNativeRendererGeometry.sorted_particles(particles,renderer,camera,space_transform)
