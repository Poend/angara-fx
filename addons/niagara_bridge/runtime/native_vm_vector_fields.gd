@tool
extends RefCounted
const Field=preload("../document/nb_vector_field.gd")
const Schema=preload("../compiler/native_vm_program.gd")
const SIGNATURES={"SampleField":[3,3],"LoadField":[3,3],"FieldDimensions":[0,3],"FieldTilingAxes":[0,3],"FieldBounds":[0,6]}

static func bind(info: Dictionary, function: Dictionary) -> Dictionary:
	var result: Dictionary={"errors":[],"binding":{}}
	var name: String=function.get("name","")
	if not SIGNATURES.has(name) or int(function.num_inputs)!=SIGNATURES[name][0] or int(function.num_outputs)!=SIGNATURES[name][1]:
		result.errors.append("Unsupported vector field VM signature.");return result
	for key: String in ["registered_parameter_map_read","registered_parameter_map_write","name","compile_name"]:
		if str(info.get(key,"")).begins_with("User."):result.errors.append("Live User vector field overrides are not integrated.")
	var data: Dictionary=info.get("vector_field",{})
	if data.get("producer")!="UVectorFieldStatic CPU lattice; original CPU access and DI tiling flags" or data.get("static")!=true:
		result.errors.append("Native static vector field metadata is unavailable.");return result
	if not data.get("size") is Array or data["size"].size()!=3 or not data["size"].all(func(v):return Schema._uint(v) and int(v)>0):result.errors.append("Invalid native vector field dimensions.")
	if not data.get("tiling") is Array or data.tiling.size()!=3 or not data.tiling.all(func(v):return v is bool):result.errors.append("Invalid vector field tiling flags.")
	if not data.get("allow_cpu_access") is bool or not data.get("has_cpu_data") is bool:result.errors.append("Missing original vector field CPU access policy.")
	for key: String in ["bounds_min","bounds_max"]:
		if not data.get(key) is Array or data[key].size()!=3 or not data[key].all(func(v):return (v is int or v is float) and is_finite(float(v))):result.errors.append("Invalid native vector field bounds.")
	if not result.errors.is_empty():return result
	var field:=Field.new();field.size=Vector3i(int(data["size"][0]),int(data["size"][1]),int(data["size"][2]))
	field.bounds_min=PackedFloat64Array(data.bounds_min);field.bounds_max=PackedFloat64Array(data.bounds_max)
	field.source_path=data.get("source_path","");field.allow_cpu_access=data.allow_cpu_access and data.has_cpu_data
	if field.allow_cpu_access:
		if not data.get("vectors_xyz") is Array or not data.vectors_xyz.all(func(v):return (v is int or v is float) and is_finite(float(v))):result.errors.append("Invalid vector field CPU lattice.");return result
		field.vectors_xyz=PackedFloat32Array(data.vectors_xyz)
		var invalid: String=field.validation_error()
		if not invalid.is_empty():result.errors.append(invalid);return result
	result.binding={"kind":"vector_field","function":name,"field":field,"tiling":Vector3i(1 if data.tiling[0] else 0,1 if data.tiling[1] else 0,1 if data.tiling[2] else 0)}
	return result

static func sample(binding: Dictionary, inputs: Array) -> Array:
	var field: Resource=binding.field
	match binding.function:
		"SampleField":
			var v: Vector3=field.sample_cpu(Vector3(inputs[0],inputs[1],inputs[2]),binding.tiling)
			return [v.x,v.y,v.z]
		"LoadField":
			if not field.allow_cpu_access:return [0.0,0.0,0.0]
			var p:=Vector3i(int(inputs[0]),int(inputs[1]),int(inputs[2]))
			if p.x<0 or p.y<0 or p.z<0 or p.x>=field.size.x or p.y>=field.size.y or p.z>=field.size.z:return [0.0,0.0,0.0]
			return [field._value(p.x,p.y,p.z,0),field._value(p.x,p.y,p.z,1),field._value(p.x,p.y,p.z,2)]
		"FieldDimensions":return [float(field.size.x),float(field.size.y),float(field.size.z)]
		"FieldTilingAxes":return [float(binding.tiling.x),float(binding.tiling.y),float(binding.tiling.z)]
		"FieldBounds":return Array(field.bounds_min)+Array(field.bounds_max)
	return []
