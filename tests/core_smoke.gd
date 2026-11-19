extends SceneTree
## Run with the Forward+ renderer to exercise the GPU compute backend.
const Player = preload("res://addons/angara_fx/runtime/effect_player.gd")
const EffectInput = preload("res://addons/angara_fx/document/nb_input.gd")
const Workspace = preload("res://addons/angara_fx/editor/workspace.tscn")
const DEMO = "res://examples/embers/embers.tres"
const EDITED = "user://angara-smoke/edited.tres"
const STEP = 1.0 / 60.0
var failures := 0

func _initialize() -> void:
	run.call_deferred()

func check(valid: bool, label: String) -> void:
	if not valid:
		failures += 1
		push_error(label)

func pin(module: Resource, name: String, value: Variant) -> void:
	for entry: Resource in module.inputs:
		if entry.input_name == name:
			entry.value = value
			return
	var entry := EffectInput.new()
	entry.input_name = name
	entry.value = value
	module.inputs.append(entry)

func simulate(effect: Resource, gpu: bool, quality: int, strength: float) -> Dictionary:
	var emitter: Resource = effect.emitters[0]
	if gpu:
		var target := EffectInput.new()
		target.input_name = "SimTarget"
		target.value = "GPUComputeSim"
		emitter.settings.append(target)
	var spawn: Resource = emitter.get_stage("ParticleSpawn")
	pin(spawn.operations[1], "Velocity", [0.0, 0.0, 0.0])
	var curl: Resource = emitter.get_stage("ParticleUpdate").operations[1]
	pin(curl, "NoiseQuality/Cost", "NewEnumerator%d" % quality)
	pin(curl, "NoiseStrength", strength)
	var player := Player.new()
	player.autoplay = false
	player.effect = effect
	root.add_child(player)
	for i in 24: player.advance(STEP)
	var backends: Array = player.get_backend_statistics()
	var label := ("GPU" if gpu else "CPU") + " quality " + str(quality)
	check(backends.size() == 1 and backends[0].backend == ("gpu_compute" if gpu else "cpu"), label + " selected")
	check(not player.diagnostics.any(func(d): return d.severity == "error"), label + ": " + str(player.diagnostics))
	var points := {}
	for particle: Dictionary in player.get_particle_snapshot():
		points[particle.id] = particle.world_position
	check(points.size() >= 12 and points.size() <= 15, label + " uses the saved emission rate")
	player.free()
	return points

func run() -> void:
	var editor := Workspace.instantiate()
	root.add_child(editor)
	editor._open_file(DEMO)
	if editor.effect == null:
		push_error("Cannot open demo in the authoring workspace")
		quit(1)
		return
	var original: Resource = editor.effect
	var duplicate := original.duplicate(true)
	editor.request_open_resource(duplicate)
	# Use the same undoable property edit and text-save handlers as the inspector.
	var declaration: Resource = editor.effect.user_parameters[0]
	editor._edit_value(declaration, [], 36.0)
	editor._save_to(EDITED)
	editor._open_file(EDITED)
	var edited: Resource = ResourceLoader.load(EDITED, "", ResourceLoader.CACHE_MODE_IGNORE_DEEP)
	if edited == null:
		push_error("Cannot reload the saved effect")
		quit(1)
		return
	check(edited.user_parameters[0].value == 36.0, "edited rate survives reload")
	check(FileAccess.get_file_as_string(EDITED).begins_with("[gd_resource"), "effect is plain Godot text")
	editor.free()
	var loaded: Resource = ResourceLoader.load(DEMO, "", ResourceLoader.CACHE_MODE_IGNORE_DEEP)
	check(loaded.user_parameters[0].value == 110.0, "shipped demo retains its default rate")
	check(loaded.emitters[0].renderers[0].material.resource_path == "res://examples/embers/ember_material.tres", "saved sprite material")
	for quality in 3:
		var cpu := simulate(edited.duplicate(true), false, quality, 180.0)
		var gpu := simulate(edited.duplicate(true), true, quality, 180.0)
		var max_error := 0.0
		var moved := 0.0
		check(cpu.size() == gpu.size(), "matching CPU and GPU populations")
		for id in cpu:
			if not gpu.has(id):
				check(false, "missing GPU particle %s" % id)
				continue
			moved = maxf(moved, cpu[id].length())
			max_error = maxf(max_error, cpu[id].distance_to(gpu[id]))
		check(moved > 0.001 and max_error < 0.0005, "flow must move particles and agree within 0.5 mm")
		print("curl ", [16,32,64][quality], " | particles ", cpu.size(), " | max CPU/GPU error (m) ", max_error)
	var still := simulate(edited.duplicate(true), false, 1, 0.0)
	check(still.values().all(func(v): return v.is_zero_approx()), "zero strength leaves particles at rest")
	var scene: Node = load("res://examples/embers/embers.tscn").instantiate()
	root.add_child(scene)
	var player: Node = scene.get_node("Effect")
	player.set_process(false)
	for i in 60: player.advance(STEP)
	check(player.get_particle_count() > 40, "demo scene produces embers")
	await process_frame
	await RenderingServer.frame_post_draw
	var image := root.get_texture().get_image()
	var bg := image.get_pixel(0, 0)
	var foreground := 0
	for y in image.get_height():
		for x in image.get_width():
			var pixel := image.get_pixel(x, y)
			if pixel.r - bg.r > 0.12: foreground += 1
	check(foreground > 60, "rendered embers visible")
	if not OS.get_cmdline_user_args().is_empty():
		check(image.save_png(OS.get_cmdline_user_args()[0]) == OK, "capture saved")
	print("Embers | particles ", player.get_particle_count(), " | foreground pixels ", foreground, " | failures ", failures)
	scene.free()
	quit(1 if failures else 0)
