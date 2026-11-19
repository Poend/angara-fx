@tool
extends Control
## Edits authored key positions. Interpolation and tangents remain in the resource.
signal curve_committed(value: Dictionary)
var curve_value: Dictionary = {}
var _drag_key: int = -1
var _range := Rect2(0, 0, 1, 1)
var _dragged: bool = false

func set_curve(value: Dictionary) -> void:
	curve_value = value.duplicate(true)
	_update_range()
	queue_redraw()

func _update_range() -> void:
	var min_time: float = 0.0
	var max_time: float = 1.0
	var min_value: float = 0.0
	var max_value: float = 1.0
	for key in curve_value.get("keys", []):
		min_time = minf(min_time, float(key.get("time", 0.0)))
		max_time = maxf(max_time, float(key.get("time", 0.0)))
		min_value = minf(min_value, float(key.get("value", 0.0)))
		max_value = maxf(max_value, float(key.get("value", 0.0)))
	var padding: float = maxf((max_value - min_value) * 0.12, 0.1)
	_range = Rect2(min_time, min_value - padding, maxf(max_time - min_time, 0.01), max_value - min_value + padding * 2.0)

func _chart_rect() -> Rect2:
	return Rect2(30, 12, maxf(size.x - 44.0, 1.0), maxf(size.y - 38.0, 1.0))

func _to_screen(time: float, value: float) -> Vector2:
	var chart := _chart_rect()
	return Vector2(chart.position.x + (time - _range.position.x) / _range.size.x * chart.size.x, chart.end.y - (value - _range.position.y) / _range.size.y * chart.size.y)

func _from_screen(point: Vector2) -> Vector2:
	var chart := _chart_rect()
	return Vector2(_range.position.x + (point.x - chart.position.x) / chart.size.x * _range.size.x, _range.position.y + (chart.end.y - point.y) / chart.size.y * _range.size.y)

func _draw() -> void:
	draw_style_box(get_theme_stylebox("panel", "Panel"), Rect2(Vector2.ZERO, size))
	var chart := _chart_rect()
	for i in 5:
		var fraction: float = float(i) / 4.0
		draw_line(Vector2(chart.position.x, chart.position.y + chart.size.y * fraction), Vector2(chart.end.x, chart.position.y + chart.size.y * fraction), Color(0.24, 0.27, 0.32))
		draw_line(Vector2(chart.position.x + chart.size.x * fraction, chart.position.y), Vector2(chart.position.x + chart.size.x * fraction, chart.end.y), Color(0.20, 0.23, 0.28))
	var keys: Array = curve_value.get("keys", [])
	if keys.is_empty():
		return
	for i in keys.size() - 1:
		var a: Dictionary = keys[i]
		var b: Dictionary = keys[i + 1]
		var previous := _to_screen(float(a.time), float(a.value))
		for step in range(1, 33):
			var t: float = float(step) / 32.0
			var time: float = lerpf(float(a.time), float(b.time), t)
			var value: float = lerpf(float(a.value), float(b.value), t)
			var interp: String = str(a.get("interp", "Linear"))
			if interp == "Constant":
				value = float(a.value)
			elif interp == "Cubic":
				var span: float = float(b.time) - float(a.time)
				value = (2 * t * t * t - 3 * t * t + 1) * float(a.value) + (t * t * t - 2 * t * t + t) * float(a.get("leave", 0)) * span + (-2 * t * t * t + 3 * t * t) * float(b.value) + (t * t * t - t * t) * float(b.get("arrive", 0)) * span
			var point := _to_screen(time, value)
			draw_line(previous, point, Color("d5aa66"), 2.0, true)
			previous = point
	for key in keys:
		draw_circle(_to_screen(float(key.time), float(key.value)), 4.5, Color("efce91"))
	var font := get_theme_default_font()
	var label_size: int = maxi(11, roundi(get_theme_default_font_size() * 0.85))
	draw_string(font, Vector2(chart.position.x, size.y - 4), "%.2f" % _range.position.x, HORIZONTAL_ALIGNMENT_LEFT, -1, label_size, Color("9ba7b5"))
	draw_string(font, Vector2(chart.end.x - 32, size.y - 4), "%.2f" % _range.end.x, HORIZONTAL_ALIGNMENT_LEFT, -1, label_size, Color("9ba7b5"))

func _gui_input(event: InputEvent) -> void:
	var keys: Array = curve_value.get("keys", [])
	if event is InputEventMouseButton and event.button_index == MOUSE_BUTTON_LEFT:
		if event.pressed:
			_drag_key = -1
			_dragged = false
			for i in keys.size():
				if _to_screen(float(keys[i].time), float(keys[i].value)).distance_to(event.position) < 12.0:
					_drag_key = i
					accept_event()
		else:
			if _drag_key >= 0 and _dragged:
				curve_committed.emit(curve_value.duplicate(true))
			_drag_key = -1
	elif event is InputEventMouseMotion and _drag_key >= 0:
		var value := _from_screen(event.position)
		var previous_time: float = float(keys[_drag_key - 1].time) + 0.0001 if _drag_key > 0 else _range.position.x
		var next_time: float = float(keys[_drag_key + 1].time) - 0.0001 if _drag_key < keys.size() - 1 else _range.end.x
		keys[_drag_key].time = clampf(value.x, previous_time, next_time)
		keys[_drag_key].value = value.y
		_dragged = true
		queue_redraw()
		accept_event()
