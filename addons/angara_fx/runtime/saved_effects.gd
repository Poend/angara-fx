@tool
extends RefCounted

const Effect = preload("res://addons/angara_fx/document/nb_effect.gd")
const Compiler = preload("res://addons/angara_fx/compiler/effect_compiler.gd")
const Emitter = preload("res://addons/angara_fx/document/nb_emitter.gd")
const Renderer = preload("res://addons/angara_fx/document/nb_renderer.gd")
const InputResource = preload("res://addons/angara_fx/document/nb_input.gd")
const MeshEntry = preload("res://addons/angara_fx/document/nb_mesh_entry.gd")
const DIRECTORY = "res://.godot/angara_fx/saves"
const INTERVAL = 0.2

static var _watches: Dictionary = {}
static var _loading: Dictionary = {}
static var _timer: SceneTreeTimer

static func document_path(resource: Resource) -> String:
	if resource == null or resource.get_script() != Effect:
		return ""
	var path := resource.resource_path
	return path if path.begins_with("res://") and path.ends_with(".tres") and not path.contains("::") else ""

static func first_error(plan: Dictionary) -> String:
	if not plan.has("emitters") or not plan.has("diagnostics"):
		return "Compiler returned an incomplete plan."
	for row: Dictionary in plan.diagnostics:
		if row.get("severity") == "error":
			return str(row.get("message", row))
	return ""

static func _all_stock(items: Array, schema: Script) -> bool:
	for item: Resource in items:
		if item == null or item.get_script() != schema:
			return false
	return true

static func has_static_renderer_layout(effect: Resource) -> bool:
	if not _all_stock(effect.settings, InputResource) or not _all_stock(effect.user_parameters, InputResource) or not _all_stock(effect.emitters, Emitter):
		return false
	for emitter: Resource in effect.emitters:
		if not _all_stock(emitter.settings, InputResource) or not _all_stock(emitter.renderers, Renderer):
			return false
		for renderer: Resource in emitter.renderers:
			if not _all_stock(renderer.properties, InputResource) or not _all_stock(renderer.mesh_entries, MeshEntry):
				return false
	return true

static func _marker(path: String) -> String:
	return DIRECTORY.path_join(path.sha256_text() + ".txt")

# Godot text resources list their external dependencies in the header. Record
# this on the saving side, so playing a frame never traverses that graph.
static func _dependency_time(root: String) -> int:
	var visited := {root: true}
	var pending: Array[String] = [root]
	var newest := 0
	while not pending.is_empty():
		var path: String = pending.pop_back()
		for dependency: String in ResourceLoader.get_dependencies(path):
			var parts := dependency.split("::", true)
			var file := ResourceUID.ensure_path(parts[0])
			if not file.begins_with("res://") and parts.size() >= 3:
				file = parts[2]
			if visited.has(file):
				continue
			visited[file] = true
			var modified := FileAccess.get_modified_time(file)
			if modified == 0:
				return -1
			newest = maxi(newest, modified)
			match file.get_extension().to_lower():
				"tres", "tscn", "gdshader", "gdshaderinc":
					pending.append(file)
				"gd":
					# Script dependencies can use runtime load() and are managed
					# by Godot. A changed script forces isolated deep loading.
					pass
				_:
					# Imported and custom formats can depend on generated files.
					return -1
	return newest

static func publish(resource: Resource, saved_path: String) -> String:
	var path := ProjectSettings.localize_path(saved_path)
	if resource == null or resource.get_script() != Effect:
		return ""
	var problem := first_error(Compiler.compile_effect(resource, {}, {"enabled_emitters_only": true}))
	if path.begins_with("res://") and path.ends_with(".tres") and OS.has_feature("editor"):
		var error := DirAccess.make_dir_recursive_absolute(DIRECTORY)
		if error != OK:
			return "Live reload: " + error_string(error)
		# A fresh revision also covers changes to external resources when the root
		# text is identical. Atomic replacement coalesces consecutive saves.
		var revision := ResourceUID.id_to_text(ResourceUID.create_id())
		var marker := _marker(path)
		var temporary := marker + "." + revision.trim_prefix("uid://") + ".tmp"
		var digest := FileAccess.get_sha256(path)
		if digest.is_empty():
			return "Live reload: saved document could not be read."
		var file := FileAccess.open(temporary, FileAccess.WRITE)
		if file == null:
			return "Live reload: " + error_string(FileAccess.get_open_error())
		var dependency_time := _dependency_time(path)
		var written := file.store_string("%s\n%s\n%.6f\n%d" % [revision, digest, Time.get_unix_time_from_system(), dependency_time])
		file.close()
		error = DirAccess.rename_absolute(temporary, marker) if written else ERR_FILE_CANT_WRITE
		if error != OK:
			DirAccess.remove_absolute(temporary)
			return "Live reload: " + error_string(error)
	return problem

