@tool
extends Button

const DragPreview = preload("res://addons/niagara_bridge/editor/drag_preview.tscn")
var workspace: Control
var stage: Resource
var module: Resource
var _drop_valid: bool = false
var _drop_after: bool = false

func _get_drag_data(_at_position: Vector2) -> Variant:
	if module == null or stage == null:
		return null
	var preview = DragPreview.instantiate()
	preview.get_node("Label").text = module.module_name
	set_drag_preview(preview)
	return {"type": "niagara_module", "workspace": workspace, "stage": stage, "module": module}

func _can_drop_data(at_position: Vector2, data: Variant) -> bool:
	_drop_valid = is_instance_valid(workspace) and workspace.can_drop_module(data, stage)
	_drop_after = at_position.y > size.y * 0.5
	queue_redraw()
	return _drop_valid

func _drop_data(at_position: Vector2, data: Variant) -> void:
	if not workspace.can_drop_module(data, stage):
		return
	var index: int = stage.operations.find(module) if module != null else stage.operations.size()
	if module != null and at_position.y > size.y * 0.5:
		index += 1
	workspace.move_module_to(data.module, data.stage, stage, index)
	_drop_valid = false
	queue_redraw()

func _notification(what: int) -> void:
	if what == NOTIFICATION_DRAG_END or what == NOTIFICATION_MOUSE_EXIT:
		_drop_valid = false
		queue_redraw()

func _draw() -> void:
	if _drop_valid and get_viewport().gui_is_dragging():
		var y: float = size.y - 2 if _drop_after else 1
		draw_line(Vector2(0, y), Vector2(size.x, y), Color("f0c879"), 2.0)
