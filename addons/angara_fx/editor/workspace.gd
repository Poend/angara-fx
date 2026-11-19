@tool
extends PanelContainer

signal document_state_changed

const Effect = preload("res://addons/angara_fx/document/nb_effect.gd")
const SavedEffects = preload("res://addons/angara_fx/runtime/saved_effects.gd")
const Emitter = preload("res://addons/angara_fx/document/nb_emitter.gd")
const Stage = preload("res://addons/angara_fx/document/nb_stage.gd")
const Renderer = preload("res://addons/angara_fx/document/nb_renderer.gd")
const InputResource = preload("res://addons/angara_fx/document/nb_input.gd")
const Registry = preload("res://addons/angara_fx/modules/module_registry.gd")
const InputRow = preload("res://addons/angara_fx/editor/input_row.tscn")
const ParameterCreator = preload("res://addons/angara_fx/editor/parameter_creator.tscn")
const PARAMETER_TYPES := ["float", "int", "bool", "Vector2", "Vector3", "Vector4", "Color"]
const EmitterCard = preload("res://addons/angara_fx/editor/emitter_card.tscn")
const SystemCard = preload("res://addons/angara_fx/editor/system_card.tscn")
const GraphStage = preload("res://addons/angara_fx/editor/graph_stage.tscn")
const GraphProperty = preload("res://addons/angara_fx/editor/graph_property_row.tscn")
const StackRow = preload("res://addons/angara_fx/editor/stack_row.tscn")
const CurvePlot = preload("res://addons/angara_fx/editor/curve_plot.tscn")
const StageHeader = preload("res://addons/angara_fx/editor/stage_header.tscn")
const ModuleActions = preload("res://addons/angara_fx/editor/module_actions.tscn")
const EmitterActions = preload("res://addons/angara_fx/editor/emitter_actions.tscn")
const ModuleClipboard = preload("res://addons/angara_fx/editor/module_clipboard.gd")
const Fingerprint = preload("res://addons/angara_fx/editor/document_fingerprint.gd")
const TimelineModel = preload("res://addons/angara_fx/editor/timeline_model.gd")
const PreviewFraming = preload("res://addons/angara_fx/editor/preview_framing.gd")
const DocumentCopy = preload("res://addons/angara_fx/document/document_copy.gd")
const ResourceSummary = preload("res://addons/angara_fx/editor/resource_summary.tscn")
const RendererAssets = preload("res://addons/angara_fx/editor/renderer_assets.tscn")
const MeshEntry = preload("res://addons/angara_fx/document/nb_mesh_entry.gd")
const MeshEntryEditor = preload("res://addons/angara_fx/editor/mesh_entry_editor.tscn")
const ResourceAssignment = preload("res://addons/angara_fx/editor/resource_assignment.tscn")
const DetailsFoldout = preload("res://addons/angara_fx/editor/details_foldout.tscn")
const DOCUMENT_SCHEMAS={"effect":Effect,"emitter":Emitter,"stage":Stage,"renderer":Renderer,"input":InputResource,"mesh_entry":MeshEntry}
const SESSION_PATH = "res://.godot/angara_fx/session.cfg"
const RECOVERY_PATH = "res://.godot/angara_fx/recovery.tres"
const RECOVERY_MEMORY_KEY = "angara_fx_unsaved_document"

var effect: Resource
var document_path: String = ""
var selected_emitter: Resource
var selected_module: Resource
var selected_stage: Resource
var selected_renderer: Resource
var _selection_kind: String = "properties"
var _graph_module_buttons: Array[Dictionary] = []
var selected_emitters: Array[Resource] = []
var history := UndoRedo.new()
var saved_version: int = 1
var _saved_fingerprint: String = ""
var _current_fingerprint: String = ""
var _fingerprint_invalid: bool = true
var _tracked_effect: Resource
var player: Node3D
var is_playing: bool = false
var preview_time: float = 0.0
var pending_action: Callable
var editor_mode: bool = false
var _refreshing: bool = false
var module_stage: Resource
var material_renderer: Resource
var material_slot: int = -1
var mesh_renderer: Resource
var mesh_entry: Resource
var _mesh_entry_expanded: Dictionary = {}
var _renderer_advanced: Dictionary = {}
var _curve_plots: Array[Dictionary] = []
var _clipboard_text: String = ""
var _recovery_file: String = ""
var _recovery_version: int = -999
var _recovery_fingerprint: String = ""
var _restoring: bool = false
var recovery_error: String = ""
var _workspace_extensions: Array[Control] = []
var _last_import_plan: String = ""
var _editor_scale: float = 1.0
var _timeline_rows: Array[Dictionary] = []
var _preview_target := Vector3(0.0, 0.1, -0.5)
var _picker_lists: Dictionary = {}
var _picker_signatures: Dictionary = {}
var _session_layout: Dictionary = {"columns": 300, "work": 500, "main": 390, "zoom": 1.0, "pan": Vector2(16, 20)}

func _ready() -> void:
	%NewEffect.pressed.connect(_request_action.bind(_new_effect))
	%OpenEffect.pressed.connect(func(): %OpenDialog.popup_centered_ratio(0.8))
	%SaveEffect.pressed.connect(save_document)
	%SaveAsEffect.pressed.connect(_show_save_dialog)
	%SavePlayerScene.pressed.connect(_show_player_scene_dialog)
	%PlayerSceneDialog.file_selected.connect(_save_player_scene)
	%FocusWorkspace.pressed.connect(_toggle_workspace_focus)
	%UndoEdit.pressed.connect(func(): history.undo(); _refresh_all())
	%RedoEdit.pressed.connect(func(): history.redo(); _refresh_all())
	%SelectSystem.pressed.connect(_select_emitter.bind(null))
	%AddEmitter.pressed.connect(_add_emitter)
	%AddSpriteEmitter.pressed.connect(_add_emitter.bind(true))
	%StackSearch.text_changed.connect(func(_text): _build_overview())
	%OpenDialog.file_selected.connect(func(path): _request_action(_open_file.bind(path)))
	%SaveDialog.file_selected.connect(_save_to)
	%DiscardDialog.confirmed.connect(func(): if preserve_session() and pending_action.is_valid(): pending_action.call())
	%PlayPause.pressed.connect(_toggle_play)
	%Restart.pressed.connect(_restart)
	%Step.pressed.connect(_step)
	%TimeSlider.value_changed.connect(_seek)
	%Duration.value_changed.connect(_set_preview_range)
	%PreviewContainer.gui_input.connect(_preview_input)
	%FrameEffect.pressed.connect(frame_effect)
	%"Emitter Tracks".get_node("Scroll/Timeline").emitter_selected.connect(_select_emitter)
	%"Emitter Tracks".get_node("Scroll/Timeline").seek_started.connect(func(): is_playing = false)
	%"Emitter Tracks".get_node("Scroll/Timeline").seek_requested.connect(_seek)
	%"Emitter Tracks".get_node("DurationControls/Apply").pressed.connect(_apply_loop_duration)
	%ModuleSearch.text_changed.connect(func(_value): _populate_module_menu())
	%MaterialDialog.file_selected.connect(_set_material)
	%MeshDialog.file_selected.connect(_set_mesh)
	%FrameOverview.pressed.connect(%Overview.frame_all)
	%Overview.navigation_changed.connect(func(value): %OverviewZoom.text = "%d%% · Middle drag to pan · Wheel to zoom" % int(value * 100))
	%Overview.navigation_changed.connect(func(_value): _cache_session_layout())
	for splitter: SplitContainer in [%Columns, %WorkSplit, %CenterSplit]:
		splitter.dragged.connect(func(_offset): _cache_session_layout())
	%RenameEmitterDialog.confirmed.connect(_commit_emitter_name)
	%RenameEmitterDialog.get_node("Name").text_submitted.connect(func(_value): _commit_emitter_name(); %RenameEmitterDialog.hide())
	%RecoveryTimer.timeout.connect(preserve_session)
	%PickerTooltipTimer.timeout.connect(_refresh_picker_tooltips)
	for picker: FileDialog in _owned_file_dialogs():
		picker.about_to_popup.connect(_picker_opening.bind(picker))
	_restore_session()
	_cache_session_layout()
	_update_title()

func _exit_tree() -> void:
	if editor_mode and is_node_ready():
		preserve_session()
	if is_instance_valid(history):
		history.free()

func set_editor_mode(value: bool) -> void:
	editor_mode = value
	%FocusWorkspace.visible = value
	if value and Engine.is_editor_hint():
		_editor_scale = maxf(EditorInterface.get_editor_scale(), 1.0)
		theme = theme.duplicate()
		theme.default_font_size = roundi(14.0 * _editor_scale)
		theme.set_font_size("font_size", "Button", roundi(13.0 * _editor_scale))
		_scale_control_tree(self)
		_fit_graph_card_widths()
		%FocusWorkspace.text = "Restore sidebars" if EditorInterface.is_distraction_free_mode_enabled() else "Focus workspace"

func add_workspace_extension(extension: Control) -> void:
	extension.workspace = self
	_workspace_extensions.append(extension)
	var toolbar := %OpenEffect.get_parent()
	toolbar.add_child(extension)
	toolbar.move_child(extension, %OpenEffect.get_index() + 1)
	var picker_ids := PackedInt64Array()
	for picker: FileDialog in extension.get_file_dialogs():
		picker_ids.append(picker.get_instance_id())
		picker.about_to_popup.connect(_picker_opening.bind(picker))
	extension.tree_exiting.connect(_remove_workspace_extension.bind(extension, picker_ids), CONNECT_ONE_SHOT)

