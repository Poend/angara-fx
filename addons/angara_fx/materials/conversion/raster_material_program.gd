@tool
extends RefCounted
const BUILTINS = {"PROJECTION_MATRIX":"mat4", "INV_PROJECTION_MATRIX":"mat4", "VIEW_MATRIX":"mat4", "INV_VIEW_MATRIX":"mat4", "MODEL_MATRIX":"mat4", "MODELVIEW_MATRIX":"mat4", "MODEL_NORMAL_MATRIX":"mat3", "MODELVIEW_NORMAL_MATRIX":"mat3", "VIEWPORT_SIZE":"vec2", "nb_instance_color":"vec4", "INSTANCE_CUSTOM":"vec4", "INSTANCE_ID":"int"}
const Program = preload("material_program.gd")
const SystemProgram = preload("system_material_program.gd")
static func source_key(material: ShaderMaterial) -> String:return NBNativeRasterProgram.source_key(material)
static func lower(compiled: Dictionary,batch: bool,epilogue: String="out_color=vec4(ALBEDO,ALPHA);",extra_header: String="",vertex_epilogue: String="") -> Dictionary:return NBNativeRasterProgram.lower(compiled,batch,epilogue,extra_header,vertex_epilogue)
static func uniform_bytes(program: Dictionary,values: Dictionary) -> PackedByteArray:return NBNativeRasterBuffers.uniform_bytes(program,values)
static func geometry_bytes(arrays: Array) -> PackedByteArray:return NBNativeRasterBuffers.geometry_bytes(arrays)
