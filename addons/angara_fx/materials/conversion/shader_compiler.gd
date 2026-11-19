@tool
extends RefCounted
## Complete reachable-node compilation and shader generation execute in C++.
const Loader=preload("graph_loader.gd")
const NativeTextures=preload("native_texture_builder.gd")
const ImportedTextures=preload("imported_texture_snapshot.gd")
const CustomArithmetic=preload("custom_arithmetic.gd")
var graph: Resource
var _body: Array[String]=[]
var _vertex_body: Array[String]=[]
func compile(source_graph: Resource,native_textures: Dictionary={},options: Dictionary={}) -> Dictionary:
	graph=source_graph
	var trace: Dictionary={}
	var result: Dictionary=NBNativeMaterialCompiler.compile(source_graph,native_textures,options,trace)
	_body.assign(trace.body);_vertex_body.assign(trace.vertex)
	return result
func support_for_features(features: Dictionary,batch: bool=false) -> Dictionary:return NBNativeMaterialCompiler.support_for_features(features,batch)
static func _verified_unbiased_full_chain(descriptor: Dictionary,texture: Texture2D) -> bool:return NBNativeMaterialCompiler.verified_unbiased_full_chain(descriptor,texture)