func _remove_workspace_extension(extension: Control, picker_ids: PackedInt64Array) -> void:
	_workspace_extensions.erase(extension)
	for id in picker_ids:
		_picker_lists.erase(id)
		_picker_signatures.erase(id)

func _toggle_workspace_focus() -> void:
	if not editor_mode or not Engine.is_editor_hint():
		return
	EditorInterface.set_distraction_free_mode(not EditorInterface.is_distraction_free_mode_enabled())
	%FocusWorkspace.text = "Restore sidebars" if EditorInterface.is_distraction_free_mode_enabled() else "Focus workspace"

func _scale_control_tree(node: Node) -> void:
	if not editor_mode:
		return
	if node is Control and node.has_theme_font_size_override("font_size"):
		if not node.has_meta("niagara_base_font_size"):
			node.set_meta("niagara_base_font_size", node.get_theme_font_size("font_size"))
		var base: int = maxi(12, int(node.get_meta("niagara_base_font_size")))
		node.add_theme_font_size_override("font_size", roundi(base * _editor_scale))
	for child in node.get_children():
		_scale_control_tree(child)

func is_dirty() -> bool:
	if effect == null:
		return false
	return _saved_fingerprint.is_empty() or _document_fingerprint() != _saved_fingerprint

func _document_fingerprint() -> String:
	if _fingerprint_invalid:
		_current_fingerprint = Fingerprint.compute(effect) if effect != null else ""
		_fingerprint_invalid = false
	return _current_fingerprint

func _invalidate_fingerprint() -> void:
	_fingerprint_invalid = true

func _track_document() -> void:
	if _tracked_effect != null and _tracked_effect.changed.is_connected(_invalidate_fingerprint):
		_tracked_effect.changed.disconnect(_invalidate_fingerprint)
	_tracked_effect = effect
	if _tracked_effect != null:
		_tracked_effect.changed.connect(_invalidate_fingerprint)
	_invalidate_fingerprint()

func _request_action(action: Callable) -> void:
	if is_dirty():
		pending_action = action
		%DiscardDialog.popup_centered()
	else:
		action.call()

func request_open_resource(resource: Resource) -> void:
	_request_action(_load_document.bind(resource, resource.resource_path))

func _new_effect() -> void:
	var resource: Resource=NBNativeWorkspaceDocument.new_effect(DOCUMENT_SCHEMAS)
	open_unsaved_effect(resource)

func open_unsaved_effect(resource: Resource) -> void:
	_load_document(resource, "")
	saved_version = -1
	_saved_fingerprint = ""
	_update_title()
	%RecoveryTimer.start()

func _open_file(path: String) -> void:
	var resource: Resource
	if ResourceLoader.exists(path):
		resource = NBNativeWorkspaceDocument.load_effect(path, Effect)
	if resource == null:
		%Status.text = "Open failed: choose an Angara FX effect .tres."
		return
	_load_document(resource, path)

func _capture_selection() -> Dictionary:
	return NBNativeEditorActions.capture_selection(selected_emitters, selected_emitter, selected_stage, selected_module)

func _swap_document(resource: Resource, selection: Dictionary) -> void:
	effect = resource
	_track_document()
	var restored: Dictionary=NBNativeEditorActions.resolve_selection(effect, selection)
	selected_emitters=restored.emitters
	selected_emitter=restored.primary
	selected_stage=restored.stage
	selected_module=restored.module
	_recovery_version = -999
	_recovery_fingerprint = ""
	_document_changed()

func _load_document(resource: Resource, path: String) -> void:
	effect = resource
	_track_document()
	document_path = path
	selected_emitter = null
	selected_module = null
	selected_stage = null
	selected_renderer = null
	material_renderer = null
	mesh_renderer = null
	mesh_entry = null
	_mesh_entry_expanded.clear()
	_renderer_advanced.clear()
	_selection_kind = "properties"
	selected_emitters.clear()
	_recovery_file = ""
	_recovery_version = -999
	_recovery_fingerprint = ""
	history.clear_history()
	saved_version = history.get_version()
	_saved_fingerprint = _document_fingerprint() if not path.is_empty() else ""
	_refresh_all()
	_restart()
	%Overview.reset_view()

func _show_save_dialog() -> void:
	if effect == null:
		return
	%SaveDialog.current_file = (effect.display_name.validate_filename() + ".tres") if document_path.is_empty() else document_path.get_file()
	%SaveDialog.popup_centered_ratio(0.8)

func save_document() -> void:
	if effect == null:
		return
	if document_path.is_empty():
		_show_save_dialog()
	else:
		_save_to(document_path)

func _show_player_scene_dialog() -> void:
	if document_path.is_empty() or is_dirty():
		%Status.text = "Save the effect before creating a player scene."
		return
	%PlayerSceneDialog.current_path = document_path.get_basename() + ".tscn"
	%PlayerSceneDialog.popup_centered_ratio(0.8)

func _save_player_scene(path: String) -> void:
	if document_path.is_empty() or is_dirty() or not path.begins_with("res://") or path.get_extension().to_lower() != "tscn":
		%Status.text = "Save the effect first and choose a .tscn inside the project."
		return
	var saved := ResourceLoader.load(document_path, "", ResourceLoader.CACHE_MODE_IGNORE)
	if saved == null or saved.get_script() != Effect:
		%Status.text = "The saved effect could not be loaded."
		return
	var instance := AngaraEffect3D.new()
	instance.name = "ParticleEffect"
	instance.effect = saved
	var scene := PackedScene.new()
	var error := scene.pack(instance)
	instance.free()
	if error == OK:
		error = ResourceSaver.save(scene, path)
	%Status.text = "Created player scene: " + path if error == OK else "Scene save failed: " + error_string(error)

func _save_to(path: String) -> void:
	var saved: Dictionary=NBNativeWorkspaceDocument.save_effect(effect, path)
	path=saved.path
	var result: Error=saved.error
	if result != OK:
		%Status.text = "Save failed (%s). Changes remain in memory." % error_string(result)
		return
	document_path = path
	saved_version = history.get_version()
	_invalidate_fingerprint()
	_saved_fingerprint = _document_fingerprint()
	_update_title()
	preserve_session()
	var problem := SavedEffects.publish(effect, path)
	%Status.text = "Saved text resource: " + path if problem.is_empty() else "Saved %s. %s" % [path, problem]

func _update_title() -> void:
	%DocumentTitle.text = "No effect open" if effect == null else effect.display_name + (" *" if is_dirty() else "")
	%UndoEdit.disabled = not history.has_undo()
	%RedoEdit.disabled = not history.has_redo()
	%SaveEffect.disabled = effect == null
	%SaveAsEffect.disabled = effect == null
	%SavePlayerScene.disabled = effect == null
	%AddEmitter.disabled = effect == null
	%SelectSystem.disabled = effect == null
	%AddSpriteEmitter.disabled = effect == null
	document_state_changed.emit()

func _clear(container: Node) -> void:
	var previous_refreshing: bool = _refreshing
	_refreshing = true
	for child in container.get_children():
		child.queue_free()
		container.remove_child(child)
	_refreshing = previous_refreshing

func _refresh_all() -> void:
	_validate_selection()
	_build_overview()
	_build_stack()
	_build_parameters()
	_build_details()
	_build_tracks()
	_update_title()
	_update_diagnostics()
	_scale_control_tree(self)

func _build_overview() -> void:
	_clear(%EmitterCards)
	_graph_module_buttons.clear()
	if effect == null:
		return
	_append_graph_card(effect, true)
	for emitter in effect.emitters:
		_append_graph_card(emitter, false)
	_refresh_selection_highlight()
	_scale_control_tree(%EmitterCards)
	_fit_graph_card_widths()
	%OverviewHint.text = "Select a module in its card to edit inputs. Drag modules between compatible stages. Ctrl/Shift-click emitter headers for multiple selection."

