@tool
extends VBoxContainer

signal mesh_requested
signal mesh_cleared
signal remove_requested
signal settings_committed(changes: Dictionary)

var entry: Resource
var _initial_rotation: Vector3
var _initial_scale: Vector3
var _initial_pivot: Vector3
var _has_shear: bool = false

func _ready() -> void:
	$Actions/Choose.pressed.connect(func(): mesh_requested.emit())
	$Actions/Clear.pressed.connect(func(): mesh_cleared.emit())
	$Header/Remove.pressed.connect(func(): remove_requested.emit())
	$Expand.toggled.connect(func(expanded): $Fields.visible = expanded)
	$Fields/Apply.pressed.connect(_commit)

func configure(value: Resource, index: int, resource_label: String) -> void:
	entry = value
	$Header/Title.text = "Mesh %s" % index
	$Resource.text = resource_label
	$Actions/Clear.disabled = value.mesh == null
	$Fields/Source.text = "Source: " + (value.source_path if not value.source_path.is_empty() else "none")
	if value.mesh != null and not value.mesh.resource_path.is_empty(): $Fields/Source.text += "\nGodot: " + value.mesh.resource_path
	$Fields/Binding.text = value.user_parameter
	_initial_rotation = value.rotation.get_euler() * (180.0 / PI)
	var unrotated: Basis = Basis(value.rotation).inverse() * value.local_transform.basis
	_initial_scale = Vector3(unrotated.x.x, unrotated.y.y, unrotated.z.z)
	_has_shear = not unrotated.is_equal_approx(Basis.from_scale(_initial_scale))
	_set_vector($Fields/Rotation, _initial_rotation)
	_set_vector($Fields/Scale, _initial_scale)
	_set_vector($Fields/Pivot, value.pivot_offset)
	# Compare against displayed values, preserving higher-precision source data
	# when only another field changes.
	_initial_rotation = _vector($Fields/Rotation)
	_initial_scale = _vector($Fields/Scale)
	_initial_pivot = _vector($Fields/Pivot)
	var found: int = -1
	for option in range($Fields/Space.item_count):
		if $Fields/Space.get_item_text(option) == value.pivot_space:
			found = option
	if found == -1:
		$Fields/Space.add_item(value.pivot_space)
		found = $Fields/Space.item_count - 1
	$Fields/Space.select(found)
	$Fields/LOD.set_value_no_signal(value.lod_index)
	if _has_shear:
		$Fields/Hint.text = "This entry has a non-rotation/scale basis. It is preserved unless rotation or scale is explicitly changed. Runtime diagnostics remain authoritative."

func _set_vector(control: Node, value: Vector3) -> void:
	control.get_node("X").set_value_no_signal(value.x)
	control.get_node("Y").set_value_no_signal(value.y)
	control.get_node("Z").set_value_no_signal(value.z)

func _vector(control: Node) -> Vector3:
	# Commit the focused SpinBox text as well when Apply receives the click.
	for axis in ["X", "Y", "Z"]:
		if control.get_node(axis).get_line_edit().has_focus():
			control.get_node(axis).apply()
	return Vector3(control.get_node("X").value, control.get_node("Y").value, control.get_node("Z").value)

func _commit() -> void:
	if entry == null:
		return
	var binding: String = $Fields/Binding.text.strip_edges()
	if not binding.is_empty() and (not binding.begins_with("User.") or binding.length() <= 5):
		$Fields/Error.text = "Use the full User.* parameter name, or leave it empty."
		$Fields/Error.show()
		return
	var rotation_degrees := _vector($Fields/Rotation)
	var mesh_scale := _vector($Fields/Scale)
	var pivot := _vector($Fields/Pivot)
	if $Fields/LOD.get_line_edit().has_focus():
		$Fields/LOD.apply()
	if not rotation_degrees.is_finite() or not mesh_scale.is_finite() or not pivot.is_finite():
		$Fields/Error.text = "Transform values must be finite numbers."
		$Fields/Error.show()
		return
	var changes: Dictionary = {
		"user_parameter": binding,
		"pivot_space": $Fields/Space.get_item_text($Fields/Space.selected),
		"lod_index": int($Fields/LOD.value),
	}
	if pivot != _initial_pivot:
		changes["pivot_offset"] = pivot
	# Avoid rewriting exact imported quaternions/bases on a pivot-only edit.
	if rotation_degrees != _initial_rotation or mesh_scale != _initial_scale:
		var rotation := Quaternion.from_euler(rotation_degrees * (PI / 180.0))
		changes["rotation"] = rotation
		changes["local_transform"] = Transform3D(Basis(rotation) * Basis.from_scale(mesh_scale), entry.local_transform.origin)
	$Fields/Error.hide()
	settings_committed.emit(changes)
