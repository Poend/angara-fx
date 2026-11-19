@tool
extends RefCounted
const Parser=preload("t3d_parser.gd")
const Graph=preload("material_graph.gd")
const OUTPUTS: Array[String] = ["BaseColor", "Metallic", "Specular", "Roughness", "Anisotropy", "EmissiveColor", "Opacity", "OpacityMask", "Normal", "Tangent", "WorldPositionOffset", "SubsurfaceColor", "ClearCoat", "ClearCoatRoughness", "AmbientOcclusion", "Refraction", "PixelDepthOffset", "MaterialAttributes", "FrontMaterial", "Displacement"]
func load_graph(manifest_path: String,material_path: String) -> Resource:return NBNativeMaterialGraphLoader.load_graph(manifest_path,material_path,Graph)
func load_function(graph: Resource,source_path: String) -> Dictionary:return NBNativeMaterialGraphLoader.load_function(graph,source_path)
func _load_document(graph: Resource,path: String) -> Dictionary:return NBNativeMaterialGraphLoader.load_document(graph,path)
func _normalize(document: Dictionary,graph: Resource) -> Dictionary:return NBNativeMaterialGraphLoader.normalize(document,graph)
func _pin(raw: Variant,document: Dictionary,graph: Resource) -> Dictionary:return NBNativeMaterialGraphLoader.pin(raw,document,graph)
func _apply_instance(graph: Resource,root: Dictionary) -> void:NBNativeMaterialGraphLoader.apply_instance(graph,root)
static func asset_path(value: String) -> String:return NBNativeMaterialGraphLoader.asset_path(value)
static func project_path(value: String) -> String:return NBNativeMaterialGraphLoader.project_path(value)
static func _manifest_key(records: Dictionary,path: String) -> String:return NBNativeMaterialGraphLoader.manifest_key(records,path)