func _append_graph_card(owner: Resource, is_system: bool) -> void:
	var card = SystemCard.instantiate() if is_system else EmitterCard.instantiate()
	%EmitterCards.add_child(card)
	card.set_meta("emitter", null if is_system else owner)
	card.set_meta("system", is_system)
	card.get_node("Content/Header/Title").text = owner.display_name
	if is_system:
		card.get_node("Content/Header/Title").pressed.connect(_select_emitter.bind(null))
		card.get_node("Content/Properties").pressed.connect(_select_emitter.bind(null))
		card.get_node("Content/Summary").pressed.connect(_show_user_parameters)
	else:
		card.get_node("Content/Header/Title").pressed.connect(_card_selected.bind(owner))
		card.get_node("Content/Header/Enabled").set_pressed_no_signal(owner.enabled)
		card.get_node("Content/Header/Enabled").toggled.connect(func(value): _edit_property(owner, "enabled", value, "Toggle emitter"))
		var backend: Variant = owner.get_input("SimTarget", "CPU")
		var backend_name: String = str(backend.get("name", "CPU")) if backend is Dictionary else str(backend)
		card.get_node("Content/Properties").text = "Properties · " + backend_name
		card.get_node("Content/Properties").pressed.connect(_select_emitter.bind(owner))
		card.get_node("Content/Summary").pressed.connect(_show_emitter_summary.bind(owner))
		card.get_node("Content/AddRenderer").pressed.connect(_graph_add_renderer.bind(owner))
		card.get_node("Content/AddMeshRenderer").pressed.connect(_graph_add_renderer.bind(owner, "MeshRenderer"))
	var names: Array = ["SystemSpawn", "SystemUpdate"] if is_system else ["EmitterSpawn", "EmitterUpdate", "ParticleSpawn", "ParticleUpdate"]
	for stage in owner.stages:
		if stage.stage_name not in names:
			names.append(stage.stage_name)
	for stage_name in names:
		_append_graph_stage(card.get_node("Content/Stages"), owner, stage_name)
	if not is_system:
		for renderer in owner.renderers:
			var row = GraphProperty.instantiate()
			card.get_node("Content/Renderers").add_child(row)
			row.text = "▧  " + _human_title(renderer.renderer_type)
			row.pressed.connect(_graph_renderer_selected.bind(owner, renderer))

func _append_graph_stage(parent: Node, owner: Resource, stage_name: String) -> void:
	var stage: Resource = owner.get_stage(stage_name)
	var group = GraphStage.instantiate()
	parent.add_child(group)
	var title = group.get_node("Header/Title")
	title.text = _stage_title(stage_name)
	title.workspace = self
	title.stage = stage
	var color: Color = Color("64c5cf") if stage_name.begins_with("System") else (Color("7abf79") if stage_name.begins_with("Particle") else Color("d5a85d"))
	title.add_theme_color_override("font_color", color)
	title.pressed.connect(_graph_stage_selected.bind(owner, stage_name))
	group.get_node("Header/Add").visible = not Registry.list_for_stage(stage_name).is_empty()
	group.get_node("Header/Add").pressed.connect(_graph_add_module.bind(owner, stage_name))
	group.get_node("Header/Paste").visible = stage != null
	group.get_node("Header/Paste").text = "Paste"
	group.get_node("Header/Paste").pressed.connect(_graph_paste_module.bind(owner, stage))
	if stage == null:
		return
	var query: String = %StackSearch.text.to_lower()
	for module in stage.operations:
		if not query.is_empty() and not module.module_name.to_lower().contains(query) and not _human_title(module.module_name).to_lower().contains(query):
			continue
		var row = StackRow.instantiate()
		group.get_node("Modules").add_child(row)
		var button = row.get_node("Select")
		button.text = _human_title(module.module_name)
		button.workspace = self
		button.stage = stage
		button.module = module
		button.pressed.connect(_graph_module_selected.bind(owner, stage, module))
		row.get_node("Enabled").set_pressed_no_signal(module.enabled)
		row.get_node("Enabled").toggled.connect(func(value): _edit_property(module, "enabled", value, "Toggle module"))
		row.get_node("Up").hide()
		row.get_node("Down").hide()
		_graph_module_buttons.append({"button": button, "module": module})

func _human_title(value: String) -> String:
	var clean: String = value.get_file().get_slice("@", 0).replace("_", " ")
	if clean.begins_with("Particles."):
		clean = "Set " + clean.trim_prefix("Particles.")
	var pattern := RegEx.new()
	pattern.compile("([a-z0-9])([A-Z])")
	return pattern.sub(clean, "$1 $2", true)

func _fit_graph_card_widths() -> void:
	# Labels are measured after theme scaling; only extreme names are clipped.
	for card in %EmitterCards.get_children():
		var needed: float = 280.0
		for group in card.get_node("Content/Stages").get_children():
			var title: Button = group.get_node("Header/Title")
			var title_width: float = title.get_theme_font("font").get_string_size(title.text, HORIZONTAL_ALIGNMENT_LEFT, -1, title.get_theme_font_size("font_size")).x
			var controls: float = 24.0
			for path in ["Header/Paste", "Header/Add"]:
				var button: Button = group.get_node(path)
				if button.visible:
					controls += button.get_combined_minimum_size().x + 4.0
			needed = maxf(needed, title_width + controls)
			for row in group.get_node("Modules").get_children():
				var button: Button = row.get_node("Select")
				var width: float = button.get_theme_font("font").get_string_size(button.text, HORIZONTAL_ALIGNMENT_LEFT, -1, button.get_theme_font_size("font_size")).x
				needed = maxf(needed, width + row.get_node("Enabled").get_combined_minimum_size().x + 30.0)
		card.custom_minimum_size.x = minf(needed, 360.0 * _editor_scale)

func _graph_owner_selected(owner: Resource) -> void:
	selected_emitter = null if owner == effect else owner
	selected_emitters.clear()
	if selected_emitter != null:
		selected_emitters.append(selected_emitter)
	selected_module = null
	selected_stage = null
	selected_renderer = null
	_selection_kind = "properties"
	_build_stack()
	_refresh_selection_highlight()

func _graph_module_selected(owner: Resource, stage: Resource, module: Resource) -> void:
	_graph_owner_selected(owner)
	selected_stage = stage
	_select_module(module)
	_refresh_selection_highlight()

func _graph_stage_selected(owner: Resource, stage_name: String) -> void:
	_graph_owner_selected(owner)
	selected_stage = owner.get_stage(stage_name)
	_selection_kind = "stage"
	_build_details()
	%DetailsTitle.text = _stage_title(stage_name)
	_refresh_selection_highlight()

func _graph_add_module(owner: Resource, stage_name: String) -> void:
	_graph_owner_selected(owner)
	var stage: Resource = owner.get_stage(stage_name)
	if stage == null:
		stage = Stage.new()
		stage.stable_id = _new_id()
		stage.stage_name = stage_name
		var stages: Array[Resource] = owner.stages.duplicate()
		stages.append(stage)
		_edit_property(owner, "stages", stages, "Add " + stage_name)
	_show_module_menu(stage)

func _graph_paste_module(owner: Resource, stage: Resource) -> void:
	_graph_owner_selected(owner)
	selected_stage = stage
	paste_module(stage)
	_refresh_selection_highlight()

func _graph_renderer_selected(owner: Resource, renderer: Resource) -> void:
	_graph_owner_selected(owner)
	_show_renderer(renderer)
	_refresh_selection_highlight()

func _graph_add_renderer(owner: Resource, renderer_type: String = "SpriteRenderer") -> void:
	_graph_owner_selected(owner)
	_add_renderer(renderer_type)

func _show_user_parameters() -> void:
	_graph_owner_selected(effect)
	_selection_kind = "parameters"
	_build_details()
	_refresh_selection_highlight()

func _show_emitter_summary(emitter: Resource) -> void:
	_graph_owner_selected(emitter)
	_selection_kind = "summary"
	_build_details()
	_refresh_selection_highlight()

func _card_selected(emitter: Resource) -> void:
	var additive: bool = Input.is_key_pressed(KEY_CTRL) or Input.is_key_pressed(KEY_META) or Input.is_key_pressed(KEY_SHIFT)
	if additive:
		if emitter in selected_emitters:
			selected_emitters.erase(emitter)
		else:
			selected_emitters.append(emitter)
		selected_emitter = selected_emitters.back() if not selected_emitters.is_empty() else null
		selected_module = null
		selected_stage = null
		selected_renderer = null
		_selection_kind = "properties"
		_build_stack()
		_build_details()
		_refresh_selection_highlight()
	else:
		_select_emitter(emitter)

func _refresh_selection_highlight() -> void:
	for card in %EmitterCards.get_children():
		var style: StyleBoxFlat = card.get_theme_stylebox("panel").duplicate()
		var system: bool = card.get_meta("system", false)
		var selected: bool = selected_emitter == null if system else card.get_meta("emitter") in selected_emitters
		style.border_color = (Color("75e0e8") if selected else Color("357d85")) if system else (Color("efc76f") if selected else Color("66543b"))
		style.bg_color = Color("25313a") if selected else Color("1d232b")
		card.add_theme_stylebox_override("panel", style)
	for item in _graph_module_buttons:
		if is_instance_valid(item.button):
			item.button.add_theme_color_override("font_color", Color("ffe0a0") if item.module == selected_module else (Color("d3d9df") if item.module.enabled else Color("6d747d")))
	_update_timeline_selection()

func _validate_selection() -> void:
	var state: Dictionary=NBNativeEditorActions.validate_selection(effect, {"emitters":selected_emitters,"primary":selected_emitter,"stage":selected_stage,"module":selected_module,"renderer":selected_renderer})
	selected_emitters=state.emitters
	selected_emitter=state.primary
	selected_stage=state.stage
	selected_module=state.module
	selected_renderer=state.renderer

func _select_emitter(emitter: Resource) -> void:
	selected_renderer = null
	_selection_kind = "properties"
	selected_emitter = emitter
	selected_module = null
	selected_stage = null
	selected_emitters.clear()
	if emitter != null:
		selected_emitters.append(emitter)
	_build_stack()
	_build_details()
	_refresh_selection_highlight()
	_scale_control_tree(%Details)

func _build_stack() -> void:
	# The editable stacks live in the system/emitter graph cards.
	if effect == null:
		%SelectionTitle.text = "SELECTION"
		return
	var owner: Resource = selected_emitter if selected_emitter != null else effect
	%SelectionTitle.text = owner.display_name

