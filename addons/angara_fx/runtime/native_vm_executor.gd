@tool
extends NBNativeVMExecutor
## Compatibility path for existing import/player code. Execution is native C++.
const Schema = preload("../compiler/native_vm_program.gd")
const Ops = preload("native_vm_ops.gd")
