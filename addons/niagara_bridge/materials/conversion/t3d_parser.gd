@tool
extends RefCounted
## Data-only object parsing executes in C++; imported text is never run.
func parse_file(path: String) -> Dictionary:return NBNativeT3DParser.parse_file(path)
func parse(text: String) -> Dictionary:return NBNativeT3DParser.parse(text)
static func object_path(reference: String) -> String:return NBNativeT3DParser.object_path(reference)