func _stage_title(stage_name: String) -> String:
	return stage_name.replace("System", "System ").replace("Emitter", "Emitter ").replace("Particle", "Particle ").strip_edges()

func _select_module(module: Resource) -> void:
	_selection_kind = "module"
	selected_renderer = null
	selected_module = module
	selected_stage = _selected_stage()
	_build_details()
	_scale_control_tree(%Details)
	_refresh_selection_highlight()

func _build_details() -> void:
	_clear(%Details)
	if effect == null:
		return
	if selected_renderer != null:
		_show_renderer(selected_renderer)
		return
	if _selection_kind == "parameters":
		%DetailsTitle.text = "User Parameters"
		var creator := ParameterCreator.instantiate()
		%Details.add_child(creator)
		var type: OptionButton = creator.get_node("Actions/Type")
		for label: String in PARAMETER_TYPES:
			type.add_item(label)
		var name_field: LineEdit = creator.get_node("Name")
		var add := func(): _create_user_parameter(name_field.text, type.get_item_text(type.selected))
		creator.get_node("Actions/Add").pressed.connect(add)
		name_field.text_submitted.connect(func(_text): add.call())
		for entry in _user_parameter_entries():
			_add_value_control(%Details, entry, [], entry.input_name, entry.value)
	elif _selection_kind == "stage":
		%DetailsTitle.text = _stage_title(selected_stage.stage_name) if selected_stage != null else "Empty stage"
		_detail_note("%s operations in execution order. Select a module in the graph to edit its inputs." % (selected_stage.operations.size() if selected_stage != null else 0))
	elif _selection_kind == "summary" and selected_emitter != null:
		%DetailsTitle.text = "Emitter Summary · " + selected_emitter.display_name
		var count: int = 0
		for stage in selected_emitter.stages:
			count += stage.operations.size()
		_detail_note("%s modules across %s authored stages.\n%s renderers.\n%s" % [count, selected_emitter.stages.size(), selected_emitter.renderers.size(), "Enabled" if selected_emitter.enabled else "Disabled"])
		for renderer in selected_emitter.renderers:
			var row = GraphProperty.instantiate()
			%Details.add_child(row)
			row.text = renderer.renderer_type
			row.pressed.connect(_show_renderer.bind(renderer))
	elif selected_module != null:
		%DetailsTitle.text = selected_module.module_name + " · line " + str(selected_module.line)
		var actions = ModuleActions.instantiate()
		%Details.add_child(actions)
		actions.get_node("Copy").pressed.connect(copy_module)
		actions.get_node("Duplicate").pressed.connect(_duplicate_module)
		actions.get_node("Remove").pressed.connect(_remove_module)
		for entry in selected_module.inputs:
			_add_value_control(%Details, entry, [], entry.input_name, entry.value, 0, selected_module)
	else:
		var owner: Resource = selected_emitter if selected_emitter != null else effect
		%DetailsTitle.text = "DETAILS · " + owner.display_name
		if selected_emitter != null:
			var actions = EmitterActions.instantiate()
			%Details.add_child(actions)
			actions.get_node("Duplicate").pressed.connect(duplicate_emitters)
			actions.get_node("Rename").pressed.connect(_show_emitter_rename)
			actions.get_node("Remove").pressed.connect(remove_emitters)
			if not selected_emitter.parent_emitter_source.is_empty():
				_detail_note("Inherited source: " + selected_emitter.parent_emitter_source + "\nThe exported override requires its parent stack to be resolved.")
				if not selected_emitter.parent_emitter_version.is_empty():
					_detail_note("Parent version: " + selected_emitter.parent_emitter_version)
			if not selected_emitter.source_lineage.is_empty():
				var lineage: Dictionary = selected_emitter.source_lineage
				_detail_note("Export provenance · " + ("verified source pair" if lineage.get("verified", false) else "unverified source pair"))
				if not str(lineage.get("parent_path", "")).is_empty():
					_detail_note("Parent provenance: " + str(lineage.parent_path) + "\nParent version: " + str(lineage.get("parent_version_guid", "")))
				if not lineage.get("export_complete", true):
					_detail_note("Source export has gaps. See Niagara Log before execution.")
		var name_row = InputRow.instantiate()
		%Details.add_child(name_row)
		name_row.get_node("InputName").text = "Name"
		name_row.get_node("String").show()
		name_row.get_node("String").text = owner.display_name
		name_row.get_node("String").text_submitted.connect(func(value): _edit_property(owner, "display_name", value, "Rename"))
		name_row.get_node("String").focus_exited.connect(func(): if not _refreshing and is_instance_valid(name_row) and not name_row.is_queued_for_deletion(): _edit_property(owner, "display_name", name_row.get_node("String").text, "Rename"))
		for entry in owner.settings:
			_add_value_control(%Details, entry, [], entry.input_name, entry.value)
	_scale_control_tree(%Details)

func _detail_note(text: String) -> void:
	var note := Label.new()
	note.text = text
	note.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	%Details.add_child(note)

func _show_renderer(renderer: Resource) -> void:
	selected_renderer = renderer
	_selection_kind = "renderer"
	selected_module = null
	_clear(%Details)
	%DetailsTitle.text = _human_title(renderer.renderer_type)
	if renderer.renderer_type == "MeshRenderer" or not renderer.mesh_entries.is_empty():
		_show_mesh_assets(renderer)
	var assignment = ResourceAssignment.instantiate()
	%Details.add_child(assignment)
	assignment.get_node("Title").text = "All-surface override" if renderer.renderer_type == "MeshRenderer" else "Material"
	assignment.get_node("Resource").text = _resource_label(renderer.material)
	assignment.get_node("Actions/Choose").pressed.connect(_choose_material.bind(renderer, -1))
	assignment.get_node("Actions/Clear").disabled = renderer.material == null
	assignment.get_node("Actions/Clear").pressed.connect(_assign_material.bind(renderer, -1, null))
	if not renderer.properties.is_empty():
		var advanced = DetailsFoldout.instantiate()
		%Details.add_child(advanced)
		var expanded: bool = _renderer_advanced.get(renderer.get_instance_id(),false)
		advanced.get_node("Toggle").set_pressed_no_signal(expanded)
		advanced.get_node("Body").visible = expanded
		advanced.get_node("Toggle").toggled.connect(func(value):
			_renderer_advanced[renderer.get_instance_id()] = value
			advanced.get_node("Body").visible = value)
		for entry in renderer.properties:
			_add_value_control(advanced.get_node("Body"), entry, [], entry.input_name, entry.value)
	_scale_control_tree(%Details)

func _resource_label(resource: Resource) -> String:
	if resource == null:
		return "none"
	if not resource.resource_name.is_empty(): return resource.resource_name
	if not resource.resource_path.is_empty() and not resource.resource_path.contains("::"): return resource.resource_path.get_file()
	return resource.get_class() + " (embedded)"

func _show_mesh_assets(renderer: Resource) -> void:
	var assets = RendererAssets.instantiate()
	%Details.add_child(assets)
	assets.get_node("AddEntry").pressed.connect(_add_mesh_entry.bind(renderer))
	assets.get_node("AddSlot").pressed.connect(_add_material_slot.bind(renderer))
	for index in range(renderer.mesh_entries.size()):
		var entry: Resource = renderer.mesh_entries[index]
		if entry == null:
			var remove = GraphProperty.instantiate()
			assets.get_node("Entries").add_child(remove)
			remove.text = "Remove unresolved mesh entry %s" % index
			remove.pressed.connect(_remove_mesh_entry.bind(renderer, index))
			continue
		var form = MeshEntryEditor.instantiate()
		assets.get_node("Entries").add_child(form)
		form.configure(entry, index, _resource_label(entry.mesh))
		form.get_node("Expand").button_pressed = _mesh_entry_expanded.get(entry.get_instance_id(), false)
		form.get_node("Expand").toggled.connect(func(expanded): _mesh_entry_expanded[entry.get_instance_id()] = expanded)
		form.mesh_requested.connect(_choose_mesh.bind(renderer, entry))
		form.mesh_cleared.connect(_assign_mesh.bind(renderer, entry, null))
		form.remove_requested.connect(_remove_mesh_entry.bind(renderer, index))
		form.settings_committed.connect(_edit_mesh_entry.bind(renderer, entry))
	for index in range(_material_slot_count(renderer)):
		var assignment = ResourceAssignment.instantiate()
		assets.get_node("Slots").add_child(assignment)
		var material: Material = renderer.surface_materials[index] if index < renderer.surface_materials.size() else null
		var parameter: String = renderer.material_parameter_names[index] if index < renderer.material_parameter_names.size() else ""
		assignment.get_node("Title").text = "Surface %s" % index
		assignment.get_node("Resource").text = _resource_label(material)
		assignment.get_node("Actions/Choose").pressed.connect(_choose_material.bind(renderer, index))
		assignment.get_node("Actions/Clear").disabled = material == null
		assignment.get_node("Actions/Clear").pressed.connect(_assign_material.bind(renderer, index, null))
		assignment.get_node("ParameterLabel").show()
		assignment.get_node("Parameter").show()
		assignment.get_node("Parameter").text = parameter
		assignment.get_node("ApplyBinding").show()
		assignment.get_node("ApplyBinding").pressed.connect(func(): _set_material_binding(renderer, index, assignment.get_node("Parameter").text))
	if renderer.mesh_entries.is_empty():
		var summary = ResourceSummary.instantiate()
		assets.get_node("Entries").add_child(summary)
		summary.text = "No authored mesh entries."

