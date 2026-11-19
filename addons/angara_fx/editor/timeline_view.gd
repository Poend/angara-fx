@tool
extends Control
## Draws emitter spawning cycles; particles may remain alive after each cycle.
signal emitter_selected(emitter: Resource)
signal seek_started
signal seek_requested(time: float)

const RULER_HEIGHT: float = 28.0
const ROW_HEIGHT: float = 30.0
var rows: Array[Dictionary] = []
var range_end: float = 5.0
var playhead: float = 0.0
var selection: Array[Resource] = []
var _scrubbing: bool = false

func set_rows(value: Array[Dictionary]) -> void:
	rows = value
	custom_minimum_size.y = RULER_HEIGHT + ROW_HEIGHT * maxi(rows.size(), 1)
	queue_redraw()

func set_selection(value: Array[Resource]) -> void:
	selection = value
	queue_redraw()

func set_time(value: float) -> void:
	if not is_equal_approx(value, playhead):
		playhead = value
		queue_redraw()

func set_range(value: float) -> void:
	range_end = maxf(value, 0.1)
	queue_redraw()

func label_width() -> float:
	return clampf(size.x * 0.28, 82.0, 150.0)

func time_to_x(value: float) -> float:
	return label_width() + clampf(value / range_end, 0.0, 1.0) * maxf(size.x - label_width() - 8.0, 1.0)

func x_to_time(value: float) -> float:
	return clampf((value - label_width()) / maxf(size.x - label_width() - 8.0, 1.0), 0.0, 1.0) * range_end

func _gui_input(event: InputEvent) -> void:
	if event is InputEventMouseButton and event.button_index == MOUSE_BUTTON_LEFT:
		if event.pressed:
			var index: int = int(floor((event.position.y - RULER_HEIGHT) / ROW_HEIGHT))
			if event.position.y >= RULER_HEIGHT and index >= 0 and index < rows.size():
				emitter_selected.emit(rows[index].emitter)
			if event.position.x >= label_width():
				_scrubbing = true
				seek_started.emit()
				seek_requested.emit(x_to_time(event.position.x))
		else:
			_scrubbing = false
		accept_event()
	elif event is InputEventMouseMotion and _scrubbing:
		if not event.button_mask & MOUSE_BUTTON_MASK_LEFT:
			_scrubbing = false
			return
		seek_requested.emit(x_to_time(event.position.x))
		accept_event()

func _draw() -> void:
	var font: Font = get_theme_default_font()
	var font_size: int = maxi(11, get_theme_default_font_size() - 1)
	var left: float = label_width()
	var width: float = maxf(size.x - left - 8.0, 1.0)
	draw_rect(Rect2(Vector2.ZERO, size), Color("13171d"))
	draw_string(font, Vector2(7, 19), "EMITTER / s", HORIZONTAL_ALIGNMENT_LEFT, left - 10, font_size, Color("8995a3"))
	var tick_count: int = maxi(1, int(width / 68.0))
	var raw_step: float = range_end / float(tick_count)
	var magnitude: float = pow(10.0, floor(log(raw_step) / log(10.0)))
	var tick_step: float = magnitude
	for multiplier: float in [1.0, 2.0, 5.0, 10.0]:
		if magnitude * multiplier >= raw_step:
			tick_step = magnitude * multiplier
			break
	for index in range(int(floor(range_end / tick_step)) + 1):
		var time: float = float(index) * tick_step
		var x: float = time_to_x(time)
		draw_line(Vector2(x, 22), Vector2(x, size.y), Color("2c3540"))
		var label: String = ("%.2f" % time).trim_suffix("0").trim_suffix(".") if tick_step < 1.0 else str(int(time))
		var label_size: float = font.get_string_size(label, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size).x
		var label_x: float = clampf(x - label_size * 0.5, left + 2.0, maxf(left + 2.0, size.x - label_size - 2.0))
		draw_string(font, Vector2(label_x, 17), label, HORIZONTAL_ALIGNMENT_LEFT, label_size + 1.0, font_size, Color("a7b2bd"))
	for index in range(rows.size()):
		var row: Dictionary = rows[index]
		var y: float = RULER_HEIGHT + ROW_HEIGHT * index
		if row.emitter in selection:
			draw_rect(Rect2(0, y, size.x, ROW_HEIGHT), Color(0.28, 0.24, 0.14, 0.5))
		var name_color: Color = Color("d4dce5") if row.enabled else Color("727b85")
		draw_string(font, Vector2(7, y + 20), row.emitter.display_name, HORIZONTAL_ALIGNMENT_LEFT, left - 14, font_size, name_color)
		if not row.known:
			draw_string(font, Vector2(left + 5, y + 20), row.caption, HORIZONTAL_ALIGNMENT_LEFT, width - 10, font_size, Color("d7aa65"))
			continue
		var duration: float = row.duration
		var color: Color = Color("95723b") if row.runnable else Color("5a5150")
		var cycles: int = int(ceil(range_end / duration)) if row.loop else 1
		# Sub-pixel loops are rendered as a continuous band instead of thousands of lines.
		if cycles > 256 or duration / range_end * width < 3.0:
			draw_rect(Rect2(left, y + 5, width, 20), color)
		else:
			for cycle in range(cycles):
				var begin: float = time_to_x(float(cycle) * duration)
				var end: float = time_to_x(float(cycle + 1) * duration)
				draw_rect(Rect2(begin + 1, y + 5, maxf(end - begin - 2, 1), 20), color)
		var caption: String = ("↻ " if row.loop else "") + String.num(duration, 3) + " s"
		if not row.runnable:
			caption += " · inactive"
		draw_string(font, Vector2(left + 6, y + 20), caption, HORIZONTAL_ALIGNMENT_LEFT, width - 12, font_size, Color("f2e5ca"))
		draw_line(Vector2(0, y + ROW_HEIGHT), Vector2(size.x, y + ROW_HEIGHT), Color("242c36"))
	var cursor_x: float = time_to_x(playhead)
	draw_line(Vector2(cursor_x, 21), Vector2(cursor_x, size.y), Color("efb859"), 2.0)
	draw_colored_polygon(PackedVector2Array([Vector2(cursor_x - 5, 19), Vector2(cursor_x + 5, 19), Vector2(cursor_x, 25)]), Color("efb859"))