static func _token(marker: String) -> String:
	var file := FileAccess.open(marker, FileAccess.READ)
	return file.get_as_text() if file != null else ""

static func watch(player: Node, path: String) -> Resource:
	if not OS.has_feature("editor") or Engine.is_editor_hint() or path.is_empty():
		return null
	if not _watches.has(path):
		var marker := _marker(path)
		var token := _token(marker)
		var launch_time := Time.get_unix_time_from_system() - Time.get_ticks_usec() / 1000000.0
		var predates_game := token.get_slice("\n", 2).to_float() <= launch_time
		_watches[path] = {"marker": marker, "seen": token if predates_game else "", "players": {}, "latest": null}
	var entry: Dictionary = _watches[path]
	entry.players[player.get_instance_id()] = weakref(player)
	if _timer == null:
		_schedule(player.get_tree())
	return entry.latest

static func unwatch(player: Node, path: String) -> void:
	if _watches.has(path):
		var entry: Dictionary = _watches[path]
		entry.players.erase(player.get_instance_id())
		if entry.players.is_empty():
			_watches.erase(path)

static func _schedule(tree: SceneTree) -> void:
	_timer = tree.create_timer(INTERVAL, true, false, true)
	_timer.timeout.connect(_poll.bind(tree), CONNECT_ONE_SHOT)

static func _poll(tree: SceneTree) -> void:
	_timer = null
	for path: String in _loading.keys():
		var status := ResourceLoader.load_threaded_get_status(path)
		if status == ResourceLoader.THREAD_LOAD_IN_PROGRESS:
			continue
		var token: String = _loading[path]
		_loading.erase(path)
		var candidate := ResourceLoader.load_threaded_get(path)
		if not _watches.has(path):
			continue
		var entry: Dictionary = _watches[path]
		if _token(entry.marker) != token or FileAccess.get_sha256(path) != token.get_slice("\n", 1):
			continue
		entry.seen = token
		if candidate == null or candidate.get_script() != Effect:
			push_warning("Angara FX could not load saved effect " + path)
			continue
		candidate.set_path_cache(path)
		for reference: WeakRef in entry.players.values():
			var player: Node = reference.get_ref()
			if is_instance_valid(player) and player.is_inside_tree():
				if player._apply_saved_effect(candidate, path):
					entry.latest = candidate
	for path: String in _watches.keys():
		if _loading.has(path):
			continue
		var entry: Dictionary = _watches[path]
		var token := _token(entry.marker)
		if token.is_empty() or token == entry.seen:
			continue
		var digest := token.get_slice("\n", 1)
		if digest.is_empty() or FileAccess.get_sha256(path) != digest:
			entry.seen = token
			continue
		# An isolated graph lets each player validate before releasing its old VM.
		var launch_second := floori(Time.get_unix_time_from_system() - Time.get_ticks_usec() / 1000000.0)
		var dependency_time := token.get_slice("\n", 3).to_int() if token.get_slice_count("\n") >= 4 else -1
		var mode := ResourceLoader.CACHE_MODE_IGNORE_DEEP
		if dependency_time >= 0 and dependency_time < launch_second:
			mode = ResourceLoader.CACHE_MODE_IGNORE
		var error := ResourceLoader.load_threaded_request(path, "", false, mode)
		if error == OK:
			_loading[path] = token
		elif error != ERR_BUSY:
			entry.seen = token
			push_warning("Angara FX could not load %s: %s" % [path, error_string(error)])
	if (not _watches.is_empty() or not _loading.is_empty()) and _timer == null:
		_schedule(tree)