func _contains_renderer(renderer: Resource) -> bool:
	return NBNativeEditorActions.contains_renderer(effect, renderer)

func _material_slot_count(renderer: Resource) -> int:
	return NBNativeEditorActions.material_slot_count(renderer)

func _add_mesh_entry(renderer: Resource) -> void:
	if not _contains_renderer(renderer):
		return
	var entries: Array[Resource] = renderer.mesh_entries.duplicate()
	entries.append(MeshEntry.new())
	_edit_property(renderer, "mesh_entries", entries, "Add mesh entry")

func _remove_mesh_entry(renderer: Resource, index: int) -> void:
	if not _contains_renderer(renderer) or index < 0 or index >= renderer.mesh_entries.size():
		return
	var entries: Array[Resource] = renderer.mesh_entries.duplicate()
	entries.remove_at(index)
	_edit_property(renderer, "mesh_entries", entries, "Remove mesh entry")

func _choose_mesh(renderer: Resource, entry: Resource) -> void:
	if not _contains_renderer(renderer) or entry not in renderer.mesh_entries:
		return
	mesh_renderer = renderer
	mesh_entry = entry
	%MeshDialog.popup_centered_ratio(0.8)

func _set_mesh(path: String) -> void:
	var resource: Resource = _load_text_asset(path)
	if not resource is Mesh:
		%Status.text = "Choose a Mesh saved as a project .tres resource."
		return
	_assign_mesh(mesh_renderer, mesh_entry, resource)

func _assign_mesh(renderer: Resource, entry: Resource, mesh: Mesh) -> void:
	if not _contains_renderer(renderer) or entry == null or entry not in renderer.mesh_entries:
		%Status.text = "The mesh entry no longer belongs to this document."
		return
	# Source path is provenance, not the chosen Godot asset path.
	_edit_property(entry, "mesh", mesh, "Assign mesh" if mesh != null else "Clear mesh")
	%Status.text = "Mesh assigned. Source reference preserved." if mesh != null else "Mesh cleared. Runtime diagnostics show any unresolved binding."

func _edit_mesh_entry(changes: Dictionary, renderer: Resource, entry: Resource) -> void:
	if _contains_renderer(renderer) and entry in renderer.mesh_entries:
		_edit_properties(entry, changes, "Edit mesh entry settings")

func _choose_material(renderer: Resource, slot: int = -1) -> void:
	if not _contains_renderer(renderer):
		return
	material_renderer = renderer
	material_slot = slot
	%MaterialDialog.title = "Assign surface %s material" % slot if slot >= 0 else "Assign material override"
	%MaterialDialog.popup_centered_ratio(0.8)

func _assign_material(renderer: Resource, slot: int, material: Material) -> void:
	if not _contains_renderer(renderer):
		%Status.text = "The renderer no longer belongs to this document."
		return
	if slot < 0:
		_edit_property(renderer, "material", material, "Assign material" if material != null else "Clear material")
		return
	var materials: Array[Material] = renderer.surface_materials.duplicate()
	if slot >= materials.size():
		materials.resize(slot + 1)
	materials[slot] = material
	_edit_property(renderer, "surface_materials", materials, "Assign surface %s material" % slot)

func _set_material_binding(renderer: Resource, slot: int, name: String) -> void:
	if not _contains_renderer(renderer):
		return
	name = name.strip_edges()
	if not name.is_empty() and (not name.begins_with("User.") or name.length() <= 5):
		%Status.text = "Use the full User.* material parameter name, or leave it empty."
		return
	var names: Array[String] = renderer.material_parameter_names.duplicate()
	if slot >= names.size():
		names.resize(slot + 1)
	names[slot] = name
	_edit_property(renderer, "material_parameter_names", names, "Edit surface %s material binding" % slot)

func _add_material_slot(renderer: Resource) -> void:
	if not _contains_renderer(renderer):
		return
	var materials: Array[Material] = renderer.surface_materials.duplicate()
	materials.resize(_material_slot_count(renderer) + 1)
	_edit_property(renderer, "surface_materials", materials, "Add material slot")

func _load_text_asset(path: String) -> Resource:
	return NBNativeWorkspaceDocument.load_asset(path)

func _user_parameter_entries() -> Array[Resource]:
	return NBNativeEditorActions.user_entries(effect)

func _build_parameters() -> void:
	_clear(%Parameters)
	if effect == null:
		return
	for entry in effect.settings:
		_add_value_control(%Parameters, entry, [], entry.input_name, entry.value)
	for entry in effect.user_parameters:
		_add_value_control(%Parameters, entry, [], entry.input_name, entry.value)
	_scale_control_tree(%Parameters)

func _create_user_parameter(raw_name: String, type: String) -> void:
	var name_key := NBNativeUserParameters.qualified_name(raw_name)
	if name_key.is_empty() or not type in PARAMETER_TYPES:
		%Status.text = "Enter a parameter name such as Speed or User.Speed."
		return
	for existing in _user_parameter_entries():
		if name_key == "User." + existing.input_name.trim_prefix("User."):
			%Status.text = "Parameter already exists: " + name_key
			return
	var entry := InputResource.new()
	entry.input_name = name_key
	entry.target_type = type
	entry.value = Color.WHITE if type == "Color" else NBNativeAttributeValues.zero(NBNativeAttributeValues.type_name(type))
	entry.provenance = "authored"
	var declarations: Array[Resource] = effect.user_parameters.duplicate()
	declarations.append(entry)
	_edit_property(effect, "user_parameters", declarations, "Add " + name_key)

func _add_binding_picker(row: Control, module: Resource, entry: Resource, value: Variant) -> bool:
	var pin_type := Registry.live_input_type(module, entry)
	if pin_type.is_empty():
		return false
	var current: String = ""
	if value is Dictionary and value.get("kind") == "reference":
		current = str(value.get("name", ""))
		if not current.begins_with("User."):
			current = ""
	var choices := PackedStringArray()
	for declaration: Resource in effect.user_parameters:
		var type := NBNativeAttributeValues.type_name(declaration.target_type)
		if NBNativeAttributeValues.compatible(pin_type, type) or (pin_type == "float" and type == "int"):
			choices.append("User." + declaration.input_name.trim_prefix("User."))
	if current.is_empty() and choices.is_empty():
		return false
	var picker := OptionButton.new()
	picker.name = "Binding"
	picker.accessibility_name = "Binding for " + entry.input_name
	row.add_child(picker)
	row.move_child(picker, 1)
	picker.add_item("Local value")
	for choice in choices:
		picker.add_item(choice)
		if choice == current:
			picker.select(picker.item_count - 1)
	if not current.is_empty() and not choices.has(current):
		picker.add_item(current + " (unavailable)")
		picker.select(picker.item_count - 1)
		picker.set_item_disabled(picker.selected, true)
	picker.item_selected.connect(func(index: int):
		var fallback: Variant = entry.value
		var has_local_default: bool = entry.has_default
		if not current.is_empty():
			var previous := NBNativeUserParameters.declaration(effect, current)
			fallback = entry.value.get("local_value", previous.value if previous != null and previous.has_default else NBNativeAttributeValues.zero(pin_type))
			has_local_default = entry.value.get("local_has_default", true)
		var next: Variant = fallback if index == 0 else {"kind":"reference", "name":choices[index - 1], "local_value":fallback}
		if index != 0 and not has_local_default:
			next["local_has_default"] = false
		_edit_properties(entry, {"value": next, "has_default": index != 0 or has_local_default}, "Change binding for " + entry.input_name)
	)
	return not current.is_empty()

