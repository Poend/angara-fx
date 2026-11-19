@tool
extends RefCounted
## Bind native external curve calls to this player's effective User parameters.
## Cached embedded curves remain source-owned; overrides never modify contracts.
const Curves=preload("curve_evaluator.gd")
const KINDS={1:"curve_di",2:"vector2_curve_di",3:"vector3_curve_di",4:"color_curve_di"}

static func parameter_names(interface: Dictionary, owner: String="") -> Array[String]:
	var result: Array[String]=[]
	for value: Variant in [owner,interface.get("name",""),interface.get("compile_name",""),interface.get("registered_parameter_map_read",""),interface.get("registered_parameter_map_write","")]:
		if value is String and value.begins_with("User.") and value not in result:result.append(value)
	return result

static func resolve(interface: Dictionary, owner: String, width: int, parameters: Dictionary) -> Dictionary:
	var result: Dictionary={"errors":[] as Array[String],"curve":interface.duplicate(true)}
	var names: Array[String]=parameter_names(interface,owner)
	if names.is_empty():return result
	if names.size()!=1:
		result.errors.append("Native curve has ambiguous runtime instance binding: "+str(names));return result
	var name: String=names[0]
	var value: Variant=parameters.get(name)
	if not value is Dictionary or value.get("kind")!=KINDS.get(width,""):
		result.errors.append("Native curve requires a typed runtime instance binding: "+name);return result
	if not value.get("errors",[]) is Array or not value.get("errors",[]).is_empty():
		result.errors.append("Invalid User curve "+name+": "+str(value.get("errors")));return result
	if not value.get("uses_lut") is bool or not value.get("channels") is Array or value.channels.size()!=width:
		result.errors.append("User curve channel layout differs from native call: "+name);return result
	for key: String in ["min_time","max_time","inv_range"]:
		var number: Variant=value.get(key)
		if not (number is float or number is int) or not is_finite(float(number)):result.errors.append("Invalid User curve time range: "+name)
	if value.uses_lut:
		var lut: Variant=value.get("lut")
		if not lut is PackedFloat32Array or lut.size()<width or lut.size()%width!=0:result.errors.append("Invalid User curve LUT: "+name)
		else:
			for sample: float in lut:
				if not is_finite(sample):result.errors.append("Nonfinite User curve LUT: "+name);break
	else:
		for channel: Variant in value.channels:
			if not channel is Dictionary or not channel.get("keys") is Array:result.errors.append("Invalid User curve rich-key data: "+name)
	if not result.errors.is_empty():return result
	result.curve["runtime_curve"]=value.duplicate(true)
	result.curve["runtime_parameter"]=name
	return result
