@tool
extends Control
## System overview navigation; emitter cards remain controls in an authored scene.
signal navigation_changed(zoom: float)
var zoom: float = 1.0
var pan := Vector2(16, 20)
var _panning: bool = false

func _ready() -> void:
	_apply_transform()

func _apply_transform() -> void:
	$Canvas.position = pan
	$Canvas.scale = Vector2.ONE * zoom
	navigation_changed.emit(zoom)
	queue_redraw()

func reset_view() -> void:
	zoom = 1.0
	pan = Vector2(16, 20)
	_apply_transform()

func frame_all() -> void:
	var extent: Vector2 = $Canvas/EmitterCards.size
	if extent.x <= 0 or extent.y <= 0:
		reset_view()
		return
	zoom = clampf(minf((size.x - 32) / extent.x, (size.y - 40) / extent.y), 0.35, 1.0)
	pan = Vector2(16, 20)
	_apply_transform()

func set_navigation(new_zoom: float, new_pan: Vector2) -> void:
	zoom = clampf(new_zoom, 0.35, 1.8)
	pan = new_pan
	_apply_transform()

func _input(event: InputEvent) -> void:
	if not is_visible_in_tree():
		_panning = false
		return
	if event is InputEventMouseButton:
		if event.button_index == MOUSE_BUTTON_MIDDLE and not event.pressed:
			_panning = false
		if not get_global_rect().has_point(event.position):
			return
		if event.button_index == MOUSE_BUTTON_MIDDLE:
			_panning = event.pressed
			get_viewport().set_input_as_handled()
		elif event.pressed and event.button_index in [MOUSE_BUTTON_WHEEL_UP, MOUSE_BUTTON_WHEEL_DOWN]:
			var cursor := get_local_mouse_position()
			var point: Vector2 = (cursor - pan) / zoom
			zoom = clampf(zoom * (1.12 if event.button_index == MOUSE_BUTTON_WHEEL_UP else 1.0 / 1.12), 0.35, 1.8)
			pan = cursor - point * zoom
			_apply_transform()
			get_viewport().set_input_as_handled()
	elif event is InputEventMouseMotion and _panning:
		pan += event.relative
		_apply_transform()
		get_viewport().set_input_as_handled()

func _draw() -> void:
	draw_rect(Rect2(Vector2.ZERO, size), Color("101318"))
	var spacing: float = 32.0 * zoom
	var offset := Vector2(fposmod(pan.x, spacing), fposmod(pan.y, spacing))
	for x in range(int(size.x / spacing) + 2):
		draw_line(Vector2(offset.x + x * spacing, 0), Vector2(offset.x + x * spacing, size.y), Color(0.12, 0.14, 0.17, 0.6))
	for y in range(int(size.y / spacing) + 2):
		draw_line(Vector2(0, offset.y + y * spacing), Vector2(size.x, offset.y + y * spacing), Color(0.12, 0.14, 0.17, 0.6))