func _add_value_control(parent: Node, entry: Resource, path: Array, label: String, value: Variant, depth: int = 0, module: Resource = null) -> void:
	var row = InputRow.instantiate()
	parent.add_child(row)
	if path.is_empty() and entry in effect.user_parameters:
		row.set_meta("parameter_default", entry)
	row.get_node("InputName").text = label
	if module != null and _add_binding_picker(row, module, entry, value):
		return
	if path.is_empty() and not entry.target_type.is_empty():
		row.get_node("InputName").text += " · " + entry.target_type
	if path.is_empty() and not entry.has_default:
		row.get_node("Summary").show()
		row.get_node("Summary").text = "No authored default"
		return
	if value is bool:
		row.get_node("Boolean").show()
		row.get_node("Boolean").set_pressed_no_signal(value)
		row.get_node("Boolean").toggled.connect(func(new_value): _edit_value(entry, path, new_value))
	elif value is float or value is int:
		var spin: SpinBox = row.get_node("Number")
		spin.show()
		spin.step = 1.0 if value is int else 0.001
		spin.set_value_no_signal(value)
		spin.value_changed.connect(func(new_value): _edit_value(entry, path, int(new_value) if value is int else new_value))
	elif value is String:
		row.get_node("String").show()
		row.get_node("String").text = value
		row.get_node("String").text_submitted.connect(func(new_value): _edit_value(entry, path, new_value))
		row.get_node("String").focus_exited.connect(func(): if not _refreshing and is_instance_valid(row) and not row.is_queued_for_deletion(): _edit_value(entry, path, row.get_node("String").text))
	elif value is Vector2 or value is Vector3 or value is Vector4 or value is Color:
		var labels := "RGBA" if value is Color else ("XY" if value is Vector2 else ("XYZ" if value is Vector3 else "XYZW"))
		for axis in labels.length():
			var component := HBoxContainer.new()
			var title := Label.new()
			title.text = labels[axis]
			var spin := SpinBox.new()
			spin.size_flags_horizontal = Control.SIZE_EXPAND_FILL
			spin.allow_greater = true
			spin.allow_lesser = true
			spin.step = 0.001
			spin.set_value_no_signal(value[axis])
			component.add_child(title)
			component.add_child(spin)
			row.get_node("Children").add_child(component)
			spin.value_changed.connect(func(number: float):
				var edited: Variant = entry.value
				for key in path:
					edited = edited[key]
				edited[axis] = number
				_edit_value(entry, path, edited)
			)
	elif value is Dictionary and depth < 10:
		var kind: String = str(value.get("kind", ""))
		row.get_node("Summary").show()
		row.get_node("Summary").text = str(value.get("name", kind))
		if kind == "curve":
			var plot = CurvePlot.instantiate()
			row.get_node("Children").add_child(plot)
			plot.set_curve(value)
			_curve_plots.append({"plot": plot, "entry": entry, "path": path.duplicate()})
			plot.curve_committed.connect(func(new_value): _edit_value(entry, path, new_value); _build_details())
		if kind == "call":
			var arguments: Array = value.get("arguments", [])
			for i in arguments.size():
				_add_value_control(row.get_node("Children"), entry, path + ["arguments", i, "value"], str(arguments[i].get("name", "Input")), arguments[i].get("value"), depth + 1)
		else:
			for key in value:
				if key in ["kind", "line", "column", "source", "raw"]:
					continue
				_add_value_control(row.get_node("Children"), entry, path + [key], str(key), value[key], depth + 1)
	elif value is Array and depth < 10:
		for i in value.size():
			_add_value_control(row.get_node("Children"), entry, path + [i], "[%s]" % i, value[i], depth + 1)
	else:
		row.get_node("Summary").show()
		row.get_node("Summary").text = _resource_label(value) if value is Resource else str(value)

func _edit_value(entry: Resource, path: Array, new_value: Variant) -> void:
	var edited: Variant = NBNativeEditorActions.edited_value(entry.value, path, new_value)
	if entry in effect.user_parameters and entry.target_type in PARAMETER_TYPES and NBNativeUserParameters.typed_value(entry.target_type, edited) == null:
		%Status.text = "Invalid %s default for %s." % [entry.target_type, entry.input_name]
		_sync_parameter_default(entry)
		return
	_edit_property(entry, "value", edited, "Edit " + entry.input_name, false)

func _sync_parameter_default(entry: Resource) -> void:
	# Both the system panel and the selected parameter details can display the
	# same declaration. Update the controls in place so edits retain focus.
	for container: Node in [%Parameters, %Details]:
		for row in container.get_children():
			if not row.has_meta("parameter_default") or row.get_meta("parameter_default") != entry:
				continue
			var value: Variant = entry.value
			if value is int or value is float:
				row.get_node("Number").set_value_no_signal(value)
			elif value is bool:
				row.get_node("Boolean").set_pressed_no_signal(value)
			elif value is Vector2 or value is Vector3 or value is Vector4 or value is Color:
				var channels := row.get_node("Children")
				for axis in channels.get_child_count():
					channels.get_child(axis).get_child(1).set_value_no_signal(value[axis])

func _parameter_default_changed(entry: Resource) -> void:
	_sync_parameter_default(entry)
	_document_changed(false)

func _edit_property(target: Resource, property: String, value: Variant, action: String, rebuild: bool = true) -> void:
	if property == "display_name" and value is String:
		value = value.strip_edges()
		if value.is_empty():
			%Status.text = "Names cannot be empty."
			return
	_edit_properties(target, {property: value}, action, rebuild)

func _edit_properties(target: Resource, changes: Dictionary, action: String, rebuild: bool = true) -> void:
	var changed := _document_changed.bind(rebuild)
	if not rebuild and effect != null and target in effect.user_parameters and changes.has("value"):
		changed = _parameter_default_changed.bind(target)
	if NBNativeEditorActions.edit_properties(history, target, changes, action, changed):
		_update_title()

func _document_changed(rebuild: bool = true) -> void:
	_invalidate_fingerprint()
	if effect != null:
		effect.emit_changed()
	if rebuild:
		_refresh_all()
	else:
		_update_title()
		_refresh_curve_plots()
		_build_tracks()
	_restart()
	if not _restoring:
		%RecoveryTimer.start()

func _refresh_curve_plots() -> void:
	var remaining: Array[Dictionary] = []
	for info in _curve_plots:
		if not is_instance_valid(info.plot) or info.plot.is_queued_for_deletion():
			continue
		var value: Variant = info.entry.value
		for key in info.path:
			value = value[key]
		info.plot.set_curve(value)
		remaining.append(info)
	_curve_plots = remaining

func _show_module_menu(stage: Resource) -> void:
	module_stage = stage
	selected_stage = stage
	%ModuleDialog.title = "Add module · " + stage.stage_name
	%ModuleSearch.text = ""
	_populate_module_menu()
	%ModuleDialog.popup_centered(Vector2i(480, 420))

func _populate_module_menu() -> void:
	_clear(%ModuleOptions)
	if module_stage == null:
		return
	for descriptor in Registry.list_for_stage(module_stage.stage_name):
		if not descriptor.name.to_lower().contains(%ModuleSearch.text.to_lower()):
			continue
		var button := Button.new()
		button.text = descriptor.name
		button.alignment = HORIZONTAL_ALIGNMENT_LEFT
		button.pressed.connect(_add_module.bind(descriptor.name))
		%ModuleOptions.add_child(button)

func _add_module(name: String) -> void:
	var module = Registry.create_module(name)
	if module == null or module_stage == null or not _contains_stage(module_stage):
		return
	var operations: Array[Resource] = module_stage.operations.duplicate()
	operations.append(module)
	_edit_property(module_stage, "operations", operations, "Add " + name)
	%ModuleDialog.hide()
	_select_module(module)

func _selected_stage() -> Resource:
	return NBNativeEditorActions.selected_module_stage(selected_emitter if selected_emitter != null else effect, selected_module)

func _duplicate_module() -> void:
	var stage := _selected_stage()
	if stage == null:
		return
	var copy: Resource = _clone_authored(selected_module)
	var operations: Array[Resource] = stage.operations.duplicate()
	operations.insert(operations.find(selected_module) + 1, copy)
	_edit_property(stage, "operations", operations, "Duplicate module")
	_select_module(copy)

func _remove_module() -> void:
	var stage := _selected_stage()
	if stage == null:
		return
	var operations: Array[Resource] = stage.operations.duplicate()
	operations.erase(selected_module)
	selected_module = null
	_edit_property(stage, "operations", operations, "Remove module")

func _add_renderer(renderer_type: String = "SpriteRenderer") -> void:
	if selected_emitter == null:
		return
	var renderer: Resource=NBNativeWorkspaceDocument.new_renderer(renderer_type, DOCUMENT_SCHEMAS)
	var renderers: Array[Resource] = selected_emitter.renderers.duplicate()
	renderers.append(renderer)
	_edit_property(selected_emitter, "renderers", renderers, "Add " + _human_title(renderer_type))
	_show_renderer(renderer)

func _set_material(path: String) -> void:
	var resource: Resource = _load_text_asset(path)
	if not resource is Material:
		%Status.text = "Choose a Material saved as a project .tres resource."
		return
	_assign_material(material_renderer, material_slot, resource)

func _move_module(stage: Resource, index: int, offset: int) -> void:
	var next: Array[Resource] = stage.operations.duplicate()
	var module: Resource = next[index]
	next.remove_at(index)
	next.insert(index + offset, module)
	_edit_property(stage, "operations", next, "Move module")

func can_drop_module(data: Variant, target_stage: Resource) -> bool:
	if not data is Dictionary or data.get("type") != "niagara_module" or data.get("workspace") != self or target_stage == null:
		return false
	var source_stage: Resource = data.get("stage")
	var module: Resource = data.get("module")
	if source_stage == null or module == null or module not in source_stage.operations or not _contains_stage(source_stage) or not _contains_stage(target_stage):
		return false
	var reason: String = Registry.validate_stage(module, target_stage.stage_name, source_stage.stage_name)
	if not reason.is_empty():
		%Status.text = reason
		return false
	return true

func _contains_stage(stage: Resource) -> bool:
	return NBNativeEditorActions.owner_of_stage(effect, stage) != null

func move_module_to(module: Resource, source_stage: Resource, target_stage: Resource, index: int) -> bool:
	var data := {"type": "niagara_module", "workspace": self, "stage": source_stage, "module": module}
	if not can_drop_module(data, target_stage):
		return false
	var moved: Dictionary=NBNativeEditorActions.move_module(history, source_stage, target_stage, module, index, _document_changed)
	if not moved.changed:
		return true
	selected_stage = target_stage
	selected_module = module
	var owner: Resource = _owner_of_stage(target_stage)
	if owner != null:
		_graph_owner_selected(owner)
		selected_stage = target_stage
		selected_module = module
		_selection_kind = "module"
	_build_details()
	_refresh_selection_highlight()
	_update_title()
	%Status.text = "Moved " + module.module_name + " to " + _stage_title(target_stage.stage_name)
	return true

