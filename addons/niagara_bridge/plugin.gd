@tool
extends EditorPlugin

const Workspace = preload("res://addons/niagara_bridge/editor/workspace.tscn")
const SavedEffects = preload("res://addons/niagara_bridge/runtime/saved_effects.gd")
var workspace: Control

func _enter_tree() -> void:
	resource_saved.connect(_resource_saved)
	workspace = Workspace.instantiate()
	EditorInterface.get_editor_main_screen().add_child(workspace)
	workspace.hide()
	workspace.set_editor_mode(true)

func _resource_saved(resource: Resource) -> void:
	var path := SavedEffects.document_path(resource)
	if not path.is_empty():
		var problem := SavedEffects.publish(resource, path)
		if not problem.is_empty():
			push_warning("Angara FX: %s: %s" % [path, problem])

func _exit_tree() -> void:
	resource_saved.disconnect(_resource_saved)
	if is_instance_valid(workspace):
		workspace.preserve_session()
		workspace.queue_free()
	workspace = null

func _has_main_screen() -> bool:
	return true

func _get_plugin_name() -> String:
	return "Angara FX"

func _get_plugin_icon() -> Texture2D:
	return EditorInterface.get_editor_theme().get_icon("GPUParticles3D", "EditorIcons")

func _make_visible(visible: bool) -> void:
	if is_instance_valid(workspace):
		workspace.visible = visible

func _handles(object: Object) -> bool:
	return object is Resource and object.get_script() == load("res://addons/niagara_bridge/document/nb_effect.gd")

func _edit(object: Object) -> void:
	if is_instance_valid(workspace) and _handles(object):
		workspace.request_open_resource(object)

func _save_external_data() -> void:
	if is_instance_valid(workspace):
		workspace.preserve_session()

func _get_unsaved_status(_for_scene: String) -> String:
	if is_instance_valid(workspace) and workspace.is_dirty():
		if not workspace.recovery_error.is_empty():
			return "Angara FX has unsaved changes and draft recovery failed. Save the effect to a writable location before closing: " + workspace.recovery_error
		return "Angara FX has unsaved effect changes. Save the effect, or restore its text recovery draft on the next plugin activation."
	return ""
