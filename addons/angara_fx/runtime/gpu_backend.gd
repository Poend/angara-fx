@tool
extends NBNativeGPUBackend
## C++ device lifecycle, dispatch, allocation, buffers and snapshot validation.
const Values=preload("res://addons/angara_fx/runtime/value_evaluator.gd")
const Kernel=preload("res://addons/angara_fx/runtime/gpu_shader_template.gd")
const Attributes=preload("res://addons/angara_fx/compiler/attribute_layout.gd")
const VectorFieldGPU=preload("res://addons/angara_fx/runtime/vector_field_gpu.gd")
const STRIDE_FLOATS: int=Kernel.STRIDE_VEC4*4
const STRIDE_BYTES: int=STRIDE_FLOATS*4
static var source_dispatch_counter: int:
    get:return NBNativeGPUBackend.get_source_dispatch_counter()
    set(value):NBNativeGPUBackend.set_source_dispatch_counter(value)