func _owner_of_stage(stage: Resource) -> Resource:
	return NBNativeEditorActions.owner_of_stage(effect, stage)

func copy_module() -> void:
	var stage := _selected_stage()
	if selected_module == null or stage == null:
		return
	_clipboard_text = ModuleClipboard.encode(selected_module, stage.stage_name)
	if DisplayServer.has_feature(DisplayServer.FEATURE_CLIPBOARD):
		DisplayServer.clipboard_set(_clipboard_text)
	%Status.text = "Copied " + selected_module.module_name + ". Paste into a compatible stage."

func paste_module(stage: Resource = null) -> bool:
	if stage == null:
		stage = selected_stage if selected_stage != null else _selected_stage()
	if stage == null or not _contains_stage(stage):
		%Status.text = "Select a target stage before pasting a module."
		return false
	var text: String = DisplayServer.clipboard_get() if DisplayServer.has_feature(DisplayServer.FEATURE_CLIPBOARD) else _clipboard_text
	var decoded: Dictionary = ModuleClipboard.decode(text)
	if decoded.has("error"):
		%Status.text = str(decoded.error)
		return false
	var module: Resource = decoded.module
	var reason: String = Registry.validate_stage(module, stage.stage_name, decoded.stage)
	if not reason.is_empty():
		%Status.text = reason
		return false
	module.stable_id = _new_id()
	var operations: Array[Resource] = stage.operations.duplicate()
	var index: int = operations.find(selected_module) + 1 if selected_module in operations else operations.size()
	operations.insert(index, module)
	_edit_property(stage, "operations", operations, "Paste module")
	_select_module(module)
	%Status.text = "Pasted " + module.module_name
	return true

func _clone_authored(source: Resource) -> Resource:
	return DocumentCopy.copy(source, true)

func duplicate_emitters() -> void:
	if effect == null or selected_emitters.is_empty():
		return
	var duplicated: Dictionary=NBNativeEditorActions.duplicate_emitters(effect, selected_emitters)
	_edit_property(effect, "emitters", duplicated.emitters, "Duplicate emitters")
	selected_emitters=duplicated.copies
	selected_emitter=selected_emitters.back()
	selected_module=null
	selected_stage=null
	_refresh_all()

func remove_emitters() -> void:
	if effect == null or selected_emitters.is_empty():
		return
	var emitters: Array[Resource] = effect.emitters.duplicate()
	for emitter in selected_emitters:
		emitters.erase(emitter)
	_edit_property(effect, "emitters", emitters, "Remove emitters")

func _show_emitter_rename() -> void:
	if selected_emitter == null:
		return
	%RenameEmitterDialog.get_node("Name").text = selected_emitter.display_name
	%RenameEmitterDialog.popup_centered()
	%RenameEmitterDialog.get_node("Name").grab_focus()
	%RenameEmitterDialog.get_node("Name").select_all()

func _commit_emitter_name() -> void:
	if selected_emitter == null:
		return
	var name: String = %RenameEmitterDialog.get_node("Name").text.strip_edges()
	if name.is_empty():
		%Status.text = "Emitter name cannot be empty."
		return
	_edit_property(selected_emitter, "display_name", name, "Rename emitter")

func _add_emitter(with_sprites: bool = false) -> void:
	if effect == null:
		return
	var emitter: Resource=NBNativeWorkspaceDocument.new_emitter(effect.emitters.size(), DOCUMENT_SCHEMAS)
	if with_sprites:
		var stack := {
			"EmitterUpdate": ["EmitterState", "SpawnRate"],
			"ParticleSpawn": ["V2/InitializeParticle", "AddVelocity"],
			"ParticleUpdate": ["ParticleState@1.1", "SolveForcesAndVelocity"],
		}
		for stage_name: String in stack:
			var stage: Resource = emitter.get_stage(stage_name)
			for module_name: String in stack[stage_name]:
				stage.operations.append(Registry.create_module(module_name))
		emitter.renderers.append(NBNativeWorkspaceDocument.new_renderer("SpriteRenderer", DOCUMENT_SCHEMAS))
	var emitters: Array[Resource] = effect.emitters.duplicate()
	emitters.append(emitter)
	_edit_property(effect, "emitters", emitters, "Add emitter")
	_select_emitter(emitter)

func _new_id() -> String:
	return NBNativeWorkspaceDocument.new_id()

func _build_tracks() -> void:
	_timeline_rows = TimelineModel.build(effect)
	%"Emitter Tracks".get_node("Scroll/Timeline").set_rows(_timeline_rows)
	%"Emitter Tracks".get_node("Scroll/Timeline").set_range(%Duration.value)
	_update_timeline_selection()

func _selected_timeline_row() -> Dictionary:
	for row in _timeline_rows:
		if row.emitter == selected_emitter:
			return row
	return {}

func _update_timeline_selection() -> void:
	%"Emitter Tracks".get_node("Scroll/Timeline").set_selection(selected_emitters)
	var row: Dictionary = _selected_timeline_row()
	%"Emitter Tracks".get_node("DurationControls").visible = not row.is_empty()
	var duration: SpinBox = %"Emitter Tracks".get_node("DurationControls/LoopDuration")
	duration.editable = not row.is_empty() and row.editable
	%"Emitter Tracks".get_node("DurationControls/Apply").disabled = not duration.editable
	if row.is_empty():
		%"Emitter Tracks".get_node("Lifecycle").text = "Select an emitter. Click the ruler or a bar to seek."
		return
	var authored: Variant = row.state.get_input("LoopDuration", 1.0) if row.state != null else 1.0
	if authored is int or authored is float:
		duration.set_value_no_signal(float(authored))
	%"Emitter Tracks".get_node("Lifecycle").text = row.emitter.display_name + " · " + row.caption + (" · add Emitter State to edit" if row.state == null else "") + ". Particles may outlive a spawning cycle."

func _apply_loop_duration() -> void:
	var row: Dictionary = _selected_timeline_row()
	if row.is_empty() or not row.editable:
		return
	var value: float = %"Emitter Tracks".get_node("DurationControls/LoopDuration").value
	if row.duration_input != null:
		_edit_property(row.duration_input, "value", value, "Change emitter loop duration")
	else:
		var entry = InputResource.new()
		entry.input_name = "LoopDuration"
		entry.value = value
		entry.provenance = "authored"
		var inputs: Array[Resource] = row.state.inputs.duplicate()
		inputs.append(entry)
		_edit_property(row.state, "inputs", inputs, "Set emitter loop duration")

func _set_preview_range(value: float) -> void:
	%TimeSlider.max_value = value
	%"Emitter Tracks".get_node("Scroll/Timeline").set_range(value)

func _ensure_player() -> void:
	if is_instance_valid(player):
		return
	var path := "res://addons/angara_fx/runtime/effect_player.gd"
	if not ResourceLoader.exists(path):
		%PreviewStatus.text = "Runtime is not available yet; document editing is active."
		return
	var script = load(path)
	if script == null or not script.can_instantiate():
		return
	player = script.new()
	player._authoring_preview = true
	player.autoplay = false
	%World.add_child(player)

func _restart() -> void:
	preview_time = 0.0
	%TimeSlider.set_value_no_signal(0.0)
	_ensure_player()
	if is_instance_valid(player):
		player.effect = effect
		player.restart()
	_update_transport()
	_update_diagnostics()

func _toggle_play() -> void:
	is_playing = not is_playing
	_update_transport()

func _step() -> void:
	is_playing = false
	if is_instance_valid(player):
		player.advance(1.0 / 60.0)
		preview_time = player.simulation_time
	_update_transport()

func _seek(value: float) -> void:
	if _refreshing:
		return
	preview_time = value
	if is_instance_valid(player):
		# Pixel coordinates are float32; snap to the nearest simulation frame.
		player.seek(snappedf(value, maxf(player.fixed_step, 0.001)))
		preview_time = player.simulation_time
	_update_transport()
	_update_diagnostics()

func _process(delta: float) -> void:
	if not is_visible_in_tree() or effect == null or not is_instance_valid(player):
		return
	if is_playing:
		player.advance(minf(delta, 0.1))
	preview_time = player.simulation_time
	if is_playing and preview_time >= %Duration.value:
		if %Loop.button_pressed:
			_restart()
		else:
			is_playing = false
	_update_transport()

func _update_transport() -> void:
	%PlayPause.text = "Ⅱ Pause" if is_playing else "▶ Play"
	%TimeLabel.text = "%.3f s" % preview_time
	%TimeSlider.set_value_no_signal(preview_time)
	%"Emitter Tracks".get_node("Scroll/Timeline").set_time(preview_time)
	if is_instance_valid(player):
		%PreviewStatus.text = "%s particles · %.3f s · %s\nRight drag: orbit · Wheel: zoom" % [player.get_particle_count(), preview_time, _preview_backend_label()]

func _preview_backend_label() -> String:
	var backends: Array[String] = []
	if player.has_method("get_backend_statistics"):
		for entry: Dictionary in player.get_backend_statistics():
			var label: String = {"cpu": "CPU", "cpu_fallback": "CPU fallback", "gpu_compute": "GPU compute + CPU readback/render"}.get(entry.backend, str(entry.backend))
			if label not in backends:
				backends.append(label)
	return " / ".join(backends) if not backends.is_empty() else "No active preview backend"

func _picker_opening(picker: FileDialog) -> void:
	_picker_signatures.erase(picker.get_instance_id())
	_picker_lists[picker.get_instance_id()] = []
	_clear_picker_tooltips(picker, _picker_lists[picker.get_instance_id()])
	%PickerTooltipTimer.start()
	call_deferred("_refresh_picker_tooltips")

func _owned_file_dialogs() -> Array[FileDialog]:
	var pickers: Array[FileDialog] = [%OpenDialog, %SaveDialog, %PlayerSceneDialog, %MaterialDialog, %MeshDialog]
	for extension in _workspace_extensions:
		pickers.append_array(extension.get_file_dialogs())
	return pickers

func _clear_picker_tooltips(node: Node, lists: Array) -> void:
	if node is Control:
		node.tooltip_text = ""
	if node is ItemList:
		lists.append(node)
		for index in range(node.item_count):
			node.set_item_tooltip_enabled(index, false)
	for child in node.get_children(true):
		_clear_picker_tooltips(child, lists)

func _refresh_picker_tooltips() -> void:
	# Only owned open pickers are checked. No per-frame traversal of editor UI.
	var active: bool = false
	for picker: FileDialog in _owned_file_dialogs():
		if not picker.visible:
			continue
		active = true
		var id: int = picker.get_instance_id()
		var signature: String = picker.current_dir + "|" + picker.current_file
		for list: ItemList in _picker_lists.get(id, []):
			if not is_instance_valid(list):
				continue
			var hovered: int = list.get_item_at_position(list.get_local_mouse_position(), true)
			if hovered >= 0:
				list.set_item_tooltip_enabled(hovered, false)
			signature += "|" + str(list.item_count)
			if list.item_count > 0:
				signature += list.get_item_text(0) + list.get_item_text(list.item_count - 1)
		if signature != _picker_signatures.get(id, ""):
			_picker_signatures[id] = signature
			_picker_lists[id] = []
			_clear_picker_tooltips(picker, _picker_lists[id])
	if not active:
		%PickerTooltipTimer.stop()

func frame_effect() -> bool:
	if not is_instance_valid(player):
		return false
	var result: Dictionary = PreviewFraming.snapshot_bounds(player.get_particle_snapshot())
	if not result.valid:
		%Status.text = "Frame effect: no live particles at this time. Play or seek to a visible part of the effect."
		return false
	_preview_target = PreviewFraming.frame(%PreviewCamera, result.bounds, Vector2(%PreviewViewport.size))
	%Status.text = "Framed current particle bounds, including sprite size."
	return true

func _preview_input(event: InputEvent) -> void:
	var camera: Camera3D = %PreviewCamera
	var target: Vector3 = _preview_target
	if event is InputEventMouseMotion and event.button_mask & MOUSE_BUTTON_MASK_RIGHT:
		var offset: Vector3 = (camera.global_position - target).rotated(Vector3.UP, -event.relative.x * 0.005)
		var pitched: Vector3 = offset.rotated(camera.global_basis.x.normalized(), -event.relative.y * 0.005)
		if absf(pitched.normalized().dot(Vector3.UP)) < 0.97:
			offset = pitched
		camera.global_position = target + offset
		camera.look_at(target, Vector3.UP)
	elif event is InputEventMouseButton and event.pressed:
		if event.button_index == MOUSE_BUTTON_WHEEL_UP:
			camera.global_position = target + (camera.global_position - target).normalized() * maxf(camera.global_position.distance_to(target) * 0.9, 0.005)
		elif event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			camera.global_position = target + (camera.global_position - target) * 1.1

func _update_diagnostics() -> void:
	%Diagnostics.clear()
	if effect == null:
		%Diagnostics.append_text("Create or open an effect.")
		return
	var entries: Array = effect.diagnostics.duplicate()
	if is_instance_valid(player):
		entries.append_array(player.diagnostics)
	if entries.is_empty():
		%Diagnostics.append_text("No reported diagnostics.")
	var shown: Dictionary = {}
	for diagnostic in entries:
		var severity: String = str(diagnostic.get("severity", "info")).to_upper()
		var message: String = str(diagnostic.get("message", diagnostic.get("reason", diagnostic)))
		var context: String = str(diagnostic.get("emitter", "")) + " / " + str(diagnostic.get("module", ""))
		var line: int = int(diagnostic.get("line", 0))
		var text := "%s  %s%s  %s\n" % [severity, context, (" :%s" % line) if line > 0 else "", message]
		if not shown.has(text):
			%Diagnostics.add_text(text)
			shown[text] = true

func _cache_session_layout() -> void:
	# During plugin/editor shutdown, children may already have left the tree.
	# Keep the last live layout while still preserving the authored document.
	for key: String in {"columns": "%Columns", "work": "%WorkSplit", "main": "%CenterSplit"}:
		var path: String = {"columns": "%Columns", "work": "%WorkSplit", "main": "%CenterSplit"}[key]
		var splitter: SplitContainer = get_node_or_null(path) as SplitContainer
		if is_instance_valid(splitter):
			_session_layout[key] = splitter.split_offset
	var overview = get_node_or_null("%Overview")
	if is_instance_valid(overview):
		_session_layout.zoom = overview.zoom
		_session_layout.pan = overview.pan

func preserve_session() -> bool:
	if not is_node_ready() or _restoring:
		return true
	_cache_session_layout()
	var saved: Dictionary=NBNativeWorkspaceDocument.preserve_session(effect, {"document_path":document_path,"dirty":is_dirty(),"last_import_plan":_last_import_plan,"recovery_file":_recovery_file,"recovery_version":_recovery_version,"recovery_fingerprint":_recovery_fingerprint,"current_fingerprint":_document_fingerprint(),"history_version":history.get_version()}, _session_layout, SESSION_PATH)
	_recovery_file=saved.recovery_file
	_recovery_version=saved.recovery_version
	_recovery_fingerprint=saved.recovery_fingerprint
	if not saved.ok:
		return _recovery_failed(saved.error)
	recovery_error=""
	if Engine.has_meta(RECOVERY_MEMORY_KEY) and Engine.get_meta(RECOVERY_MEMORY_KEY).get("effect")==effect:
		Engine.remove_meta(RECOVERY_MEMORY_KEY)
	return true

func _recovery_failed(message: String) -> bool:
	recovery_error = message
	if effect != null and is_dirty():
		Engine.set_meta(RECOVERY_MEMORY_KEY, {"effect": effect, "path": document_path})
	var status: Label = get_node_or_null("%Status") as Label
	if is_instance_valid(status):
		status.text = "RECOVERY FAILED — " + message + ". Save the effect to another location before closing."
	push_error("Angara FX: " + message)
	return false

func _restore_session() -> void:
	var memory: Dictionary=Engine.get_meta(RECOVERY_MEMORY_KEY,{})
	var restored: Dictionary=NBNativeWorkspaceDocument.restore_session(SESSION_PATH,RECOVERY_PATH,Effect,memory)
	if not restored.available:
		return
	_restoring=true
	if restored.kind=="memory":
		_load_document(restored.effect,restored.path)
		saved_version=-1
		_saved_fingerprint=""
		_restoring=false
		%Status.text="Restored unsaved effect from this editor session after recovery write failure. Save to a writable location."
		return
	_last_import_plan=restored.last_import_plan
	%Columns.split_offset=restored.layout.columns
	%WorkSplit.split_offset=restored.layout.work
	%CenterSplit.split_offset=restored.layout.main
	if restored.kind in ["recovery","saved"]:
		_load_document(restored.effect,restored.path)
		if restored.kind=="recovery":
			saved_version=-1
			_saved_fingerprint=""
			_recovery_file=restored.recovery_file
			_recovery_version=history.get_version()
			_recovery_fingerprint=_document_fingerprint()
			%Status.text="Recovered unsaved changes from a text resource. Save to keep them in the project."
	%Overview.set_navigation(restored.layout.zoom,restored.layout.pan)
	_restoring=false

func _unhandled_key_input(event: InputEvent) -> void:
	if not is_visible_in_tree() or not event is InputEventKey or not event.pressed or event.echo:
		return
	if event.ctrl_pressed or event.meta_pressed:
		if event.keycode == KEY_S:
			save_document()
			get_viewport().set_input_as_handled()
		elif event.keycode == KEY_Z:
			if event.shift_pressed:
				history.redo()
			else:
				history.undo()
			_refresh_all()
			get_viewport().set_input_as_handled()
		elif event.keycode in [KEY_C, KEY_V, KEY_D]:
			var focus: Control = get_viewport().gui_get_focus_owner()
			if focus is LineEdit or focus is TextEdit:
				return
			if event.keycode == KEY_C:
				copy_module()
			elif event.keycode == KEY_V:
				paste_module()
			elif selected_module != null:
				_duplicate_module()
			else:
				duplicate_emitters()
			get_viewport().set_input_as_handled()
